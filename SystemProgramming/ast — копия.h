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
    AST_FOR_STATEMENT,        /* <- добавлено */
    AST_BREAK_STATEMENT,
    AST_EXPR_STATEMENT,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_CALL_EXPR,
    AST_INDEX_EXPR,
    AST_IDENTIFIER,
    AST_LITERAL,
    AST_ASSIGNMENT,
    AST_INDEXED_ASSIGNMENT,
    AST_ARITHMETIC_EXPR,
    AST_ADDR_OF,
    AST_DEREF,
    AST_MEMBER_ACCESS,
    AST_RETURN_STATEMENT,
    AST_CONTINUE_STATEMENT,
    AST_ARRAY_ACCESS,
    AST_ARGUMENTLIST,
    AST_BLOCK,

    /* Дополнительные значения */
    AST_ID_LIST,           // Список идентификаторов
    AST_STRING_LITERAL,    //  Строковый литерал
    AST_BOOL_LITERAL,      // Р‘СѓР»РµРІС‹Р№ Р»РёС‚РµСЂР°Р»
    AST_CHAR_LITERAL,      // РЎРёРјРІРѕР»СЊРЅС‹Р№ Р»РёС‚РµСЂР°Р»
    AST_FLOAT_LITERAL,     // Float literal
    AST_VAR_DECL_LIST,     // Список объявлений переменных
    AST_ARRAY_LITERAL,     // Литерал массива
    AST_ARRAY_TYPE         // Тип массива

} ASTNodeType;

typedef struct ASTNode {
    ASTNodeType type;
    int has_explicit_type;
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

/* Добавляем функции для работы с ошибками */
void ast_set_error(ASTNode* node, const char* error_message);
void ast_set_data_type(ASTNode* node, const char* data_type);

extern ASTNode* root_ast;

#endif