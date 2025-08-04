#pragma once

#include <string>

#include "frontends/ast/ast.h"

class PrismImpl;
class Prism {
	PrismImpl *impl;
public:
	enum Format { HEX, LIST, TAB, CFILE };
  std::string  module_name;

	Prism(void);
	~Prism(void);

	bool parseConfig(const std::string &filename);
	bool parseAst(const Yosys::AST::AstNode &root);
	bool writeOutput(Format fmt, std::ostream &os);
};
