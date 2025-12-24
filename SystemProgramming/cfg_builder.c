#include "cfg.h"
#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Статические переменные для внутреннего состояния */
static CFGNode* current_loop_exit = NULL;
static SymbolTable* current_symbol_table = NULL;
static int ast_tree_counter = 0;

/* Вспомогательные функции */
static const char* get_operation_name_internal(ASTNodeType type, const char* value);
static void export_ast_tree_to_dot(ASTNode* node, FILE* f, int tree_id, int* node_counter);
static void ast_to_string(ASTNode* node, char* buf, int max_len);
static int segment_ends_with_break(CFGNode* exit_node);
static CFGSegment build_cfg_for_statement(CFG* cfg, ASTNode* stmt);
static CFGSegment build_cfg_for_statements(CFG* cfg, ASTNode* stmt_list);
static void mark_error_recursive(ASTNode* node, const char* error_msg);

void cfg_set_symbol_table(SymbolTable* table) {
    current_symbol_table = table;
}

CFG* cfg_create(void) {
    CFG* cfg = (CFG*)malloc(sizeof(CFG));
    cfg->nodes = NULL;
    cfg->node_count = 0;
    cfg->entry = NULL;
    cfg->exit = NULL;
    cfg->next_id = 0;
    cfg->symbol_table = NULL;
    return cfg;
}

CFGNode* cfg_create_node(CFG* cfg, CFGNodeType type, const char* label,
    ASTNode* ast_node, ASTNode* op_tree) {
    if (!cfg) return NULL;

    CFGNode* node = (CFGNode*)malloc(sizeof(CFGNode));
    node->id = cfg->next_id++;
    node->type = type;
    node->label = label ? (char*)malloc(strlen(label) + 1) : NULL;
    if (label) strcpy(node->label, label);

    node->ast_node = ast_node;
    node->op_tree = op_tree;

    node->defaultNext = NULL;
    node->conditionalNext = NULL;

    node->has_error = 0;
    node->error_message = NULL;
    node->is_break = 0;

    cfg->nodes = (CFGNode**)realloc(cfg->nodes, (cfg->node_count + 1) * sizeof(CFGNode*));
    cfg->nodes[cfg->node_count++] = node;

    return node;
}

CFGNode* cfg_create_error_node(CFG* cfg, const char* label, const char* error_message) {
    CFGNode* node = cfg_create_node(cfg, CFG_ERROR, label, NULL, NULL);
    if (!node) return NULL;

    node->has_error = 1;
    if (error_message) {
        node->error_message = (char*)malloc(strlen(error_message) + 1);
        strcpy(node->error_message, error_message);
    }

    return node;
}

void cfg_add_default_edge(CFGNode* from, CFGNode* to) {
    if (from && to) {
        from->defaultNext = to;
    }
}

void cfg_add_conditional_edge(CFGNode* from, CFGNode* to) {
    if (from && to) {
        from->conditionalNext = to;
    }
}

static void ast_to_string(ASTNode* node, char* buf, int max_len) {
    if (!node || !buf) return;

    if (!node->value) {
        snprintf(buf, max_len, "%s", getNodeTypeName(node->type));
        return;
    }

    const char* type_name = getNodeTypeName(node->type);

    if (node->type == AST_BINARY_EXPR) {
        if (node->child_count >= 2) {
            char left[256] = "", right[256] = "";
            ast_to_string(node->children[0], left, sizeof(left));
            ast_to_string(node->children[1], right, sizeof(right));
            snprintf(buf, max_len, "(%s %s %s)", left, node->value, right);
        }
        else {
            snprintf(buf, max_len, "%s", node->value);
        }
    }
    else if (node->type == AST_CALL_EXPR) {
        snprintf(buf, max_len, "%s(...)", node->value);
    }
    else if (node->type == AST_IDENTIFIER || node->type == AST_LITERAL) {
        snprintf(buf, max_len, "%s", node->value);
    }
    else {
        snprintf(buf, max_len, "%s: %s", type_name, node->value);
    }
}

