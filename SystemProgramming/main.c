#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "ast.h"
#include "cfg.h"
#include "semantic.h"
#include "calltree.h"
#include "codegen.h"

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

void print_symbol_table_details(SymbolTable* st) {
    if (!st) return;

    printf("\n════════════════════════════════════════════════════════════\n");
    printf("DETAILED SYMBOL TABLE ANALYSIS:\n");
    printf("════════════════════════════════════════════════════════════\n\n");

    /* Общая статистика */
    printf("[STATISTICS]\n");
    printf("  Total symbols: %d\n", st->symbol_count);
    printf("  Scopes: %d\n", st->scope_count);
    printf("  Global offset (next available): %d\n", st->global_offset);
    printf("  Error count: %d\n\n", st->error_count);

    /* Информация по областям видимости */
    printf("[SCOPES]\n");
    for (int i = 0; i < st->scope_count; i++) {
        Scope* scope = st->scopes[i];
        printf("  Scope %d: ", scope->id);
        switch (scope->type) {
        case SCOPE_GLOBAL: printf("GLOBAL"); break;
        case SCOPE_FUNCTION: printf("FUNCTION"); break;
        case SCOPE_BLOCK: printf("BLOCK"); break;
        }
        printf(" '%s'", scope->name ? scope->name : "(unnamed)");
        printf(" (level: %d, parent: %d", scope->level,
            scope->parent ? scope->parent->id : -1);
        printf(", local_offset: %d, param_offset: %d)\n",
            scope->local_offset, scope->param_offset);
    }
    printf("\n");

    /* Глобальные переменные */
    printf("[GLOBAL VARIABLES]\n");
    int global_count = 0;
    for (int i = 0; i < st->symbol_count; i++) {
        Symbol* sym = &st->symbols[i];
        if (sym->type == SYM_GLOBAL) {
            global_count++;
            printf("  %s", sym->name);
            if (sym->data_type) printf(" : %s", sym->data_type);
            if (sym->is_array) printf(" [%d]", sym->array_size);
            printf("  offset: %4d  size: %4d", sym->offset, sym->size);
            printf("  addr: 0x%04X", sym->address);
            printf("  %s", sym->is_initialized ? "[INIT]" : "[UNINIT]");
            printf("  %s", sym->is_used ? "[USED]" : "[NOT USED]");
            printf("\n");
        }
    }
    if (global_count == 0) printf("  (none)\n");
    printf("\n");

    /* Функции */
    printf("[FUNCTIONS]\n");
    int func_count = 0;
    for (int i = 0; i < st->symbol_count; i++) {
        Symbol* sym = &st->symbols[i];
        if (sym->type == SYM_FUNCTION) {
            func_count++;
            printf("  %s", sym->name);
            if (sym->return_type) printf(" -> %s", sym->return_type);
            printf("  params: %d", sym->param_count);
            if (sym->param_types) {
                printf(" (");
                for (int j = 0; j < sym->param_count; j++) {
                    if (j > 0) printf(", ");
                    printf("%s", sym->param_types[j] ? sym->param_types[j] : "?");
                }
                printf(")");
            }
            printf("  %s", sym->is_used ? "[CALLED]" : "[NOT CALLED]");
            printf("\n");
        }
    }
    if (func_count == 0) printf("  (none)\n");
    printf("\n");

    /* Локальные переменные и параметры по областям */
    printf("[LOCAL SYMBOLS BY SCOPE]\n");
    for (int s = 0; s < st->scope_count; s++) {
        Scope* scope = st->scopes[s];
        if (scope->type != SCOPE_GLOBAL) {
            printf("  Scope %d: ", scope->id);
            if (scope->name) printf("'%s'", scope->name);
            printf(" (");
            switch (scope->type) {
            case SCOPE_FUNCTION: printf("FUNCTION"); break;
            case SCOPE_BLOCK: printf("BLOCK"); break;
            default: printf("UNKNOWN");
            }
            printf(")\n");

            /* Параметры */
            int param_count = 0;
            for (int i = 0; i < st->symbol_count; i++) {
                Symbol* sym = &st->symbols[i];
                if (sym->scope_id == scope->id && sym->type == SYM_PARAMETER) {
                    param_count++;
                    printf("    PARAM %s", sym->name);
                    if (sym->data_type) printf(" : %s", sym->data_type);
                    printf("  offset: %+4d", sym->offset);
                    printf("  size: %2d", sym->size);
                    printf("  %s", sym->is_used ? "[USED]" : "[NOT USED]");
                    printf("\n");
                }
            }

            /* Локальные переменные */
            int local_count = 0;
            for (int i = 0; i < st->symbol_count; i++) {
                Symbol* sym = &st->symbols[i];
                if (sym->scope_id == scope->id && sym->type == SYM_LOCAL) {
                    local_count++;
                    printf("    LOCAL %s", sym->name);
                    if (sym->data_type) printf(" : %s", sym->data_type);
                    if (sym->is_array) printf(" [%d]", sym->array_size);
                    printf("  offset: %+4d", sym->offset);
                    printf("  size: %3d", sym->size);
                    printf("  %s", sym->is_initialized ? "[INIT]" : "[UNINIT]");
                    printf("  %s", sym->is_used ? "[USED]" : "[NOT USED]");
                    printf("  %s", sym->is_modified ? "[MODIFIED]" : "[READ ONLY]");
                    printf("\n");
                }
            }

            if (param_count == 0 && local_count == 0) {
                printf("    (no local symbols)\n");
            }
            printf("\n");
        }
    }

    /* Константы */
    printf("[CONSTANTS]\n");
    int const_count = 0;
    for (int i = 0; i < st->symbol_count; i++) {
        Symbol* sym = &st->symbols[i];
        if (sym->type == SYM_CONSTANT) {
            const_count++;
            printf("  %s", sym->name);
            if (sym->data_type) printf(" : %s", sym->data_type);
            printf("  [CONSTANT]\n");
        }
    }
    if (const_count == 0) printf("  (none)\n");
    printf("\n");

    /* Сводка по использованию */
    printf("[USAGE SUMMARY]\n");
    int used_symbols = 0;
    int uninitialized_symbols = 0;
    int unused_symbols = 0;

    for (int i = 0; i < st->symbol_count; i++) {
        Symbol* sym = &st->symbols[i];
        if (sym->is_used) used_symbols++;
        if (!sym->is_initialized &&
            (sym->type == SYM_LOCAL || sym->type == SYM_GLOBAL)) {
            uninitialized_symbols++;
        }
        if (!sym->is_used &&
            sym->type != SYM_FUNCTION &&
            sym->scope->type != SCOPE_GLOBAL &&
            !sym->is_constant) {
            unused_symbols++;
        }
    }

    printf("  Used symbols: %d/%d\n", used_symbols, st->symbol_count);
    printf("  Uninitialized variables: %d\n", uninitialized_symbols);
    printf("  Unused local symbols: %d\n", unused_symbols);

    if (unused_symbols > 0) {
        printf("\n  [WARNING] Unused symbols:\n");
        for (int i = 0; i < st->symbol_count; i++) {
            Symbol* sym = &st->symbols[i];
            if (!sym->is_used &&
                sym->type != SYM_FUNCTION &&
                sym->scope->type != SCOPE_GLOBAL &&
                !sym->is_constant) {
                printf("    - %s (type: %s, scope: %d)\n",
                    sym->name, symbol_get_type_str(sym->type), sym->scope_id);
            }
        }
    }

    printf("\n════════════════════════════════════════════════════════════\n");
}

