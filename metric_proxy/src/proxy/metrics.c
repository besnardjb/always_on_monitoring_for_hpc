#include "metrics.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>

#include <sys/stat.h>
#include <sys/time.h>

#include "log.h"
#include "profile.h"
#include "tau_metric_proxy_client.h"
#include "utils.h"

/*********************
* METRIC DEFINITION *
*********************/

metric_t *metric_init(const char *name, const char *doc, tau_metric_type_t type)
{
	metric_t *ret = malloc(sizeof(metric_t) );

	if(!ret)
	{
		perror("malloc");
		return NULL;
	}

	memset(ret, 0, sizeof(metric_t) );

	snprintf(ret->name, METRIC_STRING_SIZE, "%s", name);
	snprintf(ret->doc, METRIC_STRING_SIZE, "%s", doc);

	ret->type = type;
	ret->next = NULL;
	pthread_spin_init(&ret->lock, 0);

	return ret;
}

int metric_release(metric_t *m)
{
	memset(m, 0, sizeof(metric_t) );
	free(m);

	return 0;
}

int metric_update(metric_t *m, tau_metric_event_t *event)
{
	pthread_spin_lock(&m->lock);
   	m->last_ts = utils_get_ts();

	switch(m->type)
	{
		case TAU_METRIC_COUNTER:
			m->metrics.counter.value += event->value;
			//fprintf(stderr, "[COUNTER] %s == %g\n", m->name, m->metrics.counter.value);
			break;

		case TAU_METRIC_GAUGE:
		{
			double prev = m->metrics.gauge.avg;
			/* Rolling AVG */
			m->metrics.gauge.avg = (prev + event->value) / 2;
			/* MIN */
			if( (m->metrics.gauge.min == 0) || (event->value < m->metrics.gauge.min) )
			{
				m->metrics.gauge.min = event->value;
			}
			/* MAX */
			if( (m->metrics.gauge.max == 0) || (m->metrics.gauge.max < event->value) )
			{
				m->metrics.gauge.max = event->value;
			}
			//fprintf(stderr, "[GAUGE] %s == avg %g min %g max %g\n", m->name, m->metrics.gauge.avg, m->metrics.gauge.min, m->metrics.gauge.max);
		}
		break;
		default:
			tau_metric_proxy_error("Cannot update metric %s : not implemented", m->name);
	}
	pthread_spin_unlock(&m->lock);

	return 0;
}

metric_t * metric_from_snapshot(tau_metric_snapshot_t * snapshot)
{

	if(	snapshot->canary != 0x1337 )
	{
		tau_metric_proxy_error("Bad canary in snapshot %d != %d", snapshot->canary, 0x1337);
		return NULL;
	}

	metric_t *ret = metric_init(snapshot->event.name, snapshot->doc, snapshot->type);

	tau_metric_proxy_log_verbose("Loading %s == %g", snapshot->event.name, snapshot->event.value);

	if(!ret)
	{
		return NULL;
	}

	switch (ret->type)
	{
		case TAU_METRIC_COUNTER:
			ret->metrics.counter.value = snapshot->event.value;
		break;
		case TAU_METRIC_GAUGE:
			ret->metrics.gauge.avg = snapshot->event.value;
		break;
		default:
			tau_metric_proxy_error("No such metric type");
			return NULL;
	}

	return ret;
}

int metric_snapshot(metric_t *m, tau_metric_snapshot_t * snapshot)
{
	if(!m || !snapshot)
	{
		return 1;
	}

	snapshot->type = m->type;
	snprintf(snapshot->doc, METRIC_STRING_SIZE, "%s", m->doc);

	snprintf(snapshot->event.name, METRIC_STRING_SIZE, "%s", m->name);
	snapshot->event.update_ts = m->last_ts;
	snapshot->event.value = 0;
	snapshot->canary = 0x1337;

	switch (m->type)
	{
		case TAU_METRIC_COUNTER:
			snapshot->event.value = m->metrics.counter.value;
		break;
		case TAU_METRIC_GAUGE:
			snapshot->event.value = m->metrics.gauge.avg;
		break;
		default:
			tau_metric_proxy_error("No such metric type");
	}

	return 0;
}

