#include "project.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

// ============================================================
// СОЗДАНИЕ ПРОЕКТА
// ============================================================

Project* project_create(void) {
    Project* proj = (Project*)malloc(sizeof(Project));

    proj->max_files = 64;
    proj->files = (SourceFile*)malloc(proj->max_files * sizeof(SourceFile));
    proj->file_count = 0;

    proj->max_functions = 256;
    proj->functions = (FunctionInfo*)malloc(proj->max_functions * sizeof(FunctionInfo));
    proj->function_count = 0;

    proj->callgraph = callgraph_create();
    proj->global_symbols = symbol_table_create();

    return proj;
}

// ============================================================
// ДОБАВЛЕНИЕ ФАЙЛА
// ============================================================

void project_add_file(Project* proj, const char* filename, const char* filepath, ASTNode* ast) {
    if (!proj || !filename || !ast) return;

    if (proj->file_count >= proj->max_files) {
        proj->max_files *= 2;
        proj->files = (SourceFile*)realloc(proj->files, proj->max_files * sizeof(SourceFile));
    }

    SourceFile* file = &proj->files[proj->file_count];
    file->filename = (char*)malloc(strlen(filename) + 1);
    strcpy(file->filename, filename);

    file->filepath = filepath ? (char*)malloc(strlen(filepath) + 1) : (char*)malloc(1);
    if (filepath) {
        strcpy(file->filepath, filepath);
    }
    else {
        file->filepath[0] = '\0';
    }

    file->ast = ast;
    proj->file_count++;
}

// ============================================================
// ПОСТРОЕНИЕ CFG ДЛЯ ВСЕХ ФУНКЦИЙ
// ============================================================

static char* get_file_basename(const char* filepath) {
    const char* filename = filepath;
    for (int i = strlen(filepath) - 1; i >= 0; i--) {
        if (filepath[i] == '/' || filepath[i] == '\\') {
            filename = filepath + i + 1;
            break;
        }
    }

    char* basename = (char*)malloc(strlen(filename) + 1);
    strcpy(basename, filename);

    // Удаляем расширение
    for (int i = strlen(basename) - 1; i >= 0; i--) {
        if (basename[i] == '.') {
            basename[i] = '\0';
            break;
        }
    }

    return basename;
}

void project_build_cfgs(Project* proj) {
    if (!proj) return;

    // Для каждого файла
    for (int f = 0; f < proj->file_count; f++) {
        ASTNode* ast = proj->files[f].ast;
        if (!ast || ast->type != AST_PROGRAM) continue;

        // Для каждой функции в файле
        for (int i = 0; i < ast->child_count; i++) {
            ASTNode* func_def = ast->children[i];
            if (func_def->type != AST_FUNCTION_DEF) continue;

            // Получаем имя функции
            char func_name[256] = "unknown";
            if (func_def->child_count > 0 &&
                func_def->children[0]->type == AST_FUNCTION_SIGNATURE) {
                if (func_def->children[0]->value) {
                    snprintf(func_name, sizeof(func_name), "%s", func_def->children[0]->value);
                }
            }

            // Расширяем массив функций, если нужно
            if (proj->function_count >= proj->max_functions) {
                proj->max_functions *= 2;
                proj->functions = (FunctionInfo*)realloc(proj->functions,
                    proj->max_functions * sizeof(FunctionInfo));
            }

            // Создаём CFG для этой функции
            CFG* cfg = cfg_create();
            cfg_build_from_ast(cfg, func_def);

            // Добавляем в проект
            FunctionInfo* func_info = &proj->functions[proj->function_count];
            func_info->function_name = (char*)malloc(strlen(func_name) + 1);
            strcpy(func_info->function_name, func_name);

            func_info->signature = (char*)malloc(256);
            snprintf(func_info->signature, 256, "%s(...)", func_name);

            func_info->cfg = cfg;
            func_info->source_file = &proj->files[f];
            func_info->line_number = 0;

            proj->function_count++;
        }
    }
}

// ============================================================
// ПОСТРОЕНИЕ ГРАФА ВЫЗОВОВ
// ============================================================

