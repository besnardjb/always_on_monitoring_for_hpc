#include "tau_metric_proxy_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/socket.h>
#include <signal.h>

/** How often metrics are pushed up */
static double METRIC_FREQ = 0.1;

 /*
 * @brief Main flag for enabling monitoring
 *
 */
static int __monitoring_enabled = 0;

/**
 * @brief Set to 1 by the TAU_METRIC_PROXY_VERBOSE variable
 * 
 */
static int __is_verbose = 0;

static inline void __check_verbose(void)
{
    char * everbose = getenv("TAU_METRIC_PROXY_VERBOSE");

    if(everbose)
    {
        int val = atoi(everbose);

        if(val)
        {
            __is_verbose = 1;
        }
        else
        {
            __is_verbose = 0;
        }
    }
}


#define tau_metric_proxy_client_log(...)    do { if(__is_verbose) \
                                              { \
                                                fprintf(stderr, "TAUCLIENT (%s:%d): ", __FUNCTION__, __LINE__); \
                                                fprintf(stderr, __VA_ARGS__);\
                                                fprintf(stderr, "\n"); \
                                              } \
                                             } while(0)

#define tau_metric_proxy_client_perror(a) tau_metric_proxy_client_log("%s : %s", a, strerror(errno))


/*******************************
 * METRICS CLIENT SIDE STORAGE *
 *******************************/

struct tau_client_metric_s
{
    pthread_spinlock_t lock;
    double value;
    tau_metric_type_t type;
    char name[METRIC_STRING_SIZE];
    char doc[METRIC_STRING_SIZE];
    struct tau_client_metric_s * next;
};


static inline ssize_t safe_write(int fd, void *buff,  size_t size)
{
	size_t written = 0;
	while( (size - written) != 0 )
	{
		errno = 0;
		ssize_t ret = write(fd, buff + written, size-written);

		if( ret < 0 )
		{
			if(errno == EINTR)
			{
				continue;
			}

			tau_metric_proxy_client_perror("write");
			return ret;
		}

		written += ret;
	}

	return 0;
}

int tau_client_metric_desc_send(int fd, struct tau_client_metric_s *m)
{
    if(!m)
    {
        return -1;
    }

    pthread_spin_lock(&m->lock);

    tau_metric_msg_t msg;
    memset(&msg,0,sizeof(tau_metric_msg_t));
    msg.type = TAU_METRIC_MSG_DESC;
    snprintf(msg.payload.desc.name, METRIC_STRING_SIZE, "%s", m->name);
    snprintf(msg.payload.desc.doc, METRIC_STRING_SIZE, "%s", m->doc);
    msg.payload.desc.type = m->type;
    msg.canary = 0x7;

    pthread_spin_unlock(&m->lock);

    if( safe_write(fd, &msg, sizeof(tau_metric_msg_t) ) < 0)
    {
        return -1;
    }

    return 0;
}


int tau_client_metric_send(int fd, struct tau_client_metric_s *m)
{
    if(!m)
    {
        return -1;
    }

    tau_metric_msg_t msg;
    memset(&msg,0,sizeof(tau_metric_msg_t));
    msg.type = TAU_METRIC_MSG_VAL;
    snprintf(msg.payload.event.name, METRIC_STRING_SIZE, "%s", m->name);
    msg.canary = 0x7;

    pthread_spin_lock(&m->lock);

    switch(m->type)
    {
        case TAU_METRIC_COUNTER:
            /* Get the value and reset to 0 */
            msg.payload.event.value = m->value;
            m->value = 0;
        break;
        case TAU_METRIC_GAUGE:
            /* Send current value */
            msg.payload.event.value = m->value;
        break;
        case TAU_METRIC_NULL:
            return -1;
    }

    pthread_spin_unlock(&m->lock);

    if( safe_write(fd, &msg, sizeof(tau_metric_msg_t) ) < 0)
    {
        return -1;
    }


    return 0;
}

struct tau_client_metric_s * tau_client_metric_new(const char * name, const char * doc, tau_metric_type_t type)
{
    struct tau_client_metric_s * ret = malloc(sizeof(struct tau_client_metric_s));

    if(!ret)
    {
        tau_metric_proxy_client_perror("malloc");
        abort();
    }

    memset(ret, 0, sizeof(struct tau_client_metric_s));

    snprintf(ret->name, METRIC_STRING_SIZE, "%s", name);
    snprintf(ret->doc, METRIC_STRING_SIZE, "%s", doc);
    ret->type = type;

