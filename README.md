# Vark - Simple LZAV Archive

[![build](https://github.com/hypernewbie/vark/actions/workflows/build.yml/badge.svg)](https://github.com/hypernewbie/vark/actions/workflows/build.yml)
[![Windows](https://img.shields.io/badge/Windows-0078D6?style=for-the-badge&logo=windows&logoColor=white)](https://github.com/hypernewbie/vark/releases/download/v1.03/vark-windows-v1.03.zip) [![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://github.com/hypernewbie/vark/releases/download/v1.03/vark-linux-v1.03.zip) [![macOS](https://img.shields.io/badge/macOS-000000?style=for-the-badge&logo=apple&logoColor=white)](https://github.com/hypernewbie/vark/releases/download/v1.03/vark-macos-v1.03.zip)

### Load stuff at DDR2 RAM speed!

Vark is a minimal LZAV archive library and tool, designed for speed and simplicity. It uses the LZAV compression algorithm, which is fast and efficient.
For more information about LZAV, see [LZAV on GitHub](https://github.com/avaneev/lzav).

> NOTE: Vark is vibe coded using Gemini AI, albeit closely instructed and architected by the author, who is a professional software engineer. Use at your own risk.

## Features

- Simple C-style C++ interface
- Fast LZAV compression and decompression
- Minimal archive format
- Sharded compression support for fast partial reads
- Memory-Mapped I/O support (MMAP)

## Performance

* **CPU** AMD Ryzen Gen 3
* **SSD** Samsung 970 Evo Plus M2
* **Test Data:** 12.58 MB (Mixed File Types)
* **Sharded Test Data:** 9.02 MB (PNG File)

| Mode | Task | Throughput |
| --- | --- | --- |
| **Normal** (Single-threaded) | Compression | 0.486 GB/s |
|  | Decompression | 1.689 GB/s |
| **Persistent** (FP + TempBuffer) | Decompression | 2.549 GB/s |
| **Persistent** (MMAP) | **Decompression** | **6.486 GB/s** |
| **LZAV In-Memory** (Raw Ref) | Compression | 4.721 GB/s |
|  | Decompression | 10.013 GB/s |
| **Sharded** (MMAP) | Random 4KB Read | 1.037 GB/s |
|  | **Sequential 256KB Read** | **16.215 GB/s** |


## Usage

### C++ API

```cpp
#include "vark.h"

// Define implementation in one C++ file
#define VARK_IMPLEMENTATION
#include "vark.h"

Vark vark;

// Create a new archive
VarkCreateArchive( vark, "data.vark", VARK_WRITE );

// Append a file
VarkCompressAppendFile( vark, "file.txt" );

// Append a file with sharded compression (optimized for random access)
VarkCompressAppendFile( vark, "large_asset.dat", VARK_COMPRESS_SHARDED );

// Load an existing archive for reading
VarkLoadArchive( vark, "data.vark", VARK_MMAP );

// Decompress a full file
std::vector<uint8_t> data;
VarkDecompressFile( vark, "file.txt", data );

// Partially decompress a sharded file (e.g., read 1KB at offset 512)
// This is significantly faster for large files as it only decodes necessary shards
VarkDecompressFileSharded( vark, "large_asset.dat", 512, 1024, data );

// Always close when done
VarkCloseArchive( vark );
```

#### Flags

| Flag | Description |
| --- | --- |
| `VARK_PERSISTENT_FP` | Keeps the file handle open between operations. Recommended for batch tasks. |
| `VARK_MMAP` | Uses Memory-Mapped I/O for zero-copy reads. Incompatible with `VARK_WRITE`. |
| `VARK_WRITE` | Enables write/append operations. Disables read operations. |

### CLI
```
Usage:
  vark -c <archive> <files...>  Create archive
  vark -a <archive> <files...>  Append to archive
  vark -x <archive>             Extract archive
  vark -l <archive>             List archive contents
  vark <archive>                Smart mode (Extract if .vark, Create/Append otherwise)
```
## Build

Vark uses CMake for building. 

```bash
# Configure the project
cmake -S . -B build

# Build the tool and tests
cmake --build build --config Release

# Run tests
ctest --test-dir build -C Release --output-on-failure
```

The build will produce `vark` (the CLI tool) and `vark_tests` (the test suite) in the build directory.

## License

Vark is released under the MIT License:

```
	Copyright 2026 UAA Software

	Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
	associated documentation files (the "Software"), to deal in the Software without restriction,
	including without limitation the rights to use, copy, modify, merge, publish, distribute,
	sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all copies or substantial
	portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
	NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
	NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
	OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
	CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
