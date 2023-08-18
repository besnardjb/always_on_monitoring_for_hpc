#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string.h>
#include <time.h>
#include <tau_metric_proxy_client.h>

#include "config.h"


#define TIMESPEC_SET(t)                                 \
  if (clock_gettime(CLOCK_MONOTONIC, &(t))) {           \
    perror("clock_gettime");                            \
  }

#define TIMESPEC_DIFF(end,start,r) {                    \
    (r).tv_sec = (end).tv_sec - (start).tv_sec;         \
    (r).tv_nsec = (end).tv_nsec - (start).tv_nsec;      \
    if ((r).tv_nsec < 0) {                              \
      (r).tv_sec--;                                     \
      (r).tv_nsec += 1000000000L;                       \
    }                                                   \
  }



void __show_help(void)
{
	fprintf(stderr, "tau_metric_proxy_run -m -s -v -h -u [PATH] -S [PID] -- [COMMAND]\n\
\n\
Wrapper to run commands instrumented for the tau_metric_proxy.\n\n");
#ifdef TAU_METRIC_PROXY_MPI_ENABLED
	fprintf(stderr, "-m: enable MPI profiling\n");
#endif
	fprintf(stderr, "-s: enable strace profiling (requires tau_strace in path)\n\
-F [FREQ in sec]: collection freqency in seconds\n\
-S [PID]: attach tau_strace to a running process id (COMMAND is ignored, no MPI instrum)\n\
-u [PATH]: specify path to tau_metric_proxy push gateway UNIX socket\n\
-v: enable verbose output\n\
-h: show this help\n");
}

static inline void __set_mpi_preload(void)
{
	char mpi_wrapper[1024];
	char value[2048];

	snprintf(mpi_wrapper, 1024, "%s/lib/libtaumetricmpiwrap.so", TAU_METRIC_PROXY_PREFIX);
	char *current = getenv("LD_PRELOAD");

	if(current)
	{
		snprintf(value, 2048, "%s:%s", mpi_wrapper, current);
	}
	else
	{
		snprintf(value, 2048, "%s", mpi_wrapper);
	}

	setenv("LD_PRELOAD", value, 1);
}

static inline int __tau_strace_in_path(void)
{
	tau_metric_client_inhibit();
	pid_t c = fork();

	if(c == 0)
	{
		int devnull = open("/dev/null", O_WRONLY);

		if(devnull < 0)
		{
			return 1;
		}

		dup2(devnull, STDOUT_FILENO);
		dup2(devnull, STDERR_FILENO);
		close(devnull);

		char *tau_strace_cmd[3] = { "tau_strace", "-V", NULL };

		if(execvp(tau_strace_cmd[0], tau_strace_cmd) < 0)
		{
			return 1;
		}
	}
	else
	{
		tau_metric_client_enable();
		int status = 0;
		wait(&status);
		return (WEXITSTATUS(status) == 0);
	}

	return 0;
}

void __shift_to_next(char **array, int off)
{
	char * me = array[off];

	if(me)
	{
		/* Propagate down to save value on stack */
		__shift_to_next(array, off + 1);
	}

	/* Set saved value when going up */
	array[off + 1] = me;
}

int strace_attach(char * pid)
{
	pid_t c = fork();

	if(c == 0)
	{

		char * command[] = {"strace", "-c", "-p", pid};

		if( execvp(command[0], command) < 0)
		{
			perror("could not launch command");
			return 1;
		}

	}
	else
	{
		int status;
		wait(&status);
		return WEXITSTATUS(status);
	}

	return 0;
}


