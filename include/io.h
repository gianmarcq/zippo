#ifndef IO_H
#define IO_H

#include "common.h"
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#define BITSET(buf, i)   ((buf) |= (1ULL << (i)))
#define BITUNSET(buf, i) ((buf) &= ~(1ULL << (i)))

typedef struct {
    u8 *data;
    u64 size;
    int fd;
} FileInMemory;

FileInMemory FIMInit(const char *filepath);
void FIMDestroy(FileInMemory fim);


#define INTERBUF_SIZE 512
#define BIGBUF_SIZE INTERBUF_SIZE * 4

typedef struct {
    FILE *file;
    u64 buffer;
    u8 used;
    struct {
        u8 b[INTERBUF_SIZE];
        u16 size;
    } interbuf;
} BitWriter;

void BitWriterWrite(BitWriter *bw, u64 code, u8 length);
void BitWriterWrite64(BitWriter *bw, u64 value);
void BitWriterFlush(BitWriter *bw);

void StringFromBits(char *s, u64 buf, u8 len);

typedef struct {
    FileInMemory *fim;
    u64 pos;
    u64 buffer;
    u8 available;
} BitReader;

u64 BitReaderRead(BitReader *br, u8 length);
u64 BitReaderRead64(BitReader *br);

#endif // !IO_H
