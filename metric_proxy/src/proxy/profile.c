#include "profile.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ftw.h>
#include <libgen.h>
#include <dirent.h>
#include <unistd.h>

#include "log.h"
#include "metrics.h"
#include "tau_metric_proxy_client.h"
#include "utils.h"


/*************************************
 * IMPLEMENTATION OF THE METRIC DUMP *
 *************************************/

#define TAU_METRIC_DUMP_CANARY 0x77

tau_metric_dump_t * tau_metric_dump_load(const char * path)
{
    FILE * in = fopen(path, "r");

    if(!in)
    {
		tau_metric_proxy_perror("fopen");
        return NULL;
    }

    tau_metric_dump_t tmp;

    int ret = fread(&tmp, sizeof(tau_metric_dump_t), 1, in);

    if(ret != 1)
    {
        tau_metric_proxy_perror("fread");
        fclose(in);
        return NULL;
    }

    tau_metric_dump_t * dump = malloc(sizeof(tau_metric_dump_t) + tmp.metric_count * sizeof(tau_metric_snapshot_t));

    if(!dump)
    {
        tau_metric_proxy_perror("malloc");
        fclose(in);
        return NULL;
    }

    memcpy(dump, &tmp, sizeof(tau_metric_dump_t));

    tau_metric_proxy_log_verbose("Reading %d metrics from %s", tmp.metric_count, path);

    ret = fread(dump->metrics, sizeof(tau_metric_snapshot_t), tmp.metric_count, in);

    if(ret != tmp.metric_count)
    {
        tau_metric_proxy_error("Bad metric count when reading");
        goto DUMPREADFAIL;
    }

    int canary = 0;

    ret = fread(&canary, sizeof(int), 1, in);

    if(ret != 1)
    {
        tau_metric_proxy_error("Could not read canary in dump");
        goto DUMPREADFAIL;
    }

    if(canary != TAU_METRIC_DUMP_CANARY)
    {
        tau_metric_proxy_error("Bad canary in dump (%d != %d)", canary, TAU_METRIC_DUMP_CANARY);
        goto DUMPREADFAIL;
    }

    fclose(in);
    return dump;

DUMPREADFAIL:
    fclose(in);
    free(dump);
    return NULL;
}

static inline int __write_a_metric(metric_t *m, void *pfile)
{
	FILE * f = (FILE*)pfile;


    tau_metric_snapshot_t s;

    if(metric_snapshot(m, &s) )
    {
        return 1;
    }

	int ret = fwrite(&s, sizeof(tau_metric_snapshot_t), 1, f);

	if(ret != 1)
	{
		return 1;
	}

	return 0;
}

int tau_metric_dump_save(const char * path, tau_metric_job_descriptor_t * desc, metric_array_t * metrics)
{
    FILE * out = fopen(path, "w");

	if(!out)
	{
		tau_metric_proxy_perror("fopen");
		return 1;
	}

    tau_metric_dump_t dump;

    memcpy(&dump.desc, desc, sizeof(tau_metric_job_descriptor_t));
    dump.metric_count = metric_array_count(metrics);

    tau_metric_proxy_log_verbose("Saving %d metrics to %s", dump.metric_count,path);

	fwrite(&dump, sizeof(tau_metric_dump_t), 1, out);

	int ret = metric_array_iterate(metrics, __write_a_metric, (void*)out);

	if(ret)
	{
		tau_metric_proxy_error("There was an error writing some metrics");
	}

    int canary = TAU_METRIC_DUMP_CANARY;
	ret = fwrite(&canary, sizeof(int), 1, out);

    if(ret!=1)
    {
        fclose(out);
		tau_metric_proxy_perror("fwrite");
		return 1;
    }

	fclose(out);

    return 0;
}

/********************************************
 * IMPLEMENTATION OF A PROFILE ACCUMULATION *
 ********************************************/

