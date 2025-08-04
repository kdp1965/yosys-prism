#pragma once

#include <string>
#include <string.h>
#include <vector>
#include <iterator>

namespace strutil {

template<typename ... Args>
static inline std::string format(const std::string &fmt, Args ... args)
{
	size_t size = snprintf(NULL, 0, fmt.c_str(), args ...) + 1;
	char buf[size];
	snprintf(buf, size, fmt.c_str(), args ...);
	return std::string(buf, buf + size - 1);
}

template<typename T>
static inline void split(const std::string &str, const std::string &delim, T result)
{
	const char *s = str.c_str();
	const char *e = s + str.size();
	const char *d = delim.c_str();

	s += strspn(s, d);
	while (s < e) {
		size_t len = strcspn(s, d);

		*(result++) = std::string(s, len);
		s += len;
		s += strspn(s, d);
	}
}

static inline std::vector<std::string> split(const std::string &s, const std::string &delim)
{
	std::vector<std::string> elems;
	split(s, delim, std::back_inserter(elems));
	return elems;
}

};