int main(int argc, char* argv[]) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║      CFG Builder v4.0 - Full Analysis Suite              ║\n");
    printf("║   AST Parser + CFG + Semantic + Call Tree + Code Gen    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    const char* input_file = NULL;
    const char* output_dir = NULL;
    const char* asm_output = NULL;
    int export_asm = 0;

    /* ====================================================================
     * ОБРАБОТКА АРГУМЕНТОВ КОМАНДНОЙ СТРОКИ
     * ==================================================================== */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                output_dir = argv[++i];
            }
            else {
                fprintf(stderr, "[ERROR] -o flag requires an argument\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "-asm") == 0) {
            if (i + 1 < argc) {
                asm_output = argv[++i];
                export_asm = 1;
            }
            else {
                fprintf(stderr, "[ERROR] -asm flag requires an argument\n");
                return 1;
            }
        }
        else {
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "Usage: %s <input_file> [-o output_dir] [-asm asm_file]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s test.txt\n", argv[0]);
        fprintf(stderr, "  %s test.txt -o output -asm output.asm\n", argv[0]);
        return 1;
    }

    if (output_dir == NULL || strlen(output_dir) == 0) {
        output_dir = ".";
    }

    /* ====================================================================
     * СОЗДАНИЕ ВЫХОДНОЙ ДИРЕКТОРИИ
     * ==================================================================== */
    printf("[*] Creating output directory: '%s'\n", output_dir);
    int mkdir_result = create_directory(output_dir);
    if (mkdir_result == 0 || errno == EEXIST) {
        printf("[+] Output directory ready\n");
    }
    else {
        fprintf(stderr, "[ERROR] Failed to create directory\n");
        return 1;
    }

    /* ====================================================================
     * ПАРСИНГ ВХОДНОГО ФАЙЛА
     * ==================================================================== */
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

    /* ====================================================================
     * СЕМАНТИЧЕСКИЙ АНАЛИЗ
     * ==================================================================== */
    printf("[*] Running semantic analysis...\n");
    SymbolTable* symbol_table = symbol_table_create();
    semantic_analyze(root_ast, symbol_table);

    /* ====================================================================
     * ВЫВОД ПОЛНОЙ ТАБЛИЦЫ СИМВОЛОВ
     * ==================================================================== */
    printf("\n════════════════════════════════════════════════════════════\n");
    printf("SYMBOL TABLE - FULL DUMP\n");
    printf("════════════════════════════════════════════════════════════\n");

    /* Компактный формат */
    symbol_table_print(symbol_table);

    /* Подробный формат для отладки */
    print_symbol_table_details(symbol_table);

    /* ====================================================================
     * ПРОВЕРКА ОШИБОК
     * ==================================================================== */
    printf("\n════════════════════════════════════════════════════════════\n");
    printf("INITIAL ERROR SUMMARY:\n");
    printf("════════════════════════════════════════════════════════════\n");

    int total_errors = 0;

    printf("AST Errors:\n");
    total_errors += count_and_print_ast_errors(root_ast);

    if (total_errors == 0) {
        printf("  No AST errors found.\n");
    }

    printf("\nSemantic Errors:\n");
    if (symbol_table->error_count == 0) {
        printf("  No semantic errors found.\n");
    }
    else {
        for (int i = 0; i < symbol_table->error_count; i++) {
            printf("  ✗ %s\n", symbol_table->error_messages[i]);
            total_errors++;
        }
    }

    printf("\n════════════════════════════════════════════════════════════\n");
    if (total_errors > 0) {
        printf("❌ INITIAL ERRORS: %d\n", total_errors);
    }
    else {
        printf("✅ NO INITIAL ERRORS FOUND\n");
    }
    printf("════════════════════════════════════════════════════════════\n");

    /* ====================================================================
     * ВЫВОД AST
     * ==================================================================== */
    printf("\nAST TREE:\n");
    printf("════════════════════════════════════════════════════════════\n");
    dumpAST(root_ast, 0);
    printf("\n");

    /* ====================================================================
     * ЭКСПОРТ AST В DOT ФОРМАТ
     * ==================================================================== */
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

    /* ====================================================================
     * ПОСТРОЕНИЕ CFG
     * ==================================================================== */
    printf("\n[*] Setting up symbol table for CFG analysis...\n");
    cfg_set_symbol_table(symbol_table);

    // Отладочная информация
    printf("  [DEBUG] Symbol table has %d symbols and %d scopes\n",
        symbol_table->symbol_count, symbol_table->scope_count);
    printf("  [DEBUG] Scopes:\n");
    for (int i = 0; i < symbol_table->scope_count; i++) {
        Scope* scope = symbol_table->scopes[i];
        printf("    Scope %d: %s '%s' (level: %d, parent: %d)\n",
            scope->id,
            scope->type == SCOPE_GLOBAL ? "GLOBAL" :
            scope->type == SCOPE_FUNCTION ? "FUNCTION" : "BLOCK",
            scope->name ? scope->name : "(unnamed)",
            scope->level,
            scope->parent ? scope->parent->id : -1);
    }

    printf("[*] Generating Control Flow Graphs...\n");
    CFG* cfg = cfg_create();
    cfg_build_from_ast(cfg, root_ast);

    printf("[+] CFG generated with %d nodes\n", cfg->node_count);
    printf("\n[*] Checking semantics in CFG expressions...\n");
    cfg_check_semantics(cfg, symbol_table);
    printf("[+] Semantic check complete\n");

    printf("\n════════════════════════════════════════════════════════════\n");
    printf("CFG ERROR SUMMARY:\n");
    printf("════════════════════════════════════════════════════════════\n");

    int cfg_errors = 0;
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
    printf("════════════════════════════════════════════════════════════\n");

    /* ====================================================================
     * ЭКСПОРТ CFG В DOT ФОРМАТ
     * ==================================================================== */
    char cfg_dot_file[512];
    build_output_path(output_dir, "cfg_output.dot", cfg_dot_file, sizeof(cfg_dot_file));
    printf("\n[*] Exporting CFG to DOT...\n");
    cfg_export_dot(cfg, cfg_dot_file);
    printf("[+] CFG saved: %s\n", cfg_dot_file);

    /* ====================================================================
     * ПОСТРОЕНИЕ CALL TREE
     * ==================================================================== */
    printf("\n[*] Building call tree...\n");
    CallTree* call_tree = calltree_create();
    //calltree_build_from_ast(call_tree, root_ast);

    char calltree_dot_file[512];
    build_output_path(output_dir, "calltree_output.dot", calltree_dot_file, sizeof(calltree_dot_file));
    printf("[*] Exporting call tree to DOT...\n");
    calltree_export_dot(call_tree, calltree_dot_file);
    printf("[+] Call tree saved: %s\n", calltree_dot_file);

    /* ====================================================================
     * ГЕНЕРАЦИЯ КОДА NOOBIK АРХИТЕКТУРЕ
     * ==================================================================== */
    printf("\n════════════════════════════════════════════════════════════\n");
    printf("CODE GENERATION (NOOBIK Assembly):\n");
    printf("════════════════════════════════════════════════════════════\n");

    if (export_asm && asm_output) {
        char asm_file[512];
        build_output_path(output_dir, asm_output, asm_file, sizeof(asm_file));

        printf("[*] Generating assembly code for NOOBIK architecture...\n");

        CodegenOptions opt = codegen_default_options();
        opt.emit_comments = 1;      // по желанию (комменты в asm)
        opt.emit_start_stub = 1;    // по желанию (_start -> CALL _func_main; HLT)

        int ok = codegen_generate_file(cfg, symbol_table, asm_file, opt);
        if (!ok) {
            fprintf(stderr, "[ERROR] Code generation failed: %s\n", asm_file);
            return 1;
        }

        printf("[+] Assembly generated: %s\n", asm_file);
    }
    else {
        printf("[*] Assembly code generation skipped (use -asm to enable)\n");
    }

    /* ====================================================================
     * ИТОГОВАЯ ИНФОРМАЦИЯ О ГЕНЕРИРУЕМЫХ ФАЙЛАХ
     * ==================================================================== */
    printf("\n════════════════════════════════════════════════════════════\n");
    printf("GENERATED FILES:\n");
    printf("════════════════════════════════════════════════════════════\n");
    printf("  AST:        %s\n", ast_dot_file);
    printf("  CFG:        %s\n", cfg_dot_file);
    printf("  Call Tree:  %s\n", calltree_dot_file);
    if (export_asm && asm_output) {
        char asm_file[512];
        build_output_path(output_dir, asm_output, asm_file, sizeof(asm_file));
        printf("  Assembly:   %s\n", asm_file);
    }

    /* Дополнительная отладочная информация */
    printf("\n[DEBUG INFO]\n");
    printf("  Total symbols in table: %d\n", symbol_table->symbol_count);
    printf("  Memory allocated for symbols: ~%ld bytes\n",
        symbol_table->symbol_count * sizeof(Symbol));
    printf("  Global data section size: %d bytes\n", symbol_table->global_offset);

    /* Статистика по типам символов */
    int globals = 0, locals = 0, params = 0, funcs = 0, consts = 0;
    for (int i = 0; i < symbol_table->symbol_count; i++) {
        switch (symbol_table->symbols[i].type) {
        case SYM_GLOBAL: globals++; break;
        case SYM_LOCAL: locals++; break;
        case SYM_PARAMETER: params++; break;
        case SYM_FUNCTION: funcs++; break;
        case SYM_CONSTANT: consts++; break;
        }
    }
    printf("  Symbol type breakdown:\n");
    printf("    Global variables: %d\n", globals);
    printf("    Local variables: %d\n", locals);
    printf("    Parameters: %d\n", params);
    printf("    Functions: %d\n", funcs);
    printf("    Constants: %d\n", consts);

    printf("\nTO VISUALIZE GRAPHS:\n");
    printf("  dot -Tpng %s -o ast_output.png\n", ast_dot_file);
    printf("  dot -Tpng %s -o cfg_output.png\n", cfg_dot_file);
    printf("  dot -Tpng %s -o calltree_output.png\n", calltree_dot_file);
    printf("\nOr use online: https://dreampuf.github.io/GraphvizOnline/\n");
    printf("════════════════════════════════════════════════════════════\n\n");

    /* ====================================================================
     * ОЧИСТКА ПАМЯТИ
     * ==================================================================== */
    printf("[*] Cleaning up...\n");
    cfg_free(cfg);
    calltree_free(call_tree);
    freeAST(root_ast);
    symbol_table_free(symbol_table);

    printf("[+] Done!\n\n");
    return 0;
}