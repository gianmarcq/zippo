#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <assert.h>

#include "engine.h"
#include "common.h"
#include "huffman.h"
#include "io.h"

typedef struct {
    const u8 *data;
    u64 start, end;
    u64 freq[ALPHABET_SIZE];
    pthread_t t;
    u8 id;
} FreqWorker;

static void chunkRange(u64 size, u8 nchunks, u8 i, u64 *start, u64 *end) {
    u64 base = size / nchunks;
    *start = base * i;
    *end = (i == nchunks-1) ? size : *start + base;
}

void* countChunkFreq(void *arg) {
    FreqWorker *th = (FreqWorker*) arg;
    const u8 *data = th->data;
    u64 *local_freq = th->freq;
    u8 byte;

    for (u64 i = th->start; i < th->end; i++) {
        byte = data[i];
        local_freq[byte]++;
    }
    return 0;
}

static void countFrequencies(u64 freq[], const u8 *data, u64 size, u8 threads) {
    if (threads <= 1) {
        for (u64 i = 0; i < size; i++)
            freq[data[i]]++;
        return;
    }

    int s;
    FreqWorker *ths = calloc(threads, sizeof(*ths));
    for (u8 i = 0; i < threads; i++) {

        ths[i].id = i + 1;
        ths[i].data = data;
        chunkRange(size, threads, i, &ths[i].start, &ths[i].end);

        s = pthread_create(&ths[i].t, NULL, countChunkFreq, ths + i);
        if (s != 0) handle_sys_error("pthread_create");
    }

    for (u8 i = 0; i < threads; i++) {
        s = pthread_join(ths[i].t, NULL);
        if (s != 0) handle_sys_error("pthread_join");
    }

    // join partial result into freq array
    for (u8 i = 0; i < threads; i++) {
        for (u16 c = 0; c < ALPHABET_SIZE; c++) {
            freq[c] += ths[i].freq[c];
        }
    }

    free(ths);
}

static void dumpEncodings(HuffmanTree tree, u64 freq[ALPHABET_SIZE]) {
    char *sym = malloc(65);
    char *code = malloc(65);
    printf("==== Big Endian Strings ====\n");
    for (u64 i = 0; i < ALPHABET_SIZE; i++) {
        if (freq[i] > 0) {
            StringFromBits(sym, i, 8);
            StringFromBits(code, tree.encodings[i].code, tree.encodings[i].length);
            printf("(%s/%c/%lu) [%lu]: %s\n", sym, (u8) i, i, freq[i], code);
        }
    }
    free(sym);
    free(code);
}

static void dumpRelativeFrequencies(u64 *freq, u64 size) {
    for (u64 i = 0; i < ALPHABET_SIZE; i++) {
        if (freq[i] > 0) {
            printf("%c: %f\n", (char) i, (float) freq[i] / size * 100.f);
        }
    }
}

typedef struct {
    const u8 *data;
    u64 start, end, total_bits;
    Encoding *encs;
    BitWriter bw;
    pthread_t t;
    u8 id;
} WriteWorker;

#define STREAMING_INTERBUF_CAP (32 * 1024) // 32Kb
#define MEMORY_INTERBUF_CAP    (1  * 1024) //  1Kb

void *encodeChunk(void *arg) {
    WriteWorker *ww = (WriteWorker*) arg;
    const u8 *data = ww->data;
    u64 start = ww->start;
    u64 end = ww->end;
    BitWriter *bw = &ww->bw;
    Encoding *encs = ww->encs;

    u64 total_bits = 0;
    u64 plain_sym, code;
    u8 len;
    for (u64 i = start; i < end; i++) {
        plain_sym = data[i];
        code = encs[plain_sym].code;
        len = encs[plain_sym].length;
        BitWriterWrite(bw, code, len);
        total_bits += len;
    }

    ww->total_bits = total_bits;
    return 0;
}

