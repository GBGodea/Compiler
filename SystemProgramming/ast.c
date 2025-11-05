#include "ast.h"

ASTNode* root_ast = NULL;
static int node_counter = 0;

ASTNode* createASTNode(ASTNodeType type, const char* value, int line_num) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = type;
    node->line_number = line_num;
    node->children = NULL;
    node->child_count = 0;
    node->value = value ? strdup(value) : NULL;
    return node;
}

ASTNode* addChild(ASTNode* parent, ASTNode* child) {
    if (parent == NULL || child == NULL) return parent;

    parent->children = (ASTNode**)realloc(parent->children,
        sizeof(ASTNode*) * (parent->child_count + 1));
    parent->children[parent->child_count] = child;
    parent->child_count++;
    return parent;
}

const char* getNodeTypeName(ASTNodeType type) {
    switch (type) {
    case AST_PROGRAM: return "Program";
    case AST_FUNCTION_DEF: return "FunctionDef";
    case AST_FUNCTION_SIGNATURE: return "FunctionSignature";
    case AST_ARG_DEF: return "ArgDef";
    case AST_TYPE_REF: return "TypeRef";
    case AST_VAR_DECLARATION: return "VarDeclaration";
    case AST_STATEMENT_BLOCK: return "StatementBlock";
    case AST_IF_STATEMENT: return "IfStatement";
    case AST_WHILE_STATEMENT: return "WhileStatement";
    case AST_REPEAT_STATEMENT: return "RepeatStatement";
    case AST_BREAK_STATEMENT: return "BreakStatement";
    case AST_EXPR_STATEMENT: return "ExprStatement";
    case AST_BINARY_EXPR: return "BinaryExpr";
    case AST_UNARY_EXPR: return "UnaryExpr";
    case AST_CALL_EXPR: return "CallExpr";
    case AST_INDEX_EXPR: return "IndexExpr";
    case AST_IDENTIFIER: return "Identifier";
    case AST_LITERAL: return "Literal";
    default: return "Unknown";
    }
}

static void printASTDotHelper(ASTNode* node, FILE* file, int* counter) {
    if (!node) return;

    int current_id = (*counter)++;
    // ѕечать узла
    fprintf(file, "    node%d [label=\"%s", current_id, getNodeTypeName(node->type));
    if (node->value) {
        fprintf(file, "\\n%s", node->value);
    }
    fprintf(file, "\"];\n");

    // ѕечать ребер к дет¤м
    for (int i = 0; i < node->child_count; i++) {
        ASTNode* child = node->children[i];
        if (child) {
            int child_id = *counter;
            fprintf(file, "    node%d -> node%d;\n", current_id, child_id);
            printASTDotHelper(child, file, counter);
        }
    }
}

void printASTDot(ASTNode* node, FILE* file) {
    if (!node) return;
    fprintf(file, "digraph AST {\n");
    fprintf(file, "    rankdir=TB;\n");
    fprintf(file, "    node [shape=box, style=rounded];\n");
    int counter = 0;
    printASTDotHelper(node, file, &counter);
    fprintf(file, "}\n");
}

void freeAST(ASTNode* node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        freeAST(node->children[i]);
    }
    free(node->children);
    free(node->value);
    free(node);
}
