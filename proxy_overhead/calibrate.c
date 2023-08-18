#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "timer.h"


int main(int argc, char ** argv)
{
   unsetenv("FORCED_TICKS");

   calibrate_ticks();

   printf("%lld\n", get_ticks_per_second());

   return 0;
}