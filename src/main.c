#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "log.h"
#include "parser.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        ERROR("Missing input file!");
        exit(1);
    }
    
    FILE *src_file = fopen(argv[1], "r");
    if (!src_file) {
        ERROR("Failed to open source file %s", argv[1]);
        exit(1);
    }

    //find the size of the file
    fseek(src_file, 0, SEEK_END);
    long size = ftell(src_file);
    fseek(src_file, 0, SEEK_SET);

    char *src_data = malloc(size + 1);
    if (!src_data) {
        ERROR("Failed to allocate buffer for source dataof size %ld", size);
        exit(1);
    }

    size_t read = fread(src_data, 1, size, src_file);
    if (read != size) {
        ERROR("Failed to read entire file");
        exit(1);
    }

    src_data[size] = '\0';

    Token *tokens;
    size_t num_tokens = parse(src_data, &tokens);
    for (size_t i = 0; i < num_tokens; i++) {
        Token c = tokens[i];
        switch (c.type) {
            case ASM: puts("ASM");
            default: break;
        }
    }

    return 0;
}