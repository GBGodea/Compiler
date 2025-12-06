// ============================================================
// ИСПРАВЛЕННЫЙ main.c - ПРАВИЛЬНЫЙ ПОРЯДОК
// ============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "ast.h"
#include "cfg.h"
#include "semantic.h"
#include "calltree.h"

extern int yyparse();
extern FILE* yyin;
extern ASTNode* root_ast;
extern int line_num;

int create_directory(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return 0;
    }

#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

void build_output_path(const char* dir, const char* filename, char* result, int max_len) {
    if (dir == NULL || dir[0] == '\0') {
        snprintf(result, max_len, "%s", filename);
    }
    else {
        char dir_copy[512];
        strncpy(dir_copy, dir, sizeof(dir_copy) - 1);
        dir_copy[sizeof(dir_copy) - 1] = '\0';

        int dir_len = strlen(dir_copy);

        while (dir_len > 0 && (dir_copy[dir_len - 1] == '/' || dir_copy[dir_len - 1] == '\\')) {
            dir_copy[--dir_len] = '\0';
        }

        snprintf(result, max_len, "%s/%s", dir_copy, filename);
    }
}

void dumpAST(ASTNode* node, int indent) {
    if (!node) return;

    for (int i = 0; i < indent; i++) printf("  ");

    const char* type_name = getNodeTypeName(node->type);

    if (node->value) {
        printf("├─ %s: \"%s\"\n", type_name, node->value);
    }
    else {
        printf("├─ %s\n", type_name);
    }

    for (int i = 0; i < node->child_count; i++) {
        dumpAST(node->children[i], indent + 1);
    }
}

// Функция для подсчета и вывода ошибок AST
int count_and_print_ast_errors(ASTNode* node) {
    if (!node) return 0;

    int errors = 0;

    if (node->has_error && node->error_message) {
        printf("  [Line ~%d] %s\n", node->line_number, node->error_message);
        errors++;
    }

    for (int i = 0; i < node->child_count; i++) {
        errors += count_and_print_ast_errors(node->children[i]);
    }

    return errors;
}

