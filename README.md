# Zippo
A file compressor and decompressor based on the Huffman algorithm,
written in pure C. Designed with a strong focus on I/O optimization and POSIX
system programming.

## Architectural Choices and Strengths

* **Memory Mapping (`mmap`):** Input file reading is directly delegated to the
  Kernel's Page Cache via `mmap`. This eliminates the need for intermediate
  user-space buffers, guaranteeing peak performance and a fixed, trivial memory
  usage regardless of file size (up to the gigabyte range).
* **Custom Bitwise Engine:** Symmetrical `BitWriter` and `BitReader`
  implementation featuring a 64-bit accumulator. It allows reading and writing
  variable-length bit sequences while minimizing system calls (utilizing
  buffering techniques).
* **Fast Priority Queue:** Implemented a custom Min-Heap from scratch to handle
  symbol frequencies. It guarantees $\mathcal{O}(\log n)$ complexity for
  individual `HeapPush` and `HeapPop` operations.
* **No External Dependencies:** Built from scratch entirely using pure C99 and
  standard POSIX libraries.
* **POSIX Interface:** Robust and standardized CLI built thanks to `getopt.h`,
  featuring order-independent arguments and state validation.


## Usage
```bash
# Compress a file
./zippo -c -i input -o compressed

# Decompress a file
./zippo -d -i compressed -o output

# Show help message
./zippo -h
```
