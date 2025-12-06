#include "semantic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>





SymbolTable* symbol_table_create(void) {
    SymbolTable* st = (SymbolTable*)malloc(sizeof(SymbolTable));
    st->max_symbols = 256;
    st->symbols = (Symbol*)malloc(st->max_symbols * sizeof(Symbol));
    st->symbol_count = 0;
    return st;
}

void symbol_table_add(SymbolTable* st, const char* name, SymbolType type, const char* data_type) {
    if (!st || !name) return;

    
    for (int i = 0; i < st->symbol_count; i++) {
        if (strcmp(st->symbols[i].name, name) == 0) {
            
            return;
        }
    }

    if (st->symbol_count >= st->max_symbols) {
        st->max_symbols *= 2;
        st->symbols = (Symbol*)realloc(st->symbols, st->max_symbols * sizeof(Symbol));
    }

    Symbol* sym = &st->symbols[st->symbol_count];
    sym->name = (char*)malloc(strlen(name) + 1);
    strcpy(sym->name, name);
    sym->type = type;
    sym->data_type = data_type ? (char*)malloc(strlen(data_type) + 1) : NULL;
    if (data_type) strcpy(sym->data_type, data_type);
    sym->is_declared = 1;

    st->symbol_count++;
}

Symbol* symbol_table_find(SymbolTable* st, const char* name) {
    if (!st || !name) return NULL;

    for (int i = 0; i < st->symbol_count; i++) {
        if (strcmp(st->symbols[i].name, name) == 0) {
            return &st->symbols[i];
        }
    }
    return NULL;
}

int symbol_is_declared(SymbolTable* st, const char* name) {
    Symbol* sym = symbol_table_find(st, name);
    return sym != NULL && sym->is_declared;
}

void symbol_table_print_errors(SymbolTable* st) {
    if (!st) return;

    int error_count = 0;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║         SEMANTIC ANALYSIS REPORT                          ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("Declared symbols:\n");
    for (int i = 0; i < st->symbol_count; i++) {
        if (st->symbols[i].is_declared) {
            const char* type_str = "";
            switch (st->symbols[i].type) {
            case SYM_VARIABLE: type_str = "VAR"; break;
            case SYM_FUNCTION: type_str = "FUNC"; break;
            case SYM_PARAMETER: type_str = "PARAM"; break;
            }
            printf("  [%s] %s : %s\n", type_str, st->symbols[i].name,
                st->symbols[i].data_type ? st->symbols[i].data_type : "unknown");
        }
    }

    printf("\nUndeclared symbols used:\n");
    for (int i = 0; i < st->symbol_count; i++) {
        if (!st->symbols[i].is_declared) {
            printf("  ✗ %s (used but not declared)\n", st->symbols[i].name);
            error_count++;
        }
    }

    if (error_count > 0) {
        printf("\n[ERROR] Found %d undeclared variable(s)!\n", error_count);
    }
    else {
        printf("\n[OK] All symbols are properly declared.\n");
    }
    printf("\n");
}

void symbol_table_free(SymbolTable* st) {
    if (!st) return;

    for (int i = 0; i < st->symbol_count; i++) {
        free(st->symbols[i].name);
        if (st->symbols[i].data_type) {
            free(st->symbols[i].data_type);
        }
    }

    free(st->symbols);
    free(st);
}


void mark_ast_error(ASTNode* node, const char* format, ...) {
    if (!node) return;

    va_list args;
    va_start(args, format);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);

    node->has_error = 1;
    if (node->error_message) {
        free(node->error_message);
    }
    node->error_message = strdup(buffer);

    va_end(args);

    printf("    [SEMANTIC ERROR] %s\n", buffer);
}


