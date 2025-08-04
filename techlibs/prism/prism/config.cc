#include <streambuf>
#include <fstream>
#include <cctype>
#include <vector>
#include <string>
#include <list>
#include <map>

#include <stdio.h>
#include <limits.h>

#include "components.h"
#include "strutil.h"
#include "config.h"

using std::string;

// std::variant doesn't show up until c++17 :(
class Variant {
	enum Type {
		STRING,
		INTEGER,
		LIST,
		OBJECT,
		INV,
	};
	union {
		string as_string;
		long as_integer;
		std::vector<Variant *> as_list;
		std::map<string, Variant *> as_object;
	};

	Type type;
	Variant(const Variant &) { }
public:
	Variant(const string &r) : type(STRING)
	{ new (&as_string) string(r); }

	Variant(const long &r) : type(INTEGER)
	{ new (&as_integer) long(r); }

	Variant(const std::vector<Variant *> &r) : type(LIST)
	{ new (&as_list) std::vector<Variant *>(r); }

	Variant(const std::map<string, Variant *> &r) : type(OBJECT)
	{ new (&as_object) std::map<string, Variant *>(r); }

	Variant(void) : type(INV)
	{ }

	~Variant(void)
	{
		switch (type) {
		case STRING: as_string.~string(); break;
		case INTEGER: break;
		case LIST:
			for (Variant *p : as_list) {
				if (p != &inv)
					delete p;
			}
			as_list.~vector<Variant *>();
			break;
		case OBJECT:
			for (auto it : as_object) {
				if (it.second != &inv)
					delete it.second;
			}
			as_object.~map<string, Variant *>();
			break;
		case INV: break;
		}
	}

	bool isValid(void) const
	{
		return type != INV;
	}

	bool isString(void) const
	{
		return type == STRING;
	}

	const string &asString(const string &def="") const
	{
		if (!isString())
			return def;
		return as_string;
	}

	bool isInteger(void) const
	{
		return type == INTEGER;
	}

	long asInteger(long def=0) const
	{
		if (!isInteger())
			return def;
		return as_integer;
	}

	bool isList(void) const
	{
		return type == LIST;
	}

	bool isObject(void) const
	{
		return type == OBJECT;
	}

	const Variant &lookup(const string &path) const
	{
		std::vector<string> parts = strutil::split(path, "/");
		const Variant *p = this;

		for (string part : parts) {
			p = &p->get(part);
			if (!p->isValid())
				break;
		}

		return *p;
	}

	const Variant &get(const string &name) const
	{
		if (!isObject())
			return inv;

		auto it = as_object.find(name);
		if (it == as_object.end())
			return inv;

		return *it->second;
	}

	const Variant &index(unsigned int idx) const
	{
		if (!isList())
			return inv;

		if (idx > as_list.size())
			return inv;

		return *as_list[idx];
	}

	typedef std::vector<Variant *>::const_iterator const_iterator;

	const_iterator begin(void) const
	{
		if (!isList())
			return const_iterator();

		return as_list.begin();
	}

	const_iterator end(void) const
	{
		if (!isList())
			return const_iterator();

		return as_list.end();
	}

	static Variant inv;

};

Variant Variant::inv;

class CfgParser {
	struct Error {
		std::string message;

		Error(const std::string &msg)
		 : message(msg)
		{ }
	};
	std::string data;
	unsigned int pos;

	bool text(const std::string &txt)
	{
		if (data.compare(pos, txt.size(), txt))
			return false;
		pos += txt.size();

		return true;
	}

	bool eof(void)
	{
		return pos >= data.size();
	}

	void whitespace_(void)
	{
		while (!eof() && std::isspace(data[pos]))
			++pos;
	}

	bool comment(void)
	{
		if (eof() || data[pos] != '#')
			return false;

		while (!eof() && data[pos] != '\n')
			++pos;

		whitespace_();

		return true;
	}

	void whitespace(void)
	{
		whitespace_();
		while (comment())
			;
	}

	bool text_w(const std::string &txt)
	{
		if (!text(txt))
			return false;

		whitespace();

		return true;
	}

	std::string symbol(void)
	{
		unsigned int opos = pos;
		unsigned int epos;

		while (!eof()) {
			int ch = data[pos];
			if (!isalnum(ch) && ch != '-' && ch != '_')
				break;
			++pos;
		}

		epos = pos;
		if (epos != opos)
			whitespace();

		return data.substr(opos, epos - opos);
	}

	Variant *string(void)
	{
		std::string out = "";

		if (!text("\""))
			return NULL;

		while (!eof() && data[pos] != '"') {
			std::string seq;
			int ch;

			if (data[pos] == '\\') {
				++pos;

				if (eof())
					break;
				switch (data[pos]) {
				case '\\': seq = "\\"; break;
				case '"':  seq = "\""; break;
				case '/':  seq = "/";  break;
				case 'b':  seq = "\b"; break;
				case 'f':  seq = "\f"; break;
				case 'n':  seq = "\n"; break;
				case 'r':  seq = "\r"; break;
				case 't':  seq = "\t"; break;
				default:
					throw Error("unexpected escaped character");
				}
				++pos;
			} else {
				unsigned int start = pos;

				while (!eof()) {
					ch = data[pos];
					if (ch == '"' || ch == '\\')
						break;
					++pos;
				}
				seq = data.substr(start, pos - start);
			}

			out += seq;
		}

		if (eof())
			throw Error("EOF before string termination");
		if (!text("\""))
			throw Error("expected '\"'");

		whitespace();

		return new Variant(out);
	}

