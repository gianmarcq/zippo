#include <getopt.h>
#include "common.h"
#include "huffman.h"
#include <string.h>

static void print_help(const char *prog_name);
static char* consume_arg(int *argc, char ***argv) {
    char *arg = *argv[0];
    (*argc)--;
    (*argv)++;
    return arg;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        print_help(argv[0]);
        return 0;
    }

    struct {
        char *executable;
        char *input, *output;
        u8 comp, dec;
    } cli_state = {0};

    cli_state.executable = consume_arg(&argc, &argv);
    char *cmd = consume_arg(&argc, &argv);
    if (strcmp(cmd, "c") == 0) cli_state.comp = 1;
    else if (strcmp(cmd, "d") == 0) cli_state.dec = 1;
    else handle_user_error("Invalid command: %s\n", cmd);

    if (argc < 2) handle_user_error("Provide input and output file");
    if (argc > 2) handle_user_error("Illegal arguments");
    cli_state.input = consume_arg(&argc, &argv);
    cli_state.output = consume_arg(&argc, &argv);

    if (cli_state.comp) encode(cli_state.input, cli_state.output);
    else if (cli_state.dec) decode(cli_state.input, cli_state.output);

    return 0;
}

void print_help(const char *prog_name) {
    printf("Zippo - File Compressor\n");
    printf("Usage: %s [CMD] <input> <output>\n", prog_name);
    printf("Commands:\n");
    printf("  c, Compress\n");
    printf("  d, Decompress\n");
    printf("  h, Show this message\n");
}
