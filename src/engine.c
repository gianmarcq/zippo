#include <pthread.h>

#include "engine.h"
#include "common.h"
#include "huffman.h"

typedef struct {
    const u8 *data;
    u8 id;
    pthread_t t;
    u64 start, end;
    u64 freq[ALPHABET_SIZE];
} FreqWorker;

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
        ths[i].start = (size / threads) * i;
        if (i == threads - 1) ths[i].end = size;
        else ths[i].end = ths[i].start + (size / threads);

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

/* Write bits to out following the binary format */
static void writeCompressedFile(FileInMemory fim, HuffmanTree tree, u64 fsize, const char *out, u8 threads) {
    BitWriter bw = {0};
    BitWriterInit(&bw, fopen(out, "wb"), 32 * 1024);
    if (bw.sink == NULL) handle_sys_error("fopen");

    BitWriterWrite(&bw, MAGIC_NUMBER, 8 * 4);
    BitWriterWrite64(&bw, fsize); // Original file size
    HTWriteSerializedTree(tree, &bw);

    // if (threads <= 1) BitWriterWrite(&bw, 0, 8);
    // else {
    //     BitWriterWrite(&bw, threads, 8);
    //     for (u8 i = 0; i < threads; i++) {
    //     }
    // }

    for (u64 i = 0; i < fim.size; i++) {
        u64 plain_sym = fim.data[i];
        u64 code = tree.encodings[plain_sym].code;
        u8 len = tree.encodings[plain_sym].length;
        BitWriterWrite(&bw, code, len);
    }

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
     * the bit sequence formulated till you encounter a leaf,
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
    writeDecompressedFile(&br, tree, target_size, path_out);

    HTDestroy(tree);
    FIMDestroy(fim);
}