	Variant *integer(void)
	{
		const char *p = data.c_str() + pos;
		long val;
		char *ep;

		errno = 0;
		val = strtol(p, &ep, 0);
		if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
		    (errno != 0 && val == 0) || ep == p)
			throw Error("error parsing integer");

		pos += ep - p;
		whitespace();

		return new Variant(val);
	}

	Variant *list(void)
	{
		std::vector<Variant *> list;

		if (!text_w("["))
			return NULL;

		while (!text_w("]")) {
			list.push_back(any());
			if (!text_w(",")) {
				if (!text_w("]"))
					throw Error("expected list terminator ']'");
				break;
			}
		}

		return new Variant(list);
	}

	Variant *object(void)
	{
		std::map<std::string, Variant *> map;

		if (!text_w("{"))
			return NULL;

		while (!text_w("}")) {
			std::string key = symbol();

			if (key.size() == 0)
				throw Error("expected key");
			if (!text_w(":"))
				throw Error("expected ':'");

			map[key] = any();
			if (!text_w(",")) {
				if (!text_w("}"))
					throw Error("expected object terminator '}'");
				break;
			}
		}

		return new Variant(map);
	}

	Variant *any(void)
	{
		int ch;

		if (eof())
			throw Error("unexpected EOF");

		if (text_w("false"))
			return new Variant((long)false);
		if (text_w("true"))
			return new Variant((long)true);

		ch = data[pos];
		if (ch == '"') {
			return string();
		} else if (ch == '[') {
			return list();
		} else if (ch == '{') {
			return object();
		} else if (std::isdigit(ch)) {
			return integer();
		}

		throw Error("expected data-type");
	}

	Variant *global(void)
	{
		std::map<std::string, Variant *> map;

		whitespace();

		while (!eof()) {
			std::string key = symbol();

			if (key.size() == 0)
				throw Error("expected variable name");
			if (!text_w(":"))
				throw Error("expected ':'");
			map[key] = any();
		}

		return new Variant(map);
	}

	void printError(Error &err, const std::string &filename)
	{
		unsigned int lineno = 1;
		unsigned int chno = 0;
		const char *s = data.c_str();
		const char *e = s + pos;
		const char *p = s;
		std::string point;
		std::string line;
		std::string desc;

		for (; p < e; ++p) {
			if (*p == '\n') {
				++lineno;
				chno = 0;
			} else {
				++chno;
			}
		}
		while (*p && *p != '\n')
			++p;

		desc = strutil::format("%s:%d:%d", filename.c_str(), lineno, chno + 1);
		line = data.substr(pos - chno, (p - s) - (pos - chno));
		point = std::string(chno + 1, ' ');
		point[chno] = '^';
		for (unsigned int i = 0; i < chno; ++i) {
			if (line[i] == '\t')
				point[i] = '\t';
		}
		fprintf(stderr, "%s: error parsing config\n", desc.c_str());
		fprintf(stderr, "%s: %s\n", desc.c_str(), line.c_str());
		fprintf(stderr, "%s: %s\n", desc.c_str(), point.c_str());
		fprintf(stderr, "%s: error: %s\n", desc.c_str(), err.message.c_str());
	}
public:

	CfgParser(const std::string &data_)
	 : data(data_), pos(0)
	{ }

	Variant *parse(const std::string &filename)
	{
		pos = 0;

		try {
			return global();
		} catch (Error &err) {
			printError(err, filename);
		}
		return NULL;
	}
};

static std::shared_ptr<DecisionTree::Component>
makeComponent(const std::string &filename, const Variant &cmp)
{
	std::string type = cmp.get("type").asString();
	unsigned int offset = cmp.get("offset").asInteger();
	unsigned int size = cmp.get("size").asInteger();

	if (type != "lut") {
		fprintf(stderr, "%s: unknown component type \"%s\"",
				filename.c_str(), type.c_str());
		return NULL;
	}

	return std::make_shared<LUT>(size, offset);
}

static int makeStewItem(const std::string &filename, STEW::Item &item, const Variant &stewi)
{
	std::string type = stewi.get("type").asString();

	item.offset = stewi.get("offset").asInteger();
	item.size = stewi.get("size").asInteger();

	if (type == "inc") {
		item.type = STEW::INC;
	} else if (type == "mux") {
		item.type = STEW::MUX;
	} else if (type == "jmp") {
		item.type = STEW::JMP;
	} else if (type == "out") {
		item.type = STEW::OUT;
	} else if (type == "cfg") {
		item.type = STEW::CFG;
	} else {
		fprintf(stderr, "%s: unknown stew item type \"%s\"",
				filename.c_str(), type.c_str());
		return -1;
	}

	return 0;
}

