/* Binary file format
 *
 * [HEADER]
 * Magic number: 696969 (3 byte)
 * Alphabet size: uint8_t (1 byte)
 *
 * [ALPHABET]
 * For each symbol:
 *   Symbol: uint8_t (1 byte)
 *   Code length: uint8_t (1 byte)
 *   Huffman code: bit sequence (variable length)
 *
 * [ENCODED DATA]
 * Sequence of encoded bits packed into bytes
 *
 * [FOOTER]
 * Padding bit count: uint8_t (1 byte)
*/

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <math.h>
#include <sys/types.h>

/* 7-bit ASCII supported*/
#define ASCII_SIZE 1u << 7
#define MAGIC_NUMBER 69

/* A pair contains a tuple of 2 elements
 * (character, number_of_occurences) */
struct pair { int ch; size_t occ; };

/* Struct for huffman tree implementation.
 * Each node has two child and carries a pair */
struct tnode {
    struct tnode *left, *right;
    struct pair p;
};

/* Struct for priority queue implementation.
 * Carried data is a pointer to a tree node */
struct qnode {
    struct qnode* next;
    struct tnode* tn;
};

#define USAGE "Usage: <bin> c/d <file_path>\n"

int validate_args(int argc, char **argv)
{
    if (argc == 1) {
        fprintf(stderr, "ERROR: Invalid usage.\n"USAGE);
        return 1;
    } else if (argc == 2) {
        if (strlen(argv[1]) == 1)
            fprintf(stderr, "ERROR: File path not provided.\n"USAGE);
        else fprintf(stderr, "ERROR: Missing command.\n"USAGE);
        return 1;
    } else if (strlen(argv[1]) != 1) {
        fprintf(stderr, "ERROR: Invalid command %s.\n"USAGE, argv[1]);
        return 1;
    }
    return 0;
}

struct tnode* tree_create_node(struct pair p, struct tnode* left, struct tnode* right)
{
    /* Create a new node for the huffman tree. Each
     * node has two child and carries a pair (character, number_of_occurences)
     * Leaves don't have childs so left and right fields are just NULLs. */
    struct tnode* new = malloc(sizeof(struct tnode));
    assert(new != NULL && "Call to malloc() failed.");
    new->left = left;
    new->right = right;
    new->p = p;
    return new;
}

struct qnode* queue_create_node(struct tnode* tn)
{
    /* Create a new node carrying a pointer to a tree node.
     * Primarly used by other queue functions */
    struct qnode* new = malloc(sizeof(struct qnode));
    assert(new != NULL && "Call to malloc() failed.");
    new->next = NULL;
    new->tn = tn;
    return new;
}

void queue_offer(struct qnode** head, struct tnode* tn)
{
    /* Insert a new node in the queue ensuring its
     * correct position. Lower values of .occ field
     * have higher priority */
    struct qnode* probe = *head;
    struct qnode* prev_to_probe = NULL;
    /* Get the probe to the right position for the 
     * insertion of new node */
    while (probe != NULL && tn->p.occ > probe->tn->p.occ) {
        prev_to_probe = probe;
        probe = probe->next;
    }
    struct qnode* new = queue_create_node(tn);
    new->next = probe;
    /* New node's position is at the start of the queue*/
    if (prev_to_probe == NULL) *head = new;
    /* Tie new node with neighbor */
    else prev_to_probe->next = new;
}

struct tnode* queue_pop(struct qnode** head)
{
    /* Return first element in the queue (highest priority)
     * and reassign head freeing node from the heap (the pointer to the
     * tree node is stil valid and points to occupied memory)*/
    assert(*head != NULL && "Trying to pop from an empty queue.");
    /* Save pointer to tree node before freeing the qnode which carries it */
    struct tnode* tn = (*head)->tn;
    /* Save poniter to second queue node which will become the head */
    struct qnode* second_node = (*head)->next;
    free(*head);
    *head = second_node;
    return tn;
}

