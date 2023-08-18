#include "utils.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>

/***************
 * TIME GETTER *
 ***************/

double utils_get_ts(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	return (double)tv.tv_sec + 1e-6 * tv.tv_usec;
}


/**************
 * FILE UTILS *
 **************/

int utils_ispath(const char * path)
{
	struct stat st;

	if(stat(path, &st) < 0)
	{
		return 0;
	}

	/* STAT did match something */

	return 1;
}

int utils_isdir(const char * path)
{
	struct stat st;

	if(stat(path, &st) < 0)
	{
		return 0;
	}

	return S_ISDIR(st.st_mode);
}

int utils_isfile(const char * path)
{
	struct stat st;

	if(stat(path, &st) < 0)
	{
		return 0;
	}

	return S_ISREG(st.st_mode);
}


time_t utils_file_last_modif_delta(const char * path)
{
	struct stat st;
    if( stat(path, &st) < 0)
	{
		return 0;
	}

	return time(NULL) - st.st_mtime;
}
