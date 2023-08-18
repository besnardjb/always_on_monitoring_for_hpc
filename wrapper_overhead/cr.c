#include <mpi.h>
#include <stdio.h>


int MPI_Comm_rank(MPI_Comm comm, int * rank)
{
	   return PMPI_Comm_rank(comm, rank);
}

int MPI_Barrier(MPI_Comm comm)
{
   return PMPI_Barrier(comm);
}