static int segment_ends_with_break(CFGNode* exit_node) {
    if (!exit_node) return 0;
    return exit_node->is_break;
}

static CFGSegment build_cfg_for_statements(CFG* cfg, ASTNode* stmt_list) {
    if (!cfg || !stmt_list) {
        CFGSegment empty = { NULL, NULL };
        return empty;
    }

    CFGNode* first_node = NULL;
    CFGNode* last_node = NULL;

    for (int i = 0; i < stmt_list->child_count; i++) {
        CFGSegment seg = build_cfg_for_statement(cfg, stmt_list->children[i]);

        if (!first_node) {
            first_node = seg.entry;
        }

        if (last_node && seg.entry) {
            if (!last_node->is_break) {
                cfg_add_default_edge(last_node, seg.entry);
            }
        }

        last_node = seg.exit;

        if (seg.exit && seg.exit->is_break) {
            break;
        }
    }

    CFGSegment result = { first_node, last_node };
    return result;
}

static CFGSegment build_cfg_for_statement(CFG* cfg, ASTNode* stmt) {
    CFGSegment result = { NULL, NULL };

    if (!cfg || !stmt) return result;

    char label[1024] = "";

    switch (stmt->type) {
    case AST_EXPR_STATEMENT: {
        if (stmt->child_count > 0) {
            ASTNode* expr = stmt->children[0];
            ast_to_string(expr, label, sizeof(label));

            CFGNode* node = cfg_create_node(cfg, CFG_BLOCK, label, stmt, expr);

            if (current_symbol_table) {
                check_expression_semantics(expr, current_symbol_table, node);

                if (node->has_error) {
                    node->type = CFG_ERROR;

                    char error_label[1024];
                    snprintf(error_label, sizeof(error_label), "❌ %s\n%s", label, node->error_message);
                    if (node->label) free(node->label);
                    node->label = (char*)malloc(strlen(error_label) + 1);
                    strcpy(node->label, error_label);
                }
            }

            result.entry = node;
            result.exit = node;
        }
        break;
    }

    case AST_IF_STATEMENT: {
        if (stmt->child_count < 1) break;

        ASTNode* cond = stmt->children[0];
        ast_to_string(cond, label, sizeof(label));

        CFGNode* cond_node = cfg_create_node(cfg, CFG_CONDITION, label, stmt, cond);

        if (current_symbol_table) {
            check_expression_semantics(cond, current_symbol_table, cond_node);

            if (cond_node->has_error) {
                cond_node->type = CFG_ERROR;

                char error_label[1024];
                snprintf(error_label, sizeof(error_label), "❌ IF %s\n%s", label, cond_node->error_message);
                if (cond_node->label) free(cond_node->label);
                cond_node->label = (char*)malloc(strlen(error_label) + 1);
                strcpy(cond_node->label, error_label);

                result.entry = cond_node;
                result.exit = cond_node;
                break;
            }
        }

        CFGSegment then_seg = { NULL, NULL };
        if (stmt->child_count > 1) {
            then_seg = build_cfg_for_statement(cfg, stmt->children[1]);
        }

        CFGSegment else_seg = { NULL, NULL };
        if (stmt->child_count > 2) {
            else_seg = build_cfg_for_statement(cfg, stmt->children[2]);
        }

        if (then_seg.entry) {
            cfg_add_conditional_edge(cond_node, then_seg.entry);
        }

        if (else_seg.entry) {
            cfg_add_default_edge(cond_node, else_seg.entry);
        }

        CFGNode* merge_node = cfg_create_node(cfg, CFG_MERGE, "end-if", NULL, NULL);

        if (then_seg.exit) {
            if (!segment_ends_with_break(then_seg.exit)) {
                cfg_add_default_edge(then_seg.exit, merge_node);
            }
        }

        if (else_seg.exit) {
            if (!segment_ends_with_break(else_seg.exit)) {
                cfg_add_default_edge(else_seg.exit, merge_node);
            }
        }
        else {
            cfg_add_default_edge(cond_node, merge_node);
        }

        result.entry = cond_node;
        result.exit = merge_node;
        break;
    }

    case AST_WHILE_STATEMENT: {
        if (stmt->child_count < 1) break;

        ASTNode* cond = stmt->children[0];
        ast_to_string(cond, label, sizeof(label));

        CFGNode* loopcond = cfg_create_node(cfg, CFG_CONDITION, label, stmt, cond);

        if (current_symbol_table) {
            check_expression_semantics(cond, current_symbol_table, loopcond);

            if (loopcond->has_error) {
                loopcond->type = CFG_ERROR;

                char error_label[1024];
                snprintf(error_label, sizeof(error_label), "❌ WHILE %s\n%s", label, loopcond->error_message);
                if (loopcond->label) free(loopcond->label);
                loopcond->label = (char*)malloc(strlen(error_label) + 1);
                strcpy(loopcond->label, error_label);

                result.entry = loopcond;
                result.exit = loopcond;
                break;
            }
        }

        CFGNode* exitnode = cfg_create_node(cfg, CFG_MERGE, "exit-while", NULL, NULL);

        CFGNode* old_loop_exit = current_loop_exit;
        current_loop_exit = exitnode;

        CFGSegment bodyseg = { NULL, NULL };
        if (stmt->child_count > 1) {
            bodyseg = build_cfg_for_statement(cfg, stmt->children[1]);
        }

        if (bodyseg.entry) {
            cfg_add_conditional_edge(loopcond, bodyseg.entry);
        }

        if (bodyseg.exit) {
            if (!bodyseg.exit->is_break) {
                cfg_add_default_edge(bodyseg.exit, loopcond);
            }
        }

        cfg_add_default_edge(loopcond, exitnode);

        current_loop_exit = old_loop_exit;

        result.entry = loopcond;
        result.exit = exitnode;
        break;
    }

    case AST_REPEAT_STATEMENT: {
        snprintf(label, sizeof(label), "begin-repeat");
        CFGNode* repeat_entry = cfg_create_node(cfg, CFG_MERGE, label, stmt, NULL);

        CFGNode* exit_node = cfg_create_node(cfg, CFG_MERGE, "exit-repeat", NULL, NULL);

        CFGNode* old_loop_exit = current_loop_exit;
        current_loop_exit = exit_node;

        CFGSegment body_seg = { NULL, NULL };
        if (stmt->child_count > 0) {
            body_seg = build_cfg_for_statement(cfg, stmt->children[0]);
            if (body_seg.entry) {
                cfg_add_default_edge(repeat_entry, body_seg.entry);
            }
        }

        CFGNode* until_node = NULL;
        if (stmt->child_count > 1) {
            ASTNode* until_cond = stmt->children[1];
            ast_to_string(until_cond, label, sizeof(label));
            until_node = cfg_create_node(cfg, CFG_CONDITION, label, stmt, until_cond);

            if (current_symbol_table) {
                check_expression_semantics(until_cond, current_symbol_table, until_node);

                if (until_node->has_error) {
                    until_node->type = CFG_ERROR;

                    char error_label[1024];
                    snprintf(error_label, sizeof(error_label), "❌ UNTIL %s\n%s", label, until_node->error_message);
                    if (until_node->label) free(until_node->label);
                    until_node->label = (char*)malloc(strlen(error_label) + 1);
                    strcpy(until_node->label, error_label);

                    if (body_seg.exit) {
                        cfg_add_default_edge(body_seg.exit, until_node);
                    }

                    result.entry = repeat_entry;
                    result.exit = until_node;
                    current_loop_exit = old_loop_exit;
                    break;
                }
            }

            if (body_seg.exit) {
                cfg_add_default_edge(body_seg.exit, until_node);
            }

            cfg_add_conditional_edge(until_node, exit_node);
            cfg_add_default_edge(until_node, repeat_entry);
        }

        current_loop_exit = old_loop_exit;

        result.entry = repeat_entry;
        result.exit = exit_node;
        break;
    }

    case AST_BREAK_STATEMENT: {
        snprintf(label, sizeof(label), "break");
        CFGNode* node = cfg_create_node(cfg, CFG_BLOCK, label, stmt, NULL);

        if (current_loop_exit) {
            node->is_break = 1;
            cfg_add_default_edge(node, current_loop_exit);
        }

        result.entry = node;
        result.exit = node;
        break;
    }

    case AST_STATEMENT_BLOCK:
    case AST_STATEMENT_LIST: {
        result = build_cfg_for_statements(cfg, stmt);
        break;
    }

    case AST_VAR_DECLARATION: {
        snprintf(label, sizeof(label), "VAR_DECL");
        CFGNode* node = cfg_create_node(cfg, CFG_BLOCK, label, stmt, NULL);
        result.entry = node;
        result.exit = node;
        break;
    }

    default:
        break;
    }

    return result;
}

