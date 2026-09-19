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
    std::vector<CollectEntry> collects;

    // Calculated during Pass 1
    uint32_t text_base_addr;
    uint32_t data_base_addr;
};

// Redirect only RELATIVE (direct call/branch) relocations from `from` to `to`.
// Calls emitted by the object defining `to` remain pointed at `from`, which
// lets a generated TestKit entry call the original implementation.
struct LinkRedirect {
    std::string from;
    std::string to;
};

// Link the given object files into output_path. When map_path is non-empty,
// also write a symbol map (one "0xADDR NAME" line per resolved symbol, sorted
// by address) that tools like the profiler use to translate PCs into names.
bool link_objects(const std::vector<std::string>& input_files,
                  const std::string& output_path,
                  const std::string& map_path,
                  uint32_t base_addr,
                  bool emit_header = false,
                  const std::vector<LinkRedirect>& redirects = {});

#endif  // MYCCLINKER_LINKER_H
