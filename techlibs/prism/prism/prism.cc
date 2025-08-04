#include "decision_tree.h"
#include "parse_context.h"
#include "bitgroup.h"
#include "bitmask.h"
#include "filepos.h"
#include "assert.h"
#include "config.h"
#include "prism.h"
#include "frontends/ast/ast.h"

#include <iomanip>
#include <vector>

std::string module_name;
std::string PrismConfig::config;

using Yosys::AST::AstNode;
using namespace Yosys;


#define ASSERT_NODE(node, cond, msg) \
      ASSERTF(FilePos((node).filename, (node).location.first_line), cond, msg);

class AstProcessor {
	std::map<std::string, std::shared_ptr<BitGroup>> assignments;

	ParseContextTree parseContextTree;

public:
	~AstProcessor(void)
	{ }

	void processWire(const AstNode &node)
	{
		BitGroup *grp = NULL;

		if (node.str == "\\in_data") {
			grp = new OffsetBitGroup(0, node.range_left - node.range_right + 1);
			ASSERT_NODE(node, node.is_input, "`in_data` must be an input");
		} else if (node.str == "\\out_data") {
			grp = new OffsetBitGroup(0, node.range_left - node.range_right + 1);
			ASSERT_NODE(node, node.is_output, "`out_data` must be an output");
		} else if (node.str == "\\cond_out") {
			grp = new OffsetBitGroup(0x10000, node.range_left - node.range_right + 1);
			ASSERT_NODE(node, node.is_output, "`cond_out` must be an output");
		} else if (node.str == "\\version") {
      DEBUG("Version found\n");
		} else {
			// ignore other wires until they show up in an assign
			return;
		}
		DEBUG("IO \"%s\" is %d bits wide\n", node.str.c_str(), grp->size());
		std::shared_ptr<BitGroup> shgrp(grp);
		assignments[node.str] = shgrp;
	}

	std::shared_ptr<BitGroup> parseAssignTarget(const AstNode &node)
	{
		std::shared_ptr<BitGroup> grp(NULL);

		if (node.type == AST::AST_IDENTIFIER) {
			ASSERT_NODE(node, assignments.find(node.str) != assignments.end(),
					"Unknown identifier");

			grp = assignments[node.str];
			if (node.children.size() != 0) {
				const AstNode &r = *node.children[0];
				unsigned int lo = r.range_right;
				unsigned int width = 1 + r.range_left - lo;

				if (lo != 0 || width != grp->size()) {
					grp = std::shared_ptr<BitGroup>(
							new SliceBitGroup(*grp, lo, width));
				}
			}
		} else if (node.type == AST::AST_CONCAT) {
			MappedBitGroup *mgrp = new MappedBitGroup();
			for (const AstNode *child : node.children) {
				std::shared_ptr<BitGroup> cgrp = parseAssignTarget(*child);
				mgrp->concat(*cgrp);
			}
			grp = std::shared_ptr<BitGroup>(mgrp);
			ASSERT_NODE(node, grp->size() != 0, "Invalid empty concatenation");
		} else {
			ASSERT_NODE(node, false, "Unexpected node");
		}

		return grp;
	}

   void processAssignBitOr(const AstNode &left, const AstNode &right)
   {
      for (const AstNode *child : right.children) {
         if (child->type == AST::AST_BIT_OR)
            processAssignBitOr(left, *child);
         else
            assignments[child->str] = parseAssignTarget(left);
      }
   }

	// "assign x = y"
	void processAssign(const AstNode &node)
	{
		const AstNode &left = *node.children[0];
		const AstNode &right = *node.children[1];

		if (left.type == AST::AST_CONCAT ||
				assignments.find(left.str) != assignments.end()) {
			ASSERT_NODE(node, assignments.find(right.str) == assignments.end(),
					"Name conflict in assign");
         if (right.type == AST::AST_BIT_OR)
            processAssignBitOr(left, right);
         else
            assignments[right.str] = parseAssignTarget(left);
		} else if (right.type == AST::AST_CONCAT ||
				assignments.find(right.str) != assignments.end()) {
			ASSERT_NODE(node, assignments.find(left.str) == assignments.end(),
					"Name conflict in assign");
			assignments[left.str] = parseAssignTarget(right);
		}
	}