/******************************
* METRICS STORAGE DEFINITION *
******************************/

metric_array_t * metric_array_get_main(void)
{
	static metric_array_t __metric_array;
	return &__metric_array;
}

int metric_array_init(metric_array_t *ma)
{
	int i;

	for(i = 0; i < METRIC_ARRAY_SIZE; i++)
	{
		pthread_spin_init(&ma->locks[i], 0);
		ma->metrics[i] = NULL;
	}

	return 0;
}

int metric_array_release(metric_array_t *ma)
{
	int i;


	for(i = 0; i < METRIC_ARRAY_SIZE; i++)
	{
		pthread_spin_lock(&ma->locks[i]);

		metric_t *m = ma->metrics[i];

		while(m)
		{
			metric_t *to_free = m;
			m = m->next;
			metric_release(to_free);
		}

		ma->metrics[i] = NULL;

		pthread_spin_unlock(&ma->locks[i]);
	}

	return 0;
}

int metric_array_iterate(metric_array_t *ma, int (*callback)(metric_t *m, void *arg), void *arg)
{
	int i;

	for(i = 0; i < METRIC_ARRAY_SIZE; i++)
	{
		pthread_spin_lock(&ma->locks[i]);

		metric_t *m = ma->metrics[i];

		int done = 0;

		while(m && !done)
		{
			pthread_spin_lock(&m->lock);
			done = (callback)(m, arg);
			pthread_spin_unlock(&m->lock);
			m = m->next;
		}

		pthread_spin_unlock(&ma->locks[i]);
	}

	return 0;
}

static inline metric_t *__metric_array_get(metric_array_t *ma,const char *name)
{
	uint64_t hash = utils_string_hash((const unsigned char *)name);

	metric_t *m = ma->metrics[hash % METRIC_ARRAY_SIZE];

	while(m)
	{
		if(!strncmp(m->name, name, METRIC_STRING_SIZE) )
		{
			return m;
		}
		m = m->next;
	}

	return NULL;
}

metric_t *metric_array_get(metric_array_t *ma, const char *name)
{
	metric_t *ret = NULL;

	uint64_t hash = utils_string_hash((const unsigned char *)name);

	pthread_spin_lock(&ma->locks[hash % METRIC_ARRAY_SIZE]);
	ret = __metric_array_get(ma, name);
	pthread_spin_unlock(&ma->locks[hash % METRIC_ARRAY_SIZE]);

	return ret;
}

int metric_array_register(metric_array_t *ma, metric_t *m)
{
	uint64_t hash = utils_string_hash((const unsigned char *)m->name);

	pthread_spin_lock(&ma->locks[hash % METRIC_ARRAY_SIZE]);

	if(__metric_array_get(ma, m->name) )
	{
		//fprintf(stderr, "Metric %s is already registered\n", m->name);
		pthread_spin_unlock(&ma->locks[hash % METRIC_ARRAY_SIZE]);
		return 1;
	}

	unsigned int cell = hash % METRIC_ARRAY_SIZE;

	m->next = ma->metrics[cell];
	ma->metrics[cell] = m;

	pthread_spin_unlock(&ma->locks[hash % METRIC_ARRAY_SIZE]);

	return 0;
}


static int __count_metrics(metric_t *m, void *arg)
{
	int * v = (int*)arg;
	*v = *v + 1;

	return 0;
}

int metric_array_count(metric_array_t * ma)
{
	/* Fist get the metric count */
	int metric_count = 0;

	if( metric_array_iterate(ma, __count_metrics, &metric_count) != 0)
	{
		return 1;
	}

	return metric_count;
}


/*************************
 * PER JOB METRIC ARRAYS *
 *************************/

metric_array_list_entry_t * metric_array_list_entry_init(tau_metric_job_descriptor_t * desc)
{
	metric_array_list_entry_t * ret = malloc(sizeof(metric_array_list_entry_t));

	if(!ret)
	{
		perror("malloc");
		return NULL;
	}

	metric_array_init(&ret->array);

	ret->refcount = 0;
	ret->next = NULL;
	memcpy(&ret->desc, desc, sizeof(tau_metric_job_descriptor_t));

	tau_metric_proxy_log_verbose("New Job entry %p", ret);

	return ret;
}

