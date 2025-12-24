#include "ast.h"

/* Глобальная переменная для корня AST */
//ASTNode* root_ast = NULL;

ASTNode* createASTNode(ASTNodeType type, const char* value, int line_num) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));

    /* Инициализация всех полей */
    node->type = type;
    node->has_explicit_type = 0;
    node->line_number = line_num;
    node->children = NULL;
    node->child_count = 0;
    node->value = value ? strdup(value) : NULL;
    node->has_error = 0;
    node->error_message = NULL;
    node->data_type = NULL;

    return node;
}

ASTNode* addChild(ASTNode* parent, ASTNode* child) {
    if (parent == NULL || child == NULL) return parent;

    /* Увеличиваем массив детей */
    parent->children = (ASTNode**)realloc(parent->children,
        sizeof(ASTNode*) * (parent->child_count + 1));

    /* Проверяем успешность выделения памяти */
    if (!parent->children) {
        fprintf(stderr, "Memory allocation failed in addChild\n");
        return parent;
    }

    parent->children[parent->child_count] = child;
    parent->child_count++;
    return parent;
}

const char* getNodeTypeName(ASTNodeType type) {
    switch (type) {
    case AST_PROGRAM:           return "Program";
    case AST_FUNCTION_DEF:      return "FunctionDef";
    case AST_FUNCTION_SIGNATURE:return "FunctionSignature";
    case AST_ARG_DEF:           return "ArgDef";
    case AST_TYPE_REF:          return "TypeRef";
    case AST_VAR_DECLARATION:   return "VarDeclaration";
    case AST_STATEMENT_BLOCK:   return "StatementBlock";
    case AST_STATEMENT_LIST:    return "StatementList";
    case AST_IF_STATEMENT:      return "IfStatement";
    case AST_WHILE_STATEMENT:   return "WhileStatement";
    case AST_REPEAT_STATEMENT:  return "RepeatStatement";
    case AST_BREAK_STATEMENT:   return "BreakStatement";
    case AST_EXPR_STATEMENT:    return "ExprStatement";
    case AST_BINARY_EXPR:       return "BinaryExpr";
    case AST_UNARY_EXPR:        return "UnaryExpr";
    case AST_CALL_EXPR:         return "CallExpr";
    case AST_INDEX_EXPR:        return "IndexExpr";
    case AST_IDENTIFIER:        return "Identifier";
    case AST_LITERAL:           return "Literal";
    case AST_ASSIGNMENT:        return "Assignment";
    case AST_INDEXED_ASSIGNMENT:return "IndexedAssignment";
    case AST_ARITHMETIC_EXPR:   return "ArithmeticExpr";
    case AST_ADDR_OF:           return "AddrOf";
    case AST_DEREF:             return "Deref";
    case AST_MEMBER_ACCESS:     return "MemberAccess";
    case AST_RETURN_STATEMENT:  return "ReturnStatement";
    case AST_CONTINUE_STATEMENT:return "ContinueStatement";

        /* Добавьте эти строки: */
    case AST_ID_LIST:           return "IdList";
    case AST_STRING_LITERAL:    return "StringLiteral";
    case AST_VAR_DECL_LIST:     return "VarDeclList";
    case AST_ARRAY_LITERAL:     return "ArrayLiteral";
    case AST_ARRAY_TYPE:        return "ArrayType";

    default:                    return "Unknown";
    }
}

/* Вспомогательные функции для работы с ошибками */
void ast_set_error(ASTNode* node, const char* error_message) {
    if (!node) return;

    node->has_error = 1;
    if (node->error_message) {
        free(node->error_message);
    }
    node->error_message = error_message ? strdup(error_message) : NULL;
}

void ast_set_data_type(ASTNode* node, const char* data_type) {
    if (!node) return;

    if (node->data_type) {
        free(node->data_type);
    }
    node->data_type = data_type ? strdup(data_type) : NULL;
}

/* Статическая переменная для генерации ID узлов */
static int node_id_counter = 0;

/* Вспомогательная функция для рекурсивной печати AST */
static void printASTDot_impl(ASTNode* node, FILE* file) {
    if (!node) return;

    int current_id = node_id_counter++;
    const char* type_name = getNodeTypeName(node->type);

    /* Формируем метку узла */
    if (node->value) {
        /* Экранируем кавычки для DOT */
        char escaped_value[1024];
        int j = 0;
        for (int i = 0; node->value[i] != '\0' && j < 1022; i++) {
            if (node->value[i] == '"') {
                escaped_value[j++] = '\\';
                escaped_value[j++] = '"';
            }
            else if (node->value[i] == '\\') {
                escaped_value[j++] = '\\';
                escaped_value[j++] = '\\';
            }
            else {
                escaped_value[j++] = node->value[i];
            }
        }
        escaped_value[j] = '\0';

        fprintf(file, "  node%d [label=\"%s\\n%s\", shape=box, style=rounded];\n",
            current_id, type_name, escaped_value);
    }
    else {
        fprintf(file, "  node%d [label=\"%s\", shape=box, style=rounded];\n",
            current_id, type_name);
    }

    /* Добавляем информацию об ошибке, если есть */
    if (node->has_error && node->error_message) {
        fprintf(file, "  node%d [color=red, fontcolor=red];\n", current_id);
    }

    /* Добавляем информацию о типе данных, если есть */
    if (node->data_type) {
        fprintf(file, "  node%d [label=\"%s\\nType: %s\", shape=box, style=rounded];\n",
            current_id, type_name, node->data_type);
    }

    /* Рекурсивно обрабатываем детей */
    int parent_id = current_id;
    for (int i = 0; i < node->child_count; i++) {
        int child_start_id = node_id_counter;
        fprintf(file, "  node%d -> node%d;\n", parent_id, child_start_id);
        printASTDot_impl(node->children[i], file);
    }
}

void printASTDot(ASTNode* node, FILE* file) {
    if (!node) return;

    fprintf(file, "digraph AST {\n");
    fprintf(file, "  rankdir=TB;\n");
    fprintf(file, "  node [fontname=\"Courier\", fontsize=10];\n");
    fprintf(file, "  edge [fontname=\"Courier\", fontsize=10];\n\n");

    node_id_counter = 0;
    printASTDot_impl(node, file);

    fprintf(file, "}\n");
}

void freeAST(ASTNode* node) {
    if (!node) return;

    /* Рекурсивно освобождаем детей */
    for (int i = 0; i < node->child_count; i++) {
        freeAST(node->children[i]);
    }

    /* Освобождаем массивы и строки */
    if (node->children) free(node->children);
    if (node->value) free(node->value);
    if (node->error_message) free(node->error_message);
    if (node->data_type) free(node->data_type);

    /* Освобождаем сам узел */
    free(node);
}