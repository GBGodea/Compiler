#ifndef PROJECT_H
#define PROJECT_H

#include "ast.h"
#include "cfg.h"
#include "callgraph.h"
#include "semantic.h"

// ============================================================
// ИСХОДНЫЙ ФАЙЛ
// ============================================================

typedef struct {
    char* filename;         // Имя файла (например, "test.txt")
    char* filepath;         // Полный путь
    ASTNode* ast;
} SourceFile;

// ============================================================
// ИНФОРМАЦИЯ О ФУНКЦИИ
// ============================================================

typedef struct {
    char* function_name;
    char* signature;        // Строковое представление сигнатуры
    CFG* cfg;
    SourceFile* source_file;
    int line_number;
} FunctionInfo;

// ============================================================
// ПРОЕКТ
// ============================================================

typedef struct {
    SourceFile* files;
    int file_count;
    int max_files;

    FunctionInfo* functions;
    int function_count;
    int max_functions;

    CallGraph* callgraph;
    SymbolTable* global_symbols;
} Project;

// ============================================================
// ФУНКЦИИ
// ============================================================

Project* project_create(void);
void project_add_file(Project* proj, const char* filename, const char* filepath, ASTNode* ast);
void project_build_cfgs(Project* proj);
void project_build_callgraph(Project* proj);
void project_export(Project* proj, const char* output_dir);
void project_print_summary(Project* proj);
void project_free(Project* proj);

#endif