#include "exporter.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include "log.h"
#include "metrics.h"
#include "server.h"



static int __bind_listening_thread(const char *port)
{
	struct addrinfo *res = NULL;
	struct addrinfo  hints;

	memset(&hints, 0, sizeof(hints) );

	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE;

	signal(SIGPIPE, SIG_IGN);

	/* Ici getaddrinfo permet de générer les
	 * configurations demandées */
	int ret = getaddrinfo(NULL, port, &hints, &res);

	if(ret < 0)
	{
		herror("getaddrinfo");
		return -1;
	}

	struct addrinfo *tmp;

	int listen_sock = -1;

	int binded = 0;

	for(tmp = res; tmp != NULL; tmp = tmp->ai_next)
	{
		listen_sock = socket(tmp->ai_family, tmp->ai_socktype, tmp->ai_protocol);

		if(listen_sock < 0)
		{
			tau_metric_proxy_perror("sock");
			continue;
		}

		ret = bind(listen_sock, tmp->ai_addr, tmp->ai_addrlen);

		if(ret < 0)
		{
			close(listen_sock);
			tau_metric_proxy_perror("bind");
			continue;
		}

		binded = 1;
		break;
	}

	if(!binded)
	{
		tau_metric_proxy_error("Failed to bind on 0.0.0.0:%s", port);
		return -1;
	}

	/* On commence a ecouter */
	ret = listen(listen_sock, 2);

	if(ret < 0)
	{
		tau_metric_proxy_perror("listen");
		return -1;
	}

	return listen_sock;
}

static void __write_http_header(size_t len, int code, char *content_type, int fd)
{
	char header_buffer[512];

	char *response_type = NULL;

	switch(code)
	{
		case 200:
			response_type = "200 OK";
			break;

		case 404:
			response_type = "404 Not Found";
			break;
	}

	snprintf(header_buffer, 512, "HTTP/1.1 %s\nContent-Type: %s\nContent-Length: %ld\n\n", response_type, content_type, len);
	safe_write(fd, header_buffer, strlen(header_buffer) );
	//printf(header_buffer);
}

#define METRIC_TREE_MAX_SIBLINGS    4096

struct metric_tree
{
	char                basename[METRIC_STRING_SIZE];
	metric_t *          metrics[METRIC_TREE_MAX_SIBLINGS];
	int                 siblings_count;
	struct metric_tree *next;
};


char *metric_tree_get_basename(char *buff, metric_t *m)
{
	snprintf(buff, METRIC_STRING_SIZE, "%s", m->name);
	char *bracket = strchr(buff, '{');

	if(bracket)
	{
		*bracket = '\0';
	}

	return buff;
}

struct metric_tree *metric_tree_regiter(struct metric_tree *current_tree, metric_t *metric)
{
	char metric_base[METRIC_STRING_SIZE];

	metric_tree_get_basename(metric_base, metric);

	tau_metric_proxy_log_verbose("Registering %s", metric->name);

	/* Now look for metric in the tree */
	struct metric_tree *tmp = current_tree;

	while(tmp)
	{
		if(!strcmp(metric_base, tmp->basename) )
		{
			/* DID Match*/
			if(tmp->siblings_count < METRIC_TREE_MAX_SIBLINGS)
			{
				tmp->metrics[tmp->siblings_count] = metric;
				tmp->siblings_count++;
			}
			else
			{
				tau_metric_proxy_error("submetric overflow some metrics were dropped :%s", metric->name);
			}

			return current_tree;
		}

		tmp = tmp->next;
	}

	/* If we are here we do not known this base metric yet */
	struct metric_tree *new = (struct metric_tree *)malloc(sizeof(struct metric_tree) );

	if(!new)
	{
		tau_metric_proxy_perror("Failed to malloc new metric in tree");
		return current_tree;
	}

	snprintf(new->basename, METRIC_STRING_SIZE, "%s", metric_base);
	new->metrics[0]     = metric;
	new->siblings_count = 1;
	new->next           = current_tree;