int tau_metric_profile_init_from_dump(char * path_to_profile, tau_metric_dump_t * dump)
{
    /* Here we just directly copy the dump */

    FILE * out = fopen(path_to_profile, "w");

    if(!out)
    {
        tau_metric_proxy_perror("fopen");
        return 1;
    }

    size_t total_size = sizeof(tau_metric_dump_t) + dump->metric_count * sizeof(tau_metric_snapshot_t);

    size_t ret = fwrite(dump, sizeof(char), total_size, out);

    if(ret != total_size)
    {
        tau_metric_proxy_error("Error storing profile %s", path_to_profile);
        fclose(out);
        return 1;
    }

    int canary = TAU_METRIC_DUMP_CANARY;
    ret = fwrite(&canary, sizeof(int), 1, out);

    if(ret != 1)
    {
        tau_metric_proxy_error("Error storing profile canary %s", path_to_profile);
    }

    tau_metric_proxy_log("Storing new job profile in %s", path_to_profile);

    fclose(out);

    return 0;
}

tau_metric_profile_t * tau_metric_profile_load(char * path_to_profile)
{
    tau_metric_profile_t * ret = malloc(sizeof(tau_metric_profile_t));

    if(!ret)
    {
		tau_metric_proxy_perror("malloc");
        return NULL;
    }

    snprintf(ret->path, 512, "%s", path_to_profile);

    ret->dump = tau_metric_dump_load(path_to_profile);

    if(!ret->dump)
    {
        tau_metric_proxy_error("Could not load dump %s", path_to_profile);
        return  NULL;
    }

    memcpy(&ret->desc, &ret->dump->desc, sizeof(tau_metric_job_descriptor_t));

    return ret;
}

static inline int __apply_dump_to_metrics(metric_array_t *metrics, tau_metric_dump_t * dump)
{
    int i;

    for(i = 0 ; i < dump->metric_count; i++)
    {
        metric_t *new_m = metric_from_snapshot(&dump->metrics[i]);
        if(!new_m)
        {
            tau_metric_proxy_error("Failed registering snapshoted metric");
            continue;
        }

        metric_t * m = metric_array_get(metrics, dump->metrics[i].event.name);

        if(!m)
        {
            /* Note the register carries the current value from the metric*/
            metric_array_register(metrics, new_m);
        }
        else
        {
            metric_update(m, &dump->metrics[i].event);
        }
    }

    return 0;
}

int tau_metric_profile_consolidate(tau_metric_profile_t * prof, tau_metric_dump_t *dump)
{
    metric_array_t metrics;
    metric_array_init(&metrics);

    /* Start by inserting in the MA all values from the profile */
    __apply_dump_to_metrics(&metrics, prof->dump);

    /* Then merge the new run */
    __apply_dump_to_metrics(&metrics, dump);

    /* And save if needed the MPMD status*/
    if(! strstr(prof->desc.command, dump->desc.command))
    {
        /* Commands are different */
        char tmp[512];
        snprintf(tmp, 512, "%s : %s", prof->desc.command, dump->desc.command);
        snprintf(prof->desc.command, 512, "%s", tmp);
    }

    /* Update start and end times in the desc
       to keep the largest dynamic */
    if(dump->desc.start_time < prof->desc.start_time)
    {
        prof->desc.start_time = dump->desc.start_time;
    }

    if(prof->desc.end_time < dump->desc.end_time)
    {
        prof->desc.end_time = dump->desc.end_time;
    }

    /* And dump again ! */
    tau_metric_dump_save(prof->path, &prof->desc, &metrics);

    metric_array_release(&metrics);

    return 0;
}

int tau_metric_profile_free(tau_metric_profile_t **profile)
{
    if(! *profile)
    {
        return 0;
    }

    if((*profile)->dump)
    {
        free((*profile)->dump);
        (*profile)->dump = NULL;
    }

    free(*profile);
    *profile = NULL;

    return 0;
}

/********************************
 * PROFILE RUNTIME LOOKUP TABLE *
 ********************************/

/* These are the entries */

