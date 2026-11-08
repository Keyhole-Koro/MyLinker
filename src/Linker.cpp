#include "Linker.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

// A collected section, laid out after the data sections (see ObjectFormat.h,
// CollectEntry): every chunk of one name contiguously in link order. The
// section directory (`__sections`) follows the last one: one
// `[name, start, size]` row per section and a zero row, followed by the name
// strings. `__section_<name>` is a section's start and
// `__section_<name>_size` the address of its row's size word, so a reader
// can either name a section statically or look it up by string at runtime.
struct CollectedSection {
    std::string name;
    uint32_t start;                               // final address of the first byte
    uint32_t size;                                // bytes, all chunks
    std::vector<std::pair<size_t, size_t>> chunks; // (object index, CollectEntry index) in link order
};

// Where the link puts everything: worked out by layout_and_define_symbols,
// written out by write_output and described by write_map.
struct Layout {
    uint32_t text_base = 0;
    uint32_t text_size = 0;                 // every live object's text
    uint32_t data_base = 0;                 // see data_base_address
    uint32_t data_size = 0;                 // their data, then the collected sections and directory
    std::vector<CollectedSection> sections; // after the data, in order of first appearance
    std::vector<uint8_t> directory;         // `__sections`, after the last section
};

// Words in the image are big-endian, as the machine reads them.
uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

void write_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

void append_be32(std::vector<uint8_t>& out, uint32_t v) {
    out.resize(out.size() + 4);
    write_be32(&out[out.size() - 4], v);
}

// Generic instantiations are emitted into every object that uses them, the way
// C++ templates are. MyLangCompiler mangles the template and its type
// arguments into the name (`__mlg_f_7_rb_push_p0_r0_m0_n3_i32`), so two
// definitions sharing a name are the same function and the first one can stand
// for all of them. The prefix is compiler-owned: MyLangCompiler rejects any
// user declaration that starts with it.
//
// This is the symbol half of what C++ gets from a COMDAT group. The duplicate's
// bytes stay in the image, because the object format carries one text section
// per object and offers no way to drop a single function's range -- only the
// reference is deduplicated. Dropping the code too would need per-function
// sections and a group signature in the object format.
static bool is_mergeable_instantiation(const char* name) {
    static const char kPrefix[] = "__mlg_";
    return std::strncmp(name, kPrefix, sizeof(kPrefix) - 1) == 0;
}

// A label the assembler made private to its object (ObjectFormat.h).
bool is_private_label(const char* name) {
    return std::strstr(name, PRIVATE_LABEL_MARK) != nullptr;
}

bool load_object_file(const std::string& path, LoadedObject& obj) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Could not open file " << path << std::endl;
        return false;
    }

    obj.filename = path;

    // Read Header
    file.read(reinterpret_cast<char*>(&obj.header), sizeof(FileHeader));
    if (obj.header.magic != LINKER_MAGIC) {
        std::cerr << "Error: Invalid magic number in " << path << std::endl;
        return false;
    }

    // Read Text Section
    obj.text_section.resize(obj.header.text_size);
    if (obj.header.text_size > 0) {
        file.read(reinterpret_cast<char*>(obj.text_section.data()), obj.header.text_size);
    }

    // Read Data Section
    obj.data_section.resize(obj.header.data_size);
    if (obj.header.data_size > 0) {
        file.read(reinterpret_cast<char*>(obj.data_section.data()), obj.header.data_size);
    }

    // Read the collected-section blob
    obj.collect_blob.resize(obj.header.collect_size);
    if (obj.header.collect_size > 0) {
        file.read(reinterpret_cast<char*>(obj.collect_blob.data()), obj.header.collect_size);
    }

    // Read Symbols
    obj.symbols.resize(obj.header.symtable_count);
    if (obj.header.symtable_count > 0) {
        file.read(reinterpret_cast<char*>(obj.symbols.data()),
                  obj.header.symtable_count * sizeof(SymbolEntry));
    }

    // Read Relocations
    obj.relocs.resize(obj.header.reloc_count);
    if (obj.header.reloc_count > 0) {
        file.read(reinterpret_cast<char*>(obj.relocs.data()),
                  obj.header.reloc_count * sizeof(RelocEntry));
    }

    // Read collected-section chunks
    obj.collects.resize(obj.header.collect_count);
    if (obj.header.collect_count > 0) {
        file.read(reinterpret_cast<char*>(obj.collects.data()),
                  obj.header.collect_count * sizeof(CollectEntry));
    }

    // Read the debug trailer, which an object may not have
    DebugHeader debug = {};
    if (file.read(reinterpret_cast<char*>(&debug), sizeof(debug)) && debug.magic == DEBUG_MAGIC) {
        obj.debug_strings.resize(debug.string_size);
        file.read(obj.debug_strings.data(), debug.string_size);
        obj.debug_funcs.resize(debug.func_count);
        file.read(reinterpret_cast<char*>(obj.debug_funcs.data()),
                  debug.func_count * sizeof(DebugFunc));
        obj.debug_rows.resize(debug.row_count);
        file.read(reinterpret_cast<char*>(obj.debug_rows.data()),
                  debug.row_count * sizeof(DebugRow));
        if (!file) {
            std::cerr << "Error: Truncated debug trailer in " << path << std::endl;
            return false;
        }
    }

    return true;
}