	void processAssignment(const AstNode &node)
	{
		const AstNode &id = *node.children[0];
		const AstNode &cval = *node.children[1];
		std::shared_ptr<BitGroup> grp;

		if (id.str == "\\next_state") {
			// we treat 'next_state = curr_state' specially, because
			// we won't know what state we are in until after we've
			// generated the entire tree
			if (cval.type == AST::AST_IDENTIFIER && cval.str == "\\curr_state") {
				parseContextTree.setTargetState(-1);
				return;
			}
			parseContextTree.setTargetState(cval.integer);
			return;
		}

    if (cval.type == AST::AST_IDENTIFIER && cval.str == "\\version") {
      // Assign from the version attribute
      const AstNode &assignRange = *cval.children[0];
		  ASSERT_NODE(node, assignRange.type == AST::AST_RANGE, "Invalid assignment");
      
      return;
    }

		// TODO: a = b can be translated to:
		//   for each possible value of b:
		//     if (b == value)
		//        a = value;
		// for booleans, this is:
		//   if (b)
		//      a = 1
		//   else
		//      a = 0
		ASSERT_NODE(node, cval.type == AST::AST_CONSTANT, "Invalid assignment");

		grp = parseAssignTarget(id);

		if (grp->size() >= cval.bits.size()) {
			for (unsigned int bit = 0; bit < cval.bits.size(); ++bit)
				parseContextTree.assign(grp->map(bit), cval.bits[bit]);

			for (unsigned int bit = cval.bits.size(); bit < grp->size(); ++bit)
				parseContextTree.assign(grp->map(bit), 0);
		} else {
			for (unsigned int bit = 0; bit < grp->size(); ++bit)
				parseContextTree.assign(grp->map(bit), cval.bits[bit]);

			if (cval.bits.size() <= 32) {
				uint32_t mask = ~((1 << grp->size()) - 1);

				ASSERT_NODE(node, (cval.integer & mask) == 0,
						"Attempt to truncate value in assignment");
			} else {
				for (unsigned int bit = grp->size(); bit < cval.bits.size(); ++bit)
					ASSERT_NODE(node, cval.bits[bit] == 0,
							"Attempt to truncate value in assignment");
			}
		}
	}

	LogicExpression *parseLogicExpression(const AstNode &node)
	{
		LogicExpression *childa;
		LogicExpression *childb;
		Expression *expr;

		switch (node.type) {
		case AST::AST_REDUCE_BOOL:
		case AST::AST_REDUCE_OR:
			expr = parseExpression(*node.children[0]);
			return new LogicReduceOrExpression(expr);
		case AST::AST_REDUCE_AND:
			expr = parseExpression(*node.children[0]);
			return new LogicReduceAndExpression(expr);
		case AST::AST_REDUCE_XOR:
			expr = parseExpression(*node.children[0]);
			return new LogicReduceXorExpression(expr);
		case AST::AST_REDUCE_XNOR:
			expr = parseExpression(*node.children[0]);
			childa = new LogicReduceXorExpression(expr);
			return new LogicNotExpression(childa);
		case AST::AST_LOGIC_NOT:
			childa = parseLogicExpression(*node.children[0]);
			return new LogicNotExpression(childa);
		case AST::AST_LOGIC_AND:
			childa = parseLogicExpression(*node.children[0]);
			childb = parseLogicExpression(*node.children[1]);
			return new LogicAndExpression(childa, childb);
		case AST::AST_LOGIC_OR:
			childa = parseLogicExpression(*node.children[0]);
			childb = parseLogicExpression(*node.children[1]);
			return new LogicOrExpression(childa, childb);
		case AST::AST_IDENTIFIER:
		case AST::AST_CONSTANT:
			return new LogicReduceOrExpression(parseExpression(node));
		default:
			ASSERT_NODE(node, false, "unexpected node type");
		}
	}

