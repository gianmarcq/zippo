#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define ASCII_SIZE 1u << 7 // 7-bit ASCII
/* Struct for priority queue implementation */
struct qnode {
    struct qnode* next;
    struct pair {
        int ch;
        size_t occ;
    } p;
};

struct qnode* queue_create_node(struct pair p)
{
    /* Create a new node with provided pair 
     * Primarly used by other queue functions */
    struct qnode* new = malloc(sizeof(struct qnode));
    assert(new != NULL && "Call to malloc() failed.");
    new->next = NULL;
    new->p = p;
    return new;
}

void queue_offer(struct qnode** head, struct pair p)
{
    /* Insert a new node in the queue ensuring its
     * correct position. Lower values of .occ field
     * have higher priority */
    struct qnode* probe = *head;
    struct qnode* prev_to_probe = NULL;
    /* Get the probe to the right position for the 
     * insertion of new node */
    while (probe != NULL && p.occ > probe->p.occ) {
        prev_to_probe = probe;
        probe = probe->next;
    }
    struct qnode* new = queue_create_node(p);
    new->next = probe;
    /* New node's position is at the start of the queue*/
    if (prev_to_probe == NULL) *head = new;
    /* Tie new node with neighbor */
    else prev_to_probe->next = new;
}

struct qnode queue_pop(struct qnode** head)
{
    /* Return first element in the queue (highest priority)
     * and reassign head freeing returned node from the heap */
    assert(head != NULL && "Trying to pop from an empty queue.");
    /* Save first node before freeing it */
    struct qnode nd = **head;
    struct qnode* second_node = (*head)->next;
    free(*head);
    *head = second_node;
    return nd;
}

/* Struct for priority queue implementation */
struct tnode {
    struct tnode *left, *right;
    struct pair p;
};

struct tnode* tree_create_node(struct pair p, struct tnode* left, struct tnode* right)
{
    /* Create a new node for the huffman tree, leaves don't have childs
     * so left and right fields are just NULLs */
    struct tnode* new = malloc(sizeof(struct tnode));
    assert(new != NULL && "Call to malloc() failed.");
    new->left = left;
    new->right = right;
    new->p = p;
    return new;
}

void count_occurences(size_t occurences[], const char *text)
{
    /* Only 7-bit ASCII characters are allowed
     * ASCII code of the character is used for occurences indexing, in
     * this way the table of occurences is accessible in O(1) */
    size_t text_len = strlen(text);
    for (size_t i = 0; i < text_len; occurences[(int)text[i++]] += 1);
}

void encode_alphabet(struct tnode* root, char encodings[][8], char code[8])
{
    /* Recursive function for alphabet encoding. This function traverse the 
     * tree reaching the leaves, in the process a string is carried between
     * function calls which keeps track of the current path.
     * In order to build the code for a leaf it is required to traverse
     * the tree till the leaf and concatenate to code, on each level,
     * a 1 if right node is next or 0 if left node is next */

    if (root->left == NULL && root->right == NULL) {
        /* The current root is a leaf, save code to
         * encodings table*/
        strcpy(encodings[root->p.ch], code);
        printf("(%c) => %s\n", root->p.ch, code);
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

// void dump_tree(struct tnode* root)
// {
//     if (root == NULL) return;
//     printf("(%c) => %zu\n", root->p.ch, root->p.occ);
//     dump_tree(root->left);
//     dump_tree(root->right);
// }

int main(void)
{
    size_t occurences[ASCII_SIZE] = {0};
    const char *text = "AAAAbbCCCddd";
    // const char *text = "BIG BOB BITES BANANAS";
    // const char *text = "The quick brown fox jumps over the lazy dog";
    struct qnode* head = NULL;

    count_occurences(occurences, text);
    /* Insert occurences into a priority queue to sort them in 
    * descending order based on .occ field. This prepares a ds
    * useful for building the huffman tree */
    struct pair p;
    for (size_t i = 0; i < ASCII_SIZE; ++i) {
        if (occurences[i] != 0) {
            p = (struct pair) { .ch = i, .occ = occurences[i] };
            queue_offer(&head, p);
        }
    }

    struct qnode* probe = head;
    while (probe != NULL) {
        printf("(%c) => %zu\n", probe->p.ch, probe->p.occ);
        probe = probe->next;
    }

    /* Dumb stack for managing pending roots */
    size_t root_stack_index = 0;
    struct tnode* root_stack[ASCII_SIZE] = {0};

    struct qnode n1, n2;
    struct tnode *left, *right;

    while (head->next != NULL) {
        n1 = queue_pop(&head);
        n2 = queue_pop(&head);
        /* Offer to queue a new pair which does not carry a character */
        p = (struct pair) {.ch = -1, .occ = n1.p.occ + n2.p.occ};
        queue_offer(&head, p);

        // getchar();
        struct qnode* probe = head;
        printf("=========================================\n");
        while (probe != NULL) {
            printf("(%c) => %zu\n", probe->p.ch, probe->p.occ);
            probe = probe->next;
        }

        /* Tree generation for each pair of highest priority nodes.
         * NOTE: n1.occ is always lower than n2.occ
         * In the following lines of codes the sequence of instruction
         * is mandatory in order to create a tree where the right most value
         * is the lowest. In general, for each node, the value of right child
         * is lower than the value of left child.
         *
         * n2 comes first because when dealing with summed occurences (second
         * branch of if statement) the popped root from the stack is the sum
         * of the last two occurences encoutered and it is definetly
         * the left value (a value greater than right child value)

         * Popped pair is a leaf */
        if (n2.p.ch != -1) {
            printf("LEFT: added leaf\n");
            left = tree_create_node(n2.p, NULL, NULL);
        }
        /* Popped pair is a sum of occurences */
        else {
            left = root_stack[--root_stack_index];
            printf("LEFT: added sum (%zu)\n", left->p.occ);
        }

        if (n1.p.ch != -1) {
            printf("RIGHT: added leaf\n");
            right = tree_create_node(n1.p, NULL, NULL);
        }
        else {
            right = root_stack[--root_stack_index];
            printf("RIGHT: added sum (%zu)\n", right->p.occ);
        }

        /* Add root to stack.
         * This operation is required in order to preserve the roots, which will
         * not be reassigned in the next iteration. A clear example that demonstrate
         * the utilization of this concept in the algorithm is the following: 
         * A sequence of pairs where all .occ are equals e.g. a list
         * of symbols that occurs just one time in the text, in this scenario the algorithm
         * will continue to add leafs until it reaches the end and so it starts
         * tying togheter the nodes */
        root_stack[root_stack_index++] = tree_create_node(p, left, right);
    }
    printf("root_stack_index = %zu\n", root_stack_index);

    /* Generate encoding for text alphabet */
    struct tnode* root = root_stack[0];
    // printf("(%c) => %zu\n", root->p.ch, root->p.occ);
    //     printf("(%c) => %zu\n", root->left->p.ch, root->left->p.occ);
    //         printf("(%c) => %zu\n", root->left->left->p.ch, root->left->left->p.occ);
    //         printf("(%c) => %zu\n", root->left->right->p.ch, root->left->right->p.occ);
    //     printf("(%c) => %zu\n", root->right->p.ch, root->right->p.occ);
    //         printf("(%c) => %zu\n", root->right->left->p.ch, root->right->left->p.occ);
    //         printf("(%c) => %zu\n", root->right->right->p.ch, root->right->right->p.occ);

    /* Generate codes to represents all the symbols in the alphabet */
    char encodings[ASCII_SIZE][8] = {0};
    char code[8] = "";
    encode_alphabet(root, encodings, code);
    return 0;
}
