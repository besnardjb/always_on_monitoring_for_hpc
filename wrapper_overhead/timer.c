#include "timer.h"

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>


static ticks ticks_per_second = 0;

ticks get_ticks_per_second()
{
   return ticks_per_second;
}

void calibrate_ticks()
{
   char * forced_ticks = getenv("FORCED_TICKS");

   if(forced_ticks)
   {
      char * end = NULL;
      ticks_per_second = strtol(forced_ticks, &end, 10);
   }
   else
   {
      struct timeval tv_start, tv_end;

      fprintf(stderr, "Calibrating timer ...\n");

      ticks start = getticks();
      gettimeofday(&tv_start, NULL);

      sleep(3);

      ticks end = getticks();
      gettimeofday(&tv_end, NULL);

      ticks_per_second = (end - start) / (((double)tv_end.tv_sec + (double)tv_end.tv_usec *1e-6) - ((double)tv_start.tv_sec + (double)tv_start.tv_usec *1e-6));
   }


   fprintf(stderr, "Done calibrating timer at %llu ticks per second\n", ticks_per_second);

}