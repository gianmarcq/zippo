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

#define LUT_BITS (10)
/* Maximum number of bits of a code
 * If a code requires more than LUT_BITS bits
 * than it is not stored in the lut and requires
 * tree traversal to find corresponding symbol */
#define LUT_SIZE (1u << LUT_BITS)

typedef struct {
    u8 sym;
    u8 len;
} DecodeEntry;

typedef struct {
    TLink root;
    Encoding *encodings;
    u16 alphabet_size;
    DecodeEntry *lut;
} HuffmanTree;

HuffmanTree HTInit(u64 *freq, u16 alphabet_size);
void        HTDestroy(HuffmanTree tree);
void        HTWriteSerializedTree(HuffmanTree tree, BitWriter *bw);
HuffmanTree HTReadSerializedTree(BitReader *br);
void        HTBuildDecodeLut(HuffmanTree *tree);

#endif // !HUFFMAN_H