// Drop from `objects` every object the link does not need, and return the
// names it needs: __START__, the redirect entries, and every name a live
// object's relocations refer to. An object is live when it defines a needed
// name or contributes to a collected section.
std::set<std::string> keep_live_objects(std::vector<LoadedObject>& objects,
                                        const std::vector<LinkRedirect>& redirects) {
    std::set<std::string> needed_symbols;
    needed_symbols.insert("__START__");
    for (const auto& redirect : redirects) {
        // A redirect entry may otherwise have no ordinary references before
        // relocation rewriting, so it must be live from the first pass.
        needed_symbols.insert(redirect.to);
    }

    std::vector<bool> object_active(objects.size(), false);
    // An object that contributes to a collected section is live by that
    // fact: a table entry is a registration, and nothing else may refer to
    // the object (an app module is only ever reached through its rows).
    for (size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i].collects.empty()) object_active[i] = true;
    }

    // Until nothing changes, activate the objects that define a needed name,
    // then need what the active objects' relocations refer to. Only that: the
    // rest of an active object's definitions do not become needed.
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < objects.size(); ++i) {
            if (object_active[i]) continue;
            for (const auto& sym : objects[i].symbols) {
                if (sym.type == SYMBOL_DEFINED && needed_symbols.count(sym.name)) {
                    object_active[i] = true;
                    changed = true;
                    break;
                }
            }
        }
        for (size_t i = 0; i < objects.size(); ++i) {
            if (!object_active[i]) continue;
            for (const auto& reloc : objects[i].relocs) {
                if (needed_symbols.insert(reloc.symbol_name).second) changed = true;
            }
        }
    }

    std::vector<LoadedObject> active_objects;
    for (size_t i = 0; i < objects.size(); ++i) {
        if (object_active[i]) {
            active_objects.push_back(std::move(objects[i]));
        }
    }
    objects = std::move(active_objects);
    return needed_symbols;
}

// The data follows the text, on the next 4 KiB page in an MBIN image
// (emit_header) so that text and data reside on separate pages with
// distinct MMU protections (RX vs RW).
uint32_t data_base_address(uint32_t text_base, uint32_t text_size, bool emit_header) {
    uint32_t text_end = text_base + text_size;
    return emit_header ? ((text_end + 4095u) & ~4095u) : text_end;
}

// Every live object's text in link order from `base_addr`, and their data
// likewise from the data base.
void lay_out_objects(std::vector<LoadedObject>& objects, uint32_t base_addr, bool emit_header,
                     Layout& layout) {
    layout.text_base = base_addr;
    for (const auto& obj : objects) {
        layout.text_size += obj.header.text_size;
        layout.data_size += obj.header.data_size;
    }
    layout.data_base = data_base_address(layout.text_base, layout.text_size, emit_header);

    uint32_t text_at = layout.text_base;
    uint32_t data_at = layout.data_base;
    for (auto& obj : objects) {
        obj.text_base_addr = text_at;
        obj.data_base_addr = data_at;
        text_at += obj.header.text_size;
        data_at += obj.header.data_size;
    }
}