static void extract_function_calls(CFG* cfg, const char* func_name, CallGraph* cg) {
    if (!cfg || !func_name || !cg) return;

    // Проходим по всем узлам CFG
    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];

        if (!node->label) continue;

        // Ищем вызовы функций в label
        // Паттерн: "func_name(arg1, arg2, ...)"
        const char* label = node->label;
        int label_len = strlen(label);

        for (int j = 0; j < label_len - 1; j++) {
            // Ищем открывающую скобку
            if (label[j] == '(') {
                // Извлекаем имя функции перед скобкой
                int k = j - 1;
                while (k >= 0 && label[k] == ' ') k--;

                int end = k;
                while (k >= 0 && (label[k] >= 'a' && label[k] <= 'z' ||
                    label[k] >= 'A' && label[k] <= 'Z' ||
                    label[k] >= '0' && label[k] <= '9' ||
                    label[k] == '_')) {
                    k--;
                }

                if (k < end && end - k < 256) {
                    char called_func[256];
                    strncpy(called_func, label + k + 1, end - k);
                    called_func[end - k] = '\0';

                    // Добавляем вызов
                    callgraph_add_call(cg, func_name, called_func);
                }
            }
        }
    }
}

void project_build_callgraph(Project* proj) {
    if (!proj || !proj->callgraph) return;

    // Для каждой функции в проекте
    for (int i = 0; i < proj->function_count; i++) {
        FunctionInfo* func = &proj->functions[i];
        extract_function_calls(func->cfg, func->function_name, proj->callgraph);
    }
}

// ============================================================
// ЭКСПОРТ ПРОЕКТА
// ============================================================

void project_export(Project* proj, const char* output_dir) {
    if (!proj) return;

    printf("\n[*] Exporting project...\n");

    // Экспортируем CFG для каждой функции
    for (int i = 0; i < proj->function_count; i++) {
        FunctionInfo* func = &proj->functions[i];

        // Получаем имя файла без расширения
        char* basename = get_file_basename(func->source_file->filename);

        // Формируем имя выходного файла: basename.funcname.dot
        char output_file[512];
        if (output_dir && strlen(output_dir) > 0) {
            snprintf(output_file, sizeof(output_file), "%s/%s.%s.dot",
                output_dir, basename, func->function_name);
        }
        else {
            snprintf(output_file, sizeof(output_file), "%s.%s.dot",
                basename, func->function_name);
        }

        cfg_export_dot(func->cfg, output_file);
        printf("[+] CFG exported: %s\n", output_file);

        free(basename);
    }

    // Экспортируем граф вызовов
    char callgraph_file[512];
    if (output_dir && strlen(output_dir) > 0) {
        snprintf(callgraph_file, sizeof(callgraph_file), "%s/callgraph.dot", output_dir);
    }
    else {
        snprintf(callgraph_file, sizeof(callgraph_file), "callgraph.dot");
    }

    if (proj->callgraph->call_count > 0) {
        callgraph_export_dot(proj->callgraph, callgraph_file);
        printf("[+] Call graph exported: %s\n", callgraph_file);
    }
    else {
        printf("[*] No function calls found (empty call graph)\n");
    }

    printf("[+] Export complete\n");
}

// ============================================================
// ПЕЧАТЬ РЕЗЮМЕ
// ============================================================

void project_print_summary(Project* proj) {
    if (!proj) return;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║              PROJECT SUMMARY                              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("Source files:  %d\n", proj->file_count);
    for (int i = 0; i < proj->file_count; i++) {
        printf("  [%d] %s\n", i + 1, proj->files[i].filename);
    }

    printf("\nFunctions:     %d\n", proj->function_count);
    for (int i = 0; i < proj->function_count; i++) {
        printf("  [%d] %s() from %s\n", i + 1,
            proj->functions[i].function_name,
            proj->functions[i].source_file->filename);
    }

    printf("\n");
    callgraph_print_summary(proj->callgraph);
}

// ============================================================
// ОСВОБОЖДЕНИЕ ПАМЯТИ
// ============================================================

void project_free(Project* proj) {
    if (!proj) return;

    for (int i = 0; i < proj->file_count; i++) {
        free(proj->files[i].filename);
        free(proj->files[i].filepath);
        freeAST(proj->files[i].ast);
    }
    free(proj->files);

    for (int i = 0; i < proj->function_count; i++) {
        free(proj->functions[i].function_name);
        free(proj->functions[i].signature);
        cfg_free(proj->functions[i].cfg);
    }
    free(proj->functions);

    callgraph_free(proj->callgraph);
    symbol_table_free(proj->global_symbols);

    free(proj);
}
