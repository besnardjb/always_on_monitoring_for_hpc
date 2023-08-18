#ifndef UTILS_H
#define UTILS_H

#include <time.h>
#include <stdint.h>

/***************
 * TIME GETTER *
 ***************/

double utils_get_ts(void);

/**************
 * FILE UTILS *
 **************/

/**
 * @brief Return true if path matches something
 *
 * @param path path to check for being present
 * @return int True if present
 */
int utils_ispath(const char * path);


/**
 * @brief Return true for a directory
 *
 * @param path path to check for being a directory
 * @return int True if dir
 */
int utils_isdir(const char * path);

/**
 * @brief Return true for a regular file
 *
 * @param path path to check for being a file
 * @return int True if file
 */
int utils_isfile(const char * path);

/**
 * @brief Return modification time delta up to now
 *
 * @param path path to check for delta of modification time
 * @return time_t In case of error 0 is returned otherwise the delta
 */
time_t utils_file_last_modif_delta(const char * path);

/*****************
 * HASHING UTILS *
 *****************/

/* DBJ2 dy Dan Bernstein */
static inline uint64_t utils_string_hash(const unsigned char *str)
{
	uint64_t hash = 5381;
	int      c;

	while((c = *str++))
	{
		hash = ( (hash << 5) + hash) + c;
	}

	return hash;
}

#endif