// The collected sections after all data -- each name's chunks contiguous in
// link order, giving every chunk its address -- then the directory. Defines
// `__section_<name>`, `__section_<name>_size` and `__sections`.
void lay_out_collected_sections(std::vector<LoadedObject>& objects, Layout& layout,
                                std::map<std::string, uint32_t>& global_symbol_table) {
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        objects[oi].chunk_base_addr.assign(objects[oi].collects.size(), 0);
        for (size_t ci = 0; ci < objects[oi].collects.size(); ++ci) {
            std::string name(objects[oi].collects[ci].name);
            CollectedSection* sec = nullptr;
            for (auto& s : layout.sections) if (s.name == name) sec = &s;
            if (!sec) { layout.sections.push_back({name, 0, 0, {}}); sec = &layout.sections.back(); }
            sec->chunks.push_back({oi, ci});
        }
    }
    const uint32_t data_end = layout.data_base + layout.data_size;
    uint32_t at = data_end;
    for (auto& sec : layout.sections) {
        sec.start = at;
        for (const auto& ref : sec.chunks) {
            objects[ref.first].chunk_base_addr[ref.second] = at;
            at += objects[ref.first].collects[ref.second].size;
        }
        sec.size = at - sec.start;
    }
    // The directory, after every chunk: rows first, then the names they
    // point at. Always present (a lone zero row when nothing was
    // collected) so `__sections` can be imported unconditionally.
    const uint32_t row_bytes = 12;
    uint32_t names_at = at + static_cast<uint32_t>((layout.sections.size() + 1) * row_bytes);
    std::vector<uint8_t> names;
    for (size_t i = 0; i < layout.sections.size(); ++i) {
        const auto& sec = layout.sections[i];
        append_be32(layout.directory, names_at + static_cast<uint32_t>(names.size()));
        append_be32(layout.directory, sec.start);
        append_be32(layout.directory, sec.size);
        names.insert(names.end(), sec.name.begin(), sec.name.end());
        names.push_back(0);
        while (names.size() % 4 != 0) names.push_back(0);
        global_symbol_table["__section_" + sec.name] = sec.start;
        global_symbol_table["__section_" + sec.name + "_size"] = at + static_cast<uint32_t>(i * row_bytes) + 8;
    }
    append_be32(layout.directory, 0);
    append_be32(layout.directory, 0);
    append_be32(layout.directory, 0);
    layout.directory.insert(layout.directory.end(), names.begin(), names.end());
    global_symbol_table["__sections"] = at;
    layout.data_size += (at - data_end) + static_cast<uint32_t>(layout.directory.size());
}

// The final address of a symbol `obj` defines, once the object and its
// chunks are laid out. 0 for an offset in no chunk or an unknown section.
uint32_t symbol_address(const LoadedObject& obj, const SymbolEntry& sym) {
    if (sym.section == SECTION_TEXT) return obj.text_base_addr + sym.offset;
    if (sym.section == SECTION_DATA) return obj.data_base_addr + sym.offset;
    if (sym.section == SECTION_COLLECT) {
        // Inside whichever chunk of the blob holds the offset.
        for (size_t ci = 0; ci < obj.collects.size(); ++ci) {
            const auto& ce = obj.collects[ci];
            if (sym.offset >= ce.offset && sym.offset < ce.offset + ce.size) {
                return obj.chunk_base_addr[ci] + (sym.offset - ce.offset);
            }
        }
    }
    return 0;
}

