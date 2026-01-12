import struct
import json
import sys

# Constants
MAGIC = 0x4C4E4B31
SECTION_TEXT = 0
SECTION_DATA = 1
SYMBOL_UNDEFINED = 0
SYMBOL_DEFINED = 1
RELOC_ABSOLUTE = 0
RELOC_RELATIVE = 1

def create_object_file(json_path, output_path):
    with open(json_path, 'r') as f:
        data = json.load(f)

    # Prepare Data
    text_bytes = bytes(data.get('text', []))
    data_bytes = bytes(data.get('data', []))
    
    symbols = data.get('symbols', [])
    relocs = data.get('relocs', [])

    # Header
    # uint32_t magic;
    # uint32_t text_size;
    # uint32_t data_size;
    # uint32_t symtable_count;
    # uint32_t reloc_count;
    
    header = struct.pack('<IIIII', 
                         MAGIC, 
                         len(text_bytes), 
                         len(data_bytes), 
                         len(symbols), 
                         len(relocs))

    with open(output_path, 'wb') as out:
        out.write(header)
        out.write(text_bytes)
        out.write(data_bytes)
        
        # Write Symbols
        # char name[64];
        # uint32_t type;
        # uint32_t section;
        # uint32_t offset;
        for sym in symbols:
            name = sym['name'].encode('utf-8')
            # Pad name to 64 bytes
            name = name + b'\0' * (64 - len(name))
            entry = struct.pack('<64sIII', name, sym['type'], sym['section'], sym['offset'])
            out.write(entry)
            
        # Write Relocs
        # uint32_t offset;
        # char symbol_name[64];
        # uint32_t type;
        for reloc in relocs:
            sym_name = reloc['symbol_name'].encode('utf-8')
            sym_name = sym_name + b'\0' * (64 - len(sym_name))
            entry = struct.pack('<I64sI', reloc['offset'], sym_name, reloc['type'])
            out.write(entry)
            
    print(f"Created {output_path}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python obj_gen.py <input.json> <output.obj>")
        sys.exit(1)
        
    create_object_file(sys.argv[1], sys.argv[2])
