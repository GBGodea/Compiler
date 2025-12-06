#pragma once
#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    AST_PROGRAM,
    AST_FUNCTION_DEF,
    AST_FUNCTION_SIGNATURE,
    AST_ARG_DEF,
    AST_TYPE_REF,
    AST_VAR_DECLARATION,
    AST_STATEMENT_BLOCK,
    AST_STATEMENT_LIST,
    AST_IF_STATEMENT,
    AST_WHILE_STATEMENT,
    AST_REPEAT_STATEMENT,
    AST_BREAK_STATEMENT,
    AST_EXPR_STATEMENT,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_CALL_EXPR,
    AST_INDEX_EXPR,
    AST_IDENTIFIER,
    AST_LITERAL,

    // Операции присваивания
    AST_ASSIGNMENT,
    AST_INDEXED_ASSIGNMENT,

    // Бинарные операции
    AST_ARITHMETIC_EXPR,

    // Унарные операции
    AST_ADDR_OF,

    // Операции с памятью
    AST_DEREF,
    AST_MEMBER_ACCESS,

    // Управление потоком
    AST_RETURN_STATEMENT,
    AST_CONTINUE_STATEMENT
} ASTNodeType;

typedef struct ASTNode {
    ASTNodeType type;
    int line_number;
    struct ASTNode** children;
    int child_count;
    char* value;
    int has_error;
    char* error_message;
    char* data_type;
} ASTNode;

ASTNode* createASTNode(ASTNodeType type, const char* value, int line_num);
ASTNode* addChild(ASTNode* parent, ASTNode* child);
void printASTDot(ASTNode* node, FILE* file);
void freeAST(ASTNode* node);
const char* getNodeTypeName(ASTNodeType type);

extern ASTNode* root_ast;

#endif