// Enter every needed symbol the live objects define in the table, and
// reject a name two objects define.
bool define_symbols(const std::vector<LoadedObject>& objects,
                    const std::set<std::string>& needed_symbols,
                    std::map<std::string, uint32_t>& global_symbol_table) {
    std::map<std::string, std::string> all_definitions; // name -> first defining object
    for (const auto& obj : objects) {
        for (const auto& sym : obj.symbols) {
            if (sym.type != SYMBOL_DEFINED) continue;
            // Two objects defining the same name is an error whether or
            // not anyone references it: a reference would silently bind
            // to whichever came first. Private labels are excluded: the
            // assembler tags them `name-[module:n]` with the file's stem,
            // so two files with the same stem in different directories
            // (TestKit's runtime/verdict.mln and platform/.../verdict.mln)
            // repeat tags, and such labels are never referenced across
            // objects anyway.
            if (!is_mergeable_instantiation(sym.name) && !is_private_label(sym.name)) {
                auto seen = all_definitions.find(sym.name);
                if (seen != all_definitions.end()) {
                    std::cerr << "Error: Duplicate symbol definition '" << sym.name
                              << "' in " << obj.filename << " (first defined in "
                              << seen->second << ")" << std::endl;
                    return false;
                }
                all_definitions[sym.name] = obj.filename;
            }
            // Only a needed symbol goes in the table.
            if (!needed_symbols.count(sym.name)) continue;
            if (global_symbol_table.count(sym.name)) {
                if (is_mergeable_instantiation(sym.name)) {
                    // Already have an identical copy; keep the first.
                    continue;
                }
                std::cerr << "Error: Duplicate symbol definition '" << sym.name << "'" << std::endl;
                return false;
            }
            global_symbol_table[sym.name] = symbol_address(obj, sym);
        }
    }
    return true;
}

// Pass 1: keep the objects the link needs, lay them out from `base_addr`,
// and give every needed symbol its final address.
bool layout_and_define_symbols(std::vector<LoadedObject>& objects,
                               std::map<std::string, uint32_t>& global_symbol_table,
                               Layout& layout,
                               uint32_t base_addr,
                               bool emit_header,
                               const std::vector<LinkRedirect>& redirects) {
    std::set<std::string> needed_symbols = keep_live_objects(objects, redirects);
    lay_out_objects(objects, base_addr, emit_header, layout);
    // Before the symbols, so that one inside a chunk has its address.
    lay_out_collected_sections(objects, layout, global_symbol_table);
    if (!define_symbols(objects, needed_symbols, global_symbol_table)) {
        return false;
    }

    // Synthesize the `_end` symbol marking the end of the loaded image
    // (text + data, the collected sections and directory included). The
    // kernel heap allocator uses this as the start of its free region.
    if (!global_symbol_table.count("_end")) {
        global_symbol_table["_end"] = layout.data_base + layout.data_size;
    }

    // verify all needed symbols are found
    for (const auto& name : needed_symbols) {
        if (global_symbol_table.find(name) == global_symbol_table.end()) {
             std::cerr << "Error: Undefined symbol '" << name << "'" << std::endl;
             return false;
        }
    }

    return true;
}

bool object_defines_symbol(const LoadedObject& object, const std::string& name) {
    for (const auto& symbol : object.symbols) {
        if (symbol.type == SYMBOL_DEFINED && name == symbol.name) return true;
    }
    return false;
}

const LinkRedirect* redirect_for(const std::vector<LinkRedirect>& redirects,
                                 const std::string& symbol_name,
                                 const LoadedObject& object) {
    for (const auto& redirect : redirects) {
        if (redirect.from == symbol_name && !object_defines_symbol(object, redirect.to)) {
            return &redirect;
        }
    }
    return nullptr;
}

// The word a relocation leaves at address `pc`, from the word the assembler
// left there and the symbol's final address. RELATIVE fills a call or jump's
// low 26 bits with `target - pc`, the offset in bytes from the instruction
// itself, as the assembler encodes a jump to a local label; ABSOLUTE fills
// MOVI's low 21-bit immediate. Both keep the opcode and register bits.
// WORD32 replaces the whole word.
uint32_t patch_word(uint32_t type, uint32_t existing, uint32_t target, uint32_t pc) {
    switch (type) {
    case RELOC_WORD32:
        return target;
    case RELOC_RELATIVE:
        return (existing & 0xFC000000u) | ((target - pc) & 0x03FFFFFFu);
    case RELOC_ABSOLUTE:
        return (existing & 0xFFE00000u) | (target & 0x001FFFFFu);
    default:
        // No object carries another type; one has always been taken as an
        // ABSOLUTE to address 0.
        return existing & 0xFFE00000u;
    }
}

