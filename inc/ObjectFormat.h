#ifndef OBJECT_FORMAT_H
#define OBJECT_FORMAT_H

#include <stdint.h>

// Magic number: "LNK3" -> 0x4C4E4B33 (LNK1 + collected sections, see CollectEntry).
// However, checking endianness might be important. Let's assume Little Endian for now as is common.
const uint32_t LINKER_MAGIC = 0x4C4E4B33;

// Section Types
const uint32_t SECTION_TEXT = 0;
const uint32_t SECTION_DATA = 1;
const uint32_t SECTION_COLLECT = 2; // the object's collected-section blob (see CollectEntry)

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
    uint32_t collect_size;    // bytes of the collected-section blob (after the data section)
};

struct SymbolEntry {
    char name[64];
    uint32_t type;    // 0=UNDEFINED, 1=DEFINED
    uint32_t section; // 0=TEXT, 1=DATA
    uint32_t offset;  // Offset relative to section start
};

struct RelocEntry {
    uint32_t offset;      // Offset to patch, within `section`
    char symbol_name[64]; // Name of the symbol to resolve
    uint32_t type;        // 0=ABSOLUTE, 1=RELATIVE, 2=WORD32
    uint32_t section;     // 0=TEXT, 2=COLLECT (the blob)
};

// A chunk of a named collected section (`.section name` in the assembly).
// The chunk's bytes live in the object's collected-section blob, which
// follows the data section; `offset` locates the chunk there. The linker
// lays every object's chunks of one name out contiguously, in link order,
// after the data sections, so the section reads as one array, and defines
// `__section_<name>` (its first byte) and `__section_<name>_size` (a word
// holding its byte count). After the last section comes the directory,
// `__sections`: one [name (char*), start, size] row per section and a zero
// row, then the name strings, so a section can be found by string at
// runtime too (MyStdLib memory/section.mln). `__section_<name>_size` is
// the address of that section's row's size word. Symbols and relocations
// inside a chunk use SECTION_COLLECT with blob offsets. An object that contributes a chunk is
// kept live: a table entry is a registration, whether or not anything else
// refers to the object. This is how a declaration in one object lands in a
// table nobody wrote by hand -- the compiler's annotation rows, for one.
struct CollectEntry {
    char name[64];
    uint32_t offset;      // Offset of the chunk in the collected-section blob
    uint32_t size;        // Chunk size in bytes
};

// MBIN executable header (docs/design/mbin-executable-header.md).
// Header version 2 adds `sections_offset`: where in the file the section
// directory lies -- the same `[name, start, size]` rows and name strings
// the linker places after the data as `__sections` -- so a reader can find
// a collected section (the compiler's annotation table, say) in an image
// on disk without loading it. Addresses inside the directory are virtual;
// text maps at entry_point & ~0xFFF, data at the next page after text,
// which is how a reader turns them into file offsets (MyStdLib
// format/mbin.mln). 0 when the image has no collected sections.
const uint32_t MBIN_MAGIC = 0x4D42494E; // 'MBIN'
const uint32_t MBIN_VERSION_1 = 1;
const uint32_t MBIN_VERSION_2 = 2;

struct MbinHeader {
    uint32_t magic;           // 0x4D42494E ("MBIN")
    uint32_t version;         // 2
    uint32_t entry_point;     // Initial PC (virtual address)
    uint32_t text_offset;     // File offset to .text (sizeof(MbinHeader) = 36)
    uint32_t text_size;       // Size of .text in bytes
    uint32_t data_offset;     // File offset to .data (text_offset + text_size)
    uint32_t data_size;       // Size of .data in bytes, collected sections and directory included
    uint32_t bss_size;        // Size of .bss in bytes (0 for now)
    uint32_t sections_offset; // File offset of the section directory (__sections), 0 if none
};

#pragma pack(pop)

#endif // OBJECT_FORMAT_H