	Expression *parseExpression(const AstNode &node)
	{
		std::shared_ptr<BitGroup> grp;
		Expression *childa;
		Expression *childb;

		switch (node.type) {
		case AST::AST_REDUCE_BOOL:
		case AST::AST_REDUCE_OR:
		case AST::AST_REDUCE_AND:
		case AST::AST_REDUCE_XOR:
		case AST::AST_REDUCE_XNOR:
		case AST::AST_LOGIC_NOT:
		case AST::AST_LOGIC_AND:
		case AST::AST_LOGIC_OR:
			return parseLogicExpression(node);
		case AST::AST_CONSTANT:
			if (node.bits.size() <= BITS_PER_LONG) {
				if (node.integer == 1)
					return new LogicTrueExpression();
				else if (node.integer == 0)
					return new LogicFalseExpression();
				return new ConstantExpression(IntegerBitmask(node.integer, node.bits.size()));
			} else {
				BufferBitmask mask(node.bits.size());
				for (unsigned int bit = 0; bit < node.bits.size(); ++bit)
					mask.write(bit, node.bits[bit]);
				return new ConstantExpression(mask);
			}
			break;
		case AST::AST_BIT_NOT:
			childa = parseExpression(*node.children[0]);
			return new BitwiseNotExpression(childa);
		case AST::AST_BIT_AND:
			childa = parseExpression(*node.children[0]);
			childb = parseExpression(*node.children[1]);
			return new BitwiseAndExpression(childa, childb);
		case AST::AST_BIT_OR:
			childa = parseExpression(*node.children[0]);
			childb = parseExpression(*node.children[1]);
			return new BitwiseOrExpression(childa, childb);
		case AST::AST_BIT_XOR:
			childa = parseExpression(*node.children[0]);
			childb = parseExpression(*node.children[1]);
			return new BitwiseXorExpression(childa, childb);
		case AST::AST_BIT_XNOR:
			childa = parseExpression(*node.children[0]);
			childb = parseExpression(*node.children[1]);
			return new BitwiseXnorExpression(childa, childb);
		case AST::AST_EQ:
			childa = parseExpression(*node.children[0]);
			childb = parseExpression(*node.children[1]);
			return new EqualityExpression(childa, childb);
		case AST::AST_NE:
			childa = parseExpression(*node.children[0]);
			childb = parseExpression(*node.children[1]);
			return new LogicNotExpression(new EqualityExpression(childa, childb));
		case AST::AST_IDENTIFIER:
			grp = parseAssignTarget(node);
			return new IdentifierExpression(*grp);
		default:
			ASSERT_NODE(node, false, "Unexpected node type");
		}
	}

	// all if-elif*-else chains are broken into binary nested if-else
	// ditto for switch/case statements
	void processConditionalRecurse(const AstNode &caseNode, const Expression *iexp, unsigned int index)
	{
		if (index >= caseNode.children.size())
			return;

		const AstNode &condNode = *caseNode.children[index];
		ASSERT_NODE(condNode, condNode.type == AST::AST_COND,
				"Unexpected node type");
		const AstNode &cmpNode = *condNode.children[0];
		const AstNode &blockNode = *condNode.children[1];

		if (cmpNode.type == AST::AST_DEFAULT) {
			processStatement(blockNode);
		} else {
			parseContextTree.split(new EqualityExpression(iexp->clone(), parseExpression(cmpNode)));
			processStatement(blockNode);
			parseContextTree.switchSplit(false);
			processConditionalRecurse(caseNode, iexp, index + 1);
			parseContextTree.join();
		}
	}

	void processConditional(const AstNode &caseNode)
	{
		const AstNode &val = *caseNode.children[0];

		if (val.str == "\\curr_state") {
			processStateSwitch(caseNode);
		} else {
			Expression *iexp = parseExpression(val);
			processConditionalRecurse(caseNode, iexp, 1);
			delete iexp;
		}
	}

	void processStatement(const AstNode &node)
	{
		switch (node.type) {
		case AST::AST_CASE: // conditional: split our tree
			processConditional(node);
			break;
		case AST::AST_ASSIGN_EQ: // assignment: set output data
			processAssignment(node);
			break;
		case AST::AST_BLOCK: // block: no flow change; no special treatment 
			for (AstNode *child : node.children)
				processStatement(*child);
			break;
		default:
			ASSERT_NODE(node, false, "Unexpected node type");
			break;
		}
	}

