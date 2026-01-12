#include <iostream>
#include <string>
#include <vector>

#include "Linker.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: mllinker <output.bin> <input1.obj> [input2.obj ...]" << std::endl;
        return 1;
    }

    std::string output_path = argv[1];
    std::vector<std::string> input_files(argv + 2, argv + argc);

    if (!link_objects(input_files, output_path)) {
        return 1;
    }

    return 0;
}