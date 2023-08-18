#include <mpi.h>

#include <stdio.h>


#include "timer.h"

#define COUNT (long long unsigned int)10e9


#ifdef STATIC_INTER

int MPI_Comm_rank(MPI_Comm comm, int * rank)
{
	   return PMPI_Comm_rank(comm, rank);
}

int MPI_Barrier(MPI_Comm comm)
{
   return PMPI_Barrier(comm);
}

#endif

int main(int argc, char **argv)
{
   /* Measure without */
   long long unsigned int i = 0;

   calibrate_ticks();

   MPI_Init(&argc, &argv);

   int my_rank = 0;

   ticks start = getticks();

   for(i = 0; i < COUNT; i++)
   {
      MPI_Barrier(MPI_COMM_WORLD);
   }
   ticks end = getticks();


   ticks total_with = end - start;

   start = getticks();

   for(i = 0; i < COUNT; i++)
   {
      MPI_Barrier(MPI_COMM_WORLD);
   }

   end = getticks();

   ticks total_without = end - start;

   double total_in_sec = (double)total_with/(double)get_ticks_per_second();
   double totalwithout_in_sec = (double)total_without/(double)get_ticks_per_second();

   double avg_with = total_in_sec/COUNT;
   double avg_without = totalwithout_in_sec/COUNT;

   (void)printf("%llu %g %g %g\n", COUNT, avg_with, avg_without, avg_with/avg_without);

   MPI_Finalize();

   return 0;
}