void cfg_build_from_ast(CFG* cfg, ASTNode* ast) {
    if (!cfg || !ast || ast->type != AST_PROGRAM) return;

    for (int i = 0; i < ast->child_count; i++) {
        ASTNode* func_def = ast->children[i];

        if (func_def->type != AST_FUNCTION_DEF) continue;

        char func_name[256] = "unknown";
        if (func_def->child_count > 0 && func_def->children[0]->type == AST_FUNCTION_SIGNATURE) {
            if (func_def->children[0]->value) {
                snprintf(func_name, sizeof(func_name), "%s", func_def->children[0]->value);
            }
        }

        char entry_label[512];
        snprintf(entry_label, sizeof(entry_label), "entry: %s", func_name);
        CFGNode* entry = cfg_create_node(cfg, CFG_START, entry_label, NULL, NULL);
        cfg->entry = entry;

        current_loop_exit = NULL;

        CFGSegment body_seg = { NULL, NULL };
        if (func_def->child_count > 1) {
            body_seg = build_cfg_for_statement(cfg, func_def->children[1]);

            if (body_seg.entry) {
                cfg_add_default_edge(entry, body_seg.entry);
            }
        }

        CFGNode* exit = cfg_create_node(cfg, CFG_END, "return", NULL, NULL);
        if (body_seg.exit) {
            cfg_add_default_edge(body_seg.exit, exit);
        }
        else {
            cfg_add_default_edge(entry, exit);
        }

        cfg->exit = exit;
    }

    printf("[+] CFG generated with %d nodes\n", cfg->node_count);
}

