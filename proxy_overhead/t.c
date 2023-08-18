#include <mpi.h>

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#include "cycle.h"

#include "timer.h"


#define COUNT (unsigned int)10e6

int main(int argc, char **argv)
{
   /* Measure without */
   unsigned int i = 0;

   calibrate_ticks();

   MPI_Init(&argc, &argv);

   int my_rank = 0;

   ticks start = getticks();
   for(i = 0; i < COUNT; i++)
   {
      MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
   }
   ticks end = getticks();


   ticks t_with = end - start;

   double total_in_sec = (double)t_with/(double)get_ticks_per_second();

   (void)printf("%u %g\n", COUNT, total_in_sec/COUNT);

   MPI_Finalize();

   return 0;
}