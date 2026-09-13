#include "common.h"
#include "huffman.h"
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    char *prg_name;
    char *in, *out;
    char mode;
    u8 threads;
} cli_state;

static void print_help(const char *prog_name);
static void print_cli_state(cli_state cs);

int main(int argc, char **argv) {
    cli_state cs = {0};
    cs.prg_name = argv[0];

    i32 opt;
    // Enforce POSIX behaviour with "+"
    while ((opt = getopt(argc, argv, "+cdj:")) != -1) {
        switch (opt) {
            case 'c':
            case 'd':
                if (cs.mode != 0) {
                    handle_user_error("Mode already specified");
                    return EXIT_FAILURE;
                }
                cs.mode = opt;
                break;
            case 'j':
                cs.threads = atoi(optarg);
                if (cs.threads < 1) cs.threads = 1;
                break;
            default:
                handle_user_error("Invalid argument '%s'", optarg);
                print_help(cs.prg_name);
                return EXIT_FAILURE;
        }
    }

    if (cs.mode == 0 || argc - optind != 2) {
        print_help(cs.prg_name);
        return EXIT_FAILURE;
    }

    cs.in = argv[optind];
    cs.out = argv[optind + 1];

    if (cs.mode == 'c') encode(cs.in, cs.out);
    else if (cs.mode == 'd') decode(cs.in, cs.out);

    return EXIT_SUCCESS;
}

void print_help(const char *prog_name) {
    printf("Zippo - File Compressor\n");
    printf("Usage: %s <opts> <input> <output>\n", prog_name);
    printf("Options:\n");
    printf("  -c,       Compress\n");
    printf("  -d,       Decompress\n");
    printf("  -j <arg>, Specify thread count\n");
}

void print_cli_state(cli_state cs) {
    printf("Program name: %s\n", cs.prg_name);
    printf("Input file:   %s\n", cs.in);
    printf("Output file:  %s\n", cs.out);
    printf("Mode:         %c\n", cs.mode);
    printf("Threads:      %d\n", cs.threads);
}
