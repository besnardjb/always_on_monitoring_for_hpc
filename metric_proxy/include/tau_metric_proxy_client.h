#ifndef TAU_METRIC_PROXY_CLIENT_H
#define TAU_METRIC_PROXY_CLIENT_H

#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>

/*************
 * INTERNALS *
 *************/

/**
 * @brief This encodes the metric type
 */
typedef enum {
    TAU_METRIC_NULL=0,
    TAU_METRIC_COUNTER=1,    /**< counter_t */
    TAU_METRIC_GAUGE=2       /**< gauge_t */
}tau_metric_type_t;

static const char * const tau_metric_type_name[] =
{
    "TAU_METRIC_COUNTER",
    "TAU_METRIC_GAUGE"
};

#define METRIC_STRING_SIZE 300

/**
 * @brief This is used internally to register a client metric on the fly
 */
typedef struct {
    char name[METRIC_STRING_SIZE];
    char doc[METRIC_STRING_SIZE];
    int type; /* tau_metric_type_t */
}tau_metric_descriptor_t;

static inline void tau_metric_descriptor_print(tau_metric_descriptor_t *md)
{
    fprintf(stderr, "%s : %s (%s)\n", md->name, md->doc, tau_metric_type_name[md->type]);
}

/**
 * @brief This is used internally to send a metric up
 *
 */
typedef struct {
    char name[METRIC_STRING_SIZE];
    double value;
    double update_ts;
}tau_metric_event_t;

typedef struct {
    tau_metric_type_t type;
    char doc[METRIC_STRING_SIZE];
    tau_metric_event_t event;
    int canary;
}tau_metric_snapshot_t;

static inline void tau_metric_event_print(tau_metric_event_t *me)
{
    fprintf(stderr, "%s : %f\n", me->name, me->value);
}

typedef struct {
    char jobid[64];
    char command[512];
    int size;
    char nodelist[128];
    char partition[64];
    char cluster[64];
    char run_dir[256];
    size_t start_time;
    size_t end_time;
}tau_metric_job_descriptor_t;

/** This fills a metric descriptor fron environment (uses SLURM) */
void tau_metric_job_descriptor_init(tau_metric_job_descriptor_t * desc);

static inline void tau_metric_job_descriptor_print(tau_metric_job_descriptor_t * desc)
{
    fprintf(stderr, "=========================\n");
    fprintf(stderr, "JOBID %s\n", desc->jobid);
    fprintf(stderr, "CLUSTER %s\n", desc->cluster);
    fprintf(stderr, "NODES %s\n", desc->nodelist);
    fprintf(stderr, "PARTITION %s\n", desc->partition);
    fprintf(stderr, "PWD %s\n", desc->run_dir);
    fprintf(stderr, "CMD %s\n", desc->command);
    fprintf(stderr, "SIZE %d\n", desc->size);
    fprintf(stderr, "START %ld\n", desc->start_time);
    fprintf(stderr, "END %ld\n", desc->end_time);
    fprintf(stderr, "=========================\n");
}

/**
 * @brief This defines the two kinds of events on the wire protocol
 *
 */
typedef enum
{
    TAU_METRIC_MSG_DESC=0,    /**< Register a new metric IN: tau_metric_descriptor_t */
    TAU_METRIC_MSG_VAL=1,      /**< Send a metric value IN: tau_metric_event_t*/
    /* Client side */
    TAU_METRIC_MSG_LIST_ALL=2,     /**< List all metrics known server side
                                        IN: (ignored) OUT: (int N)  N*tau_metric_descriptor_t */
    TAU_METRIC_MSG_GET_ALL=3,      /**< Get all metrics values server side
                                        IN: (ignored) OUT: (int N)  N*tau_metric_event_t  */
    TAU_METRIC_MSG_GET_ONE=4,       /**< Get one metric server side
                                        IN: tau_metric_descriptor_t OUT: tau_metric_event_t */
    TAU_METRIC_MSG_JOB_DESCRIPTION=5, /** IN: inside node piggybacked (tau_metric_job_descriptor_t) OUT: NONE*/
    TAU_METRIC_MSG_COUNT
}tau_metric_msg_type_t;

static const char * const tau_metric_msg_type_name[] =
{
    "TAU_METRIC_MSG_DESC",
    "TAU_METRIC_MSG_VAL",
    "TAU_METRIC_MSG_LIST_ALL",
    "TAU_METRIC_MSG_GET_ALL",
    "TAU_METRIC_MSG_GET_ONE"
};

/**
 * @brief This is what is sent to the server when updating a value
 *
 */
typedef struct {
    tau_metric_msg_type_t type;
    union {
        tau_metric_descriptor_t desc;
        tau_metric_event_t event;
    }payload;
    char canary;
}tau_metric_msg_t;

static inline void tau_metric_msg_print(tau_metric_msg_t * msg)
{
    fprintf(stderr, "[%s] (CAN 0x7 = %X)\n", tau_metric_msg_type_name[msg->type], msg->canary);

    switch(msg->type)
    {
        case TAU_METRIC_MSG_DESC:
            tau_metric_descriptor_print(&msg->payload.desc);
        break;
        case TAU_METRIC_MSG_VAL:
            tau_metric_event_print(&msg->payload.event);
        break;
        case TAU_METRIC_MSG_GET_ONE:
            tau_metric_event_print(&msg->payload.event);
        break;
        default:
            return;
    }
}

/************
 * COUNTERS *
 ************/

struct tau_client_metric_s;
typedef struct tau_client_metric_s * tau_metric_counter_t;

tau_metric_counter_t tau_metric_counter_new(const char * name, const char * doc);
int tau_metric_counter_incr(tau_metric_counter_t counter, double increment);

/*********
 * GAUGE *
 *********/

typedef struct tau_client_metric_s * tau_metric_gauge_t;

tau_metric_gauge_t tau_metric_gauge_new(const char * name, const char * doc);
int tau_metric_gauge_incr(tau_metric_gauge_t gauge, double increment);
int tau_metric_gauge_set(tau_metric_gauge_t gauge, double value);

/********************
 * INIT AND RELEASE *
 ********************/

void tau_metric_client_init();
void tau_metric_client_release();

int tau_metric_client_connected();

static inline void tau_metric_client_inhibit(void)
{
    setenv("TAU_METRIC_PROXY_INIHIBIT_CLIENT", "1", 1);
}


static inline void tau_metric_client_enable(void)
{
    setenv("TAU_METRIC_PROXY_INIHIBIT_CLIENT", "0", 1);
}

#ifdef __cplusplus
}
#endif

#endif /* TAU_METRIC_PROXY_CLIENT_H */
