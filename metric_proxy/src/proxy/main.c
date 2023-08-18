#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>

#include "log.h"
#include "metrics.h"
#include "exporter.h"
#include "utils.h"
#include "server.h"
#include "tau_metric_proxy_client.h"


static inline int __is_numeric(const char *s)
{
	int len = strlen(s);
	int i;

	for(i = 0; i < len; i++)
	{
		if(!isdigit(s[i]) )
		{
			return 0;
		}
	}

	return 1;
}



struct _send_descs_state
{
	int target_fd;
	int left_to_send;
};

static int __send_metrics_desc(metric_t *m, void *arg)
{
	struct _send_descs_state * st = (struct _send_descs_state*)arg;

	if(st->left_to_send == 0)
	{
		return 1; /* DONE */
	}

	tau_metric_descriptor_t desc;
	snprintf(desc.doc, METRIC_STRING_SIZE, "%s", m->doc);
	snprintf(desc.name, METRIC_STRING_SIZE, "%s", m->name);
	desc.type = m->type;

	/* Send the description */
	if( safe_write(st->target_fd, &desc, sizeof(tau_metric_descriptor_t)) != 0)
	{
		return 1;
	}

	st->left_to_send--;

	return 0;
}

static int __send_metrics_event(metric_t *m, void *arg)
{
	struct _send_descs_state * st = (struct _send_descs_state*)arg;

	if(st->left_to_send == 0)
	{
		return 1; /* DONE */
	}

	tau_metric_event_t desc;
	snprintf(desc.name, METRIC_STRING_SIZE, "%s", m->name);

	desc.update_ts = m->last_ts;

	switch (m->type)
	{
		case TAU_METRIC_COUNTER:
			desc.value = m->metrics.counter.value;
			break;
		case TAU_METRIC_GAUGE:
			desc.value = m->metrics.gauge.avg;
			break;
		case TAU_METRIC_NULL:
			desc.value = 0;
			break;
	}

	tau_metric_proxy_log_verbose("Sending %s = %g", desc.name, desc.value);

	/* Send the value  */
	if( safe_write(st->target_fd, &desc, sizeof(tau_metric_event_t)) != 0)
	{
		return 1;
	}

	st->left_to_send--;

	return 0;
}


static int __list_metrics(int source_fd)
{
	/* Fist get the metric count */
	int metric_count = metric_array_count(metric_array_get_main());

	/* Send the count */
	if( safe_write(source_fd, &metric_count, sizeof(int)) != 0)
	{
		return 1;
	}

	/* Now iterate to send the data up to count (in case there are newcomers) */
	struct _send_descs_state state;
	state.left_to_send = metric_count;
	state.target_fd = source_fd;

	if( metric_array_iterate(metric_array_get_main(), __send_metrics_desc, &state) != 0)
	{
		return 1;
	}

	/* Case were elements were removed during the walk
		we still want to send the number of requested elements */
	if(state.left_to_send > 0)
	{
		while(state.left_to_send)
		{
			tau_metric_descriptor_t nulldesc;
			nulldesc.type = TAU_METRIC_NULL;

			if( safe_write(source_fd, &nulldesc, sizeof(tau_metric_descriptor_t)) != 0)
			{
				return 1;
			}

			state.left_to_send--;
		}
	}

	return 0;
}

static int __get_one(int source_fd, char * name)
{
	metric_t *existing_metric = metric_array_get(metric_array_get_main(), name);

	struct _send_descs_state st;
	st.left_to_send = 1;
	st.target_fd = source_fd;

	tau_metric_proxy_log_verbose("Get one : '%s' (%s)", name, existing_metric?"FOUND":"NOT FOUND");

	if(existing_metric)
	{
		return __send_metrics_event(existing_metric, &st);
	}
	else
	{
		tau_metric_event_t nulldesc;
		nulldesc.value = 0;
		nulldesc.update_ts = 0;
		snprintf(nulldesc.name, METRIC_STRING_SIZE, "");

		if( safe_write(source_fd, &nulldesc, sizeof(tau_metric_event_t)) != 0)
		{
			return 1;
		}
	}

	return 0;
}


