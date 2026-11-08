import json
import sys

from objformat import COLLECT_ENTRY, FILE_HEADER, LINKER_MAGIC, RELOC_ENTRY, SYMBOL_ENTRY


def create_object_file(json_path, output_path):
    with open(json_path, 'r') as f:
        data = json.load(f)

    text_bytes = bytes(data.get('text', []))
    data_bytes = bytes(data.get('data', []))
    blob_bytes = bytes(data.get('blob', []))
    symbols = data.get('symbols', [])
    relocs = data.get('relocs', [])
    collects = data.get('collects', [])

    with open(output_path, 'wb') as out:
        out.write(FILE_HEADER.pack(LINKER_MAGIC,
                                   len(text_bytes),
                                   len(data_bytes),
                                   len(symbols),
                                   len(relocs),
                                   len(collects),
                                   len(blob_bytes)))
        out.write(text_bytes)
        out.write(data_bytes)
        out.write(blob_bytes)

        # Names are NUL-padded to their 64-byte fields by the struct format.
        for sym in symbols:
            out.write(SYMBOL_ENTRY.pack(sym['name'].encode('utf-8'), sym['type'],
                                        sym['section'], sym['offset']))

        for reloc in relocs:
            out.write(RELOC_ENTRY.pack(reloc['offset'], reloc['symbol_name'].encode('utf-8'),
                                       reloc['type'], reloc.get('section', 0)))

        for ce in collects:
            out.write(COLLECT_ENTRY.pack(ce['name'].encode('utf-8'), ce['offset'], ce['size']))

    print(f"Created {output_path}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python obj_gen.py <input.json> <output.obj>")
        sys.exit(1)
        
    create_object_file(sys.argv[1], sys.argv[2])