void free_tree(struct tnode* root)
{
    /* This function traverses the tree till it found a leaf, it
     * free the leaf, then it step back and visit all the possible
     * branches. Once we know that a node is no more needed because 
     * we visited all of its child than it can be freed */
    if (root == NULL) return;
    if (root->left == NULL && root->right == NULL) return free(root);
    free_tree(root->left);
    free_tree(root->right);
    free(root);
}

void free_encodings(char* encodings[ASCII_SIZE])
{
    /* Free the characters that belongs to the alphabet */
    for (size_t i = 0; i < ASCII_SIZE; ++i) {
        if (encodings[i] != NULL) {
            free(encodings[i]);
        }
    }
}

double calculate_entropy(size_t occurences[], const char *text)
{
    /* To calculate the entropy of a stream of bytes use the formula:
     * H = -sum(p_i * log2(p_i)) */
    size_t text_len = strlen(text);
    double entropy = .0f;
    for (size_t i = 0; i < ASCII_SIZE; ++i) {
        if (occurences[i] > 0) {
            double probability = (double)occurences[i] / text_len;
            entropy -= probability * log2(probability);
        }
    }
    return entropy;
}

double calculate_code_length_avg(char* encodings[ASCII_SIZE], size_t occurences[], size_t total_chars)
{
    /* This function compute the weighted avg of the codes length, basically, given
     * a list of strings it computes the weighted sum */
    double weighted_sum = 0.0;
    for (size_t i = 0; i < ASCII_SIZE; ++i) {
        if (encodings[i] != NULL) {
            double probability = (double)occurences[i] / total_chars;
            weighted_sum += probability * strlen(encodings[i]);
        }
    }
    return weighted_sum;
}

size_t get_alphabet_size(size_t occurences[ASCII_SIZE])
{
    size_t alphabet_size = 0;
    for (size_t i = 0; i < ASCII_SIZE; ++i) {
        if (occurences[i] > 0) alphabet_size++;
    }
    return alphabet_size;
}

size_t compute_max_code_len(struct tnode* root)
{
    /* This function is responsable for the computation of
     * the code length. It basically find the longest steps required
     * to get from the root the the farthest leaf */
    if (root == NULL) return 0;
    if (root->left == NULL && root->right == NULL) return 0;

    size_t left_height = compute_max_code_len(root->left);
    size_t right_height = compute_max_code_len(root->right);

    return 1 + (left_height > right_height ? left_height : right_height);
}

void dump_analytics(char *encodings[ASCII_SIZE], size_t occurences[ASCII_SIZE], const char *text)
{
    /* Test the effectivness of the encoding */
    double entropy = calculate_entropy(occurences, text);
    size_t text_len = strlen(text);
    double length_avg = calculate_code_length_avg(encodings, occurences, text_len);

    printf("Length avg: %lf\n", length_avg);
    printf("Entropy:    %lf\n", entropy);

    double efficiency = entropy / length_avg;
    printf("Encoding efficiency: %lf%%\n", efficiency * 100);

    /* Verify Shannon theorem */
    assert(length_avg >= entropy && length_avg < entropy + 1);
}

void count_occurences(size_t occurences[], const char *text)
{
    /* Only 7-bit ASCII characters are allowed
     * ASCII code of the character is used for occurences indexing, in
     * this way the table of occurences is accessible in O(1) */
    size_t text_len = strlen(text);
    for (size_t i = 0; i < text_len; occurences[(int)text[i++]] += 1);
}

struct qnode* create_alphabet(const char *text, size_t occurences[ASCII_SIZE])
{
    /* Insert tree nodes into a priority queue to sort them in 
    * descending order based on .occ field. Offer a tree node
    * only if the symbols belong to the text alphabet (.occ > 0).
    * This operation prepares a priority queue that will be used
    * to settle the leaves of the huffman tree */
    struct qnode* nodes = NULL;
    struct pair p;
    for (size_t i = 0; i < ASCII_SIZE; ++i) {
        if (occurences[i] > 0) {
            p = (struct pair) { .ch = i, .occ = occurences[i] };
            struct tnode* tn = tree_create_node(p, NULL, NULL);
            queue_offer(&nodes, tn);
        }
    }

