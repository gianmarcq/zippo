#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

inline void raiseError(const char *s) {
    perror(s);
    exit(1);
}

#define SYS_ERROR(s) do { \
    fprintf(stderr, "[ERROR] %s:%d\n", __FILE__, __LINE__); \
    perror(s); \
    exit(EXIT_FAILURE); \
} while(0)

#define APP_ERROR(fmt, ...) do { \
    fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__); \
    exit(EXIT_FAILURE); \
} while(0)

#endif // !COMMON_H
