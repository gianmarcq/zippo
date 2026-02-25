#include "huffman.h"
#include "common.h"
#include "heap.h"
#include "io.h"

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

static void countFrequencies(u64 freq[], const u8 *data, u64 size) {
    for (u64 i = 0; i < size; i++) {
        u8 byte = data[i];
        freq[byte]++;
    }
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

static Heap fillHeap(u64 freq[]) {
    Heap heap = HeapInit(ALPHABET_SIZE, TNodeCmp);
    for (u16 c = 0; c < ALPHABET_SIZE; c++) {
        if (freq[c] > 0) {
            HeapPush(heap, TNodeInit(c, freq[c], NULL, NULL));
        }
    }
    return heap;
}

static HuffmanTree HuffmanTreeBuild(Heap heap) {
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

static void writeCompressedFile(FileInMemory fim, HuffmanTree tree, u64 fsize, const char *out) {
    BitWriter bw = { .file = fopen(out, "wb") };
    if (bw.file == NULL) SYS_ERROR("fopen");

    BitWriterWrite(&bw, MAGIC_NUMBER, 8 * 4);
    BitWriterWrite64(&bw, fsize);
    writeSerializedHuffmanTree(&bw, tree);

    for (u64 i = 0; i < fim.size; i++) {
        u64 plain_sym = fim.data[i];
        u64 code = tree.encodings[plain_sym].code;
        u8 len = tree.encodings[plain_sym].length;
        BitWriterWrite(&bw, code, len);
    }

    BitWriterFlush(&bw);

    fclose(bw.file);
}

void encode(const char *path_in, const char *path_out) {
    FileInMemory fim = FIMInit(path_in);
    u64 freq[ALPHABET_SIZE] = {0};
    countFrequencies(freq, fim.data, fim.size);

    Heap heap = fillHeap(freq);
    HuffmanTree tree = HuffmanTreeBuild(heap);
    HuffmanTreeGenerateEncodings(tree);

    writeCompressedFile(fim, tree, fim.size, path_out);

    HeapDestroy(heap);
    FIMDestroy(fim);
    HuffmanTreeDestroy(tree);
}

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
    BitWriter bw = { .file = fopen(out, "wb") };

    TLink curr = tree.root;

    while (target_size > 0) {
        curr = BitReaderRead(br, 1) == 0 ? curr->left : curr->right;
        if (curr->left == NULL && curr->right == NULL) {
            u8 sym = curr->sym;
            BitWriterWrite(&bw, (u64) sym, 8);
            curr = tree.root;
            target_size--;
        }
    }

    BitWriterFlush(&bw);
    fclose(bw.file);
}

void decode(const char *path_in, const char *path_out) {
    FileInMemory fim = FIMInit(path_in);
    BitReader br = { .fim = &fim };
    u64 magic_number = BitReaderRead(&br, 32);
    if (magic_number != MAGIC_NUMBER) {
        APP_ERROR("Corrupted file: Magic Number does not match");
    }

    u64 target_size = BitReaderRead64(&br);
    if (target_size == 0) APP_ERROR("Nothing to decompress: Original file size is zero");

    HuffmanTree tree = readSerializedHuffmanTree(&br);
    writeDecompressedFile(&br, tree, target_size,path_out);

    HuffmanTreeDestroy(tree);
    FIMDestroy(fim);
}