void check_expression(ASTNode* expr, SymbolTable* st, int line_num) {
    if (!expr || !st) return;

    switch (expr->type) {
    case AST_IDENTIFIER: {
        if (!symbol_is_declared(st, expr->value)) {
            mark_ast_error(expr, "Undeclared variable '%s'", expr->value);

            // Добавляем в таблицу символов как необъявленную
            Symbol* existing = symbol_table_find(st, expr->value);
            if (!existing) {
                symbol_table_add(st, expr->value, SYM_VARIABLE, "unknown");
                st->symbols[st->symbol_count - 1].is_declared = 0;
            }
        }
        break;
    }

    case AST_CALL_EXPR: {
        if (expr->value && !symbol_is_declared(st, expr->value)) {
            mark_ast_error(expr, "Undeclared function '%s'", expr->value);

            Symbol* existing = symbol_table_find(st, expr->value);
            if (!existing) {
                symbol_table_add(st, expr->value, SYM_FUNCTION, "unknown");
                st->symbols[st->symbol_count - 1].is_declared = 0;
            }
        }

        // Проверяем аргументы
        for (int i = 0; i < expr->child_count; i++) {
            check_expression(expr->children[i], st, line_num);
        }
        break;
    }

    case AST_BINARY_EXPR: {
        // Проверяем операнды
        for (int i = 0; i < expr->child_count; i++) {
            check_expression(expr->children[i], st, line_num);
        }

        // Можно добавить проверку типов
        if (expr->child_count >= 2) {
            ASTNode* left = expr->children[0];
            ASTNode* right = expr->children[1];

            if (left->has_error || right->has_error) {
                mark_ast_error(expr, "Invalid operands for operator '%s'",
                    expr->value ? expr->value : "unknown");
            }
        }
        break;
    }

    case AST_UNARY_EXPR: {
        if (expr->child_count > 0) {
            check_expression(expr->children[0], st, line_num);
            if (expr->children[0]->has_error) {
                mark_ast_error(expr, "Invalid operand for unary operator '%s'",
                    expr->value ? expr->value : "unknown");
            }
        }
        break;
    }

    default:
        break;
    }
}





static void add_variables_from_list(ASTNode* id_list, SymbolTable* st, const char* data_type) {
    if (!id_list || !st) return;

    if (id_list->type == AST_IDENTIFIER) {
        
        printf("    [DEBUG] Adding variable: %s\n", id_list->value);
        symbol_table_add(st, id_list->value, SYM_VARIABLE, data_type);

        
        for (int i = 0; i < id_list->child_count; i++) {
            add_variables_from_list(id_list->children[i], st, data_type);
        }
    }
    else {
        
        printf("    [DEBUG] Processing list with %d children\n", id_list->child_count);
        for (int i = 0; i < id_list->child_count; i++) {
            add_variables_from_list(id_list->children[i], st, data_type);
        }
    }
}





static void analyze_statement(ASTNode* stmt, SymbolTable* st);

static void analyze_statement(ASTNode* stmt, SymbolTable* st) {
    if (!stmt || !st) return;

    switch (stmt->type) {
    case AST_VAR_DECLARATION: {
        printf("  [DEBUG] Processing VAR_DECLARATION\n");

        
        if (stmt->child_count >= 1) {
            
            ASTNode* id_list = stmt->children[0];

            printf("  [DEBUG] id_list type: %s, children: %d\n",
                getNodeTypeName(id_list->type), id_list->child_count);

            
            char* data_type = "unknown";
            for (int i = 1; i < stmt->child_count; i++) {
                if (stmt->children[i]) {
                    if (stmt->children[i]->type == AST_TYPE_REF) {
                        if (stmt->children[i]->value) {
                            data_type = stmt->children[i]->value;
                            break;
                        }
                        else if (stmt->children[i]->child_count > 0 &&
                            stmt->children[i]->children[0]->value) {
                            data_type = stmt->children[i]->children[0]->value;
                            break;
                        }
                    }
                }
            }

            printf("  [DEBUG] data_type: %s\n", data_type);

            
            add_variables_from_list(id_list, st, data_type);
        }
        break;
    }

    case AST_EXPR_STATEMENT:
        if (stmt->child_count > 0) {
            check_expression(stmt->children[0], st, 0);
        }
        break;

    case AST_IF_STATEMENT:
        if (stmt->child_count > 0) {
            check_expression(stmt->children[0], st, 0);
        }
        for (int i = 1; i < stmt->child_count; i++) {
            analyze_statement(stmt->children[i], st);
        }
        break;

    case AST_WHILE_STATEMENT:
        if (stmt->child_count > 0) {
            check_expression(stmt->children[0], st, 0);
        }
        if (stmt->child_count > 1) {
            analyze_statement(stmt->children[1], st);
        }
        break;

    case AST_REPEAT_STATEMENT:
        if (stmt->child_count > 0) {
            analyze_statement(stmt->children[0], st);
        }
        if (stmt->child_count > 1) {
            check_expression(stmt->children[1], st, 0);
        }
        break;

    case AST_STATEMENT_BLOCK:
    case AST_STATEMENT_LIST:
        for (int i = 0; i < stmt->child_count; i++) {
            analyze_statement(stmt->children[i], st);
        }
        break;

    default:
        break;
    }
}


