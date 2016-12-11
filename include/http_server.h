/*
 * @Author:	   Tom G., <geiselto@hs-albsig.de>
 * @Date:	   08.06.2016
 * @Last-modified: 10.12.2016
 *
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <limits.h>

#define HTTP10		"HTTP/1.0"
#define HTTP11		"HTTP/1.1"
#define SUPP_MTHD	"GET"
#define	LOWER_PORT_LIM	1023
#define UPPER_PORT_LIM	USHRT_MAX

enum {
	OUT_DBG,
	OUT_INF
};

#define OUT_LEVEL	OUT_INF

#define server_out(level, x...)						\
	do {								\
		if (level >= OUT_LEVEL)					\
			fprintf(stdout, x);				\
	} while (0)

#define server_error(x)							\
	do {								\
        	fprintf(stderr, "%s(): %d: " x ": %s\n",		\
				__func__, __LINE__, strerror(errno));	\
        } while (0)

#endif /* HTTP_SERVER_H */
