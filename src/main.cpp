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
        } else {
            args.push_back(std::move(arg));
        }
    }

    if (args.size() < 2) {
        std::cout << "Usage: mllinker [--map <file>] [--base <hex_addr>] <output.bin> <input1.obj> [input2.obj ...]"
                  << std::endl;
        return 1;
    }

    std::string output_path = args[0];
    std::vector<std::string> input_files(args.begin() + 1, args.end());

    if (!link_objects(input_files, output_path, map_path, base_addr)) {
        return 1;
    }

    return 0;
}