	return new;
}

void metric_tree_free(struct metric_tree *mt)
{
	struct metric_tree *tmp = mt;

	while(tmp)
	{
		struct metric_tree *to_free = tmp;
		tmp = tmp->next;
		free(to_free);
	}
}

struct growing_string
{
	char * buffer;
	size_t current_offset;
	size_t buffer_size;
};


void *growing_string_alloc(struct growing_string *gb, size_t init_size)
{
	gb->buffer = malloc(init_size);

	if(!gb->buffer)
	{
		tau_metric_proxy_perror("malloc");
		return NULL;
	}

	gb->buffer_size    = init_size;
	gb->current_offset = 1;

	memset(gb->buffer, 0, gb->buffer_size);

	return gb->buffer;
}

void *growing_string_append(struct growing_string *gb, const char *to_add)
{
	int needed_size = strlen(to_add);

	if( (gb->buffer_size - gb->current_offset) <= needed_size)
	{
		gb->buffer_size *= 2;
		printf("New size %ld for %d", gb->buffer_size, needed_size);
		gb->buffer = realloc(gb->buffer, gb->buffer_size);

		memset(gb->buffer + gb->buffer_size / 2, 0, gb->buffer_size / 2);


		if(!gb->buffer)
		{
			tau_metric_proxy_perror("realloc");
			return NULL;
		}
	}

	strcat(gb->buffer + gb->current_offset - 1, to_add);
	gb->current_offset += needed_size;

	return gb->buffer;
}

static char *__serialize_metric_type(metric_t *m, char *buff, int len)
{
	switch(m->type)
	{
		case TAU_METRIC_COUNTER:
			snprintf(buff, len, "counter");
			break;

		case TAU_METRIC_GAUGE:
			snprintf(buff, len, "gauge");
			break;
	}

	return buff;
}

static char *__serialize_metric_value(metric_t *m, char *buff, int len)
{
	switch(m->type)
	{
		case TAU_METRIC_COUNTER:
			snprintf(buff, len, "%s %f\n", m->name, m->metrics.counter.value);
			break;

		case TAU_METRIC_GAUGE:
			snprintf(buff, len, "%s %f\n", m->name, m->metrics.gauge.avg);
			break;
	}

	return buff;
}

char *metric_tree_serialize(struct metric_tree *mt, size_t *len)
{
	struct growing_string gb;
	void *buff = growing_string_alloc(&gb, 1024 * 1024);

	/* Now walk the metric array */
	struct metric_tree *tmp = mt;

	while(tmp)
	{
		tau_metric_proxy_log_verbose("%s has %d siblings", tmp->basename, tmp->siblings_count);
		char buff[METRIC_STRING_SIZE * 2];
		snprintf(buff, METRIC_STRING_SIZE * 2, "# HELP %s %s", tmp->basename, tmp->metrics[0]->doc);
		/* Generate the metric header */
		growing_string_append(&gb, buff);

		char type[64];
		__serialize_metric_type(tmp->metrics[0], type, 64);
		snprintf(buff, METRIC_STRING_SIZE * 2, "\n# TYPE %s %s\n", tmp->basename, type);


		growing_string_append(&gb, buff);
		int i;
		for(i = 0; i < tmp->siblings_count; i++)
		{
			tau_metric_proxy_log_verbose("%s is %d / %d", tmp->metrics[i]->name, i, tmp->siblings_count);

			__serialize_metric_value(tmp->metrics[i], buff, METRIC_STRING_SIZE * 2);
			growing_string_append(&gb, buff);
		}

		tmp = tmp->next;
	}

	*len = gb.current_offset;

	return gb.buffer;
}

static int __build_metric_tree(metric_t *m, void *vppmt)
{
	struct metric_tree **ppmt = (struct metric_tree **)vppmt;

	*ppmt = metric_tree_regiter(*ppmt, m);

	return 0;
}

