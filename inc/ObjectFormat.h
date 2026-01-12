#ifndef OBJECT_FORMAT_H
#define OBJECT_FORMAT_H

#include <stdint.h>

// Magic number: "LNK1" -> 0x4C4E4B31
// However, checking endianness might be important. Let's assume Little Endian for now as is common.
const uint32_t LINKER_MAGIC = 0x4C4E4B31;

// Section Types
const uint32_t SECTION_TEXT = 0;
const uint32_t SECTION_DATA = 1;

// Symbol Types
const uint32_t SYMBOL_UNDEFINED = 0; // Import
const uint32_t SYMBOL_DEFINED = 1;   // Export

// Relocation Types
const uint32_t RELOC_ABSOLUTE = 0; // 32-bit absolute address
const uint32_t RELOC_RELATIVE = 1; // 26-bit relative jump (for CALL/B)

#pragma pack(push, 1)

struct FileHeader {
    uint32_t magic;
    uint32_t text_size;
    uint32_t data_size;
    uint32_t symtable_count;
    uint32_t reloc_count;
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
    uint32_t type;        // 0=ABSOLUTE, 1=RELATIVE
};

#pragma pack(pop)

#endif // OBJECT_FORMAT_H
