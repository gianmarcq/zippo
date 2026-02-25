#ifndef HUFFMAN_H
#define HUFFMAN_H

#include "common.h"
#include "io.h"

#define MAGIC_NUMBER 0x87654321
void encode(const char *path_in, const char *path_out);
void decode(const char *path_in, const char *path_out);

#endif // !HUFFMAN_H
