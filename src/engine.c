#include <pthread.h>
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

void* countFreqFromStartToEnd(void *arg) {
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

        s = pthread_create(&ths[i].t, NULL, countFreqFromStartToEnd, ths + i);
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
         * by the encoded data, for semplicity take, I skip this optimization */
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
            BitWriterWrite64(&bw, ths[i].total_bits);
        }
    }

    free(ths);
    BitWriterDestroy(&bw);
}

void encode(const char *path_in, const char *path_out, u8 threads) {
    FileInMemory fim = FIMInit(path_in);
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
    const u8 *data;
    u64 start, end, total_bits;
    HuffmanTree *tree;
    BitWriter bw;
    pthread_t t;
    u8 id;
} ReadWorker;

void decode(const char *path_in, const char *path_out) {
    FileInMemory fim = FIMInit(path_in);
    BitReader br = { .fim = &fim };

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
        ReadWorker *ths = calloc(nchunks, sizeof(*ths));

        // Read chunk metadata from the tail
        br.pos = fim.size - (nchunks * 8);
        for (u8 i = 0; i < nchunks; i++)
            ths[i].total_bits = BitReaderRead64(&br);

        br.pos = payload_start;
        for (u8 i = 0; i < nchunks; i++) {
            // initialize ReadWorkers and start threads
        }

        for (u8 i = 0; i < nchunks; i++) {
            // join threads and write decoded data in sequence
        }

        free(ths);
    } else {
        writeDecompressedFile(&br, tree, target_size, path_out);
    }

    HTDestroy(tree);
    FIMDestroy(fim);
}