    pthread_spin_init(&ret->lock, 0);

    return ret;
}

typedef struct {
    struct tau_client_metric_s  *metrics;
    int client_fd;
    pthread_spinlock_t lock;
    pthread_t polling_thread;
    volatile int running;
} tau_client_metric_manager;


static tau_client_metric_manager __metric_manager;


static int __unix_connect(const char * path)
{
    signal(SIGPIPE, SIG_IGN);

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);

	if(sock < 0)
	{
		tau_metric_proxy_client_perror("socket");
		return -1;
	}

	struct sockaddr_un addr;

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof( addr.sun_path ) - 1 , "%s", path);

	int ret = connect(sock,
                      (const struct sockaddr *)&addr,
                      sizeof(struct sockaddr_un));

	if(ret < 0)
	{
        tau_metric_proxy_client_perror("connect");
		close(sock);
		return -1;
	}

    tau_metric_proxy_client_log("TAU: CONNECTED to metric proxy @ %s", path);

	return sock;
}

static inline int __metrics_to_fd(int fd)
{
    /* Walk all metrics and send them on the wire */
    pthread_spin_lock(&__metric_manager.lock);

    struct tau_client_metric_s * cur = __metric_manager.metrics;

    while(cur)
    {
        if( tau_client_metric_send(fd, cur) )
        {
            /* Something went wrong just stop sending */
            return 1;
        }
        cur = cur->next;
    }

    pthread_spin_unlock(&__metric_manager.lock);

    return 0;
}


static void * __polling_thread(void *dummy)
{
    unsigned int wait_time = METRIC_FREQ * 1e6;
    unsigned int refresh_rate = wait_time / 10;
    unsigned int wait_count = 0;

    if(refresh_rate)
    {
        wait_count = wait_time / refresh_rate;
    }

    while(__metric_manager.running)
    {

        if( __metrics_to_fd(__metric_manager.client_fd) )
        {
            /* Something went wrong just stop sending */
            __metric_manager.running = 0;
            break;
        }

        /* Now wait 3 seconds */
        int cnt = wait_count;

        while(cnt)
        {
            /* Poll the running flag every MS */
            if(!__metric_manager.running)
            {
                break;
            }

            usleep(refresh_rate);
            cnt--;
        }

    }

    /* Make sure to send metrics when leaving the loop for short programs */
    __metrics_to_fd(__metric_manager.client_fd);

    return NULL;
}

static inline int __send_job_description_fd(int fd)
{
    tau_metric_job_descriptor_t desc;
    tau_metric_job_descriptor_init(&desc);

    tau_metric_msg_t msg;
    memset(&msg,0,sizeof(tau_metric_msg_t));
    msg.type = TAU_METRIC_MSG_JOB_DESCRIPTION;
    msg.canary = 0x7;

    pthread_spin_lock(&__metric_manager.lock);

    /* Send MSG */
    safe_write(fd, &msg, sizeof(tau_metric_msg_t));
    /* Piggyback the description */
    safe_write(fd, &desc, sizeof(tau_metric_job_descriptor_t));

    pthread_spin_unlock(&__metric_manager.lock);

    return 0;
}


int tau_client_metric_manager_init(const char * unix_path)
{
    __metric_manager.metrics = NULL;
    pthread_spin_init(&__metric_manager.lock, 0);

    __metric_manager.client_fd = __unix_connect(unix_path);

    if(__metric_manager.client_fd < 0)
    {
        return -1;
    }

    /* To begin with we say hellow with our own job description */
    __send_job_description_fd(__metric_manager.client_fd);

    /* If we are here we are connected we
       can proceed to start  the polling thread */
    __metric_manager.running = 1;
    if( pthread_create(&__metric_manager.polling_thread,
                       NULL,
                       __polling_thread,
                       NULL) )
    {
        close(__metric_manager.client_fd);
        return -1;
    }

    /* All OK monitoring is enabled */
    __monitoring_enabled = 1;

    return 0;
}

int tau_client_metric_manager_release()
{
    __metric_manager.running = 0;
    pthread_join(__metric_manager.polling_thread, NULL);

    close(__metric_manager.client_fd);

    pthread_spin_lock(&__metric_manager.lock);

    struct tau_client_metric_s * cur = __metric_manager.metrics;
    struct tau_client_metric_s * to_free = NULL;

    while(cur)
    {
        to_free = cur;
        cur = cur->next;
        free(to_free);
    }

    return 0;
}


