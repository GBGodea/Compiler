#pragma once
#ifndef CFG_H
#define CFG_H

#include "ast.h"
#include "semantic.h"

typedef enum {
    CFG_BLOCK,
    CFG_CONDITION,
    CFG_START,
    CFG_END,
    CFG_MERGE,
    CFG_ERROR
} CFGNodeType;

typedef struct CFGNode {
    int id;
    CFGNodeType type;
    char* label;
    ASTNode* ast_node;
    ASTNode* op_tree;
    struct CFGNode* defaultNext;
    struct CFGNode* conditionalNext;

    int has_error;
    char* error_message;
    int is_break;
} CFGNode;

typedef struct {
    CFGNode* entry;
    CFGNode* exit;
} CFGSegment;

typedef struct {
    CFGNode** nodes;
    int node_count;
    CFGNode* entry;
    CFGNode* exit;
    int next_id;

    SymbolTable* symbol_table;
} CFG;

CFG* cfg_create(void);
CFGNode* cfg_create_node(CFG* cfg, CFGNodeType type, const char* label,
    ASTNode* ast_node, ASTNode* op_tree);
CFGNode* cfg_create_error_node(CFG* cfg, const char* label, const char* error_message);

void cfg_add_default_edge(CFGNode* from, CFGNode* to);
void cfg_add_conditional_edge(CFGNode* from, CFGNode* to);

void cfg_build_from_ast(CFG* cfg, ASTNode* ast);
void cfg_export_dot(CFG* cfg, const char* filename);

void cfg_set_symbol_table(SymbolTable* table);
void check_expression_semantics(ASTNode* expr, SymbolTable* symbol_table, CFGNode* cfg_node);
void cfg_check_semantics(CFG* cfg, SymbolTable* symbol_table);

void cfg_free(CFG* cfg);

/* Вспомогательные функции для внутреннего использования */
void escape_string_for_dot(const char* input, char* output, size_t max_len);
const char* get_operation_name(ASTNodeType type, const char* value);

#endif