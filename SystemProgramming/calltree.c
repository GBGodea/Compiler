#include "calltree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>





CallTree* calltree_create(void) {
    CallTree* ct = (CallTree*)malloc(sizeof(CallTree));
    ct->roots = NULL;
    ct->root_count = 0;
    ct->next_id = 0;
    return ct;
}

CallTreeNode* calltree_create_node(CallTree* ct, const char* func_name) {
    if (!ct || !func_name) return NULL;

    CallTreeNode* node = (CallTreeNode*)malloc(sizeof(CallTreeNode));
    node->id = ct->next_id++;
    node->function_name = (char*)malloc(strlen(func_name) + 1);
    strcpy(node->function_name, func_name);
    node->children = (CallTreeNode**)malloc(sizeof(CallTreeNode*) * 10);
    node->child_count = 0;
    node->max_children = 10;

    return node;
}

static CallTreeNode* calltree_find_node(CallTreeNode* root, const char* func_name) {
    if (!root || !func_name) return NULL;

    if (strcmp(root->function_name, func_name) == 0) {
        return root;
    }

    for (int i = 0; i < root->child_count; i++) {
        CallTreeNode* found = calltree_find_node(root->children[i], func_name);
        if (found) return found;
    }

    return NULL;
}

void calltree_add_call(CallTree* ct, const char* caller, const char* callee) {
    if (!ct || !caller || !callee) return;

    
    CallTreeNode* caller_node = NULL;

    
    for (int i = 0; i < ct->root_count; i++) {
        caller_node = calltree_find_node(ct->roots[i], caller);
        if (caller_node) break;
    }

    
    if (!caller_node) {
        caller_node = calltree_create_node(ct, caller);
        if (!ct->roots) {
            ct->roots = (CallTreeNode**)malloc(sizeof(CallTreeNode*) * 10);
        }
        ct->roots[ct->root_count++] = caller_node;
    }

    
    if (caller_node->child_count >= caller_node->max_children) {
        caller_node->max_children *= 2;
        caller_node->children = (CallTreeNode**)realloc(caller_node->children,
            caller_node->max_children * sizeof(CallTreeNode*));
    }

    CallTreeNode* callee_node = calltree_create_node(ct, callee);
    caller_node->children[caller_node->child_count++] = callee_node;
}





static void extract_calls_from_expression(ASTNode* expr, const char* current_func, CallTree* ct) {
    if (!expr || !current_func || !ct) return;

    
    if (expr->type == AST_CALL_EXPR) {
        if (expr->value) {
            printf("    [CALL] %s -> %s\n", current_func, expr->value);
            calltree_add_call(ct, current_func, expr->value);
        }
        
        return;
    }

    
    switch (expr->type) {
    case AST_BINARY_EXPR:
    case AST_UNARY_EXPR:
        for (int i = 0; i < expr->child_count; i++) {
            extract_calls_from_expression(expr->children[i], current_func, ct);
        }
        break;

    default:
        break;
    }
}

static void extract_calls_from_statement(ASTNode* stmt, const char* current_func, CallTree* ct) {
    if (!stmt || !current_func || !ct) return;

    switch (stmt->type) {
    case AST_EXPR_STATEMENT:
        if (stmt->child_count > 0) {
            extract_calls_from_expression(stmt->children[0], current_func, ct);
        }
        break;

    case AST_IF_STATEMENT:
        
        if (stmt->child_count > 0) {
            extract_calls_from_expression(stmt->children[0], current_func, ct);
        }
        
        for (int i = 1; i < stmt->child_count; i++) {
            extract_calls_from_statement(stmt->children[i], current_func, ct);
        }
        break;

    case AST_WHILE_STATEMENT:
        
        if (stmt->child_count > 0) {
            extract_calls_from_expression(stmt->children[0], current_func, ct);
        }
        
        if (stmt->child_count > 1) {
            extract_calls_from_statement(stmt->children[1], current_func, ct);
        }
        break;

    case AST_REPEAT_STATEMENT:
        
        if (stmt->child_count > 0) {
            extract_calls_from_statement(stmt->children[0], current_func, ct);
        }
        
        if (stmt->child_count > 1) {
            extract_calls_from_expression(stmt->children[1], current_func, ct);
        }
        break;

    case AST_STATEMENT_BLOCK:
    case AST_STATEMENT_LIST:
        for (int i = 0; i < stmt->child_count; i++) {
            extract_calls_from_statement(stmt->children[i], current_func, ct);
        }
        break;

    default:
        break;
    }
}

void calltree_build_from_ast(CallTree* ct, ASTNode* ast) {
    if (!ast || ast->type != AST_PROGRAM || !ct) return;

    printf("[*] Building call tree...\n");

    
    for (int i = 0; i < ast->child_count; i++) {
        ASTNode* func_def = ast->children[i];

        if (func_def->type != AST_FUNCTION_DEF) continue;

        
        char func_name[256] = "unknown";
        if (func_def->child_count > 0 && func_def->children[0]->type == AST_FUNCTION_SIGNATURE) {
            if (func_def->children[0]->value) {
                snprintf(func_name, sizeof(func_name), "%s", func_def->children[0]->value);
            }
        }

        printf("[*] Analyzing calls in %s...\n", func_name);

        
        if (func_def->child_count > 1) {
            extract_calls_from_statement(func_def->children[1], func_name, ct);
        }
    }

    printf("[+] Call tree built\n");
}





static void export_node_to_dot(CallTreeNode* node, FILE* f) {
    if (!node || !f) return;

    
    fprintf(f, "  node%d [label=\"%s\", shape=box, fillcolor=lightblue, style=filled];\n",
        node->id, node->function_name);

    
    for (int i = 0; i < node->child_count; i++) {
        if (node->children[i]) {
            fprintf(f, "  node%d -> node%d;\n", node->id, node->children[i]->id);
            export_node_to_dot(node->children[i], f);
        }
    }
}

void calltree_export_dot(CallTree* ct, const char* filename) {
    if (!ct || !filename) return;

    FILE* f = fopen(filename, "w");
    if (!f) {
        perror("fopen (CalltreeDOT)");
        return;
    }

    fprintf(f, "digraph CallTree {\n");
    fprintf(f, "  rankdir=TD;\n");
    fprintf(f, "  node [fontname=\"Courier\", fontsize=10];\n");
    fprintf(f, "  edge [fontname=\"Courier\", fontsize=9];\n\n");

    
    for (int i = 0; i < ct->root_count; i++) {
        if (ct->roots[i]) {
            export_node_to_dot(ct->roots[i], f);
        }
    }

    fprintf(f, "}\n");
    fclose(f);
}





static void free_node(CallTreeNode* node) {
    if (!node) return;

    for (int i = 0; i < node->child_count; i++) {
        if (node->children[i]) {
            free_node(node->children[i]);
        }
    }

    if (node->function_name) {
        free(node->function_name);
    }
    if (node->children) {
        free(node->children);
    }
    free(node);
}

void calltree_free(CallTree* ct) {
    if (!ct) return;

    for (int i = 0; i < ct->root_count; i++) {
        if (ct->roots[i]) {
            free_node(ct->roots[i]);
        }
    }

    if (ct->roots) {
        free(ct->roots);
    }
    free(ct);
}
