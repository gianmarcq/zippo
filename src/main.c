#include <getopt.h>
#include <stdlib.h>
#include "common.h"
#include "huffman.h"

static void print_help(const char *prog_name) {
    printf("Zippo - File Compressor\n\n");
    printf("Usage: %s [OPTIONS] -i <input> -o <output>\n", prog_name);
    printf("Options:\n");
    printf("  -c, --compress\n");
    printf("  -d, --decompress\n");
    printf("  -i, --input <f>   Input target\n");
    printf("  -o, --output <f>  Output target\n");
    printf("  -h, --help        Show this message\n");
}

int main(int argc, char **argv) {
    /* If the program is called with no arguments then
     * print the help message end exit */
    if (argc == 1) {
        print_help(argv[0]);
        return 0;
    }

    struct {
        char *in, *out;
        u8 comp, dec;
    } state = {0};

    /* struct option array for flags definition,
     * see: man getopt_long */
    struct option longopts[] = {
        { "compress",   no_argument,       0, 'c' },
        { "decompress", no_argument,       0, 'd' },
        { "input",      required_argument, 0, 'i' },
        { "output",     required_argument, 0, 'o' },
        { "help",       no_argument,       0, 'h' },
    };

    int opt = 0;
    char optstring[] = "cdi:o:h";
    while ((opt = getopt_long(argc, argv, optstring, longopts, NULL)) != -1) {
        switch (opt) {
            case 'c': state.comp = 1;      break;
            case 'd': state.dec = 1;       break;
            case 'i': state.in = optarg;   break;
            case 'o': state.out = optarg;  break;
            case 'h':
                      print_help(argv[0]);
                      return 0;
            default: return EXIT_FAILURE;
        }
    }

    /* Not every arguments has been parsed, it means there is
     * some garbage in argv */
    if (optind < argc) {
        APP_ERROR("Invalid argument: %s", argv[optind]);
    }

    if (state.in == NULL || state.out == NULL) APP_ERROR("Provide Input and Output: (-i) and (-o)");
    if (state.comp == state.dec) APP_ERROR("Specify a Single action: (-c) or (-d)");

    if (state.comp) {
        encode(state.in, state.out);
    } else if (state.dec) {
        decode(state.in, state.out);
    }

    return 0;
}