/* Write bits to out following the binary format */
static void writeCompressedFile(FileInMemory fim, HuffmanTree tree, u64 fsize, const char *out, u8 threads) {
    FILE *fout = fopen(out, "wb");
    if (fout == NULL) handle_sys_error("fopen");

    BitWriter bw = {0};
    BitWriterInit(&bw, fout, STREAMING_INTERBUF_CAP);

    BitWriterWrite(&bw, MAGIC_NUMBER, 8 * 4);
    BitWriterWrite64(&bw, fsize); // Original file size
    HTWriteSerializedTree(tree, &bw);

    BitWriterFlush(&bw); // Byte-align the serialized tree
    if (threads <= 1) BitWriterWrite(&bw,  (u8) 0, 8);
    else              BitWriterWrite(&bw, threads, 8);
    BitWriterFlush(&bw); // Write new byte immediatly before chunk processing

    if (threads <= 1) {
        for (u64 i = 0; i < fim.size; i++) {
            u64 plain_sym = fim.data[i];
            u64 code = tree.encodings[plain_sym].code;
            u8 len = tree.encodings[plain_sym].length;
            BitWriterWrite(&bw, code, len);
        }

        BitWriterDestroy(&bw);
        return;
    }

    int s;
    WriteWorker *ths = calloc(threads, sizeof(*ths));
    for (u8 i = 0; i < threads; i++) {
        ths[i].id = i + 1;
        ths[i].data = fim.data;
        ths[i].encs = tree.encodings;
        /* sink = NULL, BitWriter in Memory Mode
         * NOTE: Initial interbuf capacity could be calculated in order
         * to approximate the number of bytes that will be occupied
         * by the encoded data, for semplicity sake, I skip this optimization */
        BitWriterInit(&ths[i].bw, NULL, MEMORY_INTERBUF_CAP);
        chunkRange(fim.size, threads, i, &ths[i].start, &ths[i].end);

        s = pthread_create(&ths[i].t, NULL, encodeChunk, ths + i);
        if (s != 0) handle_sys_error("pthread_create");
    }

    for (u8 i = 0; i < threads; i++) {
        s = pthread_join(ths[i].t, NULL);
        if (s != 0) handle_sys_error("pthread_join");

        BitWriterFlush(&ths[i].bw);
        fwrite(ths[i].bw.interbuf.b, 1, ths[i].bw.interbuf.size, fout);
        BitWriterDestroy(&ths[i].bw);
    }

    if (threads > 1) {
        for (u8 i = 0; i < threads; i++) {
            BitWriterWrite64(&bw, ths[i].end - ths[i].start);
            BitWriterWrite64(&bw, ths[i].total_bits);
        }
    }

    free(ths);
    BitWriterDestroy(&bw);
}

void encode(const char *path_in, const char *path_out, u8 threads) {
    FileInMemory fim = FIMOpen(path_in, 0, FIM_RD);
    u64 freq[ALPHABET_SIZE] = {0};

    /* Step 1: Count symbol frequencies */
    countFrequencies(freq, fim.data, fim.size, threads);

    /* Next Steps... */
    HuffmanTree tree = HTInit(freq, ALPHABET_SIZE);

    /* Step 5: Write binary file following */
    writeCompressedFile(fim, tree, fim.size, path_out, threads);

    FIMDestroy(fim);
    HTDestroy(tree);
}

static void writeDecompressedFile(BitReader *br, HuffmanTree tree, u64 target_size, const char *out) {
    BitWriter bw = {0};
    BitWriterInit(&bw, fopen(out, "wb"), 32 * 1024);

    TLink curr = tree.root;

    /* Decoding happens by leveraging prefix-free property
     * of the Huffman codes. Traverse the tree by keeping track of
     * the bit sequence formulated until you encounter a leaf,
     * there you know you've decoded a symbol */
    while (target_size > 0) {
        curr = BitReaderRead(br, 1) == 0 ? curr->left : curr->right;
        if (curr->left == NULL && curr->right == NULL) {
            u8 sym = curr->sym;
            BitWriterWrite(&bw, (u64) sym, 8);
            curr = tree.root;
            target_size--;
        }
    }

    BitWriterDestroy(&bw);
}

typedef struct {
    u64 d_size, d_offset; // decompressed chunk size (byte), chunk offset in original file
    u64 c_bits, c_offset; // compressed chunk size (bits), chunk offset in compressed file
} ChunkInfo;

// shared across threads
typedef struct {
    ChunkInfo *chunks;
    HuffmanTree *tree;
    FileInMemory *in_fim;       // compressed input data
    u8 *out_data;               // mapped output
    _Atomic(u8) next_chunk;
    u8 total_chunks;
} DecompContext;