int metric_array_list_entry_release(metric_array_list_entry_t * malie)
{
	if(!malie)
	{
		return 1;
	}

	metric_array_release(&malie->array);
	free(malie);

	return 0;
}

metric_array_list_t __metric_array_list;

void metric_array_list_init(void (*release_callback)(tau_metric_job_descriptor_t *, metric_array_t *))
{
	pthread_spin_init(&__metric_array_list.lock, 0);
	__metric_array_list.head = NULL;
	__metric_array_list.release_callback = release_callback;
}

void metric_array_list_release(void)
{
	metric_array_list_entry_t *tmp = __metric_array_list.head;
	
	while(tmp)
	{
		metric_array_list_entry_t *to_free = tmp;
		tmp = tmp->next;
		metric_array_list_entry_release(to_free);
	}
}

metric_array_list_entry_t * metric_array_list_get_no_lock(const char * jobid)
{
	metric_array_list_entry_t *tmp = __metric_array_list.head;
	
	while(tmp)
	{
		if(!strcmp(jobid, tmp->desc.jobid))
		{
			return tmp;
		}
		tmp = tmp->next;
	}

	return NULL;
}

metric_array_t * metric_array_list_acquire(tau_metric_job_descriptor_t * desc)
{
	metric_array_t * ret = NULL;

	if(!strlen(desc->jobid))
	{
		/* No data not much to be done */
		return NULL;
	}

	pthread_spin_lock(&__metric_array_list.lock);

	metric_array_list_entry_t * ent = metric_array_list_get_no_lock(desc->jobid);

	if(!ent)
	{
		ent = metric_array_list_entry_init(desc);
		ent->next = __metric_array_list.head;
		__metric_array_list.head = ent;
	}
	else
	{
		tau_metric_proxy_log_verbose("Joining existing job %s ref %ld", ent->desc.jobid, ent->refcount);
	}

	ent->refcount++;

	ret = &ent->array;

	pthread_spin_unlock(&__metric_array_list.lock);

	return ret;
}

int metric_array_list_relax(const char * jobid)
{
	int ret = 1;

	pthread_spin_lock(&__metric_array_list.lock);

	metric_array_list_entry_t * ent = metric_array_list_get_no_lock(jobid);

	if(ent)
	{
		ent->refcount--;

		tau_metric_proxy_log_verbose("Leaving job %s ref %ld", ent->desc.jobid, ent->refcount);

		if(!ent->refcount)
		{
			tau_metric_proxy_log_verbose("Last do release job %s", ent->desc.jobid);

			__metric_array_list.head->desc.end_time = time(NULL);

			if(__metric_array_list.release_callback)
			{
				(__metric_array_list.release_callback)(&ent->desc, &ent->array);
			}

			/* Now work on removing myself from the JOB ID list*/
			if(!strcmp(jobid, __metric_array_list.head->desc.jobid))
			{
				/* First element is ours remove it */
				__metric_array_list.head = __metric_array_list.head->next;
			}

			metric_array_list_entry_t * tmp = __metric_array_list.head;

			while(tmp)
			{
				if(tmp->next)
				{
					if(!strcmp(jobid, tmp->next->desc.jobid))
					{
						/* First element is ours remove it */
						tmp->next = tmp->next->next;
					}
				}
				tmp = tmp->next;
			}

			metric_array_list_entry_release(ent);
		}
	}

	pthread_spin_unlock(&__metric_array_list.lock);

	return ret;
}

/**********************
 * JOB METRIC STORAGE *
 **********************/

struct lockfile
{
	char hostname[64];
	pid_t pid;
};

void lockfile_init(struct lockfile * l)
{
	gethostname(l->hostname, 64);
	l->pid = getpid();
}

int lockfile_equals(struct lockfile * a, struct lockfile * b)
{
	if(strcmp(a->hostname, b->hostname))
	{
		return 0;
	}

	return (a->pid == b->pid);
}


static inline int __file_pid_matches(const char * path)
{
	struct lockfile needle;
	lockfile_init(&needle);
	struct lockfile haystack = { 0 };

	FILE * in = fopen(path, "r");

	if(!in)
	{
		return 0;
	}

	fread(&haystack, sizeof(struct lockfile), 1, in);

	fclose(in);

	return lockfile_equals(&haystack, &needle);
}




