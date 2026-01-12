#ifndef MYCCLINKER_LINKER_H
#define MYCCLINKER_LINKER_H

#include <cstdint>
#include <string>
#include <vector>

#include "ObjectFormat.h"

// Data Structures to hold loaded Object File content
struct LoadedObject {
    std::string filename;
    FileHeader header;
    std::vector<uint8_t> text_section;
    std::vector<uint8_t> data_section;
    std::vector<SymbolEntry> symbols;
    std::vector<RelocEntry> relocs;

    // Calculated during Pass 1
    uint32_t text_base_addr;
    uint32_t data_base_addr;
};

bool link_objects(const std::vector<std::string>& input_files, const std::string& output_path);

#endif  // MYCCLINKER_LINKER_H
