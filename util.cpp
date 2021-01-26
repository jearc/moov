#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>
#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>

void die(std::string_view str)
{
	std::cout << str << std::endl;
	exit(EXIT_FAILURE);
}

std::string sec_to_timestr(uint32_t sec)
{
	auto ss = std::stringstream{} << std::setfill('0')
		<< std::setw(2) << (sec / 3600) << ":"
		<< std::setw(2) << ((sec % 3600) / 60) << ":"
		<< std::setw(2) << (sec % 60);

	return ss.str();
}