    return nodes;
}

struct tnode* build_tree(struct qnode* nodes)
{
    /* Build up the huffman tree, pop 2 nodes and offer to the
     * queue a new node which is the parent of popped nodes,
     * the parent node carries a .occ which is the sum of child's 
     * .occ. Continue the process till one node remains in
     * the queue, the last remaining node is the root of the huffman tree */
    struct pair p;
    struct tnode *t1, *t2, *tn;
    while (nodes->next != NULL) {
        t1 = queue_pop(&nodes);
        t2 = queue_pop(&nodes);
        p = (struct pair) {.ch = -1, .occ = t1->p.occ + t2->p.occ};
        tn = tree_create_node(p, t1, t2);
        queue_offer(&nodes, tn);
    }
    struct tnode* root = nodes->tn;
    /* nodes contains only one element, it can be freed
     * because it won't be used for next steps */
    free(nodes);
    return root;
}

void encode_alphabet(struct tnode* root, char* encodings[ASCII_SIZE], char* code)
{
    /* Recursive function for alphabet encoding. This function traverse the 
     * tree reaching the leaves, in the process a string is carried between
     * function calls which keeps track of the current path.
     * In order to build the code for a leaf it is required to traverse
     * the tree till the leaf and concatenate to the code, on each level,
     * a 1 if right node is next or 0 if left node is next */
    if (root->left == NULL && root->right == NULL) {
        /* The current root is a leaf, save code to encodings table*/
        encodings[root->p.ch] = malloc(strlen(code) + 1);
        assert(encodings[root->p.ch] != NULL && "Call to malloc() failed.");
        strcpy(encodings[root->p.ch], code);
        // printf("(%c) => %s\n", root->p.ch, encodings[root->p.ch]);
        return;
    }
    strcat(code, "0");
    encode_alphabet(root->left, encodings, code);
    code[strlen(code)-1] = '\0';
    strcat(code, "1");
    encode_alphabet(root->right, encodings, code);
    /* Strip last character after righe node because
     * the execution is going up by one level in the tree */
    code[strlen(code)-1] = '\0';
}

void generate_encodings(char* encodings[ASCII_SIZE], struct tnode *root) {
    /* Generate codes to represents all the symbols in the alphabet */
    size_t max_code_len = compute_max_code_len(root) + 1;

    /* Reserve a memory space for keeping track of position in the tree.
     * the memory must be zeroed, this space will be used for
     * alphabet encoding which will concatenate a bit or remove the last one
     * based on the edges path */
    char *code = calloc(max_code_len, sizeof(char));
    encode_alphabet(root, encodings, code);
    free(code);
}

char* read_content_from_file(const char* file_path)
{
    FILE *fp = fopen(file_path, "r");
    if (fp == NULL) return NULL;
    char *text = NULL;

    /* Compute length of file */
    long length;
    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    text = calloc(length + 1, sizeof(char));
    if (text == NULL) return NULL;
    /* Read lenght bytes from fp, 1 byte at a time
     * and put the content into text */
    fread(text, 1, length, fp);
    fclose(fp);
    return text;
}

char *build_comp_file_path(const char *file_path)
{
    /* Build compressed file path prepending a 'z' to file_path name,
     * must distinguish two cases: file name and file path */
    char *comp_file_path = calloc(strlen(file_path)+2, sizeof(char));
    strcpy(comp_file_path, file_path);
    size_t i = strlen(file_path)-1;
    while (i != 0 && file_path[i] != '/') i--;
    /* prepend 'z' to file name */
    size_t j = strlen(comp_file_path);
    comp_file_path[j+1] = '\0';
    while (j > i) {
        comp_file_path[j] = comp_file_path[j-1];
        j--;
    }
    if (i == 0) comp_file_path[i] = 'z';
    else comp_file_path[i+1] = 'z';

    /* strip extension from file name */
    size_t len = strlen(comp_file_path);
    while (i < len && comp_file_path[i] != '.') i++;
    comp_file_path[i] = '\0';
    return comp_file_path;
}