static int makeConfig(const std::string &filename, PrismConfig &pc, const Variant &cfg)
{
	pc.title = cfg.lookup("title").asString();
	pc.version = cfg.lookup("version").asString();

	const Variant &muxes = cfg.lookup("muxes");
	if (!muxes.isValid()) {
		fprintf(stderr, "%s: mux configuration missing\n", filename.c_str());
		return -1;
	}
	pc.tree.wires.muxes.nBits = muxes.get("size").asInteger();
	pc.tree.wires.muxes.nMux = muxes.get("count").asInteger();

	for (const Variant *pair : cfg.lookup("wiremap")) {
		unsigned int a = pair->index(0).asInteger();
		unsigned int b = pair->index(1).asInteger();
		pc.tree.wires.mappings.push_back(std::pair<unsigned int, unsigned int>(a, b));
	}

	for (std::pair<unsigned int, unsigned int> item : pc.tree.wires.mappings) {
		if (item.second >= pc.tree.wires.muxes.nMux) {
			fprintf(stderr, "%s: wire-map invalid: index out-of-bounds\n",
					filename.c_str());
			return -1;
		}
	}

	pc.tree.wires.nVirtualOutput = 0;
	for (const Variant *cmp : cfg.lookup("decision-tree/static-components")) {
		std::shared_ptr<DecisionTree::Component> p =
			makeComponent(filename, *cmp);

		if (p == NULL)
			return -1;

		pc.tree.staticComponents.push_back(p);
		pc.tree.wires.nVirtualOutput += p->inputSize;
	}

	for (const Variant *cmp : cfg.lookup("decision-tree/conditional-components")) {
		std::shared_ptr<DecisionTree::Component> p =
				makeComponent(filename, *cmp);

		if (p == NULL)
			return -1;

		pc.tree.condComponents.push_back(p);
		pc.tree.wires.nVirtualOutput += p->inputSize;
	}


	const Variant &stew = cfg.lookup("stew");
	if (!stew.isValid()) {
		fprintf(stderr, "%s: STEW configuration missing\n", filename.c_str());
		return -1;
	}
	pc.stew.count = stew.get("count").asInteger();
	pc.stew.size = stew.get("size").asInteger();
	for (const Variant *stewi : stew.get("items")) {
		STEW::Item item;
		int rc;

		rc = makeStewItem(filename, item, *stewi);
		if (rc)
			return -1;

		pc.stew.items.push_back(item);
	}

	return 0;
}

int PrismConfig::parse(const std::string &filename, PrismConfig &pc)
{
	std::ifstream t(filename);
	std::string str((std::istreambuf_iterator<char>(t)),
			std::istreambuf_iterator<char>());
	CfgParser parser(str);
	int rc;

	Variant *cfg = parser.parse(filename);
	if (cfg == NULL)
		return -1;

	rc = makeConfig(filename, pc, *cfg);
  config = filename;
	delete cfg;

	return rc;
}

void PrismConfig::fallback(PrismConfig &pc)
{
	pc.title = "LUT4+LUT4";
	pc.tree.wires.muxes.nBits = 4;
	pc.tree.wires.muxes.nMux = 7;
	pc.tree.wires.nVirtualOutput = 14;
	pc.tree.wires.mappings = {
		{  4, 3 },
		{  8, 0 },
		{  9, 1 },
		{ 10, 2 },
		{ 11, 3 },
		{ 12, 5 },
		{ 13, 6 },
	};
	pc.tree.staticComponents = { // ... should match STEW cfg order
		std::make_shared<LUT>(4, 0), // highest priority first
		std::make_shared<LUT>(4, 4),
	};
	pc.tree.condComponents = { // ... should match STEW cfg order
		std::make_shared<LUT>(2,  8), // cout-bit[0]
		std::make_shared<LUT>(2, 10), // cout-bit[1]
		std::make_shared<LUT>(2, 12), // cout-bit[1]
	};
	pc.stew.count = 48;
	pc.stew.size = 168; // rounded up from 165
	pc.stew.items = {
		{ STEW::INC,   0,  1 },
		{ STEW::MUX,   1, 28 },
		{ STEW::JMP,  29,  6 },
		{ STEW::JMP,  35,  6 }, // b is the else case, goes second
		{ STEW::OUT,  65, 24 },
		{ STEW::OUT,  89, 24 }, // b is the else case, goes second
		{ STEW::OUT,  41, 24 }, // default case goes last
		{ STEW::CFG, 121, 16 },
		{ STEW::CFG, 137, 16 }, // b is the else case, goes second
		{ STEW::CFG, 153,  4 }, // cout-bit[0]
		{ STEW::CFG, 157,  4 }, // cout-bit[1]
		{ STEW::CFG, 161,  4 }, // cout-bit[2]
	};
}