int main(int argc, char **argv)
{
	tau_metric_client_init();

	int enable_mpi    = 0;
	int enable_strace = 0;
	char * enable_strace_attach_pid = NULL;

	int opt;

	while( (opt = getopt(argc, argv, ":hvsmS:u:F:") ) != -1)
	{
		switch(opt)
		{
			case 'h':
				__show_help();
				return 1;

			case 'v':
				setenv("TAU_METRIC_PROXY_VERBOSE", "1", 1);
				break;

			case 'm':
#ifdef TAU_METRIC_PROXY_MPI_ENABLED
				enable_mpi = 1;
#else
				fprintf(stderr, "cannot handle '-m' tau_metric_proxy was not compiled with MPI support\n");
				return 1;
#endif
				break;

			case 's':
				if(__tau_strace_in_path() )
				{
					enable_strace = 1;
				}
				else
				{
					fprintf(stderr, "Cannot locate tau_strace in PATH, ignoring '-s' argument\n");
				}
				break;
			case 'S':
				if(__tau_strace_in_path() )
				{
					enable_strace_attach_pid = optarg;
				}
				break;
			case 'F':
				setenv("TAU_METRIC_FREQ",  optarg, 1);
				break;
			case 'u':
				setenv("TAU_METRIC_PROXY", optarg, 1);
				/* Try to reconnect the client */
				tau_metric_client_init();
				break;
			case '?':
				fprintf(stderr, "No such option: '-%c'", optopt);
				return 1;
		}
	}

	if(enable_strace_attach_pid)
	{
		if(!tau_metric_client_connected())
		{
			fprintf(stderr, "Error: could not connect to the tau_metric_proxy use '-u' to alter socket if needed.\n");
			return 1;
		}

		tau_metric_counter_t attach_counter = tau_metric_counter_new("tau_metric_proxy_run_total{operation=\"attach\"}",
																	 "Number of attach with the tau_metric_proxy_run command");
		tau_metric_counter_incr(attach_counter, 1.0);

		/* We are in attach mode */
		return strace_attach(enable_strace_attach_pid);
	}



	char **extra_arguments = malloc(sizeof(char *) * ( (argc - optind) + 4 /* Room for NULL and 'strace -c --' wrapper */) );

	if(!extra_arguments)
	{
		perror("malloc");
		return 1;
	}

	if(argc - optind)
	{
		int i   = optind;
		int cnt = 0;
		/* Make sure to also copy the NULL */
		for(; i <= argc; i++)
		{
			extra_arguments[cnt] = argv[i];
			cnt++;
		}
	}

	/* To help clustering later on we cut out the command here */
	char actual_command[512];
	int i=0;
	actual_command[0] = '\0';
	do
	{
		char tmp[128];
		snprintf(tmp, 128, "%s ", extra_arguments[i]);
		strncat(actual_command, tmp, 511);
		i++;
	}while(extra_arguments[i]);

	setenv("TAU_LAUNCHER_TARGET_CMD", actual_command, 1);


	if(tau_metric_client_connected() )
	{
		if(enable_mpi)
		{
			__set_mpi_preload();
		}

		if(enable_strace)
		{
			/* We need to shift the extra arg array by 3*/
			__shift_to_next(extra_arguments, 0);
			__shift_to_next(extra_arguments, 0);
			__shift_to_next(extra_arguments, 0);

			/* Insert the tau_strace command */
			extra_arguments[0] = "tau_strace";
			extra_arguments[1] = "-c";
			extra_arguments[2] = "--";
		}
	}

	tau_metric_counter_t run_counter = tau_metric_counter_new("tau_metric_proxy_run_total{operation=\"exec\"}",
															  "Number of launch with the tau_metric_proxy_run command");
	tau_metric_counter_t runtime = tau_metric_counter_new("tau_metric_proxy_run_total{metric=\"time\"}", "Total run time in seconds");


    struct timespec tbeg, tend, ttotal;
    TIMESPEC_SET(tbeg);

	tau_metric_counter_t crash_counter = tau_metric_counter_new("tau_metric_proxy_run_total{operation=\"badreturn\"}",
																					"Number of executions returning a non-0 status");

	/* Now run subprogram */
	pid_t c = fork();
	int retcode = 0;

	if(!c)
	{
		if( execvp(extra_arguments[0], extra_arguments) < 0)
		{
			perror("could not launch command");
			return 1;
		}
	}
	else
	{
		tau_metric_counter_incr(run_counter, 1.0);

		int status;
		wait(&status);
		retcode = WEXITSTATUS(status);

		TIMESPEC_SET(tend);
		TIMESPEC_DIFF(tend, tbeg, ttotal)
		tau_metric_counter_incr(runtime, ttotal.tv_sec + ttotal.tv_nsec / 1000000000.0 );
	}

	if(retcode != 0)
	{
		tau_metric_counter_incr(crash_counter, 1.0);
	}

	tau_metric_client_release();
	return retcode;
}
