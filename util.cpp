#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "moov.h"

void die(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(EXIT_FAILURE);
}

double parsetime(char *str)
{
	double sec = 0;
	for (int i = 0; i < 3; i++) {
		while (*str && !isdigit(*str))
			str++;
		if (!*str)
			break;

		sec *= 60;
		sec += strtol(str, &str, 10);
		str++;
	}
	return sec;
}

timestr sec_to_timestr(int seconds)
{
	timestr ts;

	int hh = seconds / 3600;
	int mm = (seconds % 3600) / 60;
	int ss = seconds % 60;

	if (hh)
		snprintf(ts.str, 39, "%d:%02d:%02d", hh, mm, ss);
	else
		snprintf(ts.str, 39, "%d:%02d", mm, ss);

	return ts;
}