static int __get_metrics(int source_fd)
{
	/* Fist get the metric count */
	int metric_count = metric_array_count(metric_array_get_main());

	/* Send the count */
	if( safe_write(source_fd, &metric_count, sizeof(int)) != 0)
	{
		return 1;
	}

	/* Now iterate to send the data up to count (in case there are newcomers) */
	struct _send_descs_state state;
	state.left_to_send = metric_count;
	state.target_fd = source_fd;

	if( metric_array_iterate(metric_array_get_main(), __send_metrics_event, &state) != 0)
	{
		return 1;
	}

	/* Case were elements were removed during the walk
		we still want to send the number of requested elements */
	if(state.left_to_send > 0)
	{
		while(state.left_to_send)
		{
			tau_metric_event_t nulldesc;
			nulldesc.value = 0;
			nulldesc.update_ts = 0;
			snprintf(nulldesc.name, METRIC_STRING_SIZE, "");

			if( safe_write(source_fd, &nulldesc, sizeof(tau_metric_event_t)) != 0)
			{
				return 1;
			}

			state.left_to_send--;
		}
	}

	return 0;
}

struct per_client_context
{
	int init_done;
	tau_metric_job_descriptor_t job_desc;
	metric_array_t * metric_array;
};

void store_per_job_metrics(tau_metric_job_descriptor_t *desc, metric_array_t * metrics)
{
	tau_metric_proxy_log_verbose("Storing per job metrics");

	metric_per_job_dump(desc, metrics);

}

void __client_leaving_callback(int source_fd, void * p_extra_ctx)
{
	struct per_client_context * ctx = (struct per_client_context*)p_extra_ctx;

	if(ctx->metric_array)
	{
		metric_array_list_relax(ctx->job_desc.jobid);
	}
}

static inline int __push_metric_desc(tau_metric_msg_t *msg, metric_array_t * ma)
{
	/* See if we need to register the new metric */
	metric_t *existing_metric = metric_array_get(ma, msg->payload.desc.name);

	if(!existing_metric)
	{
		metric_t *new_metric = metric_init(msg->payload.desc.name, msg->payload.desc.doc, msg->payload.desc.type);

		/* Try to insert */
		if(metric_array_register(ma, new_metric) )
		{
			/* There was a race metric is already here */
			metric_release(new_metric);
		}

		/* Get the metric again */
		existing_metric = metric_array_get(ma, msg->payload.desc.name);
	}

	/* Check types do match */
	if(existing_metric->type != msg->payload.desc.type)
	{
		tau_metric_proxy_error("Mismatching types for metric %s, disconnecting client\n", existing_metric->name);
		return 1;
	}

	return 0;
}

static inline int __update_metric_value(tau_metric_msg_t *msg, metric_array_t * ma)
{
		metric_t *existing_metric = metric_array_get(ma, msg->payload.event.name);

		if(!existing_metric)
		{
			tau_metric_proxy_error("No such metric %s, disconnecting client\n", msg->payload.event.name);
			return 1;
		}

		metric_update(existing_metric, &msg->payload.event);

		return 0;
}



int __unix_server_callback(int source_fd, tau_metric_msg_t *msg, void * p_extra_ctx)
{
	//tau_metric_msg_print(msg);

	struct per_client_context * ctx = (struct per_client_context*)p_extra_ctx;

	if(!ctx)
	{
		tau_metric_proxy_error("We did not get a context in client loop");
		return 1;
	}

	switch(msg->type)
	{
		case TAU_METRIC_MSG_JOB_DESCRIPTION:
			/* Copy the job description locally */
			memset(ctx, 0, sizeof(struct per_client_context));
			safe_read(source_fd, &ctx->job_desc, sizeof(tau_metric_job_descriptor_t));
			//tau_metric_job_descriptor_print(&ctx->job_desc);
			/* Lookup for the local metric array */
			ctx->metric_array = metric_array_list_acquire(&ctx->job_desc);
		break;
		case TAU_METRIC_MSG_DESC:
		{
			/* Push in main array */
			if( __push_metric_desc(msg, metric_array_get_main()) )
			{
				return 1;
			}

			/* Push in per job array */
			if(ctx->metric_array)
			{
				if( __push_metric_desc(msg, ctx->metric_array) )
				{
					return 1;
				}
			}

			/* If we are here all OK */
		}
		break;

		case TAU_METRIC_MSG_VAL:
		{
			if(__update_metric_value(msg, metric_array_get_main()))
			{
				return 1;
			}

			if(ctx->metric_array)
			{
				if(__update_metric_value(msg, ctx->metric_array))
				{
					return 1;
				}
			}
		}
		break;

		case TAU_METRIC_MSG_LIST_ALL:
			return __list_metrics(source_fd);
		break;

		case TAU_METRIC_MSG_GET_ALL:
			return __get_metrics(source_fd);
		break;

		case TAU_METRIC_MSG_GET_ONE:
			return __get_one(source_fd, msg->payload.desc.name);
		break;

		default:
			if( (0 < msg->type) && (msg->type < TAU_METRIC_MSG_COUNT) )
			{
				tau_metric_proxy_error("%s callback not implemented yet", tau_metric_msg_type_name[msg->type]);
			}
			else
			{
				tau_metric_proxy_error("No such message type %d", msg->type);
			}
			return 1;
	}



	return 0;
}

