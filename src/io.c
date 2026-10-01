#include "io.h"
#include "common.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>

FileInMemory FIMOpen(const char *filepath, u64 size, FIMFlag flag) {
    FileInMemory fim = {
        .data = NULL,
        .size = 0,
        .fd = -1,
        .flag= flag
    };

    i32 open_flags;
    switch (flag) {
        case FIM_RD:
            open_flags = O_RDONLY;
            break;
        case FIM_RW:
            open_flags = O_RDWR | O_CREAT;
            break;
        default:
            handle_user_error("Invalid FIMOpen flag");
            break;
    }

    fim.fd = open(filepath, open_flags, 0644);
    if (fim.fd < 0) handle_sys_error("open");
    //        user group others
    // 0644 = 110  100   100

    if (flag == FIM_RW) {
        if (size <= 0) handle_user_error("Cannot read or write to file with size %lu", size);
        if (ftruncate(fim.fd, size) != 0) handle_sys_error("ftruncate");
        fim.size = size;
    } else if (flag == FIM_RD) {
        /* Retrieve information about the file pointed
         * by fim.fd (number of bytes, ...) */
        struct stat info;
        if (fstat(fim.fd, &info) == -1) handle_sys_error("fstat");
        fim.size = info.st_size;
        /* WARNING: an emtpy file would cause
         * segfault on reading attempt,
         * returning and leaving fim.data = NULL */
        if (fim.size == 0) return fim;
    }

    i32 map_prot = flag == FIM_RW ? (PROT_WRITE | PROT_READ) : PROT_READ;
    i32 map_flags = flag == FIM_RD ? MAP_PRIVATE : MAP_SHARED;
    fim.data = mmap(NULL, fim.size, map_prot, map_flags, fim.fd, 0);
    if (fim.data == MAP_FAILED) handle_sys_error("mmap");
    return fim;
}

void FIMDestroy(FileInMemory fim) {
    munmap(fim.data, fim.size);
    close(fim.fd);
}

void StringFromBits(char *s, u64 buf, u8 len) {
    assert(len < 64);
    for (u8 i = 0; i < len; i++)
        s[i] = ((buf >> i) & (1ULL)) + 48;
    s[len] = 0;
}

static void writeInterbuf(BitWriter *bw) {
    if (bw->interbuf.size > 0) {
        fwrite(bw->interbuf.b, sizeof(*bw->interbuf.b), bw->interbuf.size, bw->sink);
        bw->interbuf.size = 0;
    }
}

void BitWriterInit(BitWriter *bw, FILE *sink, u64 interbuf_cap) {
    bw->sink = sink;
    bw->interbuf.cap = interbuf_cap;
    bw->interbuf.b = malloc(interbuf_cap);
}

void BitWriterDestroy(BitWriter *bw) {
    BitWriterFlush(bw);
    free(bw->interbuf.b);
    if (bw->sink) fclose(bw->sink);
}

/* This function allows to write 8 bytes by splitting in half
 * the bit sequence in order to overcome the length limitation
 * Ref: BitWriterWrite */
void BitWriterWrite64(BitWriter *bw, u64 value) {
    BitWriterWrite(bw, value & 0xFFFFFFFFULL, 32);
    BitWriterWrite(bw, value >> 32, 32);
}

void bwMakeInterbufRoom(BitWriter *bw, u64 need) {
    if (bw->interbuf.size + need <= bw->interbuf.cap) return;
    // Streaming mode: dump interbuf content to file and continue accumulate-dump loop
    if (bw->sink) writeInterbuf(bw);
    else {
        /* Memory mode: expand interbuf in order to store more bytes.
         * When the chunk processing will be done, the entire interbuf
         * will be written to destination file */
        while (bw->interbuf.size + need > bw->interbuf.cap)
            bw->interbuf.cap = bw->interbuf.cap ? bw->interbuf.cap * 2 : 4096;
        bw->interbuf.b = reallocarray(bw->interbuf.b, bw->interbuf.cap, sizeof(*bw->interbuf.b));
        if (bw->interbuf.b == NULL) handle_sys_error("reallocarray");
    }
}

void bwMakeBufRoom(BitWriter *bw, u64 need) {
    if (bw->used + need <= 64) return;
    while (bw->used >= 8) {
        bwMakeInterbufRoom(bw, 8);
        bw->interbuf.b[bw->interbuf.size++] = (u8)(bw->buffer);
        bw->buffer >>= 8;
        bw->used -= 8;
    }
}

void BitWriterWrite(BitWriter *bw, u64 code, u8 length) {
    /* The buffer might be storing some bits which are waiting to be written */
    bwMakeBufRoom(bw, length);

    bw->buffer |= code << bw->used;
    bw->used += length;

    while (bw->used >= 32) {
        bwMakeInterbufRoom(bw, 32);

        bw->interbuf.b[bw->interbuf.size++] = (u8)(bw->buffer);
        bw->interbuf.b[bw->interbuf.size++] = (u8)(bw->buffer >> 8);
        bw->interbuf.b[bw->interbuf.size++] = (u8)(bw->buffer >> 16);
        bw->interbuf.b[bw->interbuf.size++] = (u8)(bw->buffer >> 24);
        bw->buffer >>= 32;
        bw->used -= 32;
    }
}

/* Write final bits stored in the buffer, no
 * need for manual padding */
void BitWriterFlush(BitWriter *bw) {
    while (bw->used > 0) {
        bwMakeInterbufRoom(bw, 8);
        u8 byte = bw->buffer & 0xFF;
        bw->interbuf.b[bw->interbuf.size++] = byte;
        bw->buffer >>= 8;
        if (bw->used >= 8) bw->used -= 8;
        else bw->used = 0;
    }
    // Streaming mode: empty interbuf and write to file
    if (bw->sink) writeInterbuf(bw);
}

void BitReaderByteAlign(BitReader *br) {
    /* Skip bits in the buffer */
    br->buffer = 0;
    br->available = 0;
}

static void BitReaderFulfillRequest(BitReader *br, u8 req_bits) {
    while (br->available < req_bits) {
        u8 byte = 0;
        if (br->pos < br->fim->size) {
            byte = br->fim->data[br->pos++];
        }
        br->buffer |= ((u64) byte << br->available);
        br->available += 8;
    }
}

/* This function extract bytes from the file
 * in order to fullfill the bits request.
 * Pending bits are stored in buffer, a
 * <length> number of bits is returned */
u64 BitReaderRead(BitReader *br, u8 length) {
    assert(length <= 56);
    BitReaderFulfillRequest(br, length);

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

u64 BitReaderPeek(BitReader *br, u8 length) {
    assert(length <= 56);
    BitReaderFulfillRequest(br, length);
    return br->buffer & ~(~0ULL << length);
}

void BitReaderSkip(BitReader *br, u8 length) {
    (void)BitReaderRead(br, length);
}
