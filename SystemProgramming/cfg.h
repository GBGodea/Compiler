#pragma once
#ifndef CFG_H
#define CFG_H

#include "ast.h"
#include "semantic.h"

// ============================================================
// ТИПЫ УЗЛОВ CFG
// ============================================================

typedef enum {
    CFG_BLOCK,      // Обычный блок/оператор
    CFG_CONDITION,  // Условие (if, while)
    CFG_START,      // Вход функции
    CFG_END,        // Выход из функции (return)
    CFG_MERGE,      // Слияние потоков
    CFG_ERROR       // Ошибка
} CFGNodeType;

// ============================================================
// СТРУКТУРЫ
// ============================================================

typedef struct CFGNode {
    int id;
    CFGNodeType type;
    char* label;

    // AST привязка для деревьев операций
    ASTNode* ast_node;    // Исходный AST-узел (statement)
    ASTNode* op_tree;     // Дерево операции (expression)

    // Двоичные переходы вместо массивов
    struct CFGNode* defaultNext;      // Переход "по умолчанию" (else, продолжение)
    struct CFGNode* conditionalNext;  // Условный переход (true-ветка)

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

// ============================================================
// ПУБЛИЧНЫЕ ФУНКЦИИ
// ============================================================

CFG* cfg_create(void);
CFGNode* cfg_create_node(CFG* cfg, CFGNodeType type, const char* label,
    ASTNode* ast_node, ASTNode* op_tree);
CFGNode* cfg_create_error_node(CFG* cfg, const char* label, const char* error_message);

void cfg_add_default_edge(CFGNode* from, CFGNode* to);
void cfg_add_conditional_edge(CFGNode* from, CFGNode* to);

void cfg_build_from_ast(CFG* cfg, ASTNode* ast);
void cfg_export_dot(CFG* cfg, const char* filename);

// 🔴 СЕМАНТИЧЕСКИЙ АНАЛИЗ В CFG
void cfg_set_symbol_table(SymbolTable* table);
void check_expression_semantics(ASTNode* expr, SymbolTable* symbol_table, CFGNode* cfg_node);
void cfg_check_semantics(CFG* cfg, SymbolTable* symbol_table);

void cfg_free(CFG* cfg);

#endif // CFG_H
