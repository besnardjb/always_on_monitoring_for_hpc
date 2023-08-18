#ifndef TAUMPROX_LOG_H
#define TAUMPROX_LOG_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/*****************
 * ERROR LOGGERS *
 *****************/

int tau_metric_proxy_is_verbose(void);
int tau_metric_proxy_set_verbose(void);


#define tau_metric_proxy_error(...)    do { fprintf(stderr, "ERROR (%s:%d)\t: ", __FILE__, __LINE__); \
                                    fprintf(stderr, __VA_ARGS__);\
                                    fprintf(stderr, "\n"); } while(0)

#define tau_metric_proxy_log(...)    do { fprintf(stderr, "INFO (%s:%d): ", __FUNCTION__, __LINE__); \
                                          fprintf(stderr, __VA_ARGS__);\
                                          fprintf(stderr, "\n"); } while(0)

#define tau_metric_proxy_log_verbose(...)    do { if(tau_metric_proxy_is_verbose()) \
                                              { \
                                                fprintf(stderr, "DEBUG (%s:%d): ", __FUNCTION__, __LINE__); \
                                                fprintf(stderr, __VA_ARGS__);\
                                                fprintf(stderr, "\n"); \
                                              } \
                                             } while(0)


#define tau_metric_proxy_perror(a) tau_metric_proxy_error("%s : %s", a, strerror(errno))
#define tau_metric_proxy_posix_check( expr , code ) do{ if( (expr) < 0 ) { ioc_perror( expr ); return code;} }while(0)



#endif /* TAUMPROX_LOG_H */