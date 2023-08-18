#include "server.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "log.h"

/**********
* HELPER *
**********/

ssize_t safe_write(int fd, void *buff, size_t size)
{
	size_t written = 0;

	while( (size - written) != 0)
	{
		errno = 0;
		ssize_t ret = write(fd, buff + written, size - written);

		if(ret < 0)
		{
			if(errno == EINTR)
			{
				continue;
			}

			tau_metric_proxy_perror("write");
			return ret;
		}

		written += ret;
	}

	return 0;
}

ssize_t safe_read(int fd, void *buff, size_t size)
{
	int off = 0;

	size_t size_left = size;

	while(size_left)
	{
		int ret = read(fd, buff + off, size_left);

		if(ret < 0)
		{
			return -1;
		}

		if(ret == 0)
		{
			return 0;
		}

		off       += ret;
		size_left -= ret;
	}

	return size;
}

/******************
* CLIENT CONTEXT *
******************/

static void *__client_polling(void *pctx)
{
	struct tau_metric_server_client_ctx_s *ctx = ( struct tau_metric_server_client_ctx_s *)pctx;


	tau_metric_proxy_log_verbose("New Proxy Client");

	while(ctx->running)
	{
		tau_metric_msg_t msg;

		int ret = safe_read(ctx->client_fd, &msg, sizeof(tau_metric_msg_t) );

		if(ret < 0)
		{
			/* Failed to read */
			tau_metric_proxy_error("CLIENT : Failed to read");
			goto CLIENT_REJECT;
		}

		if(ret == 0)
		{
			/* EOF */
			goto CLIENT_REJECT;
		}

		/* Make sure canary is correct */
		if(msg.canary != 0x7)
		{
			tau_metric_proxy_error("CLIENT : Bad canary");
			goto CLIENT_REJECT;
		}

		/* Send message to upper layer */
		if( (ctx->callback)(ctx->client_fd, &msg, ctx->extra_ctx) )
		{
			/* Upper layer disqualified client */
			tau_metric_proxy_error("CLIENT : callback rejected");
			goto CLIENT_REJECT;
		}
	}

CLIENT_REJECT:
	if(ctx->exit_callback)
	{
		(ctx->exit_callback)(ctx->client_fd, ctx->extra_ctx);
	}
	ctx->running = 0;
	close(ctx->client_fd);
	return NULL;
}

struct tau_metric_server_client_ctx_s *tau_metric_server_client_ctx_new(int client_fd, 
																		tau_metric_proxy_server_callback_t cb,
																		tau_metric_proxy_server_end_callback_t exit_cb,
																		size_t extra_ctx_size)
{
	struct tau_metric_server_client_ctx_s *ctx = malloc(sizeof(struct tau_metric_server_client_ctx_s) );

	if(!ctx)
	{
		return NULL;
	}

	ctx->running   = 1;
	ctx->client_fd = client_fd;
	ctx->next      = NULL;
	ctx->callback  = cb;
	ctx->exit_callback = exit_cb;
	ctx->extra_ctx = malloc(extra_ctx_size);
	if(ctx->extra_ctx)
	{
		memset(ctx->extra_ctx, 0, extra_ctx_size);
	}

	if(pthread_create(&ctx->client_thread, NULL, __client_polling, (void *)ctx) < 0)
	{
		close(client_fd);
		free(ctx);
		return NULL;
	}

	return ctx;
}

int tau_metric_server_client_ctx_free(struct tau_metric_server_client_ctx_s *ctx)
{
	/* Thread may have left beforehand
	 * for example in case of garbage data */
	if(ctx->running)
	{
		ctx->running = 0;
		close(ctx->client_fd);
	}

	pthread_join(ctx->client_thread, NULL);

	free(ctx->extra_ctx);

	free(ctx);

	return 0;
}

/**********************
* UNIX SOCKET SERVER *
**********************/