tau_metric_server_t   unix_server;
tau_metric_exporter_t prom_exporter;

void __server_stop_on_interupt(int sig)
{
	tau_metric_proxy_log("Received SIGINT, stoping servers ... \n");
	tau_metric_exporter_release(&prom_exporter);
	tau_metric_server_stop(&unix_server);
	/* Initialize metrics storage */
	metric_array_release(metric_array_get_main());
	metric_array_list_release();
	metric_per_job_release();
	tau_metric_proxy_log("DONE will now exit.\n");
	exit(1);
}

static void __show_help(void)
{
	fprintf(stderr,"tau_metric_proxy -p [EXPORTER PORT] -s [UNIX_GATEWAY] -v\n\
\n\
A high performance push gateway for Prometeus.\n\
\n\
-p [PORT]: where to run the prometheus exporter (default: 1337)\n\
-u [PATH]: where to run the prometheus UNIX gateway (default: /tmp/tau_metric_proxy.[UID].unix)\n\
-i: do not aggregate profiles (default yes) to be used for worker nodes\n\
-h: show this help\n");
}

int main(int argc, char **argv)
{
	/* Register stop handle on sigint */
	signal(SIGINT, __server_stop_on_interupt);

	/* Handle options */

	char exporter_port[8];

	snprintf(exporter_port, 8, "%s", "1337");

	char unix_socket_path[1024];

	snprintf(unix_socket_path, 1024, "/tmp/tau_metric_proxy.%d.unix", getuid());

	/* Make sure a previous socket is not present */
	if(utils_ispath(unix_socket_path))
	{
		/* Try to remove it or fail */
		tau_metric_proxy_log("Trying to remove a previous proxy socket at %s", unix_socket_path);
		if(unlink(unix_socket_path) < 0)
		{
			tau_metric_proxy_perror("unlink");
			tau_metric_proxy_error("Failed to remove existing socket");
			return 1;
		}
		tau_metric_proxy_log("Previous socket removed @ %s", unix_socket_path);
	}





	struct passwd *pw = getpwuid(getuid());
	const char *homedir = pw->pw_dir;

	char profiles_path[1024];
	snprintf(profiles_path, 1024, "%s/.tauproxy/", homedir);

	int is_profile_merger = 1;

	int opt;

	while( (opt = getopt(argc, argv, ":p:u:P:ivh") ) != -1)
	{
		switch(opt)
		{
			case 'h':
				__show_help();
				return 1;
			case 'v':
				tau_metric_proxy_log("Verbose mode activated");
				tau_metric_proxy_set_verbose();
				break;

			case 'p':
				if(!__is_numeric(optarg) )
				{
					tau_metric_proxy_error("-p only takes numeric arguments had: %s", optarg);
					return 1;
				}
				tau_metric_proxy_log("Exporter port set to %s", optarg);
				snprintf(exporter_port, 8, "%s", optarg);
				break;

			case 'u':
				tau_metric_proxy_log("UNIX push gateway socket set to %s", optarg);
				snprintf(unix_socket_path, 1024, "%s", optarg);
				break;
			case 'P':
				tau_metric_proxy_log("Profile storage path set to %s", optarg);
				snprintf(profiles_path, 1024, "%s", optarg);
				break;
			case 'i':
				tau_metric_proxy_log("Profile aggregation on this proxy was inhibited");
				is_profile_merger = 0;
				break;
			case '?':
				tau_metric_proxy_error("No such option: '-%c'", optopt);
				return 1;
		}
	}

	/* Initialize main metrics storage */
	metric_array_init(metric_array_get_main());
	metric_array_list_init(store_per_job_metrics);

	if( metric_per_job_init(profiles_path, is_profile_merger) )
	{
		return 1;
	}

	/* Start the exporter */
	if(tau_metric_exporter_init(&prom_exporter, exporter_port) < 0)
	{
		tau_metric_proxy_error("Failed to start TAU Prometheus exporter\n");
		return 1;
	}

	/* Start UNIX socket Server (block the process) */


	if(tau_metric_server_run(&unix_server,
	                         unix_socket_path,
	                         __unix_server_callback,
							 __client_leaving_callback,
							 sizeof(struct per_client_context)) < 0)
	{
		return 1;
	}


	/* Server has no reason to come back from here all is handled in SIGINT */

	return 0;
}