void write_byte(FILE *fp, char *buffer, size_t *b)
{
    /* Build a byte by taking the first 8 bits of the buffer
     * for each bit treated as a char, perform bitwise operations
     * in order to build the byte that will be written to output file */
    size_t i;
    int byte = 0;
    for (i = 0; i < 8; ++i) {
        byte |= ((int)(buffer[i] - 48) << (7-i));
    }
    fputc(byte, fp);

    /* Once a byte has been written, it's important to
     * consume the byte from the buffer, therefore 
     * proceed by shifting the remaining bits by 8 position */
    for (i = 8; buffer[i] != '\0'; ++i) buffer[i-8] = buffer[i];
    buffer[i-8] = '\0';
    *b -= 8;
}

void push_byte(char byte, char *buffer, size_t *b)
{
    /* Iterate through byte, obtain bits and push them to the buffer */
    size_t i;
    size_t bit_position;
    char bit;

    for (i = (*b); i < *b + 8; ++i) {
        bit_position = 7 - (i - (*b));
        /* Extract bits from byte, going from MSB to LSB */
        bit = ((byte >> bit_position) & 1) + 48;
        buffer[i] = bit;
    }
    buffer[i] = '\0';
    *b += 8;
}

/* This macro allows to consume most bytes of the buffer leaving
 * some bits that can't form a byte, it also increment bytes count */
#define CONSUME_BYTES(fp, buffer, b, bytes_count) do { \
    while (b >= 8) {\
        write_byte(fp, buffer, &b);\
        bytes_count++;\
    }\
} while (0)

size_t write_encoded_text(const char *file_path, char *encodings[ASCII_SIZE], const char *text, size_t alphabet_size)
{
    FILE *fp = fopen(file_path, "wb");
    assert(fp != NULL && "Unable to open file for writing");

    /* Fill the buffer with the sequence of bits that will be
     * written to the binary file. For this implementation
     * each bit occupies an entire byte
     * WARNING: allocated size take into account that for
     * each time you push something to the buffer, then
     * you write most bytes by consuming the just added bits */
    size_t b = 0;
    size_t written_bytes = 0;
    char* buffer = calloc(ASCII_SIZE, sizeof(char));

    /* push the magic number */
    for (size_t i = 0; i < 3; ++i)
        push_byte((char)MAGIC_NUMBER, buffer, &b);
    CONSUME_BYTES(fp, buffer, b, written_bytes);

    /* push the alphabet_size */
    push_byte((char)alphabet_size, buffer, &b);
    /* write the entire alphabet in the following format: 
     * <character><code_len><code>
     * ^ 8 bit    ^ 8 bit   ^ code_len bit*/
    for (size_t i = 0; i < ASCII_SIZE; ++i) {
        if (encodings[i] != NULL) {
            // printf("(%c) => %s\n", (int) i, encodings[i]);
            push_byte((char)i, buffer, &b);

            /* code_len is the number of bit required to store the code */
            int code_len = strlen(encodings[i]);
            push_byte((char)code_len, buffer, &b);

            strcat(buffer, encodings[i]);
            b += code_len;
            CONSUME_BYTES(fp, buffer, b, written_bytes);
        }
    }

    /* Proceed to write the encoded text using the
     * encodings table, keep in mind to write
     * bytes every once in a while in order to
     * prevet reaching the limit size for the buffer */
    size_t text_len = strlen(text);
    for (size_t i = 0; i < text_len; ++i) {
        strcat(buffer, encodings[(int)text[i]]);
        b += strlen(encodings[(int)text[i]]);
        CONSUME_BYTES(fp, buffer, b, written_bytes);
    }

    int padding = 0;
    if (b != 0){
        /* Add padding to the buffer in order to write
         * a proper byte to the file */
        while (b < 8) {
            buffer[b++] = '0';
            padding++;
        }
        buffer[b] = '\0';
    }

    /* Push last byte which is the padding used for previous
     * byte in order to reach 8 bits */
    push_byte((char)padding, buffer, &b);
    CONSUME_BYTES(fp, buffer, b, written_bytes);
    assert(b == 0 && strlen(buffer) == 0);

    free(buffer);
    fclose(fp);

    return written_bytes;
}


