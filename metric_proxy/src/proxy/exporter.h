#ifndef TAU_METRIC_PROXY_EXPORTER_H
#define TAU_METRIC_PROXY_EXPORTER_H

#include <pthread.h>

typedef struct
{
    pthread_t listening_thread;
    int running;
    int listen_fd;
}tau_metric_exporter_t;

int tau_metric_exporter_init(tau_metric_exporter_t * exporter, const char * port);
int tau_metric_exporter_release(tau_metric_exporter_t *exporter);

#endif /* TAU_METRIC_PROXY_EXPORTER_H */