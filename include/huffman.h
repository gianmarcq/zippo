#ifndef HUFFMAN_H
#define HUFFMAN_H

#include "io.h"
#include "common.h"

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
    u16 alphabet_size;
} HuffmanTree;

HuffmanTree HTInit(u64 *freq, u16 alphabet_size);
void        HTDestroy(HuffmanTree tree);
void        HTWriteSerializedTree(HuffmanTree tree, BitWriter *bw);
HuffmanTree HTReadSerializedTree(BitReader *br);

#endif // !HUFFMAN_H
