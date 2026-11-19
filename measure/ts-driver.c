#include <stdio.h>
#include <stdlib.h>
#include <tree_sitter/api.h>
extern const TSLanguage *LANGFN(void);
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) return 1;
    fclose(f);
    TSParser *p = ts_parser_new();
    ts_parser_set_language(p, LANGFN());
    TSTree *t = ts_parser_parse_string(p, NULL, buf, (uint32_t)n);
    printf("children=%u\n", ts_node_child_count(ts_tree_root_node(t)));
    ts_tree_delete(t);
    ts_parser_delete(p);
    free(buf);
    return 0;
}