// Patch every relocation of the live objects with its symbol's final
// address (see patch_word).
bool apply_relocations(std::vector<LoadedObject>& objects,
                       const std::map<std::string, uint32_t>& global_symbol_table,
                       const std::vector<LinkRedirect>& redirects) {
    for (auto& obj : objects) {
        for (const auto& reloc : obj.relocs) {
            std::string sym_name(reloc.symbol_name);

            // Test interception changes only direct control-flow relocations.
            // Absolute references (for example taking a function address) keep
            // their source identity so production ABI remains unchanged.
            if (reloc.type == RELOC_RELATIVE) {
                if (const LinkRedirect* redirect = redirect_for(redirects, sym_name, obj)) {
                    sym_name = redirect->to;
                }
            }

            auto symbol = global_symbol_table.find(sym_name);
            if (symbol == global_symbol_table.end()) {
                std::cerr << "Error: Undefined symbol '" << sym_name << "' referenced in "
                          << obj.filename << std::endl;
                return false;
            }
            uint32_t target_addr = symbol->second;

            uint32_t patch_offset = reloc.offset; // within this object's text, data or blob
            bool in_blob = reloc.section == SECTION_COLLECT;
            bool in_data = reloc.section == SECTION_DATA;
            std::vector<uint8_t>& section_bytes = in_blob ? obj.collect_blob
                                                 : (in_data ? obj.data_section : obj.text_section);
            if (patch_offset + 4 > section_bytes.size()) {
                std::cerr << "Error: Relocation offset out of bounds in " << obj.filename
                          << std::endl;
                return false;
            }
            if ((in_blob || in_data) && reloc.type != RELOC_WORD32) {
                std::cerr << "Error: only .word relocations are allowed in a data or collected section ("
                          << obj.filename << ")" << std::endl;
                return false;
            }
            if (reloc.type == RELOC_ABSOLUTE && target_addr >= (1u << 21)) {
                std::cerr << "Error: Absolute relocation target 0x" << std::hex
                          << target_addr << std::dec
                          << " exceeds 21-bit immediate range in " << obj.filename
                          << std::endl;
                return false;
            }

            uint8_t* word = &section_bytes[patch_offset];
            uint32_t pc = obj.text_base_addr + patch_offset;
            write_be32(word, patch_word(reloc.type, read_be32(word), target_addr, pc));
        }
    }

    return true;
}

bool write_output(const std::string& output_path,
                  const std::vector<LoadedObject>& objects,
                  const Layout& layout,
                  bool emit_header,
                  uint32_t entry_point) {
    std::ofstream outfile(output_path, std::ios::binary);
    if (!outfile) {
        std::cerr << "Error: Could not open output file " << output_path << std::endl;
        return false;
    }

    if (emit_header) {
        const uint32_t header_size = static_cast<uint32_t>(sizeof(MbinHeader));
        // The directory follows every object's data and every collected
        // chunk; its file offset is what a reader on disk needs.
        uint32_t plain_data = 0;
        for (const auto& obj : objects) plain_data += static_cast<uint32_t>(obj.data_section.size());
        uint32_t chunk_bytes = 0;
        for (const auto& sec : layout.sections) chunk_bytes += sec.size;
        uint32_t sections_offset = layout.sections.empty()
            ? 0u
            : header_size + layout.text_size + plain_data + chunk_bytes;
        const uint32_t fields[] = {
            MBIN_MAGIC,
            MBIN_VERSION_2,
            entry_point,
            header_size,                    // text_offset = 36
            layout.text_size,
            header_size + layout.text_size, // data_offset
            layout.data_size,
            0,                              // bss_size
            sections_offset,
        };
        static_assert(sizeof(fields) == sizeof(MbinHeader), "one word per MbinHeader field");
        uint8_t header[sizeof(MbinHeader)];
        for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
            write_be32(&header[4 * i], fields[i]);
        }
        outfile.write(reinterpret_cast<const char*>(header), sizeof(header));
    }

    // Write all Text sections
    for (const auto& obj : objects) {
        if (!obj.text_section.empty()) {
            outfile.write(reinterpret_cast<const char*>(obj.text_section.data()),
                          obj.text_section.size());
        }
    }

    // Write all Data sections
    for (const auto& obj : objects) {
        if (!obj.data_section.empty()) {
            outfile.write(reinterpret_cast<const char*>(obj.data_section.data()),
                          obj.data_section.size());
        }
    }

    // Then the collected sections: each name's chunks contiguously, in the
    // order layout_and_define_symbols assigned, then the directory.
    for (const auto& sec : layout.sections) {
        for (const auto& ref : sec.chunks) {
            const auto& obj = objects[ref.first];
            const auto& ce = obj.collects[ref.second];
            if (ce.size > 0) {
                outfile.write(reinterpret_cast<const char*>(&obj.collect_blob[ce.offset]), ce.size);
            }
        }
    }
    if (!layout.directory.empty()) {
        outfile.write(reinterpret_cast<const char*>(layout.directory.data()), layout.directory.size());
    }

    std::cout << "Successfully created " << output_path << std::endl;
    if (emit_header) {
        std::cout << "Header: MBIN (version 2, 36 bytes), Entry: 0x" << std::hex << entry_point << std::dec << std::endl;
    }
    std::cout << "Text Size: " << layout.text_size << " bytes" << std::endl;
    std::cout << "Data Size: " << layout.data_size << " bytes" << std::endl;

    return true;
}