	void processStateRecurse(const AstNode &node, unsigned int index)
	{
		if (index >= node.children.size())
			return;

		const AstNode &condNode = *node.children[index];
		ASSERT_NODE(condNode, condNode.type == AST::AST_COND,
				"Unexpected node type");
		const AstNode &cmpNode = *condNode.children[0];
		const AstNode &blockNode = *condNode.children[1];
		FilePos fpos(cmpNode.filename, cmpNode.location.first_line);

		if (cmpNode.type == AST::AST_DEFAULT) {
			parseContextTree.defaultStateCase(fpos);
			processStatement(blockNode);
		} else {
			unsigned int state = cmpNode.integer;

			parseContextTree.splitStateCase(state, fpos);
			processStatement(blockNode);
			parseContextTree.switchSplit(false);
			processStateRecurse(node, index + 1);
			parseContextTree.join();
		}
	}

	void processStateSwitch(const AstNode &node)
	{
		const AstNode &statevar = *node.children[0];

		parseContextTree.enterStateSwitch(statevar.str);
		processStateRecurse(node, 1);
		parseContextTree.exitStateSwitch();
	}

	// "always @*"
	void processAlways(const AstNode &node)
	{
		/* we're only looking for an unconditional loop */
		for (const AstNode *child : node.children) {
			if (child->type == AST::AST_POSEDGE ||
			    child->type == AST::AST_NEGEDGE ||
			    child->type == AST::AST_EDGE)
				return;
		}

		for (const AstNode *child : node.children)
			processStatement(*child);
	}

	// node in global scope
	void processGlobalNode(const AstNode &node)
	{
		switch (node.type) {
		case AST::AST_MODULE:
			for (AstNode *child : node.children)
				processGlobalNode(*child);
			break;
		case AST::AST_WIRE:
			processWire(node);
			break;
		case AST::AST_ALWAYS:
			processAlways(node);
			break;
		case AST::AST_ASSIGN:
			processAssign(node);
			break;
		case AST::AST_LOCALPARAM:
			break;
		default:
			ASSERT_NODE(node, false, "Unexpected node type");
			break;
		}
	}

	void write(Bitmask &out, const STEW &stew, const DecisionTree &tree)
	{
		parseContextTree.writeStates(out, stew, tree);
	}
};

class Columnizer {
	std::vector<std::string> headers;
	std::list<std::vector<std::string>> rows;
	std::vector<unsigned int> widths;
	unsigned int nColumns;

	char vsplit;
	char join;
	char hsplit;
public:

	Columnizer(const std::vector<std::string> &hdrs,
		char vsplit='|', char join='+', char hsplit='-')
	 : headers(hdrs), nColumns(headers.size()),
	   vsplit(vsplit), join(join), hsplit(hsplit)
	{
		for (std::string &str : headers)
			widths.push_back(str.size());
	}

	void append(const std::vector<std::string> &row)
	{
		ASSERT(row.size() == headers.size(),);
		rows.push_back(row);

		for (unsigned int c = 0; c < nColumns; ++c) {
			unsigned int width = row[c].size();

			if (width > widths[c])
				widths[c] = width;
		}
	}

	void write(std::ostream &os) const
	{
		writeSplit(os);

		writeRow(os, headers);

		writeSplit(os);

		for (const std::vector<std::string> &row : rows)
			writeRow(os, row);

		writeSplit(os);

		os << std::setfill(' ');
	}

private:
	void writeRow(std::ostream &os, const std::vector<std::string> &row) const
	{
		os << vsplit << std::setfill(' ');
		for (unsigned int c = 0; c < nColumns; ++c) {
			os << " " << std::right << std::setw(widths[c]) << row[c];
			os << " " << vsplit;
		}
		os << "\n";
	}

	void writeSplit(std::ostream &os) const
	{
		os << join << std::setfill(hsplit);
		for (unsigned int c = 0; c < nColumns; ++c) {
			os << hsplit << std::setw(widths[c]) << "";
			os << hsplit << join;
		}
		os << "\n";
	}
};


class PrismImpl {
	DecisionTree tree;
	BufferBitmask output;
  std::string config;
	const STEW stewConfig;
	const InputMux::Config muxConfig;
public:
	PrismImpl(const PrismConfig &cfg)
	 : tree(cfg.tree), output(cfg.stew.size * cfg.stew.count), stewConfig(cfg.stew),
	   muxConfig(cfg.tree.wires.muxes)
	{ 
    config = cfg.config;
  }

