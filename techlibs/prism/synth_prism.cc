#include "kernel/yosys.h"
#include "kernel/sigtools.h"

#include "frontends/ast/ast.h"

#include "prism/prism.h"

#include <memory>
#include <list>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

class OutputFileType {
protected:
	std::string filename;
	Prism::Format format;
	std::ostream *stream;

public:
	OutputFileType(const std::string &fname, Prism::Format fmt)
	 : filename(fname), format(fmt), stream(NULL)
	{ }

	~OutputFileType(void)
	{
		if (stream != NULL)
			delete stream;
	}

	bool open(void)
	{
		std::ofstream *ff = new std::ofstream;

		ff->open(filename.c_str(), std::ofstream::trunc);
		if (ff->fail()) {
			log_error("Unable to open \"%s\" for writing: %s",
					filename.c_str(), strerror(errno));
			delete ff;
			return false;
		}

		stream = ff;
		return true;
	}

	void remove(void)
	{
		std::remove(filename.c_str());
	}

	void write(Prism &prism)
	{
		prism.writeOutput(format, *stream);
	}
};

static std::unique_ptr<OutputFileType> make_file(const std::string &fname, Prism::Format fmt)
{
	return std::unique_ptr<OutputFileType>(new OutputFileType(fname, fmt));
}

struct SynthPrismPass : public Pass
{
	SynthPrismPass() : Pass("synth_prism", "synthesis for PRISM chromas") { }

	void help() override
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    synth_prism [options]\n");
		log("\n");
		log("This command runs synthesis for PRISM architectures.\n");
		log("\n");
		log("    -top <module>\n");
		log("        use the specified module as top module\n");
		log("\n");
		log("    -cfg <file>\n");
		log("        read the PRISM configuration from the specified file. \n");
		log("\n");
		log("    -hex <file>\n");
		log("        write the PRISM table in HEX to the specified file.\n");
		log("\n");
		log("    -list <file>\n");
		log("        write the PRISM table in list format to the specified file.\n");
		log("\n");
		log("    -tab <file>\n");
		log("        write the PRISM table to the specified file.\n");
		log("    -cfile <file>\n");
		log("        write the PRISM table in compilable C to the specified file.\n");
		log("\n");
	}

	string top_module;
	string module_name;
	string cfg_file;

	void clear_flags() override
	{
		top_module = "\\prism_fsm";
		cfg_file = "";
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		std::list<std::unique_ptr<OutputFileType>> outputs;
		size_t argidx;

		clear_flags();

		for (argidx = 1; argidx < args.size(); argidx++) {
			if (args[argidx] == "-top" && argidx+1 < args.size()) {
        module_name = args[++argidx];
				top_module = "\\" + args[argidx];
				continue;
			}
			if (args[argidx] == "-hex" && argidx+1 < args.size()) {
				outputs.push_back(make_file(args[++argidx], Prism::HEX));
				continue;
			}
			if (args[argidx] == "-list" && argidx+1 < args.size()) {
				outputs.push_back(make_file(args[++argidx], Prism::LIST));
				continue;
			}
			if (args[argidx] == "-tab" && argidx+1 < args.size()) {
				outputs.push_back(make_file(args[++argidx], Prism::TAB));
				continue;
			}
			if (args[argidx] == "-cfile" && argidx+1 < args.size()) {
				outputs.push_back(make_file(args[++argidx], Prism::CFILE));
				continue;
			}
			if (args[argidx] == "-cfg" && argidx+1 < args.size()) {
				cfg_file = args[++argidx];
				continue;
			}
			break;
		}
		extra_args(args, argidx, design);

		if (!design->full_selection())
			log_cmd_error("This command only operates on fully selected designs!\n");

		log_header(design, "Executing SYNTH_PRISM pass.\n");
		log_push();

		RTLIL::Module *module = design->module(top_module);
		AST::AstModule *ast_module = dynamic_cast<AST::AstModule *>(module);
		if (ast_module == NULL) {
			log_error("no \"%s\" module\n", top_module.c_str());
		} else {
			AST::AstNode *ast = ast_module->ast->clone();
			Prism prism;
			bool ret;

      prism.module_name = module_name;
			log("Simplifying AST.\n");
			while (ast->simplify(true, 1, -1, false));

			if (!cfg_file.empty()) {
				log("Parsing configuration.\n");
				if (!prism.parseConfig(cfg_file))
					log_error("failed to parse PRISM configuration.\n");
			}

			log("Parsing AST.\n");
			for (auto &&ftype : outputs)
				ftype->open();

			ret = prism.parseAst(*ast);
			if (!ret) {
				for (auto &&ftype : outputs)
					ftype->remove();
				log_error("failed to parse and generate PRISM data.\n");
			} else if (outputs.size() != 0) {
				for (auto &&ftype : outputs)
					ftype->write(prism);
			} else {
				prism.writeOutput(Prism::TAB, std::cout);
			}

			delete ast;
		}

		log_pop();
	}
} SynthPrismPass;

PRIVATE_NAMESPACE_END
