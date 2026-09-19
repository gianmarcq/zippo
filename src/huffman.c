#include "huffman.h"
#include "common.h"
#include "heap.h"
#include "io.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

#define ALPHABET_SIZE 256

typedef struct TNode *TLink;
struct TNode {
    TLink left, right;
    u8 sym;
    u64 freq;
};

typedef struct {
    u64 code;
    u8 length;
} Encoding;

typedef struct {
    TLink root;
    Encoding *encodings;
} HuffmanTree;

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

static TLink TNodeInit(u8 sym, u64 freq, TLink left, TLink right) {
    TLink node = malloc(sizeof(*node));
    node->sym = sym;
    node->freq = freq;
    node->left = left;
    node->right = right;
    return node;
}

static i32 TNodeCmp(const void *a, const void *b) {
    const TLink nodeA = (const TLink) a;
    const TLink nodeB = (const TLink) b;
    return nodeA->freq - nodeB->freq;
}

/* Populate the priority queue directly with nodes of
 * the huffman tree */
static Heap fillHeap(u64 freq[]) {
    Heap heap = HeapInit(ALPHABET_SIZE, TNodeCmp);
    for (u16 c = 0; c < ALPHABET_SIZE; c++) {
        if (freq[c] > 0) {
            HeapPush(heap, TNodeInit(c, freq[c], NULL, NULL));
        }
    }
    return heap;
}

/* Build an Huffman tree starting from a priority queue.
 * This function implements the notorius algorithm for
 * the Huffman tree building */
static HuffmanTree HuffmanTreeBuild(Heap heap) {
    /* Handle edge-case: there is a single symbol
     * in the input file that correspond to a single
     * node in the tree, which is invalidating
     * general logic. */
    if (HeapSize(heap) == 1) {
        TLink node = HeapPop(heap);
        u8 dummy_sym = (node->sym == 0) ? 1 : 0;
        TLink dummy_node = TNodeInit(dummy_sym, 0, NULL, NULL);
        TLink root = TNodeInit(0, node->freq, node, dummy_node);
        HeapPush(heap, root);
    }

    while (HeapSize(heap) > 1) {
        TLink x = HeapPop(heap);
        TLink y = HeapPop(heap);
        TLink z = TNodeInit(0, x->freq + y->freq, x, y);
        HeapPush(heap, z);
    }

    HuffmanTree tree = { .root = HeapPop(heap), .encodings = calloc(ALPHABET_SIZE, sizeof(Encoding))};
    return tree;
}

/* Free a generic binary tree */
static void freeTree(TLink root) {
    if (root == NULL) return;
    freeTree(root->left);
    freeTree(root->right);
    free(root);
}

static void HuffmanTreeDestroy(HuffmanTree tree) {
    freeTree(tree.root);
    free(tree.encodings);
}

/* Generate encodings by traversing the Huffman tree:
 * label each left branch with zeros and each right branch
 * with ones. Save bit sequence formulated when arriving to a leaf */
static void encodingsGenerationR(TLink root, Encoding *encs, u64 code, u8 depth) {
    if (root->left == NULL && root->right == NULL) {
        encs[root->sym] = (Encoding) {.code = code, .length = depth};
        return;
    }
    encodingsGenerationR(root->left, encs, code, depth+1);
    BITSET(code, depth);
    encodingsGenerationR(root->right, encs, code, depth+1);
}

static void HuffmanTreeGenerateEncodings(HuffmanTree tree) {
    encodingsGenerationR(tree.root, tree.encodings, 0, 0);
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

/* Serialize the Huffman tree by traversing it in pre-order
 * and writing to file its topology. When encountering a leaf
 * save the symbol associated */
static void writeTreeR(BitWriter *bw, TLink root) {
    if (root == NULL) return;
    if (root->left == NULL && root->right == NULL) {
        BitWriterWrite(bw, 1ULL, 1);
        BitWriterWrite(bw, root->sym, 8);
        return;
    }
    BitWriterWrite(bw, 0, 1);
    writeTreeR(bw, root->left);
    writeTreeR(bw, root->right);
}

static void writeSerializedHuffmanTree(BitWriter *bw, HuffmanTree tree) {
    writeTreeR(bw, tree.root);
}

/* Write bits to out following the binary format */
static void writeCompressedFile(FileInMemory fim, HuffmanTree tree, u64 fsize, const char *out, u8 threads) {
    BitWriter bw = {0};
    BitWriterInit(&bw, fopen(out, "wb"), 32 * 1024);
    if (bw.sink == NULL) handle_sys_error("fopen");

    BitWriterWrite(&bw, MAGIC_NUMBER, 8 * 4);
    BitWriterWrite64(&bw, fsize); // Original file size
    writeSerializedHuffmanTree(&bw, tree);

    // BitWriterWrite(&bw, threads, 8);
    // BitWriterWrite(&bw, threads, 8);

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

    /* Step 2: Symbols gets inserted into a Min Heap
     * defining symbol order based on frequence */
    Heap heap = fillHeap(freq);

    /* Step 3: Build the Huffman tree given the heap */
    HuffmanTree tree = HuffmanTreeBuild(heap);

    /* Step 4: Compute codes for symbols by traversing the tree
     * and getting to the leaves (actual symbols) */
    HuffmanTreeGenerateEncodings(tree);

    /* Step 5: Write binary file following */
    writeCompressedFile(fim, tree, fim.size, path_out, threads);

    HeapDestroy(heap);
    FIMDestroy(fim);
    HuffmanTreeDestroy(tree);
}

/* Deserialize the Huffman tree and rebuild it
 * node by node */
static TLink readTreeR(BitReader *br) {
    u64 bit = BitReaderRead(br, 1);
    if (bit == 1) {
        u8 symbol = BitReaderRead(br, 8);
        return TNodeInit(symbol, 0, NULL, NULL);
    } else {
        TLink left = readTreeR(br);
        TLink right = readTreeR(br);
        return TNodeInit(0, 0, left, right);
    }
}

static HuffmanTree readSerializedHuffmanTree(BitReader *br) {
    return (HuffmanTree) { .root = readTreeR(br) };
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

    HuffmanTree tree = readSerializedHuffmanTree(&br);
    writeDecompressedFile(&br, tree, target_size, path_out);

    HuffmanTreeDestroy(tree);
    FIMDestroy(fim);
}
