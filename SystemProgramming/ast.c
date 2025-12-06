#include "ast.h"

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
    case AST_ASSIGNMENT: return "AST_ASSIGNMENT";
    case AST_INDEXED_ASSIGNMENT: return "AST_INDEXED_ASSIGNMENT";
    case AST_ARITHMETIC_EXPR: return "AST_ARITHMETIC_EXPR";
    case AST_ADDR_OF: return "AST_ADDR_OF";
    case AST_DEREF: return "AST_DEREF";
    case AST_MEMBER_ACCESS: return "AST_MEMBER_ACCESS";
    case AST_RETURN_STATEMENT: return "AST_RETURN_STATEMENT";
    case AST_CONTINUE_STATEMENT: return "AST_CONTINUE_STATEMENT";
    default:                    return "Unknown";
    }
}

static int node_id_counter = 0;

static void printASTDot_impl(ASTNode* node, FILE* file) {
    if (!node) return;

    int current_id = node_id_counter++;
    const char* type_name = getNodeTypeName(node->type);

   
    if (node->value) {
        fprintf(file, "  node%d [label=\"%s\\n\\\"%s\\\"\", shape=box, style=rounded];\n",
            current_id, type_name, node->value);
    }
    else {
        fprintf(file, "  node%d [label=\"%s\", shape=box, style=rounded];\n",
            current_id, type_name);
    }

   
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

    for (int i = 0; i < node->child_count; i++) {
        freeAST(node->children[i]);
    }

    free(node->children);
    free(node->value);
    free(node);
}