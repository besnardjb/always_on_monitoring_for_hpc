#ifndef TAU_METRIC_PROXY_SERVER_H
#define TAU_METRIC_PROXY_SERVER_H

#include "tau_metric_proxy_client.h"

#include <pthread.h>

/**********
* HELPER *
**********/

ssize_t safe_write(int fd, void *buff,  size_t size);
ssize_t safe_read(int fd, void *buff, size_t size);

/******************
* CLIENT CONTEXT *
******************/

/** This callback is called for each incoming message */
typedef int (*tau_metric_proxy_server_callback_t)(int source_fd, tau_metric_msg_t *msg, void * extra_ctx);

/** This callback is called when the client leaves */
typedef void (*tau_metric_proxy_server_end_callback_t)(int source_fd, void * extra_ctx);


/**
 * @brief This structure stores the context for each client
 *
 */
struct tau_metric_server_client_ctx_s
{
	int                                    running;
	pthread_t                              client_thread; /**< Client polling thread */
	int                                    client_fd;     /**< Client socket */
	struct tau_metric_server_client_ctx_s *next;          /**< Clients are chained */
	tau_metric_proxy_server_callback_t     callback;      /**< The callback is passed to each client */
	tau_metric_proxy_server_end_callback_t exit_callback; /**< This is call when the client leaves */
	void *                                 extra_ctx;     /**< A pointer allocated to handle transitive ctx between CBs*/
};

struct tau_metric_server_client_ctx_s *tau_metric_server_client_ctx_new(int client_fd,
																		tau_metric_proxy_server_callback_t cb,
																		tau_metric_proxy_server_end_callback_t end,
																		size_t extra_ctx_size);
int tau_metric_server_client_ctx_free(struct tau_metric_server_client_ctx_s *ctx);

/**********************
* UNIX SOCKET SERVER *
**********************/

/**
 * @brief This is the UNIX socket server instance for metric aggregation
 *
 */
typedef struct
{
    char                                   path[512];            /**< Server PATH */
    int                                    running;              /**< Flag indicating listen socket open */
	int                                    fd;                   /**< Server listening Fd */
	tau_metric_proxy_server_callback_t     callback;             /**< Callback to be called */
	size_t                                 callback_ctx_size;    /**< What to pass in for the callback size*/
	tau_metric_proxy_server_end_callback_t exit_callback; 		 /**< This is call when the client leaves */
	pthread_t                              server_listen_thread; /**< Listening thread */
	struct tau_metric_server_client_ctx_s *clients;              /**< List of clients */
}tau_metric_server_t;

int tau_metric_server_run(tau_metric_server_t * server, const char * path, 	
						  tau_metric_proxy_server_callback_t callback,
						  tau_metric_proxy_server_end_callback_t end,
						  size_t extra_ctx_size);
int tau_metric_server_stop(tau_metric_server_t *server);


#endif /* TAU_METRIC_PROXY_SERVER_H */
