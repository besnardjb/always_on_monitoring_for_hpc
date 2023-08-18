#ifndef PROFILE_H
#define PROFILE_H

#include "metrics.h"

/*************************************
 * IMPLEMENTATION OF THE METRIC DUMP *
 *************************************/

typedef struct
{
    int metric_count;
    tau_metric_job_descriptor_t desc;
    tau_metric_snapshot_t metrics[0];
}tau_metric_dump_t;

tau_metric_dump_t * tau_metric_dump_load(const char * path);
int tau_metric_dump_save(const char * path, tau_metric_job_descriptor_t * desc, metric_array_t * metrics);

/********************************************
 * IMPLEMENTATION OF A PROFILE ACCUMULATION *
 ********************************************/

typedef struct
{
    char path[512];
    tau_metric_job_descriptor_t desc;
    tau_metric_dump_t * dump;
}tau_metric_profile_t;

tau_metric_profile_t * tau_metric_profile_load(char * path_to_profile);
int tau_metric_profile_consolidate(tau_metric_profile_t * prof, tau_metric_dump_t *dump);
int tau_metric_profile_free(tau_metric_profile_t **profile);

/********************************
 * PROFILE RUNTIME LOOKUP TABLE *
 ********************************/

typedef struct tau_metric_profile_store_entry
{
    char jobid[64];
    struct tau_metric_profile_store_entry * next;
}tau_metric_profile_store_entry_t;

tau_metric_profile_store_entry_t * tau_metric_profile_store_entry_new(const char * jobid);
int tau_metric_profile_store_entry_free(tau_metric_profile_store_entry_t * ent);

#define TAU_METRIC_PROFILE_HT_SIZE 1024

typedef struct
{
    char job_directory[512];
    char profile_directory[512];
    tau_metric_profile_store_entry_t * entries[TAU_METRIC_PROFILE_HT_SIZE];
}tau_metric_profile_store_t;

int tau_metric_profile_store_init(const char * job_storage_path);
int tau_metric_profile_store_release();

int tau_metric_profile_store_consolidate();





#endif /* PROFILE_H */