#include <iostream>
#include <string>
#include <vector>

#include "Linker.h"

int main(int argc, char* argv[]) {
    // Pull the optional "--map <file>" flag out of the argument list first so the
    // remaining args keep their historic positional meaning:
    //   mllinker <output.bin> <input1.obj> [input2.obj ...]
    std::string map_path;
    uint32_t base_addr = 0;
    bool emit_header = false;
    std::vector<LinkRedirect> redirects;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--map") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --map requires a file path" << std::endl;
                return 1;
            }
            map_path = argv[++i];
        } else if (arg == "--base") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --base requires an address" << std::endl;
                return 1;
            }
            base_addr = std::stoul(argv[++i], nullptr, 16);
        } else if (arg == "--header") {
            emit_header = true;
        } else if (arg == "--redirect") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --redirect requires <original>=<entry>" << std::endl;
                return 1;
            }
            std::string spec = argv[++i];
            size_t separator = spec.find('=');
            if (separator == std::string::npos || separator == 0 || separator + 1 == spec.size()) {
                std::cerr << "Error: invalid redirect '" << spec
                          << "' (expected <original>=<entry>)" << std::endl;
                return 1;
            }
            redirects.push_back({spec.substr(0, separator), spec.substr(separator + 1)});
        } else {
            args.push_back(std::move(arg));
        }
    }

    if (args.size() < 2) {
        std::cout << "Usage: mllinker [--map <file>] [--base <hex_addr>] [--header]"
                  << " [--redirect <original>=<entry>] <output.bin> <input1.obj> [input2.obj ...]"
                  << std::endl;
        return 1;
    }

    std::string output_path = args[0];
    std::vector<std::string> input_files(args.begin() + 1, args.end());

    if (!link_objects(input_files, output_path, map_path, base_addr, emit_header, redirects)) {
        return 1;
    }

    return 0;
}
