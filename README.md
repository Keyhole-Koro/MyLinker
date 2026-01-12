# Minimal MyCCLinker

This is a minimal implementation of the `MyCCLinker` consistent with the design document.

## Structure
*   `inc/ObjectFormat.h`: Defines the `.obj` file format (Header, Sections, Symbols, Relocs).
*   `src/main.cpp`: The linker implementation (C++).
*   `tools/obj_gen.py`: A helper script to generate `.obj` files from JSON (since Assembler support is pending).
*   `test/`: Sample JSON inputs for testing.

## How to Build
```bash
make
# or
g++ -o mycclinker src/main.cpp -Iinc
```

## How to Test
1.  **Generate Object Files:**
    Use the python script to create binary object files from the JSON descriptions.
    ```bash
    python3 tools/obj_gen.py test/test_A.json test/A.obj
    python3 tools/obj_gen.py test/test_B.json test/B.obj
    ```

2.  **Run Linker:**
    Link the object files into a single executable.
    ```bash
    ./mycclinker program.bin test/A.obj test/B.obj
    ```

3.  **Verify Output:**
    The linker should report the text and data sizes.
    `A.obj` has 8 bytes text. `B.obj` has 4 bytes text. Total Text = 12 bytes.
    `funcA` is at 0x00. `funcB` is at 0x08.
    The relocation in `A.obj` (at offset 4) should be patched to point to `funcB`.
    ```bash
    hexdump -C program.bin
    ```