	void parseAst(const AstNode &root)
	{
		AstProcessor proc;

		proc.processGlobalNode(root);
		proc.write(output, stewConfig, tree);
	}

	void writeTabOutput(std::ostream &os)
	{
		for (unsigned int word = 0; word < stewConfig.count; ++word) {
			BitmaskSlice slice(output,
					(stewConfig.count - word - 1) * stewConfig.size,
					stewConfig.size);

			os << slice.to_str(false).c_str() << "\n";
		}
	}

	void writeHexOutput(std::ostream &os)
	{
		ASSERT(!(output.size() & 0x7), "Output size is not a multiple of 8 bits");
		unsigned int bit = output.size();
		for (unsigned int nibble = 0; bit > 0; ++nibble) {
			unsigned char c;
			bit -= 4;

			if (nibble % 48 == 0)
				os << strutil::format("%04x: ", nibble >> 1);
			c = output.nibble(bit);
			if (c <= 9)
				c += '0';
			else
				c += 'a' - 0xa;
			os << c;
			if (nibble % 2 == 1)
				os << " ";

			if (nibble % 48 == 47)
				os << "\n";
		}
	}

  static std::string basename(const std::string& path)
  {
      size_t pos = path.find_last_of("/\\");
      return (pos == std::string::npos) ? path : path.substr(pos + 1);
  }

	void writeCOutput(std::ostream &os)
	{
		ASSERT(!(output.size() & 0x7), "Output size is not a multiple of 8 bits");

    os << "/*\n";
    os << "==============================================================\n";
    os << "PRISM Downloadable Configuration\n\n";
    os << strutil::format("Input:    %s.sv\n", ::module_name.c_str());
    //os << strutil::format("Version:  %s\n", .c_str());
    os << strutil::format("Config:   %s\n", basename(config).c_str());
    os << "==============================================================\n";
    os << "*/\n\n";
    os << "#include <stdint.h>\n\n";
    os << strutil::format("const uint32_t %s[] =\n{\n", ::module_name.c_str());

    unsigned int count = 0;
    for (int word = stewConfig.count - 1; word >= 0; --word) {
      BitmaskSlice slice(output,
          (stewConfig.count - word - 1) * stewConfig.size,
          stewConfig.size);

      unsigned int bits = slice.size();
      unsigned int bit = 0;
      unsigned int nibble;
      char         str[9];
      int          strIdx = 6;
      std::vector<std::string> wordList;

      strcpy(str, "00000000");
      for (nibble = 0; bit < bits; nibble += 2) {
        unsigned char c;

        c = slice.nibble(bit+4);
        if (c <= 9)
            c += '0';
        else
            c += 'a' - 0xa;
        str[strIdx] = c;
        c = slice.nibble(bit);
        if (c <= 9)
            c += '0';
        else
            c += 'a' - 0xa;
        str[strIdx+1] = c;
        strIdx -= 2;

        bit += 8;

        if (nibble && (nibble % 8 == 6))
        {
          wordList.push_back(str);
          strcpy(str, "00000000");
          strIdx = 6;
          count++;
        }
      }
      if (strIdx != 6)
        wordList.push_back(str);

      // Output words to the file for this 
      os << "   ";
      for (int i = wordList.size() - 1; i >= 0; i--)
      {
        os << "0x";
        os << wordList[i];
        os << ", ";
      }
      os << "\n";
    }
    os << "\n};\n";
    os << strutil::format("const uint32_t %s_count = %d;\n", ::module_name.c_str(), count);
    os << strutil::format("const uint32_t %s_width = %d;\n", ::module_name.c_str(),
                stewConfig.size);
	}

