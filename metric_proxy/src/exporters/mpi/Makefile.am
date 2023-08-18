CC=@MPICC@
AM_CFLAGS = -I$(top_srcdir)/include/

mpi_wrappers.c: mpi_wrappers.w
	${PYTHON} $(top_srcdir)/dist/llnl_mpiwrap/wrap.py  -f $^ > ./mpi_wrappers.c

lib_LTLIBRARIES = libtaumetricmpiwrap.la

libtaumetricmpiwrap_la_SOURCES = mpi_wrappers.c
libtaumetricmpiwrap_la_LIBADD = ../../client/libtaumetricclient.la