void semantic_analyze(ASTNode* ast, SymbolTable* st) {
    if (!ast || !st || ast->type != AST_PROGRAM) return  

    printf("[*] Scanning for function declarations...\n");

    for (int i = 0; i < ast->child_count; i++) {
        ASTNode* func_def = ast->children[i];

        if (func_def->type != AST_FUNCTION_DEF) continue;

        
        char func_name[256] = "unknown";
        if (func_def->child_count > 0 && func_def->children[0]->type == AST_FUNCTION_SIGNATURE) {
            if (func_def->children[0]->value) {
                snprintf(func_name, sizeof(func_name), "%s", func_def->children[0]->value);
            }
        }

        
        symbol_table_add(st, func_name, SYM_FUNCTION, "function");
    }

    printf("[+] Found %d functions\n", st->symbol_count);

    
    
    

    for (int i = 0; i < ast->child_count; i++) {
        ASTNode* func_def = ast->children[i];

        if (func_def->type != AST_FUNCTION_DEF) continue;

        printf("[*] Analyzing function...\n");

        
        
        

        if (func_def->child_count > 0 && func_def->children[0]->type == AST_FUNCTION_SIGNATURE) {
            ASTNode* sig = func_def->children[0];

            
            if (sig->child_count > 0 && sig->children[0]) {
                ASTNode* arg_list = sig->children[0];

                
                if (arg_list->type == AST_ARG_DEF) {
                    
                    char* param_type = "unknown";
                    if (arg_list->child_count > 0 && arg_list->children[0]->value) {
                        param_type = arg_list->children[0]->value;
                    }
                    symbol_table_add(st, arg_list->value, SYM_PARAMETER, param_type);
                }
                else {
                    
                    for (int j = 0; j < arg_list->child_count; j++) {
                        ASTNode* arg = arg_list->children[j];
                        if (arg->type == AST_ARG_DEF) {
                            char* param_type = "unknown";
                            if (arg->child_count > 0 && arg->children[0]->value) {
                                param_type = arg->children[0]->value;
                            }
                            symbol_table_add(st, arg->value, SYM_PARAMETER, param_type);
                        }
                    }
                }
            }
        }

        
        
        

        if (func_def->child_count > 1) {
            analyze_statement(func_def->children[1], st);
        }
    }
}

void symbol_table_add_error(SymbolTable* st, const char* error_message) {
    if (!st || !error_message) return;

    if (st->error_count >= 1024) {
        printf("[WARNING] Error message buffer full!\n");
        return;
    }

    st->error_messages[st->error_count] = (char*)malloc(strlen(error_message) + 1);
    strcpy(st->error_messages[st->error_count], error_message);
    st->error_count++;
}

Symbol* symbol_table_lookup(SymbolTable* st, const char* name) {
    return symbol_table_find(st, name);
}

// В semantic.c добавь поле для отслеживания ошибок
void semantic_check_expression(ASTNode* node, SymbolTable* table, int* has_error) {
    if (!node) return;

    if (node->type == AST_IDENTIFIER) {
        // Проверяем, объявлена ли переменная
        if (!symbol_table_lookup(table, node->value)) {
            // ОШИБКА: переменная не объявлена
            node->has_error = 1;  // Добавь это поле в ASTNode
            if (!node->error_message) {
                node->error_message = (char*)malloc(256);
                snprintf(node->error_message, 256,
                    "Variable '%s' is not declared", node->value);
            }
            *has_error = 1;
            return;
        }
    }

    // Рекурсивно проверяем детей
    for (int i = 0; i < node->child_count; i++) {
        semantic_check_expression(node->children[i], table, has_error);
    }
}