static const char* get_operation_name_internal(ASTNodeType type, const char* value) {
    if (value) return value;

    switch (type) {
    case AST_ASSIGNMENT: return "Assign";
    case AST_INDEXED_ASSIGNMENT: return "IndexAssign";
    case AST_BINARY_EXPR: return "BinOp";
    case AST_ARITHMETIC_EXPR: return "ArithOp";
    case AST_UNARY_EXPR: return "UnOp";
    case AST_ADDR_OF: return "Addr";
    case AST_DEREF: return "Deref";
    case AST_INDEX_EXPR: return "Index";
    case AST_MEMBER_ACCESS: return "Member";
    case AST_CALL_EXPR: return "FunctionCall";
    case AST_LITERAL: return "Const";
    case AST_IDENTIFIER: return "Load";
    case AST_RETURN_STATEMENT: return "Return";
    case AST_BREAK_STATEMENT: return "Break";
    case AST_CONTINUE_STATEMENT: return "Continue";
    case AST_IF_STATEMENT: return "If";
    case AST_WHILE_STATEMENT: return "While";
    case AST_REPEAT_STATEMENT: return "Repeat";
    case AST_VAR_DECLARATION: return "VarDecl";
    case AST_EXPR_STATEMENT: return "ExprStmt";
    default: return "Unknown";
    }
}