tau_metric_profile_store_entry_t * tau_metric_profile_store_entry_new(const char * jobid)
{
    tau_metric_profile_store_entry_t * ret = malloc(sizeof(tau_metric_profile_store_entry_t));

    if(!ret)
    {
        tau_metric_proxy_perror("malloc");
        return NULL;
    }

    snprintf(ret->jobid, 64, "%s", jobid);
    ret->next = NULL;

    return ret;
}

int tau_metric_profile_store_entry_free(tau_metric_profile_store_entry_t * ent)
{
    free(ent);
    return 0;
}

/* This is the storage side */

static tau_metric_profile_store_t __profile_storage = { 0 };


int __tau_metric_profile_store_is_present(const char * jobid)
{
    uint64_t cell = utils_string_hash((const unsigned char *)jobid);
 
    tau_metric_profile_store_entry_t * tmp = __profile_storage.entries[cell % TAU_METRIC_PROFILE_HT_SIZE];

    while(tmp)
    {
        if(!strcmp(tmp->jobid, jobid))
        {
            return 1;
        }

        tmp = tmp->next;
    }

    return 0;
}

int __tau_metric_profile_store_remove(const char * jobid)
{
    uint64_t cell = utils_string_hash((const unsigned char *)jobid);
    int cellidx = cell % TAU_METRIC_PROFILE_HT_SIZE;

    tau_metric_proxy_log("Removing profile for %s", jobid);

    tau_metric_profile_store_entry_t * tmp = __profile_storage.entries[cellidx];

    /* Handle head */
    if(tmp)
    {
        if( !strcmp(tmp->jobid, jobid) )
        {
            __profile_storage.entries[cellidx] = tmp->next;
            tau_metric_profile_store_entry_free(tmp);
        }
    }  

    /* Now walk the list for removal */
    while(tmp)
    {
        if(tmp->next)
        {
            if( !strcmp(tmp->next->jobid, jobid) )
            {
                tau_metric_profile_store_entry_t * to_free = tmp->next;
                tmp->next = tmp->next->next;
                tau_metric_profile_store_entry_free(to_free);
            } 
        }

        tmp = tmp->next;
    }

    return 0;
}


int __tau_metric_profile_store_add(const char * jobid)
{
    if(__tau_metric_profile_store_is_present(jobid))
    {
        /* Nothing to do already known */
        return 0;
    }

    uint64_t cell = utils_string_hash((const unsigned char *)jobid);

    tau_metric_profile_store_entry_t *new = tau_metric_profile_store_entry_new(jobid);

    int cellidx = cell % TAU_METRIC_PROFILE_HT_SIZE;

    new->next = __profile_storage.entries[cellidx];
    __profile_storage.entries[cellidx] = new;

    return 0;
}

int __profile_dir_scan(const char *fpath, const struct stat *sb, int typeflag)
{
    if(typeflag != FTW_F)
    {
        return 0;
    }

    if(strstr(fpath, ".profile"))
    {
        char path[512];
        snprintf(path, 512, "%s", fpath);
        char * bname = basename(path);
        char * point = strrchr(bname, '.');
        *point = '\0';

        tau_metric_proxy_log_verbose("Scanned profile %s", bname);

        /* We now have the profile entry */
        __tau_metric_profile_store_add(bname);
    }

    return 0;
}

int __tau_metric_profile_store_scan()
{

    tau_metric_proxy_log_verbose("Looking for profiles in %s", __profile_storage.profile_directory);

    if( ftw(__profile_storage.profile_directory, __profile_dir_scan, 128) < 0)
    {
        tau_metric_proxy_perror("ftw");
        return 1;
    }

    return 0;
}

