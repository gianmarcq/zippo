#include "io.h"
#include <assert.h>

FileInMemory FIMInit(const char *filepath) {
    FileInMemory fim = {NULL, 0, -1};
    fim.fd = open(filepath, O_RDONLY);
    if (fim.fd == -1) SYS_ERROR("open");

    struct stat info;
    if (fstat(fim.fd, &info) == -1) SYS_ERROR("fstat");
    fim.size = info.st_size;

    if (fim.size == 0) {
        APP_ERROR("%s is Empty, Nothing to Do", filepath);
    }

    fim.data = mmap(NULL, fim.size, PROT_READ, MAP_PRIVATE, fim.fd, 0);
    if (fim.data == MAP_FAILED) SYS_ERROR("mmap");
    return fim;
}

void FIMDestroy(FileInMemory fim) {
    close(fim.fd);
    munmap(fim.data, fim.size);
}

void StringFromBits(char *s, u64 buf, u8 len) {
    assert(len < 64);
    for (u8 i = 0; i < len; i++)
        s[i] = ((buf >> i) & (1ULL)) + 48;
    s[len] = 0;
}

static void writeInterbuf(BitWriter *bw) {
    if (bw->interbuf.size > 0) {
        fwrite(bw->interbuf.b, sizeof(*bw->interbuf.b), bw->interbuf.size, bw->file);
        bw->interbuf.size = 0;
    }
}

/* This function allows to write 8 bytes by splitting in half
 * the bit sequence in order to overcome the length limitation
 * Ref: BitWriterWrite */
void BitWriterWrite64(BitWriter *bw, u64 value) {
    BitWriterWrite(bw, value & 0xFFFFFFFFULL, 32);
    BitWriterWrite(bw, value >> 32, 32);
}

void BitWriterWrite(BitWriter *bw, u64 code, u8 length) {
    /* The buffer might be storing some bits which are waiting to be written */
    assert(length <= 56);

    bw->buffer |= code << bw->used;
    bw->used += length;

    while (bw->used >= 8) {
        u8 byte = bw->buffer & (0xFF);
        bw->interbuf.b[bw->interbuf.size++] = byte;
        if (bw->interbuf.size == INTERBUF_SIZE) {
            writeInterbuf(bw);
        }
        bw->buffer >>= 8;
        bw->used -= 8;
    }
}

void BitWriterFlush(BitWriter *bw) {
    if (bw->used > 0) {
        u8 byte = bw->buffer & 0xFF;
        bw->interbuf.b[bw->interbuf.size++] = byte;
        bw->used = 0;
    }
    writeInterbuf(bw);
}

u64 BitReaderRead(BitReader *br, u8 length) {
    assert(length <= 56);
    while (br->available < length) {
        u8 byte = 0;
        if (br->pos < br->fim->size) {
            byte = br->fim->data[br->pos++];
        }
        br->buffer |= ((u64) byte << br->available);
        br->available += 8;
    }

    u64 res = br->buffer & ~(~0ULL << length);
    br->buffer >>= length;
    br->available -= length;

    return res;
}

u64 BitReaderRead64(BitReader *br) {
    u64 low = BitReaderRead(br, 32);
    u64 high = BitReaderRead(br, 32);
    return low | (high << 32);
}
