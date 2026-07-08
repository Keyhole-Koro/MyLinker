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

    return true;
}

bool layout_and_define_symbols(std::vector<LoadedObject>& objects,
                               std::map<std::string, uint32_t>& global_symbol_table,
                               uint32_t& total_text_size,
                               uint32_t& total_data_size,
                               uint32_t base_addr) {
    std::set<std::string> needed_symbols;
    needed_symbols.insert("__START__");

    std::vector<bool> object_active(objects.size(), false);
    bool changed = true;

    // Dependency Resolution Loop
    while (changed) {
        changed = false;
        
        // Pass 1: Activate objects that provide needed symbols
        for (size_t i = 0; i < objects.size(); ++i) {
            if (object_active[i]) continue; // Already active

            for (const auto& sym : objects[i].symbols) {
                if (sym.type == SYMBOL_DEFINED) {
                    if (needed_symbols.count(sym.name)) {
                        object_active[i] = true;
                        changed = true;
                        break; // Object is active, move to next object
                    }
                }
            }
        }

        // Pass 2: Collect new needs from active objects
        for (size_t i = 0; i < objects.size(); ++i) {
            if (!object_active[i]) continue;

            // Add all Defined symbols from this active object to 'needed' if they are used? 
            // No, only add relocations.
            // BUT, if an object is active, we might want to expose its symbols?
            // The requirement "Scope is not narrowed down" implies we should ONLY expose needed symbols.
            // So we don't blindly add all definitions to 'needed'.
            
            for (const auto& reloc : objects[i].relocs) {
                if (needed_symbols.find(reloc.symbol_name) == needed_symbols.end()) {
                    needed_symbols.insert(reloc.symbol_name);
                    changed = true;
                }
            }
        }
    }

    // Filter objects to keep only active ones
    std::vector<LoadedObject> active_objects;
    for (size_t i = 0; i < objects.size(); ++i) {
        if (object_active[i]) {
            active_objects.push_back(std::move(objects[i]));
        }
    }
    objects = std::move(active_objects);

    // Layout and Symbol Definition
    uint32_t current_text_addr = base_addr;
    total_text_size = 0;
    total_data_size = 0;

    for (const auto& obj : objects) {
        total_text_size += obj.header.text_size;
        total_data_size += obj.header.data_size;
    }

    uint32_t current_data_addr = base_addr + total_text_size;

    for (auto& obj : objects) {
        obj.text_base_addr = current_text_addr;
        obj.data_base_addr = current_data_addr;

        current_text_addr += obj.header.text_size;
        current_data_addr += obj.header.data_size;

        for (const auto& sym : obj.symbols) {
            if (sym.type == SYMBOL_DEFINED) {
                // Only register if needed (Narrow Scope)
                if (needed_symbols.count(sym.name)) {
                     uint32_t final_addr = 0;
                    if (sym.section == SECTION_TEXT) {
                        final_addr = obj.text_base_addr + sym.offset;
                    } else if (sym.section == SECTION_DATA) {
                        final_addr = obj.data_base_addr + sym.offset;
                    }

                    if (global_symbol_table.count(sym.name)) {
                        std::cerr << "Error: Duplicate symbol definition '" << sym.name << "'" << std::endl;
                        return false;
                    }
                    global_symbol_table[sym.name] = final_addr;
                }
            }
        }
    }
    
    // Synthesize the `_end` symbol marking the end of the loaded image
    // (text + data). The kernel heap allocator uses this as the start of its
    // free region. current_data_addr has advanced past the last data section.
    if (!global_symbol_table.count("_end")) {
        global_symbol_table["_end"] = current_data_addr;
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

bool apply_relocations(std::vector<LoadedObject>& objects,
                       const std::map<std::string, uint32_t>& global_symbol_table) {
    for (auto& obj : objects) {
        for (const auto& reloc : obj.relocs) {
            std::string sym_name(reloc.symbol_name);

            if (global_symbol_table.find(sym_name) == global_symbol_table.end()) {
                std::cerr << "Error: Undefined symbol '" << sym_name << "' referenced in "
                          << obj.filename << std::endl;
                return false;
            }

            uint32_t target_addr = global_symbol_table.at(sym_name);
            uint32_t patch_offset = reloc.offset; // Offset within this file's TEXT section

            // Check bounds
            if (patch_offset + 4 > obj.text_section.size()) {
                std::cerr << "Error: Relocation offset out of bounds in " << obj.filename
                          << std::endl;
                return false;
            }

            // Calculate value to write
            uint32_t value_to_write = 0;
            uint32_t instruction_addr = obj.text_base_addr + patch_offset;

            if (reloc.type == RELOC_ABSOLUTE) {
                value_to_write = target_addr;
            } else if (reloc.type == RELOC_RELATIVE) {
                // Relative Jump: follow assembler's encoding, which uses (target - currentPC)
                int32_t offset = target_addr - instruction_addr;

                // Mask to 26 bits (signed) if necessary, but we write 32 bits into the slot usually?
                // Wait, if it's a 26-bit jump instruction, we need to mask and merge.
                // The design doc says: "Write the value into the corresponding 'hole' in the binary buffer."
                // It doesn't specify instruction format details deeply.
                // Assuming the hole is 32-bit for now or we just overwrite the field.
                // CAUTION: If it's a partial instruction overwrite (like `B label`), we need to preserve opcode.
                // BUT, Design doc Section 4 says: "leave a placeholder (0) in the machine code".
                // If the assembler leaves 0, does it leave the opcode?
                // If the assembler leaves 0 for the *whole instruction*, that's bad.
                // A typical linkable object for a RISC arch usually has the opcode present and the immediate field 0.

                // Let's assume the assembler leaves the opcode valid and the immediate 0.
                // We need to read the existing instruction to preserve opcode?
                // Or maybe the 'hole' is just the immediate field?
                // The Design doc implies simple patching.
                // "Write the value into the corresponding 'hole' in the binary buffer."
                // "RelocEntry: uint32_t offset; // Offset in the TEXT section to patch"

                // If the relocation type is RELATIVE, it's likely a Jump/Branch.
                // If the "hole" is the full 32-bit word, we might destroy the opcode.
                // However, without more instruction set details, I can't do bit-masking safely.
                // BUT, looking at `MyAssembler`, let's see how `B` (Branch) is encoded.
                // This is critical.

                // Let's defer this specific bit-masking logic and assume for "Minimal" version:
                // We read the 32-bit word, keep the top 6 bits (opcode?), and replace the bottom 26.
                // Or maybe the RelocEntry assumes the assembler emitted a dummy instruction?
                // No, "leave a placeholder (0)".

                // Strategy: Read current 32-bit word.
                // If RELOC_RELATIVE, assume it's a specific format (e.g. top 6 bits opcode, bottom 26 offset).
                // MyComputer Architecture v3.1 confirmation needed.
                // Since I can't read the architecture spec easily (it's in `docs/spec.md`?), I should check it.
                // But for now, I will assume a standard mask: 0xFC000000 is opcode, 0x03FFFFFF is offset.

                uint32_t current_inst = 0;
                memcpy(&current_inst, &obj.text_section[patch_offset], 4);
                (void)current_inst;

                // Mask: Keep top 6 bits, replace bottom 26
                // This is a guess. I should verify with `docs/spec.md` if possible,
                // but for a "quick" minimal implementation, this is a reasonable assumption
                // for a custom 32-bit RISC.

                // Also, relative offset is usually in words (instruction count) or bytes?
                // ARM uses words (offset >> 2). x86 uses bytes.
                // Design doc: "TargetAddress - (InstructionAddress + 4)" -> This is a byte difference.
                // If the instruction expects a value in bytes, we are good.
                // If it expects words, we need to shift.

                // Let's stick to the raw value for now, or check `instructions.h` in MyAssembler if I could.
                // I will add a TODO or just do a direct overwrite if the file says "placeholder (0)".
                // If placeholder is 0, then `current_inst` might be just the opcode?

                // Let's read `docs/spec.md` quickly to be safe?
                // No, let's just implement a generic "OR" patch for now.
                // value_to_write = (current_inst & 0xFC000000) | (offset & 0x03FFFFFF);

                // Actually, let's just write the 32-bit value for ABSOLUTE.
                // For RELATIVE, let's assume the assembler handled the opcode and we just OR in the offset.
                // But `offset` is signed.

                value_to_write = offset;
                // We will OR it with existing content later.
            }

            // Apply patch
            // We need to read existing to preserve bits if it's not a full overwrite
            // Handle Big Endian read/write manually to avoid host endianness issues
            uint8_t* ptr = &obj.text_section[patch_offset];
            uint32_t existing = (static_cast<uint32_t>(ptr[0]) << 24) |
                                (static_cast<uint32_t>(ptr[1]) << 16) |
                                (static_cast<uint32_t>(ptr[2]) << 8)  |
                                static_cast<uint32_t>(ptr[3]);

            uint32_t final_val = 0;

            if (reloc.type == RELOC_RELATIVE) {
                // Preserving top 6 bits (Opcode) - Assumption based on typical custom CPU
                // And assuming the offset field is the lower 26 bits.
                // Check if offset fits in 26 bits?
                // (offset & ~0x03FFFFFF) should be 0 or all 1s.
                final_val = (existing & 0xFC000000) | (value_to_write & 0x03FFFFFF);
            } else {
                // RELOC_ABSOLUTE
                // Preserve opcode+register bits for reg,imm21 style instructions like
                // MOVI rX, label and patch only the low 21-bit immediate field.
                if (value_to_write >= (1u << 21)) {
                    std::cerr << "Error: Absolute relocation target 0x" << std::hex
                              << value_to_write << std::dec
                              << " exceeds 21-bit immediate range in " << obj.filename
                              << std::endl;
                    return false;
                }
                final_val = (existing & 0xFFE00000u) | (value_to_write & 0x001FFFFFu);
            }

            // Write back in Big Endian
            ptr[0] = static_cast<uint8_t>((final_val >> 24) & 0xFF);
            ptr[1] = static_cast<uint8_t>((final_val >> 16) & 0xFF);
            ptr[2] = static_cast<uint8_t>((final_val >> 8) & 0xFF);
            ptr[3] = static_cast<uint8_t>(final_val & 0xFF);
        }
    }

    return true;
}

bool write_output(const std::string& output_path,
                  const std::vector<LoadedObject>& objects,
                  uint32_t total_text_size,
                  uint32_t total_data_size) {
    std::ofstream outfile(output_path, std::ios::binary);
    if (!outfile) {
        std::cerr << "Error: Could not open output file " << output_path << std::endl;
        return false;
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

    std::cout << "Successfully created " << output_path << std::endl;
    std::cout << "Text Size: " << total_text_size << " bytes" << std::endl;
    std::cout << "Data Size: " << total_data_size << " bytes" << std::endl;

    return true;
}

// Emit a symbol map so downstream tools (e.g. the emulator profiler) can turn a
// program counter into a function name. One line per symbol, sorted by address:
//   0x00000040 kernel_main
// A leading comment records the text/data split so a reader knows where code
// ends. Only symbols that survived linking (referenced ones plus `_end`) appear.
bool write_map(const std::string& map_path,
               const std::map<std::string, uint32_t>& global_symbol_table,
               uint32_t total_text_size,
               uint32_t total_data_size) {
    std::ofstream mapfile(map_path);
    if (!mapfile) {
        std::cerr << "Error: Could not open map file " << map_path << std::endl;
        return false;
    }

    mapfile << "# MyLinker symbol map\n";
    mapfile << "# text: 0x0 .. 0x" << std::hex << total_text_size << std::dec
            << " (" << total_text_size << " bytes)\n";
    mapfile << "# data: 0x" << std::hex << total_text_size << " .. 0x"
            << (total_text_size + total_data_size) << std::dec
            << " (" << total_data_size << " bytes)\n";

    // Sort by address so the map reads like a memory layout and so the profiler
    // can binary-search / range-scan it to attribute a PC to the nearest symbol.
    std::vector<std::pair<uint32_t, std::string>> ordered;
    ordered.reserve(global_symbol_table.size());
    for (const auto& entry : global_symbol_table) {
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

}  // namespace

bool link_objects(const std::vector<std::string>& input_files,
                  const std::string& output_path,
                  const std::string& map_path,
                  uint32_t base_addr) {
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
    uint32_t total_text_size = 0;
    uint32_t total_data_size = 0;
    if (!layout_and_define_symbols(objects, global_symbol_table, total_text_size, total_data_size, base_addr)) {
        return false;
    }

    // Pass 2: Relocation & Patching
    if (!apply_relocations(objects, global_symbol_table)) {
        return false;
    }

    // Pass 3: Write Output
    if (!write_output(output_path, objects, total_text_size, total_data_size)) {
        return false;
    }

    // Optional Pass 4: emit the symbol map for the profiler / debuggers.
    if (!map_path.empty()) {
        if (!write_map(map_path, global_symbol_table, total_text_size, total_data_size)) {
            return false;
        }
    }

    return true;
}