/* Публичная версия для использования другими модулями */
const char* get_operation_name(ASTNodeType type, const char* value) {
    return get_operation_name_internal(type, value);
}

static void export_ast_tree_to_dot(ASTNode* node, FILE* f, int tree_id, int* node_counter) {
    if (!node || !f) return;

    int node_id = (*node_counter)++;
    const char* op_name = get_operation_name_internal(node->type, node->value);

    char node_label[1024] = "";

    if (node->type == AST_IDENTIFIER) {
        if (node->value && node->value[0] != '\0') {
            snprintf(node_label, sizeof(node_label), "Load(%s)", node->value);
        }
        else {
            snprintf(node_label, sizeof(node_label), "Load(unknown)");
        }
    }
    else if (node->type == AST_LITERAL) {
        if (node->value && node->value[0] != '\0') {
            snprintf(node_label, sizeof(node_label), "Const(%s)", node->value);
        }
        else {
            snprintf(node_label, sizeof(node_label), "Const");
        }
    }
    else if (node->type == AST_ADDR_OF) {
        if (node->child_count > 0 && node->children[0]->value) {
            snprintf(node_label, sizeof(node_label), "Addr(%s)", node->children[0]->value);
        }
        else {
            snprintf(node_label, sizeof(node_label), "Addr");
        }
    }
    else if (node->type == AST_INDEX_EXPR) {
        if (node->child_count > 0 && node->children[0]->value) {
            snprintf(node_label, sizeof(node_label), "Indexer(%s)", node->children[0]->value);
        }
        else {
            snprintf(node_label, sizeof(node_label), "Indexer");
        }
    }
    else if (node->type == AST_CALL_EXPR) {
        if (node->value && node->value[0] != '\0') {
            snprintf(node_label, sizeof(node_label), "FunctionCall(%s)", node->value);
        }
        else {
            snprintf(node_label, sizeof(node_label), "FunctionCall");
        }
    }
    else if (node->type == AST_ASSIGNMENT) {
        snprintf(node_label, sizeof(node_label), "Store");
    }
    else if (node->type == AST_BINARY_EXPR && node->value) {
        const char* op_word = "BinOp";

        if (strcmp(node->value, "+") == 0) op_word = "Add";
        else if (strcmp(node->value, "-") == 0) op_word = "Sub";
        else if (strcmp(node->value, "*") == 0) op_word = "Mul";
        else if (strcmp(node->value, "/") == 0) op_word = "Div";
        else if (strcmp(node->value, "%") == 0) op_word = "Mod";
        else if (strcmp(node->value, "=") == 0 || strcmp(node->value, ":=") == 0) op_word = "Store";
        else if (strcmp(node->value, "==") == 0) op_word = "Eq";
        else if (strcmp(node->value, "!=") == 0) op_word = "NotEq";
        else if (strcmp(node->value, "<") == 0) op_word = "Lt";
        else if (strcmp(node->value, ">") == 0) op_word = "Gt";
        else if (strcmp(node->value, "<=") == 0) op_word = "LtEq";
        else if (strcmp(node->value, ">=") == 0) op_word = "GtEq";
        else if (strcmp(node->value, "&") == 0) op_word = "And";
        else if (strcmp(node->value, "|") == 0) op_word = "Or";
        else if (strcmp(node->value, "^") == 0) op_word = "Xor";
        else if (strcmp(node->value, "<<") == 0) op_word = "LShift";
        else if (strcmp(node->value, ">>") == 0) op_word = "RShift";

        snprintf(node_label, sizeof(node_label), "%s", op_word);
    }
    else if (node->type == AST_UNARY_EXPR && node->value) {
        const char* op_word = "UnOp";

        if (strcmp(node->value, "-") == 0) op_word = "Neg";
        else if (strcmp(node->value, "+") == 0) op_word = "Pos";
        else if (strcmp(node->value, "!") == 0) op_word = "Not";
        else if (strcmp(node->value, "~") == 0) op_word = "BitNot";

        snprintf(node_label, sizeof(node_label), "%s", op_word);
    }
    else {
        if (op_name && op_name[0] != '\0') {
            snprintf(node_label, sizeof(node_label), "%s", op_name);
        }
        else {
            snprintf(node_label, sizeof(node_label), "NodeType:%d", node->type);
        }
    }

    if (node->has_error && node->error_message) {
        char error_label[2048];
        snprintf(error_label, sizeof(error_label), "%s\n❌ %s", node_label, node->error_message);

        fprintf(f, "    tree%d_node%d [label=\"%s\", shape=ellipse, "
            "fillcolor=\"#FF6B6B\", fontcolor=white, style=filled, penwidth=2];\n",
            tree_id, node_id, error_label);
    }
    else if (node->type == AST_IDENTIFIER) {
        fprintf(f, "    tree%d_node%d [label=\"%s\", shape=box, "
            "fillcolor=\"#A8E6CF\", style=filled];\n",
            tree_id, node_id, node_label);
    }
    else if (node->type == AST_LITERAL) {
        fprintf(f, "    tree%d_node%d [label=\"%s\", shape=box, "
            "fillcolor=\"#FFD93D\", style=filled];\n",
            tree_id, node_id, node_label);
    }
    else {
        fprintf(f, "    tree%d_node%d [label=\"%s\", shape=ellipse, "
            "fillcolor=lightblue, style=filled];\n",
            tree_id, node_id, node_label);
    }

    for (int i = 0; i < node->child_count; i++) {
        int child_id = *node_counter;
        fprintf(f, "    tree%d_node%d -> tree%d_node%d;\n",
            tree_id, node_id, tree_id, child_id);
        export_ast_tree_to_dot(node->children[i], f, tree_id, node_counter);
    }
}

