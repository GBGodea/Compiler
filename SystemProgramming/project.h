#ifndef PROJECT_H
#define PROJECT_H

#include "ast.h"
#include "cfg.h"
#include "callgraph.h"
#include "semantic.h"

typedef struct {
    char* filename;       
    char* filepath;      
    ASTNode* ast;
} SourceFile;

typedef struct {
    char* function_name;
    char* signature;       
    CFG* cfg;
    SourceFile* source_file;
    int line_number;
} FunctionInfo;

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

Project* project_create(void);
void project_add_file(Project* proj, const char* filename, const char* filepath, ASTNode* ast);
void project_build_cfgs(Project* proj);
void project_build_callgraph(Project* proj);
void project_export(Project* proj, const char* output_dir);
void project_print_summary(Project* proj);
void project_free(Project* proj);

#endif