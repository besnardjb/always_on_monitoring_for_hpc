#include "log.h"

static int __verbose = 0;

int tau_metric_proxy_is_verbose(void)
{
	return __verbose;
}

int tau_metric_proxy_set_verbose(void)
{
	__verbose = 1;
}