void escape_string_for_dot(const char* input, char* output, size_t max_len) {
    if (!input || !output) return;

    size_t j = 0;
    for (size_t i = 0; input[i] != '\0' && j < max_len - 1; i++) {
        switch (input[i]) {
        case '\n':
            if (j + 2 < max_len) {
                output[j++] = '\\';
                output[j++] = 'n';
            }
            break;
        case '\"':
            if (j + 2 < max_len) {
                output[j++] = '\\';
                output[j++] = '\"';
            }
            break;
        case '\\':
            if (j + 2 < max_len) {
                output[j++] = '\\';
                output[j++] = '\\';
            }
            break;
        case '<':
            if (j + 4 < max_len) {
                strcpy(&output[j], "&lt;");
                j += 4;
            }
            break;
        case '>':
            if (j + 4 < max_len) {
                strcpy(&output[j], "&gt;");
                j += 4;
            }
            break;
        case '&':
            if (j + 5 < max_len) {
                strcpy(&output[j], "&amp;");
                j += 5;
            }
            break;
        default:
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
}

void cfg_export_dot(CFG* cfg, const char* filename) {
    if (!cfg || !filename) return;

    FILE* f = fopen(filename, "w");
    if (!f) {
        perror("fopen (CFG DOT)");
        return;
    }

    fprintf(f, "digraph CFG {\n");
    fprintf(f, "  rankdir=TB;\n");
    fprintf(f, "  node [fontname=\"Courier\", fontsize=10];\n");
    fprintf(f, "  edge [fontname=\"Courier\", fontsize=9];\n\n");

    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];

        char final_label[2048];
        if (node->has_error && node->error_message) {
            char escaped_error[1024];
            escape_string_for_dot(node->error_message, escaped_error, sizeof(escaped_error));

            if (node->label) {
                snprintf(final_label, sizeof(final_label),
                    "%s\\n❌ %s", node->label, escaped_error);
            }
            else {
                snprintf(final_label, sizeof(final_label),
                    "❌ ERROR\\n%s", escaped_error);
            }
        }
        else if (node->label) {
            strncpy(final_label, node->label, sizeof(final_label));
        }
        else {
            snprintf(final_label, sizeof(final_label), "Node %d", node->id);
        }

        if (node->has_error) {
            fprintf(f, "  node%d [label=\"%s\", "
                "shape=box, fillcolor=\"#FF6B6B\", "
                "fontcolor=white, style=filled, penwidth=2, "
                "fontname=\"Courier-Bold\"];\n",
                node->id, final_label);
        }
        else if (node->type == CFG_CONDITION) {
            fprintf(f, "  node%d [label=\"%s\", "
                "shape=diamond, fillcolor=\"#FFD93D\", style=filled];\n",
                node->id, final_label);
        }
        else if (node->type == CFG_MERGE) {
            fprintf(f, "  node%d [label=\"%s\", "
                "shape=box, fillcolor=\"#95E1D3\", style=filled];\n",
                node->id, final_label);
        }
        else if (node->type == CFG_START) {
            fprintf(f, "  node%d [label=\"%s\", "
                "shape=circle, fillcolor=\"#6BCF7F\", style=filled];\n",
                node->id, final_label);
        }
        else if (node->type == CFG_END) {
            fprintf(f, "  node%d [label=\"%s\", "
                "shape=circle, fillcolor=\"#FF9A76\", style=filled];\n",
                node->id, final_label);
        }
        else {
            fprintf(f, "  node%d [label=\"%s\", "
                "shape=box, fillcolor=\"lightblue\", style=filled];\n",
                node->id, final_label);
        }
    }

    fprintf(f, "\n");

    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];

        if (node->defaultNext) {
            fprintf(f, "  node%d -> node%d [label=\"false\"];\n",
                node->id, node->defaultNext->id);
        }

        if (node->conditionalNext) {
            fprintf(f, "  node%d -> node%d [label=\"true\", style=dashed];\n",
                node->id, node->conditionalNext->id);
        }
    }

    fprintf(f, "\n");
    fprintf(f, "  // ============ EXPRESSION TREES ============\n\n");

    ast_tree_counter = 0;

    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* cfg_node = cfg->nodes[i];

        if (!cfg_node->op_tree) continue;

        fprintf(f, "  subgraph cluster_expr_%d {\n", ast_tree_counter);
        fprintf(f, "    style=filled;\n");
        fprintf(f, "    color=lightgrey;\n");
        fprintf(f, "    label=\"Expression Tree %d\";\n", ast_tree_counter);

        int node_counter = 0;
        export_ast_tree_to_dot(cfg_node->op_tree, f, ast_tree_counter, &node_counter);

        fprintf(f, "  }\n\n");

        ast_tree_counter++;
    }

    fprintf(f, "}\n");
    fclose(f);
}