struct tau_client_metric_s * __tau_client_metric_manager_get(const char * name)
{
    struct tau_client_metric_s * cur = __metric_manager.metrics;

    while(cur)
    {
        if(!strncmp(cur->name, name, METRIC_STRING_SIZE))
        {
            return cur;
        }

        cur = cur->next;
    }

    return NULL;
}


struct tau_client_metric_s * tau_client_metric_manager_get(const char * name)
{
    struct tau_client_metric_s * ret = NULL;

    pthread_spin_lock(&__metric_manager.lock);

    ret = __tau_client_metric_manager_get(name);

    pthread_spin_unlock(&__metric_manager.lock);

    return ret;
}

struct tau_client_metric_s * tau_client_metric_manager_register(const char * name,
                                                                const char * doc,
                                                                tau_metric_type_t type)
{

    pthread_spin_lock(&__metric_manager.lock);

    struct tau_client_metric_s * existing = __tau_client_metric_manager_get(name);

    if(existing)
    {
        pthread_spin_unlock(&__metric_manager.lock);
        tau_metric_proxy_client_log("there is already a registered metric with name %s", name);
        return NULL;
    }

    struct tau_client_metric_s * new = tau_client_metric_new(name, doc, type);

    new->next = __metric_manager.metrics;
    __metric_manager.metrics = new;


    /* Note MM is locked when calling this function */
    tau_client_metric_desc_send(__metric_manager.client_fd,
                                new);

    pthread_spin_unlock(&__metric_manager.lock);

    return new;
}

/**
 * @brief If the user wants to call interactively
 * 
 */
static volatile int __init_done = 0;

static int __is_inhibited(void)
{
    char * v = getenv("TAU_METRIC_PROXY_INIHIBIT_CLIENT");

    if(v)
    {
        int val = atoi(v);

        if(val)
        {
            __init_done = 1;
        }
        else
        {
            __init_done = 0;
        }
    }

    return 0;
}

static inline void _check_refresh(void)
{
    char * refresh = getenv("TAU_METRIC_FREQ");

    if(refresh)
    {
        char *stringEnd = NULL;
        METRIC_FREQ = strtod(refresh, &stringEnd);

        if(METRIC_FREQ && (stringEnd != refresh))
        {
            tau_metric_proxy_client_log("Setting monitoring freq to %g seconds", METRIC_FREQ);
        }
        else
        {
            tau_metric_proxy_client_log("Failed to parse %s reseting monitoring freq to 0.1", refresh);
            METRIC_FREQ = 0.1;
        }
    }
    else
    {
        tau_metric_proxy_client_log("Using default monitoring frequency of %g, use TAU_METRIC_FREQ to alter", METRIC_FREQ);
    }

}

void tau_metric_client_init() __attribute__((constructor));
void tau_metric_client_init()
{
    __is_inhibited();

    if(__init_done)
    {
        return;
    }

    __check_verbose();
    _check_refresh();

    tau_metric_proxy_client_log("Proxy Starting");

    __monitoring_enabled = 0;

    /* This is the default */
    char proxy_addr[1024];
    snprintf(proxy_addr, 1024, "/tmp/tau_metric_proxy.%d.unix", getuid() );


    /* Can override through env */
    const char * env_proxy_addr = getenv("TAU_METRIC_PROXY");

    if(env_proxy_addr)
    {
        snprintf(proxy_addr, 1024, "%s", env_proxy_addr);
    }

    if( tau_client_metric_manager_init(proxy_addr) )
    {
        /* Failed to connect */
        tau_metric_proxy_client_log("failed to connect to monitoring proxy @ %s", proxy_addr);
        return;
    }

    tau_metric_proxy_client_log("Monitoring Proxy Enabled");
    __init_done = 1;
    __monitoring_enabled = 1;
}

int tau_metric_client_connected()
{
    return __monitoring_enabled;
}


void tau_metric_client_release() __attribute__((destructor));
void tau_metric_client_release()
{
    if(!__monitoring_enabled)
    {
        return;
    }

    tau_client_metric_manager_release();

    tau_metric_proxy_client_log("Monitoring Proxy Finalized");

    __monitoring_enabled = 0;
}


/************
 * COUNTERS *
 ************/

tau_metric_counter_t tau_metric_counter_new(const char * name, const char * doc)
{
    if(!__monitoring_enabled)
    {
        return NULL;
    }

    return tau_client_metric_manager_register(name, doc, TAU_METRIC_COUNTER);
}

