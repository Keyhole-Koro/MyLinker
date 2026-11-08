#ifndef OBJECT_FORMAT_H
#define OBJECT_FORMAT_H

#include <stdint.h>

// The constants are enumerators, not `const` variables, so that any C or C++
// file can include this header: in C a file-scope const is a definition with
// external linkage, and two C files defining it would not link together.
// tools/objformat.py repeats them for the Python tools.

// "LNK3" (LNK1 + collected sections, see CollectEntry). The header and the
// tables are written in the host's byte order, little-endian in practice; the
// bytes of the text, data and collected sections are big-endian, as the
// machine reads them.
enum { LINKER_MAGIC = 0x4C4E4B33 };

// SymbolEntry.section and RelocEntry.section
enum ObjSection {
    SECTION_TEXT = 0,
    SECTION_DATA = 1,
    SECTION_COLLECT = 2, // the object's collected-section blob (see CollectEntry)
};

// SymbolEntry.type
enum ObjSymbolType {
    SYMBOL_UNDEFINED = 0, // imported: another object defines it
    SYMBOL_DEFINED = 1,   // defined here, at `offset` in `section`
};

// RelocEntry.type
enum ObjRelocType {
    RELOC_ABSOLUTE = 0, // absolute address into a 21-bit immediate (MOVI rX, label)
    RELOC_RELATIVE = 1, // 26-bit pc-relative offset of a call or jump
    RELOC_WORD32 = 2,   // absolute address written in full into a data word (.word symbol)
};

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

// An undefined symbol has no section or offset of its own: both are 0.
struct SymbolEntry {
    char name[64];
    uint32_t type;    // ObjSymbolType: UNDEFINED or DEFINED
    uint32_t section; // ObjSection: TEXT, DATA or COLLECT (an offset into the blob)
    uint32_t offset;  // Offset relative to section start
};

struct RelocEntry {
    uint32_t offset;      // Offset to patch, within `section`
    char symbol_name[64]; // Name of the symbol to resolve
    uint32_t type;        // ObjRelocType: ABSOLUTE, RELATIVE or WORD32
    uint32_t section;     // ObjSection: TEXT, DATA or COLLECT; only a WORD32 patches DATA or COLLECT
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

// Optional debug trailer, after the collected-section chunks: source
// positions for tools that read addresses. It holds no bytes of the program,
// so an object without one links to the same image, and a reader that stops
// at the chunks never sees it. The strings come first; `name`, `file` and
// `cost` are offsets into them, and a row's `cost` is 0 outside a cost region
// (the pool starts with an empty string).
//   DebugHeader, char strings[string_size],
//   DebugFunc funcs[func_count], DebugRow rows[row_count]
enum { DEBUG_MAGIC = 0x4D444247 }; // 'MDBG'

struct DebugHeader {
    uint32_t magic;
    uint32_t string_size;
    uint32_t func_count;
    uint32_t row_count;
};

// A function starts at `offset` in the object's text. `name` is the name
// written in the source; the symbol may be mangled or private.
struct DebugFunc {
    uint32_t offset;
    uint32_t name;
    uint32_t file;
};

// The code from `offset` up to the next row came from `file`:`line`.
struct DebugRow {
    uint32_t offset;
    uint32_t file;
    uint32_t line;
    uint32_t cost;
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
enum {
    MBIN_MAGIC = 0x4D42494E, // 'MBIN'
    MBIN_VERSION_1 = 1,
    MBIN_VERSION_2 = 2,
};

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