	void writeListOutput(std::ostream &os)
	{
		static const struct {
			const char *name;
			bool indexedByNumber;
			bool indexedByLetter;
		} types[] = {
			[STEW::NIL] = { "Nil", false, false },
			[STEW::INC] = { "Inc", false, false },
			[STEW::MUX] = { "Mux",  true, false },
			[STEW::JMP] = { "Jmp", false,  true },
			[STEW::OUT] = { "Out", false,  true },
			[STEW::CFG] = { "Cfg", false,  true },
		};
		unsigned int counts[sizeof(types)/sizeof(types[0])] = { 0, };
		std::vector<std::string> line;
		unsigned int nOutput = 0;

		line.push_back("ST");

		for (unsigned int i = 0; i < stewConfig.items.size(); ++i) {
			STEW::Item item = stewConfig.items[i];
			if (item.type == STEW::OUT)
				++nOutput;

			if (item.type == STEW::MUX) {
				for (unsigned int m = 0; m < muxConfig.nMux; ++m)
					line.push_back(strutil::format("Mux%d", m));
			}
		}

		for (unsigned int i = 0; i < stewConfig.items.size(); ++i) {
			STEW::Item item = stewConfig.items[i];
			std::string txt;

			if (item.type == STEW::MUX)
				continue;
			else if (item.type == STEW::OUT && counts[item.type] + 1 == nOutput)
				txt = types[item.type].name;
			else if (types[item.type].indexedByNumber)
				txt = strutil::format("%s%d", types[item.type].name, counts[item.type]);
			else if (types[item.type].indexedByLetter)
				txt = strutil::format("%s%c", types[item.type].name, 'A' + counts[item.type]);
			else
				txt = types[item.type].name;

			line.push_back(txt);
			counts[item.type]++;
		}

		line.push_back("STEW");
		Columnizer cz(line);
		line.clear();

		for (unsigned int word = 0; word < stewConfig.count; ++word) {
			BitmaskSlice stew(output,
					(stewConfig.count - word - 1) * stewConfig.size,
					stewConfig.size);
			STEW::Item muxItem = stewConfig.slice(STEW::MUX);

			line.push_back(strutil::format("%x", word));
			for (unsigned int m = 0; m < muxConfig.nMux; ++m) {
				BitmaskSlice mux(stew,
						muxItem.offset + m * muxConfig.nBits,
						muxConfig.nBits);
				line.push_back(mux.to_str(false));
			}

			for (unsigned int i = 0; i < stewConfig.items.size(); ++i) {
				STEW::Item item = stewConfig.items[i];

				if (item.type == STEW::MUX)
					continue;

				BitmaskSlice data(stew, item.offset, item.size);
				line.push_back(data.to_str(false));
			}

			line.push_back(stew.to_str(false));

			cz.append(line);
			line.clear();
		}

		cz.write(os);
	}
};

Prism::Prism(void)
 : impl(NULL)
{ }

Prism::~Prism(void)
{
	if (impl != NULL)
		delete impl;
}

bool Prism::parseConfig(const std::string &filename)
{
	PrismConfig cfg;
	int rc;

	rc = PrismConfig::parse(filename, cfg);
	if (rc)
		return false;

	impl = new PrismImpl(cfg);
  ::module_name = module_name;

	return true;
}

bool Prism::parseAst(const Yosys::AST::AstNode &root)
{
	if (impl == NULL) {
		PrismConfig cfg;
		PrismConfig::fallback(cfg);
		DEBUG("no configuration specified, using default"
				" \"%s\" configuration\n", cfg.title.c_str());
		impl = new PrismImpl(cfg);
	}

	try {
		impl->parseAst(root);
	} catch (Assertion &e) {
		if (e.filepos.lineno)
			fprintf(stderr, "at %s:%d:\n",
					e.filepos.filename.c_str(), e.filepos.lineno);
		fprintf(stderr, "%s\n", e.message.c_str());
		return false;
	}

	return true;
}

bool Prism::writeOutput(Prism::Format fmt, std::ostream &os)
{
	if (impl == NULL)
		return false;

	try {
		if (fmt == Prism::TAB)
			impl->writeTabOutput(os);
		else if (fmt == Prism::HEX)
			impl->writeHexOutput(os);
		else if (fmt == Prism::LIST)
			impl->writeListOutput(os);
		else if (fmt == Prism::CFILE)
			impl->writeCOutput(os);
		else
			return false;
	} catch (Assertion &e) {
		fprintf(stderr, "%s\n", e.message.c_str());
		return false;
	}

	return true;
}

// vim: ts=2 sw=2