int tau_metric_counter_incr(tau_metric_counter_t counter, double increment)
{
    if(!__monitoring_enabled)
    {
        return 1;
    }

    if(!counter)
    {
        return 1;
    }


    pthread_spin_lock(&counter->lock);

    counter->value += increment;

    pthread_spin_unlock(&counter->lock);

    return 0;
}

/*********
 * GAUGE *
 *********/

tau_metric_gauge_t tau_metric_gauge_new(const char * name, const char * doc)
{
    if(!__monitoring_enabled)
    {
        return NULL;
    }

    return tau_client_metric_manager_register(name, doc, TAU_METRIC_GAUGE);
}

int tau_metric_gauge_incr(tau_metric_gauge_t gauge, double increment)
{
    if(!__monitoring_enabled)
    {
        return 1;
    }

    if(!gauge)
    {
        return 1;
    }

    pthread_spin_lock(&gauge->lock);

    gauge->value += increment;

    pthread_spin_unlock(&gauge->lock);


    return 0;
}

int tau_metric_gauge_set(tau_metric_gauge_t gauge, double value)
{
    if(!__monitoring_enabled)
    {
        return 1;
    }

    if(!gauge)
    {
        return 1;
    }

    pthread_spin_lock(&gauge->lock);

    gauge->value = value;

    pthread_spin_unlock(&gauge->lock);

    return 0;
}


static inline int __env_fill_string_if_present(char * env, char * dest, size_t size)
{
    char * v = getenv(env);

    if(v)
    {
        snprintf(dest, size, "%s", v);
        return 0;
    }

    return 1;
}


static inline void __env_fill_string_if_present_list(char * env[], int choices, char * dest, size_t size)
{
    int i;

    for(i = 0 ; i < choices ; i++)
    {
        if(!__env_fill_string_if_present(env[i], dest, size))
        {
            break;
        }
    }
}

int64_t __get_value_from_env(const char * value)
{
    int64_t ret = -1;

    char * v = getenv(value);

    if(v)
    {
        ret = (int64_t)atoi(v);
    }

    return ret;
}


void read_command_line_from_proc(char * dest, int len)
{
    /* All zero */
    memset(dest, '\0', len);

    FILE * in = fopen("/proc/self/cmdline", "r");

    size_t ret = fread(dest, sizeof(char), len - 1, in);

    fclose(in);

    dest[ret] = '\0';

    int i;

    for(i = 0 ; i < ret; i++)
    {
        if(dest[i] == '\0')
        {
            dest[i] = ' ';
        }
    }
}



void tau_metric_job_descriptor_init(tau_metric_job_descriptor_t * desc)
{
    /* All empty if we cannot resolve some */
    memset(desc, 0, sizeof(tau_metric_job_descriptor_t));

    char * env_job[] = {"SLURM_JOBID", "PMIX_ID"};
    __env_fill_string_if_present_list(env_job, 2, desc->jobid, 64);

    /* Make sure to concatenate the step id when present */
    char * stepid = getenv("SLURM_STEP_ID");

    if(stepid)
    {
        char tmp[128];
        snprintf(tmp, 64, "%s-%s", desc->jobid, stepid);
        snprintf(desc->jobid, 64, "%s", tmp);
    }

    /* PMIX Puts the rank as part of the job ID*/
    char * point = strchr(desc->jobid, '.');

    if(point)
    {
        *point = '\0';
    }

    desc->size = __get_value_from_env("SLURM_NTASKS");

    if(desc->size < 0)
    {
         desc->size = __get_value_from_env("OMPI_COMM_WORLD_SIZE");
    }

    __env_fill_string_if_present("SLURM_JOB_NODELIST", desc->nodelist, 128);
    __env_fill_string_if_present("SLURM_JOB_PARTITION", desc->partition, 64);
    __env_fill_string_if_present("SLURM_CLUSTER_NAME", desc->cluster, 64);
    __env_fill_string_if_present("SLURM_SUBMIT_DIR", desc->run_dir, 256);

    getcwd(desc->run_dir, 256);


    char * cmd_line_from_launcher = getenv("TAU_LAUNCHER_TARGET_CMD");

    if(cmd_line_from_launcher)
    {
        snprintf(desc->command, 512, "%s", cmd_line_from_launcher);
    }
    else
    {
        read_command_line_from_proc(desc->command, 512);
    }

    desc->start_time = time(NULL);
    desc->end_time = time(NULL);
}