// The symbols a map lists. The relocation table holds only what something
// referenced, but a reader needs a name for every function: one left out would
// be charged to whichever symbol comes before it. Block labels stay out unless
// they were referenced, because the assembler tags them `name-[module:n]` and
// they would split a function into pieces.
std::map<std::string, uint32_t> collect_map_symbols(
        const std::vector<LoadedObject>& objects,
        const std::map<std::string, uint32_t>& global_symbol_table) {
    std::map<std::string, uint32_t> symbols = global_symbol_table;
    for (const auto& obj : objects) {
        for (const auto& sym : obj.symbols) {
            if (sym.type != SYMBOL_DEFINED) continue;
            if (is_private_label(sym.name)) continue;
            if (sym.section == SECTION_TEXT || sym.section == SECTION_DATA) {
                symbols.emplace(sym.name, symbol_address(obj, sym));
            }
        }
    }
    return symbols;
}

// Emit a symbol map so downstream tools (e.g. the emulator profiler) can turn a
// program counter into a function name. One line per symbol, sorted by address:
//   0x00000040 kernel_main
// A leading comment records the text/data split so a reader knows where code
// ends. `symbols` comes from collect_map_symbols.
bool write_map(const std::string& map_path,
               const std::map<std::string, uint32_t>& symbols,
               const Layout& layout) {
    std::ofstream mapfile(map_path);
    if (!mapfile) {
        std::cerr << "Error: Could not open map file " << map_path << std::endl;
        return false;
    }

    mapfile << "# MyLinker symbol map\n";
    mapfile << "# text: 0x" << std::hex << layout.text_base << " .. 0x"
            << (layout.text_base + layout.text_size) << std::dec
            << " (" << layout.text_size << " bytes)\n";
    mapfile << "# data: 0x" << std::hex << layout.data_base << " .. 0x"
            << (layout.data_base + layout.data_size) << std::dec
            << " (" << layout.data_size << " bytes)\n";

    // Sort by address so the map reads like a memory layout and so the profiler
    // can binary-search / range-scan it to attribute a PC to the nearest symbol.
    std::vector<std::pair<uint32_t, std::string>> ordered;
    ordered.reserve(symbols.size());
    for (const auto& entry : symbols) {
        ordered.emplace_back(entry.second, entry.first);
    }
    std::sort(ordered.begin(), ordered.end());

    for (const auto& entry : ordered) {
        char addr_buf[16];
        std::snprintf(addr_buf, sizeof(addr_buf), "0x%08X", entry.first);
        mapfile << addr_buf << ' ' << entry.second << '\n';
    }

    std::cout << "Wrote symbol map " << map_path << " (" << ordered.size()
              << " symbols)" << std::endl;
    return true;
}