static inline int __bind_unix_socket(const char *path)
{
/* UNIX socket descriptor */
	struct sockaddr_un addr;

	/* Clear it */
	memset(&addr, 0, sizeof(addr) );
	/* Set family to UNIX */
	addr.sun_family = AF_UNIX
	                  /* Set socket PATH */;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	/* Create socket FD */
	int listen_socket = socket(AF_UNIX, SOCK_STREAM, 0);

	if(listen_socket < 0)
	{
		tau_metric_proxy_perror("socket");
		return -1;
	}

	/* BIND the socket to UNIX socket */
	int ret = bind(listen_socket, ( struct sockaddr * )&addr, sizeof(addr) );

	if(ret < 0)
	{
		tau_metric_proxy_perror("bind");
		tau_metric_proxy_error("Failed to bind on unix://%s", path);
		return -1;
	}

	/* On commence a ecouter */
	ret = listen(listen_socket, 2);

	if(ret < 0)
	{
		tau_metric_proxy_perror("listen");
		return -1;
	}


	tau_metric_proxy_log("UNIX push gateway running on %s", path);

	return listen_socket;
}

static void __client_list_prune(tau_metric_server_t *server)
{
	/* Some clients may already have left the list (disconnecting)
	 * it is then important to remove them from the list to free
	 * memory on the go */

	struct tau_metric_server_client_ctx_s *cur        = server->clients;
	struct tau_metric_server_client_ctx_s *to_process = NULL;

	struct tau_metric_server_client_ctx_s *new_list = NULL;

	while(cur)
	{
		to_process = cur;
		/* Already move to previous next */
		cur = cur->next;

		/* Update the previous elem */
		if(to_process->running)
		{
			to_process->next = new_list;
			new_list         = to_process;
		}
		else
		{
			/* Collect the thread and skip it */
			tau_metric_server_client_ctx_free(to_process);
		}
	}

	/* Now that we are done set the new list */
	server->clients = new_list;
}

static void __client_list_free(tau_metric_server_t *server)
{
	struct tau_metric_server_client_ctx_s *cur     = server->clients;
	struct tau_metric_server_client_ctx_s *to_free = NULL;

	while(cur)
	{
		to_free = cur;
		/* Already move to previous next */
		cur = cur->next;

		tau_metric_server_client_ctx_free(to_free);
	}

	server->clients = NULL;
}

static void *__server_listen_loop(void *pserver)
{
	tau_metric_server_t *server = (tau_metric_server_t *)pserver;

	struct sockaddr client_info;
	socklen_t       addr_len;
	int             cnt = 0;

	while(1)
	{
		int ret = accept(server->fd, &client_info, &addr_len);

		if(ret < 0)
		{
			break;
		}

		/* Insert new client context */
		struct tau_metric_server_client_ctx_s *cctx = tau_metric_server_client_ctx_new(ret,
																					   server->callback,
																					   server->exit_callback,
																					   server->callback_ctx_size);

		if(!cctx)
		{
			/* Something went wrong */
			tau_metric_proxy_error("Failed initializing client ctx");
			close(ret);
			continue;
		}

		/* Insert in list */
		cctx->next      = server->clients;
		server->clients = cctx;

		/* Prune the server list */
		__client_list_prune(server);
	}

	tau_metric_proxy_log("UNIX server : leaving");

	server->running = 0;
	close(server->fd);

	return NULL;
}

int tau_metric_server_run(tau_metric_server_t *server,
						  const char *path,
						  tau_metric_proxy_server_callback_t callback,
						  tau_metric_proxy_server_end_callback_t exit_cb,
						  size_t extra_ctx_size)
{
	/* Start listening server */
	server->fd       = __bind_unix_socket(path);
	server->callback = callback;
	server->exit_callback = exit_cb;
	server->callback_ctx_size = extra_ctx_size;

	snprintf(server->path, 512, "%s", path);

	if(server->fd < 0)
	{
		fprintf(stderr, "TAU_METRIC_AGGREGATOR: ERROR creating server socket");
		return -1;
	}

	server->running = 1;

	/* Start server listening thread */
	if(pthread_create(&server->server_listen_thread, NULL, __server_listen_loop, (void *)server) )
	{
		tau_metric_proxy_perror("pthread_create");
		return -1;
	}

	/* Join listen thread */
	pthread_join(server->server_listen_thread, NULL);

	/* Clean server */
	tau_metric_server_stop(server);

	return 0;
}

int tau_metric_server_stop(tau_metric_server_t *server)
{
	/* Close listen FD to unlock tau_metric_server_run */
	if(server->running)
	{
		shutdown(server->fd, SHUT_RDWR);
		close(server->fd);
		server->running = 0;
	}

	/* Kick all clients */
	__client_list_free(server);

	unlink(server->path);

	return 0;
}
