#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])

AC_INIT([tau_metric_proxy], [0.5], [jbbesnard@paratools.fr])
AC_CONFIG_SRCDIR([src/proxy/metrics.h])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE([disable])
AC_CONFIG_MACRO_DIRS([m4])
AM_SILENT_RULES([yes]) 

LT_INIT

#
# * Checks for programs *
#

# C compiler

AC_PROG_CC

# Python

AM_PATH_PYTHON

# MPICC

AC_CHECK_PROG(MPICC, mpicc, mpicc)

if test -n "${MPICC}"; then
    AC_MSG_NOTICE([MPICC found as ${MPICC}])
    AC_DEFINE([TAU_METRIC_PROXY_MPI_ENABLED], [1], [MPI Support is BUILT])
else
    AC_MSG_WARN([MPICC not found, MPI modules disabled])
fi

AM_CONDITIONAL([MPI_ENABLED], [test -n "${MPICC}"])
AC_SUBST([MPICC])

AC_CHECK_PROG(PYTHONPIP, pip, pip)

if test -z "${PYTHONPIP}"; then
    AC_MSG_WARN([PIP not found in path the cli client will not be installed])
fi


AM_CONDITIONAL([HAVE_PYTHON_PIP], [test -n "${PYTHONPIP}"])

#
# * CHECKS FOR LIBRARIES *
#
AC_CHECK_LIB([pthread], [pthread_create])

#
# * CHECKS FOR HEADERS *
#
AC_CHECK_HEADERS([arpa/inet.h netdb.h stddef.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

#
# * Checks for typedefs, structures, and compiler characteristics *
#
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT64_T

#
# * Checks for library functions *
#
AC_FUNC_MALLOC
AC_CHECK_FUNCS([gettimeofday memset socket strstr])

#
# Export Prefix for LIBRARY Preload
#
AC_DEFINE_UNQUOTED(TAU_METRIC_PROXY_PREFIX, ["$prefix"], [Metric proxy install prefix])



AC_CONFIG_FILES([
tests/Makefile
Makefile
src/Makefile
src/client/Makefile
src/client/taumetricproxy.pc
src/proxy/Makefile
src/exporters/Makefile
src/launcher/Makefile
src/exporters/mpi/Makefile
include/Makefile
src/cli_client/Makefile
src/deploy/Makefile
src/profile_inspect/Makefile
])

AC_OUTPUT
