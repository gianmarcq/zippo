#ifndef ENGINE_H
#define ENGINE_H

#include "io.h"
#define ALPHABET_SIZE 256
#define MAGIC_NUMBER 0x87654321

void encode(const char *path_in, const char *path_out, u8 threads);
void decode(const char *path_in, const char *path_out);

#endif // !ENGINE_H

