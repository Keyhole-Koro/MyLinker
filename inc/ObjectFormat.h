#ifndef OBJECT_FORMAT_H
#define OBJECT_FORMAT_H

#include <stdint.h>

// Magic number: "LNK2" -> 0x4C4E4B32 (LNK1 + collected sections, see CollectEntry).
// However, checking endianness might be important. Let's assume Little Endian for now as is common.
const uint32_t LINKER_MAGIC = 0x4C4E4B32;

// Section Types
const uint32_t SECTION_TEXT = 0;
const uint32_t SECTION_DATA = 1;

// Symbol Types
const uint32_t SYMBOL_UNDEFINED = 0; // Import
const uint32_t SYMBOL_DEFINED = 1;   // Export

// Relocation Types
const uint32_t RELOC_ABSOLUTE = 0; // absolute address into a 21-bit immediate (MOVI rX, label)
const uint32_t RELOC_RELATIVE = 1; // 26-bit relative jump (for CALL/B)
const uint32_t RELOC_WORD32 = 2;   // absolute address written in full into a data word (.word symbol)

#pragma pack(push, 1)

struct FileHeader {
    uint32_t magic;
    uint32_t text_size;
    uint32_t data_size;
    uint32_t symtable_count;
    uint32_t reloc_count;
    uint32_t collect_count;   // CollectEntry records after the relocations
};

struct SymbolEntry {
    char name[64];
    uint32_t type;    // 0=UNDEFINED, 1=DEFINED
    uint32_t section; // 0=TEXT, 1=DATA
    uint32_t offset;  // Offset relative to section start
};

struct RelocEntry {
    uint32_t offset;      // Offset in the TEXT section to patch
    char symbol_name[64]; // Name of the symbol to resolve
    uint32_t type;        // 0=ABSOLUTE, 1=RELATIVE, 2=WORD32
};

// A chunk of this object's TEXT that belongs to a named collected section
// (`.section name` in the assembly). The linker gathers every object's
// chunks of the same name, in link order, into an index it places after the
// data: `__<name>_start` points at (address, size) pairs, one per chunk, and
// `__<name>_end` at the word after the last. Readers walk the pairs; the
// chunk bytes themselves stay where the assembler put them. This is how a
// declaration in one object lands in a table nobody wrote by hand -- the
// compiler's annotation rows, for one.
struct CollectEntry {
    char name[64];
    uint32_t offset;      // Offset of the chunk in the TEXT section
    uint32_t size;        // Chunk size in bytes
};

// MBIN v2 Executable Header
const uint32_t MBIN_MAGIC = 0x4D42494E; // 'MBIN'
const uint32_t MBIN_VERSION_1 = 1;

struct MbinHeader {
    uint32_t magic;         // 0x4D42494E ("MBIN")
    uint32_t version;       // 1
    uint32_t entry_point;   // Initial PC (virtual address)
    uint32_t text_offset;   // File offset to .text (32 bytes)
    uint32_t text_size;     // Size of .text in bytes
    uint32_t data_offset;   // File offset to .data (32 + text_size)
    uint32_t data_size;     // Size of .data in bytes
    uint32_t bss_size;      // Size of .bss in bytes (0 for now)
};

#pragma pack(pop)

#endif // OBJECT_FORMAT_H
