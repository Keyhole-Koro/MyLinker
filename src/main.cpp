#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "Linker.h"

namespace {

// `--base`: hexadecimal digits, after an optional 0x, of at most 32 bits.
// Nothing else is accepted: std::stoul threw on "zz", stopped at "40zz" and
// let "100000040" through truncated to 0x40.
bool parse_base_address(const std::string& text, uint32_t& address) {
    size_t at = 0;
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) at = 2;
    if (at == text.size()) return false;
    uint64_t value = 0;
    for (; at < text.size(); ++at) {
        const char c = text[at];
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        value = value * 16 + static_cast<uint64_t>(digit);
        if (value > 0xFFFFFFFFu) return false;
    }
    address = static_cast<uint32_t>(value);
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    // Pull the optional "--map <file>" flag out of the argument list first so the
    // remaining args keep their historic positional meaning:
    //   mllinker <output.bin> <input1.obj> [input2.obj ...]
    std::string map_path;
    std::string lines_path;
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
        } else if (arg == "--lines") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --lines requires a file path" << std::endl;
                return 1;
            }
            lines_path = argv[++i];
        } else if (arg == "--base") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --base requires an address" << std::endl;
                return 1;
            }
            std::string value = argv[++i];
            if (!parse_base_address(value, base_addr)) {
                std::cerr << "Error: --base expects a hexadecimal address of at most 32 bits, got '"
                          << value << "'" << std::endl;
                return 1;
            }
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
        std::cout << "Usage: mllinker [--map <file>] [--lines <file>] [--base <hex_addr>] [--header]"
                  << " [--redirect <original>=<entry>] <output.bin> <input1.obj> [input2.obj ...]"
                  << std::endl;
        return 1;
    }

    std::string output_path = args[0];
    std::vector<std::string> input_files(args.begin() + 1, args.end());

    if (!link_objects(input_files, output_path, map_path, lines_path, base_addr, emit_header, redirects)) {
        return 1;
    }

    return 0;
}