/* STUFFS FOR DECODING 
 *
 *
 * */

/* Dynamic buffer, this struct is used to extract bits
 * from the compressed file and decode them contemporarily */
struct dbuffer {
    u_int64_t capacity;
    u_int64_t size;
    char *content;
};

struct dbuffer *dbuffer_create(u_int64_t initial_capacity)
{
    /* Create dynamic buffer with initial_capacity and initialize 
     * content string with zeros */
    assert(initial_capacity != 0 && "Cannot initialize capacity with 0 value");
    struct dbuffer *buf = malloc(sizeof(struct dbuffer));
    assert(buf != NULL && "Call to malloc() failed.");
    buf->capacity = initial_capacity;
    buf->size = 0;
    buf->content = calloc(initial_capacity, sizeof(char));
    assert(buf->content != NULL && "Call to calloc() failed.");
    return buf;
}

void dbuffer_append(struct dbuffer *buf, char c)
{
    /* Append character to string and expand content capacity if needed */
    assert(buf != NULL && "Tried to append to an uninitialized buffer");
    if (buf->size == buf->capacity) {
        buf->content = realloc(buf->content, buf->capacity*2);
        assert(buf->content != NULL && "Call to realloc() failed.");
        buf->capacity <<= 1;
    }
    buf->content[buf->size++] = c;
}

typedef __uint128_t buffer;

void buffer_append_byte(buffer *buf, char c, char *buf_size)
{
    (*buf) <<= 8;
    *buf += c;
    *buf_size += 8;
}

char buffer_retrieve_byte(buffer *buf, char *buf_size)
{
    char c = (char) *buf;
    (*buf) >>= 8;
    *buf_size -= 8;
    return c;
}

size_t buffer_retrieve_bits(buffer *buf, size_t nbits, char *buf_size)
{
    /* Maximum number for buf_size is 64 */
    buffer res = (((buffer)-1 << (*buf_size-nbits)));
    res &= *buf;
    res >>= (*buf_size-nbits);

    buffer mask = (buffer)-1 >> ((sizeof(buffer)*8)-(*buf_size-nbits));
    (*buf) &= mask;

    *buf_size -= nbits;
    return res;
}

char *bits_to_string(buffer bits, int nbits)
{
    char *str = calloc(nbits, sizeof(char));
    buffer j = 0;
    for (int i = nbits-1; i >= 0; i--) {
        str[j++] = ((bits >> i) & 1) + 48;
    }
    str[j] = '\0';
    return str;
}

