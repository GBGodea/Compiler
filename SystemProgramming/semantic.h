#pragma once
#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

typedef enum {
    SYM_VARIABLE = 0,
    SYM_FUNCTION = 1,
    SYM_PARAMETER = 2
} SymbolType;

typedef struct {
    char* name;
    SymbolType type;
    char* data_type;
    int is_array;
    int is_declared;
    int line_number;
} Symbol;

typedef struct {
    Symbol* symbols;
    int symbol_count;
    int max_symbols;
    char* error_messages[1024];
    int error_count;
} SymbolTable;

/* Основные функции таблицы символов */
SymbolTable* symbol_table_create(void);
void symbol_table_add(SymbolTable* st, const char* name, SymbolType type,
    const char* data_type);
Symbol* symbol_table_lookup(SymbolTable* st, const char* name);
Symbol* symbol_table_find(SymbolTable* st, const char* name);
int symbol_is_declared(SymbolTable* st, const char* name);
void symbol_table_add_error(SymbolTable* st, const char* error_message);
void symbol_table_print_errors(SymbolTable* st);
void symbol_table_free(SymbolTable* st);

/* Функции семантического анализа */
void semantic_analyze(ASTNode* ast, SymbolTable* symbol_table);
void check_expression(ASTNode* expr, SymbolTable* st, int line_num);
void mark_ast_error(ASTNode* node, const char* format, ...);
void semantic_check_expression(ASTNode* node, SymbolTable* table, int* has_error);

#endif // SEMANTIC_H