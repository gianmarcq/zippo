# Zippo

A lossless file compressor and decompressor based on Huffman coding, written from scratch in C99 for POSIX systems, with no external dependencies.

Zippo was built to explore the full pipeline of an entropy coder (frequency analysis, tree construction, bit-level encoding) while paying close attention to systems-level concerns: how files are read from disk, how bits are packed into memory, and how the tool behaves on edge cases. As the pipeline grew, so did the question of how to make it fast, and that's how Zippo became a natural playground for exploring concurrency.

## Features

- Lossless compression and decompression of arbitrary binary files
- Memory-mapped input (`mmap`), so the file is never copied into a user-space buffer
- Optional multithreading (`-j N`) with POSIX threads: frequency counting, chunk encoding and chunk decoding are all parallelized
- Custom bit-level I/O with a 64-bit accumulator
- Min-heap implemented from scratch, no standard-library containers
- POSIX command-line interface with order-independent options (`getopt`)
- Pure C99, no third-party libraries

## Build and usage

```bash
make                # default build
make debug          # -O0 -g with -fsanitize=address

./zippo -c input.txt compressed.zp     # compress
./zippo -d compressed.zp output.txt    # decompress
./zippo -cj4 big.bin out.zp            # compress with 4 threads
```

## How it works

**Compression**

1. **Input mapping.** The source file is mapped into memory with `mmap` and never copied into a user-space buffer; the histogram and the encoding pass both read from the mapping.
2. **Frequency analysis.** The input is scanned once to build a 256-entry byte histogram. With multiple threads the file is split into contiguous ranges by `chunkRange`, each worker counts into its own private `freq` array, and the partial histograms are summed only after every worker has been joined.
3. **Tree construction.** Symbols with non-zero frequency are inserted into a min-heap; the two lowest-frequency nodes are repeatedly merged until a single Huffman tree remains. A lone symbol is a special case and gets a dummy sibling so that the tree still has a node to branch on.
4. **Code table.** The tree is traversed to assign a variable-length prefix code to each byte value: left edges are `0`, right edges are `1`, and the bit sequence collected at each leaf becomes its encoding.
5. **Header.** The output starts with the magic number, the original file size and the serialized tree (pre-order topology) so the decoder can rebuild the exact same tree. The tree is byte-aligned with `BitWriterFlush`, then a single byte records the chunk count (`0` for a single-threaded run, otherwise the number of chunks).
6. **Encoding and parallel execution.** The input is scanned a second time and each byte is replaced by its code through the `BitWriter`. In the single-threaded case the whole file is emitted as one continuous bitstream. With multiple threads each worker encodes its own range into a private in-memory `BitWriter`; after the join the main thread writes every buffer to the file in order and appends each chunk's `total_bits` as the tail table the decoder will read.

In the single-threaded case the per-chunk splitting of steps 2 and 6 is skipped and the payload is written as one bitstream, with no tail table.

**Decompression**

1. **Header parsing.** The magic number and the original file size are read through the `BitReader`, then the serialized Huffman tree is rebuilt node by node.
2. **Chunk count.** A single byte after the tree tells whether the file was produced by a single-threaded run (`0`) or by a multi-threaded one (the number of chunks).
3. **Chunk metadata.** In the multi-threaded case the per-chunk bit lengths are stored in a table at the tail of the file. The decoder reads it backwards and derives each chunk's bit offset in the compressed payload and its byte offset in the decompressed output (`chunkRange`).
4. **Output mapping.** The destination file is created with the original size via `ftruncate` and mapped with `mmap` (`MAP_SHARED`), so workers can write into disjoint regions without copying.
5. **Decoding.** Each chunk walks the tree bit by bit, relying on the prefix-free property of Huffman codes: every time a leaf is reached, the corresponding byte is emitted and the traversal restarts from the root. A chunk stops once it has produced its expected number of bytes.
6. **Parallel execution.** A pool of worker threads pulls chunk indices from an atomic counter (`atomic_fetch_add` on `next_chunk`); each worker owns a private `BitReader` positioned at its chunk's bit offset and writes only to its own output range. No locks are needed because the input is read-only and the output ranges never overlap.

