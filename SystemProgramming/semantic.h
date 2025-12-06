#pragma once
#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

// ============================================================
// рхош яхлбнкнб
// ============================================================

typedef enum {
    SYM_VARIABLE = 0,
    SYM_FUNCTION = 1,
    SYM_PARAMETER = 2
} SymbolType;

// ============================================================
// рюакхжю яхлбнкнб
// ============================================================

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

SymbolTable* symbol_table_create(void);

void symbol_table_add(SymbolTable* st, const char* name, SymbolType type,
    const char* data_type);

Symbol* symbol_table_lookup(SymbolTable* st, const char* name);
Symbol* symbol_table_find(SymbolTable* st, const char* name);

int symbol_is_declared(SymbolTable* st, const char* name);

void symbol_table_add_error(SymbolTable* st, const char* error_message);

void symbol_table_print_errors(SymbolTable* st);

void symbol_table_free(SymbolTable* st);

void semantic_analyze(ASTNode* ast, SymbolTable* symbol_table);

#endif // SEMANTIC_H
