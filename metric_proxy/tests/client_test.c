#include <tau_metric_proxy_client.h>

#include <unistd.h>
#include <stdio.h>

int main(int argc, char ** argv)
{
    tau_metric_counter_t cnts[512];

    int i;

    for(i = 0 ; i < 512; i++)
    {
        char name[64];
        char doc[64];
        sprintf(name, "tau_counter_%d_values", i);
        sprintf(doc, "Doc for TEST C %d", i);
        cnts[i] = tau_metric_counter_new(name, doc);
    }

    int cnt = 0;

    while(cnt < 1e6)
    {
        tau_metric_counter_incr(cnts[cnt%512], 1);
        cnt++;
        usleep(150);
    }

    return 0;
}
