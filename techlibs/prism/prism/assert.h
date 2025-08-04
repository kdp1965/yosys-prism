#pragma once

#include <exception>
#include <string>

#include "filepos.h"

class Assertion : public std::exception {
public:
	std::string message;
	FilePos filepos;
	Assertion(const char *msg="")
	 : message(msg)
	{ }

	Assertion(const char *msg, const FilePos &fp)
	 : message(msg), filepos(fp)
	{ }
};

#define ASSERT_STRIFY_(x) #x
#define ASSERT_STRIFY(x) ASSERT_STRIFY_(x)
#define ASSERT_FILELINE __FILE__ ":" ASSERT_STRIFY(__LINE__)
#define ASSERT_MSG(cond) "Assertion '" #cond "` failed at " ASSERT_FILELINE
#define ASSERT(cond,message) \
  do { if (!(cond)) { throw Assertion(ASSERT_MSG(cond) ": " message); } } while (0)

#define ASSERTF(fp,cond,message) \
  do { if (!(cond)) { throw Assertion(ASSERT_MSG(cond) ": " message, fp); } } while (0)

#define DEBUG printf
