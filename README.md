# Vark - Simple LZAV Archive

Vark is a minimal LZAV archive library and tool, designed for speed and simplicity. It uses the LZAV compression algorithm, which is fast and efficient. For more information about LZAV, see [LZAV on GitHub](https://github.com/avaneev/lzav).

> NOTE: Vark is vide coded using Gemini AI, albeit closely instructed and architected by the author, who is a professional software engineer. Use at your own risk.

## Features

- Simple C++ interface
- Fast LZAV compression and decompression
- Minimal archive format

## Performance

* **CPU** AMD Ryzen Gen 3
* **SSD** Samsung 970 Evo Plus M2
* **Data Size:** 12.58 MB

| Mode | Task | Latency | Throughput |
| --- | --- | --- | --- |
| **Normal** (Single-threaded) | Compression | 62.05 ms | 0.213 GB/s |
|  | Decompression | 8.97 ms | 1.470 GB/s |
| **Persistent** (FP + TempBuffer) | Decompression | 55.98 ms | 2.356 GB/s |
| **Persistent** (MMAP) | **Decompression** | **21.51 ms** | **6.131 GB/s** |
| **LZAV In-Memory** (Raw Ref) | Compression | 3.21 ms | 4.105 GB/s |
|  | Decompression | 1.42 ms | 9.319 GB/s |


## Usage

```cpp
#include "vark.h"

int main() {
    Vark vark;
    ASSERT_TRUE(VarkCreateArchive(vark, "test.vark"));
    ASSERT_TRUE(VarkCompressAppendFile(vark, "test.txt"));
    return 0;
}
```

### CLI
```
Usage:
  vark -c <archive> <files...>  Create archive
  vark -a <archive> <files...>  Append to archive
  vark -x <archive>             Extract archive
  vark -l <archive>             List archive contents
  vark <archive>                Smart mode (Extract if .vark, Create/Append otherwise)
```

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