static char *__generate_metrics(size_t *len)
{
	*len = 0;

	struct metric_tree *mt = NULL;

	metric_array_iterate(metric_array_get_main(), __build_metric_tree, (void *)&mt);

	char *ret = metric_tree_serialize(mt, len);

	metric_tree_free(mt);

	return ret;
}

static void *__send_metrics(void *pfd)
{
	int fd = *( (int *)pfd);

	/* First read what is requested */
	char buffer[1024];

	FILE *socket = fdopen(fd, "rw+");

	if(!socket)
	{
		tau_metric_proxy_perror("fdopen");
		close(fd);
	}

	while(fgets(buffer, 1024, socket) )
	{
		if(strlen(buffer) < 6)
		{
			continue;
		}

		char *http = strstr(buffer, " HTTP");

		if(http)
		{
			*http = '\0';
		}

		if(buffer[0] == 'G' && buffer[1] == 'E' && buffer[2] == 'T')
		{
			char *file_path = &buffer[4];

			if(*file_path == '/' && strlen(file_path) == 1)
			{
				/* Get root data */
				static const char *const default_page = "\
<html>\
<head><title>Node Exporter</title></head>\
<body>\
<h1>TAU Metrics Proxy Exporter</h1>\
<p><a href='/metrics'>Metrics</a></p>\
</body>\
</html>";
				__write_http_header(strlen(default_page), 200, "text/html", fd);
				safe_write(fd, (void *)default_page, strlen(default_page) );
			}
			else
			{
				/* Skip / */
				file_path++;
			}

			tau_metric_proxy_log_verbose("GET %s", file_path);

			if(strstr(file_path, "metrics") )
			{
				size_t len  = 0;
				char * data = __generate_metrics(&len);

				__write_http_header(len, 200, "text/plain", fd);
				safe_write(fd, (void *)data, len);

				free(data);
				break;
			}
			else
			{
				__write_http_header(0, 404, "text/html", fd);
			}
		}
	}

	fclose(socket);


	close(fd);

	return NULL;
}

static void *__accept_loop(void *pexporter)
{
	tau_metric_exporter_t *exporter = (tau_metric_exporter_t *)pexporter;

	struct sockaddr client_info;
	socklen_t       addr_len;

	while(exporter->running)
	{
		int client_socket = accept(exporter->listen_fd, &client_info, &addr_len);

		if(client_socket < 0)
		{
			break;
		}

		int *fd_copy = malloc(sizeof(int) );

		if(!fd_copy)
		{
			tau_metric_proxy_perror("malloc");
			abort();
		}

		*fd_copy = client_socket;


		/* Send metrics to client */
		pthread_t req_th;
		pthread_create(&req_th, NULL, __send_metrics, fd_copy);
		pthread_detach(req_th);
	}

	tau_metric_proxy_log("Exporter thread leaving");

	return NULL;
}

int tau_metric_exporter_init(tau_metric_exporter_t *exporter, const char *port)
{
	exporter->listen_fd = __bind_listening_thread(port);

	if(exporter->listen_fd < 0)
	{
		tau_metric_proxy_error("Failed to bind listening socket to 0.0.0.0:%s", port);
		return -1;
	}

	exporter->running = 1;
	if(pthread_create(&exporter->listening_thread, NULL, __accept_loop, (void *)exporter) < 0)
	{
		tau_metric_proxy_perror("pthread_create");
		close(exporter->listen_fd);
		return -1;
	}

	tau_metric_proxy_log("now listening on 0.0.0.0:%s", port);



	return 0;
}

int tau_metric_exporter_release(tau_metric_exporter_t *exporter)
{
	if(exporter->running)
	{
		exporter->running = 0;
		shutdown(exporter->listen_fd, SHUT_RDWR);
		close(exporter->listen_fd);
	}

	pthread_join(exporter->listening_thread, NULL);

	return 0;
}