int main(int argc, char **argv)
{
    int res = validate_args(argc, argv);
    if (res != 0) return 1;

    const char *file_path = argv[2];
    switch (argv[1][0]) {
        case 'c':
            {
                const char *text = read_content_from_file(file_path);
                if (text == NULL) {
                    fprintf(stderr, "ERROR: Unable to open '%s'.\n"USAGE, file_path);
                    return 1;
                }

                const char *comp_file_path = build_comp_file_path(file_path);

                /* STEP 1: find occurences of each character in the input text and
                 * build up a priority queue of tree leaves */
                printf("Starting compression...\n");
                size_t occurences[ASCII_SIZE] = {0};
                count_occurences(occurences, text);
                struct qnode* nodes = create_alphabet(text, occurences);

                /* If we didn't managed to put at least one node in the queue,
                 * then the text input must be empty */
                if (nodes == NULL) {
                    printf("Input text is empty.\n");
                    return 1;
                }

                /* STEP 2: Build the huffman tree by popping, for each iteration,
                 * the two nodes that have lowest occurrences and push the parent
                 * node that will store the sum of child's occurences */
                printf("Building huffman tree...\n");
                struct tnode *root = build_tree(nodes);

                /* STEP 3: Generate the encodings for each character of the
                 * alphabet by traversing the tree and memorizing the path
                 * by concateneting a '0' (left child) or a '1' (right child) 
                 * to the code for a specified character */
                printf("Generating encodings...\n");
                char *encodings[ASCII_SIZE] = {0};
                generate_encodings(encodings, root);
                dump_analytics(encodings, occurences, text);

                /* STEP 4: Write stream of bits to a binary file following
                 * the decided format */
                printf("Writing to '%s'...\n", comp_file_path);
                size_t alphabet_size = get_alphabet_size(occurences);
                size_t written_bytes = write_encoded_text(comp_file_path, encodings, text, alphabet_size);
                if (written_bytes == 0) {
                    fprintf(stderr, "Impossible to write file\n");
                    return 1;
                }
                printf("Done... %zu bytes written\n", written_bytes);

                /* Free shit out */
                free_tree(root);
                free_encodings(encodings);
                free((char*)text);
                free((char*)comp_file_path);
            }
            break;
        case 'd':
            {
                FILE *fp = fopen(file_path, "rb");
                if (fp == NULL) {
                    fprintf(stderr, "ERROR: Unable to open '%s'.\n"USAGE, file_path);
                    return 1;
                }

                printf("Starting decompression...\n");
                /* Buffer data type is a 16 byte unsigned integer */
                buffer buf = 0;
                /* Buf size has to deal with 128 maximum buf length, therefore a
                 * char type is just fine */
                char buf_size = 0;

                /* MAGIC NUMBER */
                for (char i = 0; i < 3; ++i) {
                    buffer_append_byte(&buf, fgetc(fp), &buf_size);
                    if (buffer_retrieve_byte(&buf, &buf_size) != MAGIC_NUMBER) {
                        fprintf(stderr, "ERROR: Unknown bytes, provide a valid compressed binary file\n");
                        return 1;
                    }
                }

                /* ALPHABET SIZE */
                buffer_append_byte(&buf, fgetc(fp), &buf_size);
                char alphabet_size = buffer_retrieve_byte(&buf, &buf_size);
                printf("Alphabet size: %d\n", (int) alphabet_size);

                char character, code_len;
                size_t code;
                for (char i = 0; i < alphabet_size; ++i) {
                    /* CHARACTER */
                    buffer_append_byte(&buf, fgetc(fp), &buf_size);
                    character = buffer_retrieve_bits(&buf, 8, &buf_size);
                    printf("char: %c\n", character);

                    /* CODE LENGTH */
                    buffer_append_byte(&buf, fgetc(fp), &buf_size);
                    code_len = buffer_retrieve_bits(&buf, 8, &buf_size);
                    printf("code_len: %d\n", code_len);

                    /* ACTUAL CODE STRING */
                    while (buf_size < code_len) buffer_append_byte(&buf, fgetc(fp), &buf_size);
                    code = buffer_retrieve_bits(&buf, code_len, &buf_size);
                    printf("code: %s\n", bits_to_string(code, code_len));

                    /* READ THIS
                     * Create hashmap implementation to store key-value pair as follows
                     * key: code, value: character 
                     * in this way you can then extract the corrisponding character for
                     * a given code in O(1) */

                    printf("buffer: %s\n", bits_to_string(buf, 128));
                }

                printf("buf_size: %d\n", buf_size);
                /* START READING STREAM A CODES */

                fclose(fp);
            }
            break;
        default:
            fprintf(stderr, "ERROR: Invalid command.\n"USAGE);
            return 1;

    }

    return 0;
}