typedef struct {
    pthread_t t;
    DecompContext *dc;
    BitReader br;         // Read bits from input file (compressed)
} ReadWorker;

void decodeChunk(ChunkInfo *ci, BitReader *br, HuffmanTree *tree, u8 *out, u64 offset) {
    TLink curr = tree->root;
    u64 written_bytes = 0;

    /* Decoding happens by leveraging prefix-free property
     * of the Huffman codes. Traverse the tree by keeping track of
     * the bit sequence formulated until you encounter a leaf,
     * there you know you've decoded a symbol */
    while (written_bytes < ci->d_size) {
        curr = BitReaderRead(br, 1) == 0 ? curr->left : curr->right;
        if (curr->left == NULL && curr->right == NULL) {
            out[offset + (written_bytes++)] = curr->sym;
            curr = tree->root;
        }
    }
}

void *decodeController(void *arg) {
    ReadWorker *rw = (ReadWorker*) arg;
    DecompContext *dc = rw->dc;

    while (1) {
        u8 chunk_id = atomic_fetch_add(&dc->next_chunk, 1);
        if (chunk_id >= dc->total_chunks) break;
        rw->br = (BitReader) { .fim = dc->in_fim, .pos = dc->chunks[chunk_id].c_offset };
        decodeChunk(&dc->chunks[chunk_id], &rw->br, dc->tree, dc->out_data, dc->chunks[chunk_id].d_offset);
    }

    return 0;
}

void decode(const char *path_in, const char *path_out, u8 threads) {
    FileInMemory in_fim = FIMOpen(path_in, 0, FIM_RD);
    BitReader br = { .fim = &in_fim };

    u64 magic_number = BitReaderRead(&br, 32);
    if (magic_number != MAGIC_NUMBER) {
        handle_user_error("Corrupted file: Magic Number does not match");
    }

    u64 target_size = BitReaderRead64(&br);
    if (target_size == 0) handle_user_error("Nothing to decompress: Original file size is zero");

    HuffmanTree tree = HTReadSerializedTree(&br);
    BitReaderByteAlign(&br);
    u8 nchunks = BitReaderRead(&br, 8);

    if (nchunks > 1) {
        u64 payload_start = br.pos;
        ChunkInfo *chunks = malloc(nchunks * sizeof(*chunks));

        /* Read chunk metadata from the tail and calculate the file offset
         * in the decompressed file (allow parallel writing on output file) */
        br.pos = in_fim.size - (nchunks * 16);
        u64 d_offset, c_offset = payload_start, total_uncompressed_size;
        d_offset = total_uncompressed_size = 0;
        for (u8 i = 0; i < nchunks; i++) {
            chunks[i].d_size = BitReaderRead64(&br); // original chunk size
            chunks[i].c_bits = BitReaderRead64(&br); // compressed chunk size (in bits)

            chunks[i].d_offset = d_offset;
            chunks[i].c_offset = c_offset;

            d_offset += chunks[i].d_size;
            c_offset += (chunks[i].c_bits + 7) >> 3;
            total_uncompressed_size += chunks[i].d_size;
        }

        /* extend file length to total_uncompressed_size in order to avoid
         * SIGBUS when trying to access a position in the memory that goes beyond
         * real file size */
        FileInMemory out_fim = FIMOpen(path_out, total_uncompressed_size, FIM_RW);
        DecompContext dc = {
            .chunks = chunks,
            .tree = &tree,
            .in_fim = &in_fim,
            .out_data = out_fim.data,
            .next_chunk = 0,
            .total_chunks = nchunks
        };

        ReadWorker *rw = calloc(threads, sizeof(*rw));
        br.pos = payload_start;
        i32 s;
        for (u8 i = 0; i < threads; i++) {
            rw[i].dc = &dc;
            s = pthread_create(&rw[i].t, NULL, decodeController, rw + i);
            if (s != 0) handle_sys_error("pthread_create");
        }

        for (u8 i = 0; i < threads; i++) {
            s = pthread_join(rw[i].t, NULL);
            if (s != 0) handle_sys_error("pthread_join");
        }

        free(rw);
        free(chunks);
        FIMDestroy(out_fim);
    } else {
        writeDecompressedFile(&br, tree, target_size, path_out);
    }

    HTDestroy(tree);
    FIMDestroy(in_fim);
}
