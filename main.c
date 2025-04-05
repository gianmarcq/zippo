#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

/* 7-bit ASCII supported*/
#define ASCII_SIZE 1u << 7

/* A pair contains a tuple of 2 elements
 * (character, number_of_occurences) */
struct pair { int ch; size_t occ; } p;


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

void count_occurences(size_t occurences[], const char *text)
{
    /* Only 7-bit ASCII characters are allowed
     * ASCII code of the character is used for occurences indexing, in
     * this way the table of occurences is accessible in O(1) */
    size_t text_len = strlen(text);
    for (size_t i = 0; i < text_len; occurences[(int)text[i++]] += 1);
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
    /* This function compute the weighted avg of the codes length */
    double weighted_sum = 0.0;
    for (size_t i = 0; i < ASCII_SIZE; ++i) {
        if (encodings[i] != NULL) {
            double probability = (double)occurences[i] / total_chars;
            weighted_sum += probability * strlen(encodings[i]);
        }
    }
    return weighted_sum;
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
        strcpy(encodings[root->p.ch], code);
        // printf("%zu\n", strlen(encodings[root->p.ch]));
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

char* read_content_from_file(const char* file_path)
{
    FILE *fp = fopen(file_path, "r");
    char *text = NULL;

    /* Compute length of file */
    long length;
    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    text = calloc(length + 1, sizeof(char));
    assert(text != NULL && "Call to malloc() failed.");
    /* Read lenght bytes from fp, 1 byte at a time
     * and put the content into text */
    fread(text, 1, length, fp);
    fclose(fp);
    return text;
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

void write_byte(FILE *fp, char *buffer, size_t *b)
{
    size_t i;
    int byte = 0;
    printf("Writing ");
    for (i = 0; i < 8; ++i) {
        printf("%c", buffer[i]);
        byte |= ((int)(buffer[i] - 48) << (7-i));
    }
    printf("\n");
    fputc(byte, fp);

    /* Shift remaining bits by 8 position */
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
        bit = ((byte >> bit_position) & 1) + 48;
        buffer[i] = bit;
    }
    buffer[i] = '\0';
    *b += 8;
}

int main(void)
{
    const char *file_path = "mussolini_speech.txt";
    const char *text = read_content_from_file(file_path);

    size_t occurences[ASCII_SIZE] = {0};
    struct qnode* nodes = NULL;
    count_occurences(occurences, text);

    /* Insert tree nodes into a priority queue to sort them in 
    * descending order based on .occ field. Offer a tree node
    * only if the symbols belong to the text alphabet (.occ > 0).
    * This operation prepares a priority queue that will be used
    * to settle the leaves of the huffman tree */
    struct pair p;
    size_t alphabet_size = 0;
    for (size_t i = 0; i < ASCII_SIZE; ++i) {
        if (occurences[i] > 0) {
            p = (struct pair) { .ch = i, .occ = occurences[i] };
            struct tnode* tn = tree_create_node(p, NULL, NULL);
            queue_offer(&nodes, tn);
            alphabet_size++;
        }
    }

    /* If we didn't managed to put at least one node in the queue,
     * then the text input must be empty */
    if (nodes == NULL) {
        printf("Text input is empty.\n");
        return 0;
    }

    /* Build up the huffman tree, pop 2 nodes and offer to the
     * queue a new node which is the parent of popped nodes,
     * the parent node carries a .occ which is the sum of child's 
     * .occ. Continue the process till one node remains in
     * the queue, the last remaining node is the root of the huffman tree */
    struct tnode *t1, *t2, *tn;
    while (nodes->next != NULL) {
        t1 = queue_pop(&nodes);
        t2 = queue_pop(&nodes);
        p = (struct pair) {.ch = -1, .occ = t1->p.occ + t2->p.occ};
        tn = tree_create_node(p, t1, t2);
        queue_offer(&nodes, tn);
    }

    /* Generate codes to represents all the symbols in the alphabet */
    struct tnode* root = nodes->tn;
    size_t max_code_len = compute_max_code_len(root) + 1;
    char *encodings[ASCII_SIZE] = {0};

    /* Reserve a memory space for keeping track of position in the tree.
     * the memory must be zeroed, this space will be used for
     * alphabet encoding which will concatenate a bit or remove the last one
     * based on the edges path */
    char *code = calloc(max_code_len, sizeof(char));
    encode_alphabet(root, encodings, code);

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

    FILE *fp = fopen("comp", "wb");
    assert(fp != NULL && "Unable to open file for writing");

    /* Fill the buffer with the sequence of bits that will be
     * written to the binary file. For this implementation
     * each bit occupies an entire byte 
     * WARNING: allocated size take into account that for
     * each time you push something to the buffer, then
     * you write most bytes by consuming the just added bits */
    size_t b = 0;
    char* buffer = calloc(ASCII_SIZE, sizeof(char));

    /* push the alphabet_size */
    push_byte((char)alphabet_size, buffer, &b);
    /* write the entire alphabet in the following format: 
     * <character><code_len><code>
     * ^ 8 bit    ^ 8 bit   ^ code_len bit*/
    for (size_t i = 0; i < ASCII_SIZE; ++i) {
        if (encodings[i] != NULL) {

            push_byte((char)i, buffer, &b);

            /* code_len is the number of bit required to store the code */
            int code_len = strlen(encodings[i]);
            push_byte((char)code_len, buffer, &b);

            strcat(buffer, encodings[i]);
            b += code_len;
            while (b >= 8) write_byte(fp, buffer, &b);
        }
    }

    /* Proceed to write the encoded text using the
     * encodings table, keep in mind to write
     * bytes every once in a while in order to
     * prevet reaching the limit size for the buffer */
    for (size_t i = 0; i < text_len; ++i) {
        strcat(buffer, encodings[text[i]]);
        b += strlen(encodings[text[i]]);
        while (b >= 8) write_byte(fp, buffer, &b);
    }

    /* Make sure to write remaining bits */
    while (b < 8) buffer[b++] = '0';
    buffer[b] = '\0';
    while (b >= 8) write_byte(fp, buffer, &b);
    assert(b == 0 && strlen(buffer) == 0);

    fclose(fp);

    /* Free shit out */
    free_tree(root);
    free_encodings(encodings);

    free(buffer);
    free((char*)text);
    free(nodes);
    free(code);
    return 0;
}
