#pragma once

#include <string>

struct FilePos {
	std::string filename;
	unsigned int lineno;

	FilePos(void)
	 : lineno(0)
	{ }

	FilePos(const FilePos &pos)
	 : filename(pos.filename), lineno(pos.lineno)
	{ }

	FilePos(const std::string &f, unsigned int line)
	 : filename(f), lineno(line)
	{ }
};