// Emit the line table: what the compiler said about each address, in address
// order. A `line` row covers the code up to the next row; a `-` in its last
// column means the code has no cost kind.
//   file 0 src/main.mln
//   func 0x00000040 0 main
//   line 0x00000040 0 12 -
bool write_lines(const std::string& lines_path, const std::vector<LoadedObject>& objects) {
    std::ofstream out(lines_path);
    if (!out) {
        std::cerr << "Error: Could not open line table " << lines_path << std::endl;
        return false;
    }

    std::map<std::string, uint32_t> files;
    std::vector<std::string> file_order;
    auto file_id = [&](const std::string& path) {
        auto [entry, added] = files.emplace(path, static_cast<uint32_t>(file_order.size()));
        if (added) file_order.push_back(path);
        return entry->second;
    };

    struct Func { uint32_t addr; uint32_t file; std::string name; };
    struct Row { uint32_t addr; uint32_t file; uint32_t line; std::string cost; };
    std::vector<Func> funcs;
    std::vector<Row> rows;
    for (const auto& obj : objects) {
        const char* strings = obj.debug_strings.c_str();
        for (const auto& func : obj.debug_funcs) {
            funcs.push_back({obj.text_base_addr + func.offset, file_id(strings + func.file),
                             strings + func.name});
        }
        for (const auto& row : obj.debug_rows) {
            rows.push_back({obj.text_base_addr + row.offset, file_id(strings + row.file),
                            row.line, strings + row.cost});
        }
    }
    std::stable_sort(funcs.begin(), funcs.end(),
                     [](const Func& a, const Func& b) { return a.addr < b.addr; });
    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row& a, const Row& b) { return a.addr < b.addr; });

    char addr[16];
    out << "# MyLinker line table\n";
    for (size_t i = 0; i < file_order.size(); ++i) {
        out << "file " << i << ' ' << file_order[i] << '\n';
    }
    for (const auto& func : funcs) {
        std::snprintf(addr, sizeof(addr), "0x%08X", func.addr);
        out << "func " << addr << ' ' << func.file << ' ' << func.name << '\n';
    }
    for (const auto& row : rows) {
        std::snprintf(addr, sizeof(addr), "0x%08X", row.addr);
        out << "line " << addr << ' ' << row.file << ' ' << row.line << ' '
            << (row.cost.empty() ? "-" : row.cost) << '\n';
    }
    return true;
}

}  // namespace

bool link_objects(const std::vector<std::string>& input_files,
                  const std::string& output_path,
                  const std::string& map_path,
                  const std::string& lines_path,
                  uint32_t base_addr,
                  bool emit_header,
                  const std::vector<LinkRedirect>& redirects) {
    std::vector<LoadedObject> objects;
    objects.reserve(input_files.size());

    // Pass 0: Load all files
    for (const auto& path : input_files) {
        LoadedObject obj;
        if (!load_object_file(path, obj)) {
            return false;
        }
        objects.push_back(std::move(obj));
    }

    // Pass 1: Layout & Symbol Definition
    std::map<std::string, uint32_t> global_symbol_table;
    Layout layout;
    if (!layout_and_define_symbols(objects, global_symbol_table, layout, base_addr, emit_header,
                                   redirects)) {
        return false;
    }

    for (const auto& redirect : redirects) {
        if (!global_symbol_table.count(redirect.from)) {
            std::cerr << "Error: redirect source symbol '" << redirect.from << "' is undefined" << std::endl;
            return false;
        }
        if (!global_symbol_table.count(redirect.to)) {
            std::cerr << "Error: redirect entry symbol '" << redirect.to << "' is undefined" << std::endl;
            return false;
        }
    }

    // Pass 2: Relocation & Patching
    if (!apply_relocations(objects, global_symbol_table, redirects)) {
        return false;
    }

    // Determine entry point
    uint32_t entry_point = base_addr;
    if (global_symbol_table.count("__START__")) {
        entry_point = global_symbol_table.at("__START__");
    }

    // Pass 3: Write Output
    if (!write_output(output_path, objects, layout, emit_header, entry_point)) {
        return false;
    }

    // Optional Pass 4: emit the symbol map for the profiler / debuggers.
    if (!map_path.empty()) {
        if (!write_map(map_path, collect_map_symbols(objects, global_symbol_table), layout)) {
            return false;
        }
    }

    if (!lines_path.empty() && !write_lines(lines_path, objects)) {
        return false;
    }

    return true;
}