In the single-threaded case steps 3 and 6 are skipped: the payload is read as one continuous bitstream straight from the current `BitReader` position.

## Binary file format

All multi-byte integers are stored little-endian, matching the way
`BitWriterWrite64` emits the low 32 bits before the high 32 bits.

| Field                  | Size                     | Description                                                                                   |
|------------------------|--------------------------|-----------------------------------------------------------------------------------------------|
| Magic number           | 4 bytes                  | Constant `0x87654321`, used to detect binary format                                           |
| Original file size     | 8 bytes                  | Uncompressed size in bytes                                                                     |
| Serialized Huffman tree| variable (bit-packed)    | Pre-order traversal: `0` for an internal node, `1` followed by the 8-bit symbol for a leaf    |
| Chunk count            | 1 byte                   | `0` for a single-threaded file, otherwise the number of chunks (= thread count)                |
| Encoded payload        | variable (bit-packed)    | Concatenation of the per-chunk bitstreams; with multiple chunks each one is byte-aligned       |
| Chunk bit lengths      | 8 bytes × chunk count    | Present only when chunk count > 1, stored at the tail; one little-endian bit count per chunk   |

The serialized tree is byte-aligned (via `BitWriterFlush`) before the
chunk-count byte, so the payload always starts on a fresh byte boundary.
When there is more than one chunk, the decoder locates the bit-length table
by seeking to `file_size - chunk_count * 8` from the end of the file.

## Design decisions

**`mmap` for input.** Mapping the file lets the kernel page cache serve reads directly, which avoids an explicit `read` loop and an intermediate buffer. Since the input is scanned twice (histogram, then encoding), the second pass is typically served from the page cache. The file is paged in on demand, so input size is not limited by allocated memory.

**Multithreading.** Parallelism is opt-in through `-j N` and is applied to the three stages that can be split without changing the output format. All workers share read-only data (the mapped input and the code table) and never touch a shared mutable structure, so no locks are required.

- **Frequency counting** (`countFrequencies` / `countChunkFreq`). The input is divided into `N` contiguous ranges by `chunkRange`; each `FreqWorker` scans its range and accumulates into a private 256-entry `freq` array. Only after every thread is joined does the main thread sum the partial histograms. In the single-threaded path the same loop runs inline.
- **Chunk encoding** (`encodeChunk` / `WriteWorker`). Every worker is given the shared input, the shared `tree.encodings` table and its own range, and writes into its own `BitWriter` running in *memory mode* (sink `NULL`, buffer grown on demand). After the join the main thread flushes each buffer to the file in order and records each chunk's `total_bits`, which becomes the tail table the decoder reads. Because each encoder starts from the root and its own accumulator, every chunk is byte-aligned and independent.
- **Chunk decoding** (`decodeController` / `ReadWorker`). Workers pull chunk indices from the `_Atomic u8 next_chunk` counter with `atomic_fetch_add`, which gives lock-free dynamic load balancing instead of a static pre-assignment. Each `ReadWorker` has a private `BitReader` and writes to a disjoint `mmap` region of the output file, so the writes cannot race. The chunk table is built from the tail metadata before any worker starts.

The work unit is therefore a contiguous byte range of the original file: for compression it is a slice of the input to histogram or encode, and for decompression it is the pair (compressed bit offset, decompressed byte offset) reconstructed from the chunk table.

**Bit-level I/O.** `BitWriter` and `BitReader` are symmetric. A 64-bit accumulator collects variable-length codes at bit level; once it holds at least 32 bits, those are flushed to the intermediate buffer, which stores a much larger quantity of bytes. Writing to the output file happens only when the intermediate buffer is full, which keeps the number of `write` system calls low.

## Testing

`test.sh` is a round-trip harness that runs `./zippo` as a subprocess and compares
input and output with `cmp -s`. A failing case is recorded and the suite continues,
so the first failure does not hide the rest. The property under test
is `decode(encode(x)) == x`, byte for byte, over a range of inputs.