int tau_metric_profile_store_init(const char * job_storage_path)
{
    if(!utils_isdir(job_storage_path))
    {
        tau_metric_proxy_error("Job storage file must exist to start the profile storage");
        return 1;
    }

    snprintf(__profile_storage.job_directory, 512, "%s", job_storage_path);

    snprintf(__profile_storage.profile_directory, 512, "%s/profiles/", job_storage_path);

    if(!utils_isdir(__profile_storage.profile_directory))
    {
        if( mkdir(__profile_storage.profile_directory, 0700) < 0 )
        {
            tau_metric_proxy_perror("mkdir");
            return 1;  
        }
    }

    /* Empty the cell list */
    int i;
    for(i = 0 ; i < TAU_METRIC_PROFILE_HT_SIZE; i++)
    {
        __profile_storage.entries[i] = NULL;
    }

    if(__tau_metric_profile_store_scan())
    {
        return 1;
    }

    tau_metric_profile_store_consolidate();

    return 0;
}

char * tau_metric_profile_job_to_path(const char * jobid)
{
    static char buffer[512];

    char prefix[3];
    snprintf(prefix, 2, "%s", jobid);

    snprintf(buffer, 512, "%s/%s/", __profile_storage.profile_directory, prefix);

    if(!utils_isdir(buffer))
    {
        if(mkdir(buffer, 0700) < 0)
        {
            tau_metric_proxy_perror("mkdir");
            return NULL;
        }
    }

    snprintf(buffer, 512, "%s/%s/%s.profile", __profile_storage.profile_directory, prefix, jobid);

    return buffer;
}

int tau_metric_profile_store_release();


int __tau_metric_profile_store_insert(const char * dump_path)
{
    tau_metric_dump_t * dump = tau_metric_dump_load(dump_path);

    if(!dump)
    {
        tau_metric_proxy_error("Failed to load %s", dump_path);

        /* Failed to load could be being written */
        return 1;
    }


    char * path = tau_metric_profile_job_to_path(dump->desc.jobid);

    tau_metric_profile_t * profile = NULL;

    /* If we are here we did load it
       now check if an existing profile has this id */
    if(__tau_metric_profile_store_is_present(dump->desc.jobid))
    {
        /* It means we should be able to open it */

        if(!utils_isfile(path))
        {
            free(dump);
            tau_metric_proxy_error("Could not locate profile file %s", path);
            return 1;
        }

        profile = tau_metric_profile_load(path);

        if(!profile)
        {
            free(dump);
            tau_metric_proxy_error("Removing apparently corrupted profile %s", path);
            /* We are provably corruped in the profile better delete */
            __tau_metric_profile_store_remove(dump->desc.jobid);
            unlink(path);
            return 1;
        }


        /* And now sum up variables */
        if( tau_metric_profile_consolidate(profile, dump) )
        {
            free(dump);
            return 1;
        }

        tau_metric_profile_free(&profile);
    }
    else
    {
        /* We need to create it from scratch */
        if( tau_metric_profile_init_from_dump(path, dump) )
        {
            free(dump);
            return 1;
        }

        /* And now add it to known list */
        __tau_metric_profile_store_add(dump->desc.jobid);

    }

    free(dump);

    return 0;
}



int tau_metric_profile_store_consolidate()
{
    /* Here we do a scandir on the job path */

    int counter = 0;
    double start = utils_get_ts();

    DIR *d = opendir(__profile_storage.job_directory);

    if(!d)
    {
        tau_metric_proxy_perror("opendir");
        return 1;
    }

    struct dirent *dir;

    char fullpath[512];

    while ((dir = readdir(d)) != NULL)
    {
        if(dir->d_type == DT_REG)
        {
            /* Only check regular files with extension */
            if(strstr(dir->d_name, ".taumetric"))
            {
                snprintf(fullpath, 512, "%s/%s", __profile_storage.job_directory, dir->d_name);
                tau_metric_proxy_log_verbose("Profiles: Processing %s", fullpath);

                if( __tau_metric_profile_store_insert(fullpath) == 0)
                {
                    /* Processed it we can now delete ! */
                    unlink(fullpath);
                }
                counter++;
            }
        }
    }

    closedir(d);

    double end = utils_get_ts();

    tau_metric_proxy_log("Profiles: aggregated %d profiles in %g seconds", counter, end-start);


    return 0;
}
