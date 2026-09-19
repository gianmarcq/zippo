#include "huffman.h"
#include "heap.h"
#include "io.h"
#include <stdlib.h>

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

/* Free a generic binary tree */
static void freeTree(TLink root) {
    if (root == NULL) return;
    freeTree(root->left);
    freeTree(root->right);
    free(root);
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

/* Populate the priority queue directly with nodes of
 * the huffman tree */
static Heap fillHeap(u64 freq[], u16 alphabet_size) {
    Heap heap = HeapInit(alphabet_size, TNodeCmp);
    for (u16 c = 0; c < alphabet_size; c++) {
        if (freq[c] > 0) {
            HeapPush(heap, TNodeInit(c, freq[c], NULL, NULL));
        }
    }
    return heap;
}

/* Build an Huffman tree starting from a priority queue.
 * This function implements the notorius algorithm for
 * the Huffman tree building */
static HuffmanTree HuffmanTreeBuild(Heap heap, u16 alphabet_size) {
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

    HuffmanTree tree = { .root = HeapPop(heap), .encodings = calloc(alphabet_size, sizeof(Encoding)), .alphabet_size = alphabet_size};
    return tree;
}

HuffmanTree HTInit(u64 *freq, u16 alphabet_size) {
    /* Step 2: Symbols gets inserted into a Min Heap
     * defining symbol order based on frequence */
    Heap heap = fillHeap(freq, alphabet_size);

    /* Step 3: Build the Huffman tree given the heap */
    HuffmanTree tree = HuffmanTreeBuild(heap, alphabet_size);
    HeapDestroy(heap);

    /* Step 4: Compute codes for symbols by traversing the tree
     * and getting to the leaves (actual symbols) */
    HuffmanTreeGenerateEncodings(tree);

    return tree;
}

void HTDestroy(HuffmanTree tree) {
    freeTree(tree.root);
    free(tree.encodings);
}

void HTWriteSerializedTree(HuffmanTree tree, BitWriter *bw) {
    writeTreeR(bw, tree.root);
}

HuffmanTree HTReadSerializedTree(BitReader *br) {
    return (HuffmanTree) { .root = readTreeR(br) };
}