static void mark_error_recursive(ASTNode* node, const char* error_msg) {
    if (!node) return;

    node->has_error = 1;
    if (!node->error_message) {
        node->error_message = (char*)malloc(strlen(error_msg) + 1);
        strcpy(node->error_message, error_msg);
    }
}

void check_expression_semantics(ASTNode* expr, SymbolTable* symbol_table, CFGNode* cfg_node) {
    if (!expr || !symbol_table) return;

    switch (expr->type) {
    case AST_IDENTIFIER: {
        Symbol* sym = symbol_table_lookup(symbol_table, expr->value);
        if (!sym) {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg),
                "Undeclared variable '%s'", expr->value);

            cfg_node->has_error = 1;
            cfg_node->error_message = (char*)malloc(strlen(error_msg) + 1);
            strcpy(cfg_node->error_message, error_msg);

            expr->has_error = 1;
            if (!expr->error_message) {
                expr->error_message = (char*)malloc(strlen(error_msg) + 1);
                strcpy(expr->error_message, error_msg);
            }

            printf("    [ERROR] Undeclared variable '%s'\n", expr->value);
        }
        break;
    }

    case AST_BINARY_EXPR: {
        int has_child_error = 0;
        if (expr->child_count >= 2) {
            check_expression_semantics(expr->children[0], symbol_table, cfg_node);
            check_expression_semantics(expr->children[1], symbol_table, cfg_node);

            if ((expr->children[0] && expr->children[0]->has_error) ||
                (expr->children[1] && expr->children[1]->has_error)) {
                has_child_error = 1;
            }
        }

        if (has_child_error) {
            mark_error_recursive(expr, "Child expression has error");
            cfg_node->has_error = 1;
            if (!cfg_node->error_message) {
                cfg_node->error_message = (char*)malloc(256);
                strcpy(cfg_node->error_message, "Child expression has error");
            }
        }
        break;
    }

    case AST_UNARY_EXPR: {
        if (expr->child_count > 0) {
            check_expression_semantics(expr->children[0], symbol_table, cfg_node);

            if (expr->children[0] && expr->children[0]->has_error) {
                mark_error_recursive(expr, "Child expression has error");
                cfg_node->has_error = 1;
                if (!cfg_node->error_message) {
                    cfg_node->error_message = (char*)malloc(256);
                    strcpy(cfg_node->error_message, "Child expression has error");
                }
            }
        }
        break;
    }

    case AST_CALL_EXPR: {
        Symbol* sym = symbol_table_lookup(symbol_table, expr->value);
        if (!sym || sym->type != SYM_FUNCTION) {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg),
                "Undeclared function '%s'", expr->value);

            cfg_node->has_error = 1;
            cfg_node->error_message = (char*)malloc(strlen(error_msg) + 1);
            strcpy(cfg_node->error_message, error_msg);

            expr->has_error = 1;
            if (!expr->error_message) {
                expr->error_message = (char*)malloc(strlen(error_msg) + 1);
                strcpy(expr->error_message, error_msg);
            }

            printf("    [ERROR] Undeclared function '%s'\n", expr->value);
        }

        int has_child_error = 0;
        for (int i = 0; i < expr->child_count; i++) {
            check_expression_semantics(expr->children[i], symbol_table, cfg_node);
            if (expr->children[i] && expr->children[i]->has_error) {
                has_child_error = 1;
            }
        }

        if (has_child_error) {
            mark_error_recursive(expr, "Child expression has error");
            cfg_node->has_error = 1;
            if (!cfg_node->error_message) {
                cfg_node->error_message = (char*)malloc(256);
                strcpy(cfg_node->error_message, "Child expression has error");
            }
        }
        break;
    }

    case AST_INDEX_EXPR: {
        int has_child_error = 0;
        if (expr->child_count > 0) {
            check_expression_semantics(expr->children[0], symbol_table, cfg_node);
            if (expr->children[0] && expr->children[0]->has_error) {
                has_child_error = 1;
            }
        }
        if (expr->child_count > 1) {
            check_expression_semantics(expr->children[1], symbol_table, cfg_node);
            if (expr->children[1] && expr->children[1]->has_error) {
                has_child_error = 1;
            }
        }

        if (has_child_error) {
            mark_error_recursive(expr, "Child expression has error");
            cfg_node->has_error = 1;
            if (!cfg_node->error_message) {
                cfg_node->error_message = (char*)malloc(256);
                strcpy(cfg_node->error_message, "Child expression has error");
            }
        }
        break;
    }

    default:
        break;
    }
}

void cfg_check_semantics(CFG* cfg, SymbolTable* symbol_table) {
    if (!cfg || !symbol_table) return;

    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];

        if (!node->op_tree) continue;

        printf("    [CFG Node %d] Checking expression...\n", node->id);
        check_expression_semantics(node->op_tree, symbol_table, node);
    }
}

void cfg_free(CFG* cfg) {
    if (!cfg) return;

    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];
        if (node->label) free(node->label);
        if (node->error_message) free(node->error_message);
        free(node);
    }

    if (cfg->nodes) free(cfg->nodes);
    free(cfg);
}