static inline int __check_lock_file(const char * path_to_prof)
{
	pid_t my_pid = getpid();

	char path_to_lock[512];
	snprintf(path_to_lock, 512, "%s/lock", path_to_prof);

	if(utils_isfile(path_to_lock))
	{
		/* Check content matching */
		if( !__file_pid_matches(path_to_lock))
		{
			/* They are different check the last modification time
			   we issue a warning if it is less than 2 minutes */
			if(utils_file_last_modif_delta(path_to_lock) < 120)
			{
				tau_metric_proxy_error("%s is held by another processes", path_to_lock);
				tau_metric_proxy_error("Make sure to have only a single profile aggregator");
				tau_metric_proxy_error("If it is a leftover, remove %s", path_to_lock);
				return 1;
			}
		}
		else {
			/* All OK we matched update file timestamp for next check */
			utime(path_to_lock, NULL);
			return 0;
		}
	}

	/* We need to create it */
	struct lockfile myinfo;
	lockfile_init(&myinfo);

	FILE * out = fopen(path_to_lock, "w");
	fwrite(&myinfo, sizeof(struct lockfile), 1, out);
	fclose(out);

	return 0;
}

metric_per_job_t __per_job_metric = { 0 };


static int __wait_and_watch()
{
	int to_wait = 3 * 1e6;
	unsigned int period = 1000;

	while(0 < to_wait)
	{
		if(!__per_job_metric.is_running)
		{
			return 1;
		}
		usleep(period);
		to_wait -= period;
	}

	return 0;
}

static void * __profile_merger_thread(void * dummy)
{
	while(__per_job_metric.is_running)
	{
		tau_metric_proxy_log_verbose("Scanning for new profiles");
		__check_lock_file(__per_job_metric.path);
		tau_metric_profile_store_consolidate();
		__wait_and_watch();
	}

	return NULL;
}


int metric_per_job_init(const char * path, int is_leader)
{
	/* Save path */
	snprintf(__per_job_metric.path, 512, "%s", path);
	__per_job_metric.is_leader = is_leader;
	__per_job_metric.is_running = 0;

	tau_metric_proxy_log("Profiles are stored in %s", path);


	if(!utils_isdir(path))
	{
		if(mkdir(path, 0700) < 0)
		{
			tau_metric_proxy_error("Could not create directory %s",path);
			return 1;
		}
	}

	if(is_leader)
	{
		if( __check_lock_file(path) )
		{
			return 1;
		}

		if( tau_metric_profile_store_init(path) )
		{
			return 1;
		}

		if( pthread_create(&__per_job_metric.merger_thread, NULL, __profile_merger_thread, NULL) < 0)
		{
			tau_metric_proxy_perror("pthread_create");
			return 1;
		}

		__per_job_metric.is_running = 1;
	}

	return 0;
}

int metric_per_job_release(void)
{
	if(!__per_job_metric.is_leader)
	{
		/* Nothing to do */
		return 0;
	}

	__per_job_metric.is_running = 0;
	pthread_join(__per_job_metric.merger_thread, NULL);

	char path_to_lock[512];
	snprintf(path_to_lock, 512, "%s/lock", __per_job_metric.path);

	if( utils_isfile(path_to_lock) )
	{
		tau_metric_proxy_log_verbose("removing Lock file %s", path_to_lock);
		unlink(path_to_lock);
	}

	return 0;
}


int metric_per_job_dump(tau_metric_job_descriptor_t * desc, metric_array_t * metrics)
{
	if(!strlen(__per_job_metric.path))
	{
		tau_metric_proxy_error("No path set to store per job dumps");
		return 1;
	}

	char hostname[64];

	if(gethostname(hostname, 64) < 0)
	{
		tau_metric_proxy_perror("gethosname");
		return 1;
	}

	char path_to_run[512];
	snprintf(path_to_run, 512, "%s/%s-%s.%d.taumetric", __per_job_metric.path, desc->jobid, hostname, getpid());

	if( tau_metric_dump_save(path_to_run, desc, metrics) )
	{
		return 1;
	}

	return 0;
}