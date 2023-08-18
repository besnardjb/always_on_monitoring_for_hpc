#ifndef TAU_METRIC_PROXY_METRICS_H
#define TAU_METRIC_PROXY_METRICS_H

#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>
#include <time.h>

#include "tau_metric_proxy_client.h"

/****************************
* METRIC TYPES DEFINITIONS *
****************************/

/**
 * @brief it is defined as the sum of its contributors
*/
typedef struct
{
	double value; /**< Total value from all contributors */
}counter_t;

/**
 * @brief This is a gauge whic can vary
 *        over time. We generate extra
 *        metrics from it to provide more insights
 */
typedef struct
{
	double min; /**< Minimum value on contributors */
	double max; /**< Maximum value on contributors */
	double avg; /**< Moving Average value from contributors */
}gauge_t;

/*********************
* METRIC DEFINITION *
*********************/

/**
 * @brief This is the main storage for a metric
 *
 */

typedef struct metric_s
{
	char               name[METRIC_STRING_SIZE]; /**< Name of the given metric */
	char               doc[METRIC_STRING_SIZE];  /**< Documentation of the metric */
	tau_metric_type_t  type;                     /**< Type of the metric */
   double             last_ts;                  /**< Timestamp when last updated */
	union
	{
		/* data */
		counter_t counter;
		gauge_t   gauge;
	}                  metrics; /**< Metric storage in an union */
	pthread_spinlock_t lock;    /**< Lock protecting metric update */
	/* ----- */
	struct metric_s *  next;    /**< Metrics are stored as lists of metrics */
} metric_t;

metric_t *metric_init(const char *name, const char *doc, tau_metric_type_t type);
int metric_release(metric_t *m);

int metric_update(metric_t *m, tau_metric_event_t *event);


metric_t * metric_from_snapshot(tau_metric_snapshot_t * snapshot);

int metric_snapshot(metric_t *m, tau_metric_snapshot_t * snapshot);

/******************************
* METRICS STORAGE DEFINITION *
******************************/

#define METRIC_ARRAY_SIZE    1024

/**
 * @brief This is where metrics are stored server side
 *
 */
typedef struct
{
	metric_t *         metrics[METRIC_ARRAY_SIZE];  /**< Hash table of metrics */
	pthread_spinlock_t locks[METRIC_ARRAY_SIZE];    /**< Lock for each bucket */
}metric_array_t;

/**
 * @brief Initialize the metric storage
 *
 * @return int 0 on success
 */
int metric_array_init(metric_array_t *ma);

/**
 * @brief Release metric storage
 *
 * @return int 0 on success
 */
int metric_array_release(metric_array_t *ma);

/**
 * @brief Get a metric from the metric array
 *
 * @param name metric name
 * @return metric_t* NUL if none pointer to metric otherwise
 */
metric_t *metric_array_get(metric_array_t *ma, const char *name);

/**
 * @brief Register a new metric
 *
 * @param m The new metric to register
 * @return int 1 if the metric is already present (same name); 0 on success
 */
int metric_array_register(metric_array_t *ma, metric_t *m);

/**
 * @brief Scan all metrics invoking a callback
 *
 * @param callback callback to be invoked
 * @param arg extra argument to pass to the callback
 * @return int 0 on success
 */
int metric_array_iterate(metric_array_t *ma, int (*callback)(metric_t *m, void *arg), void *arg);

/**
 * @brief Count the metrics in a give array
 * 
 * @param ma target metric array
 * @return int number of metrics
 */
int metric_array_count(metric_array_t * ma);

/**
 * @brief Get the central metric array (the one per node)
 * 
 * @return metric_array_t* the node level metric array
 */
metric_array_t * metric_array_get_main(void);

/*************************
 * PER JOB METRIC ARRAYS *
 *************************/

/**
 * @brief This is an entry in the metric array
 * 
 */
typedef struct metric_array_list_entry_s
{
	tau_metric_job_descriptor_t desc;
	uint64_t refcount;
	metric_array_t array;
	struct metric_array_list_entry_s * next;
}metric_array_list_entry_t;

/**
 * @brief Allocate a new metric array entry
 * 
 * @param job_id The job ID it stands for
 * @return metric_array_list_entry_t* new metric array (allocated)
 */
metric_array_list_entry_t * metric_array_list_entry_init(tau_metric_job_descriptor_t * desc);

/**
 * @brief Release a metric array entry
 * 
 * @param malie the entry to free
 * @return int 0 if all OK
 */
int metric_array_list_entry_release(metric_array_list_entry_t * malie);

/**
 * @brief This is the main manager for per-job data
 * 
 */
typedef struct metric_array_list_s
{
	pthread_spinlock_t lock;
	struct metric_array_list_entry_s * head;
	void (*release_callback)(tau_metric_job_descriptor_t *desc, metric_array_t *array);
}metric_array_list_t;

/**
 * @brief Initializes the main per-job storage
 * 
 */
void metric_array_list_init(void (*release_callback)(tau_metric_job_descriptor_t * , metric_array_t *));

/**
 * @brief Releases the main per-job storage
 * 
 */
void metric_array_list_release(void);

/**
 * @brief Get the metric array for a given job
   @warning This does not lock it has to be done in parent function
 * 
 * @param jobid the JOB ID we look for
 * @return metric_array_list_entry_t* the corresponding array entry or NULL if not found
 */
metric_array_list_entry_t * metric_array_list_get_no_lock(const char * jobid);


/**
 * @brief Here we register a new job (this either increments the job or allocate it)
 * 
 * @param jobid JOB ID to query
 * @return metric_array_t* possibly new array
 */
metric_array_t * metric_array_list_acquire(tau_metric_job_descriptor_t *desc);

/**
 * @brief Conversely to @ref metric_array_list_acquire we release when reaching 0
 * 
 * @param jobid JOB ID to release
 * @return int 1 if the job is unknown 0 if all OK
 */
int metric_array_list_relax(const char * jobid);


/**********************
 * JOB METRIC STORAGE *
 **********************/

typedef struct {
	char path[512];
	int is_leader;
	pthread_t merger_thread;
	volatile int is_running;
}metric_per_job_t;

/**
 * @brief Init the per-job context directory for profiles
 * 
 * @param path path to the profile storage directory
 * @param is_leader defines if the given instance should reduce profiles
 */
int metric_per_job_init(const char * path, int is_leader);

/**
 * @brief Notify relase of the per-job metric tracker
 * 
 * @return int 0 on success
 */
int metric_per_job_release(void);

/**
 * @brief Dump one job in the profile storage directory
 * 
 * @param desc description of the job
 * @param metrics the metrics to dump for this job instance
 */
int metric_per_job_dump(tau_metric_job_descriptor_t * desc, metric_array_t * metrics);

#endif /* TAU_METRIC_PROXY_METRICS_H */