int main(int argc, char* argv[]) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║      CFG Builder v2.2 - Full Analysis Suite              ║\n");
    printf("║   AST Parser + CFG + Semantic + Call Tree Analysis       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file> [-o output_dir]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Arguments:\n");
        fprintf(stderr, "  <input_file>    Source file to parse (required)\n");
        fprintf(stderr, "  -o output_dir   Output directory for generated files (optional)\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s test.txt                 # Output to current directory\n", argv[0]);
        fprintf(stderr, "  %s test.txt -o output       # Output to 'output' directory\n", argv[0]);
        fprintf(stderr, "\n");
        return 1;
    }

    const char* input_file = argv[1];
    const char* output_dir = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                output_dir = argv[i + 1];
                i++;
            }
            else {
                fprintf(stderr, "[ERROR] -o flag requires an argument\n");
                return 1;
            }
        }
    }

    if (output_dir != NULL && strlen(output_dir) > 0) {
        printf("[*] Creating output directory: '%s'\n", output_dir);

        int mkdir_result = create_directory(output_dir);
        if (mkdir_result == 0 || errno == EEXIST) {
            printf("[+] Output directory ready\n");
        }
        else {
            fprintf(stderr, "[ERROR] Failed to create directory\n");
            return 1;
        }
    }
    else {
        printf("[*] Output directory: current directory (.)\n");
        output_dir = "";
    }

    FILE* input = fopen(input_file, "r");
    if (!input) {
        fprintf(stderr, "[ERROR] Cannot open input file: %s\n", input_file);
        return 1;
    }

    printf("[*] Reading input file: %s\n", input_file);
    yyin = input;
    line_num = 1;

    printf("[*] Parsing...\n");
    int parse_result = yyparse();
    fclose(input);

    if (parse_result != 0) {
        fprintf(stderr, "\n[ERROR] Parse failed\n");
        return 1;
    }

    if (!root_ast) {
        fprintf(stderr, "[ERROR] No AST generated\n");
        return 1;
    }

    printf("[+] Parse successful! (%d lines)\n\n", line_num);

    printf("[*] Running semantic analysis...\n");
    SymbolTable* symbol_table = symbol_table_create();
    semantic_analyze(root_ast, symbol_table);
    symbol_table_print_errors(symbol_table);

    printf("\n════════════════════════════════════════════════════════════\n");
    printf("INITIAL ERROR SUMMARY:\n");
    printf("════════════════════════════════════════════════════════════\n");

    int total_errors = 0;

    // Проверяем ошибки в AST
    printf("AST Errors:\n");
    // Вместо вложенной функции используем отдельную функцию
    total_errors += count_and_print_ast_errors(root_ast);

    if (total_errors == 0) {
        printf("  No AST errors found.\n");
    }

    // Проверяем необъявленные символы
    printf("\nUndeclared Symbols:\n");
    int undeclared_count = 0;
    for (int i = 0; i < symbol_table->symbol_count; i++) {
        if (!symbol_table->symbols[i].is_declared) {
            printf("  ✗ %s (used but not declared)\n", symbol_table->symbols[i].name);
            undeclared_count++;
        }
    }
    total_errors += undeclared_count;

    if (undeclared_count == 0) {
        printf("  No undeclared symbols.\n");
    }

    printf("\n════════════════════════════════════════════════════════════\n");
    if (total_errors > 0) {
        printf("❌ INITIAL ERRORS: %d\n", total_errors);
    }
    else {
        printf("✅ NO INITIAL ERRORS FOUND\n");
    }

    printf("════════════════════════════════════════════════════════════\n");
    printf("AST TREE:\n");
    printf("════════════════════════════════════════════════════════════\n");
    dumpAST(root_ast, 0);
    printf("\n");

    char ast_dot_file[512];
    build_output_path(output_dir, "ast_output.dot", ast_dot_file, sizeof(ast_dot_file));

    printf("[*] Exporting AST to DOT...\n");
    FILE* ast_dot = fopen(ast_dot_file, "w");
    if (!ast_dot) {
        fprintf(stderr, "[ERROR] Cannot create AST DOT file\n");
        return 1;
    }

    printASTDot(root_ast, ast_dot);
    fclose(ast_dot);
    printf("[+] AST saved: %s\n", ast_dot_file);

    // ✅ ШАयेก 1: УСТАНАВЛИВАЕМ ТАБЛИЦУ СИМВОЛОВ ПЕРЕД ГЕНЕРАЦИЕЙ CFG
    printf("\n[*] Setting up symbol table for CFG analysis...\n");
    cfg_set_symbol_table(symbol_table);

    // ✅ ШАYEK 2: ГЕНЕРИРУЕМ CFG
    printf("[*] Generating Control Flow Graphs...\n");
    CFG* cfg = cfg_create();
    cfg_build_from_ast(cfg, root_ast);

    printf("[+] CFG generated with %d nodes\n", cfg->node_count);

    // ✅ ШАYEK 3: ПРОВЕРЯЕМ СЕМАНТИКУ ВСЕХ ВЫРАЖЕНИЙ (это отмечает ошибки в узлах)
    printf("\n[*] Checking semantics in CFG expressions...\n");
    cfg_check_semantics(cfg, symbol_table);
    printf("[+] Semantic check complete\n");

    printf("\n════════════════════════════════════════════════════════════\n");
    printf("CFG ERROR SUMMARY:\n");
    printf("════════════════════════════════════════════════════════════\n");

    int cfg_errors = 0;

    // Проверяем ошибки в CFG
    printf("CFG Node Errors:\n");
    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];
        if (node->has_error && node->error_message) {
            printf("  [Node %d] %s\n", node->id, node->error_message);
            cfg_errors++;
        }
    }

    if (cfg_errors == 0) {
        printf("  No CFG node errors found.\n");
    }

    printf("\n════════════════════════════════════════════════════════════\n");
    if (cfg_errors > 0) {
        printf("❌ CFG ERRORS: %d\n", cfg_errors);
        printf("❌ TOTAL ERRORS: %d\n", total_errors + cfg_errors);
    }
    else {
        printf("✅ NO CFG ERRORS FOUND\n");
        printf("✅ TOTAL ERRORS: %d\n", total_errors);
    }

    char cfg_dot_file[512];
    build_output_path(output_dir, "cfg_output.dot", cfg_dot_file, sizeof(cfg_dot_file));

    printf("\n[*] Exporting CFG to DOT...\n");
    cfg_export_dot(cfg, cfg_dot_file);
    printf("[+] CFG saved: %s\n", cfg_dot_file);

    printf("\n[*] Building call tree...\n");
    CallTree* call_tree = calltree_create();
    calltree_build_from_ast(call_tree, root_ast);

    char calltree_dot_file[512];
    build_output_path(output_dir, "calltree_output.dot", calltree_dot_file, sizeof(calltree_dot_file));

    printf("[*] Exporting call tree to DOT...\n");
    calltree_export_dot(call_tree, calltree_dot_file);
    printf("[+] Call tree saved: %s\n", calltree_dot_file);

    printf("\n════════════════════════════════════════════════════════════\n");
    printf("GENERATED FILES:\n");
    printf("════════════════════════════════════════════════════════════\n");
    printf("  AST:        %s\n", ast_dot_file);
    printf("  CFG:        %s\n", cfg_dot_file);
    printf("  Call Tree:  %s\n", calltree_dot_file);
    printf("\n");
    printf("TO VISUALIZE:\n");
    printf("  dot -Tpng %s -o ast_output.png\n", ast_dot_file);
    printf("  dot -Tpng %s -o cfg_output.png\n", cfg_dot_file);
    printf("  dot -Tpng %s -o calltree_output.png\n", calltree_dot_file);
    printf("\nOr use online: https://dreampuf.github.io/GraphvizOnline/\n");
    printf("════════════════════════════════════════════════════════════\n\n");

    printf("[*] Cleaning up...\n");
    cfg_free(cfg);
    calltree_free(call_tree);
    freeAST(root_ast);
    symbol_table_free(symbol_table);

    printf("[+] Done!\n\n");

    return 0;
}
