#include "semantic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/*
 * Size model (bytes):
 *   - Backend is word-oriented, so most scalar types occupy 4 bytes.
 *   - Arrays use element_size * array_size.
 *   - We keep `char` as a 4-byte scalar (ASCII code in low byte), which makes
 *     `array of char` usable for "strings" with current instruction set.
 */
static int data_type_size_bytes(const char* t) {
    if (!t) return 4;
    if (strcmp(t, "din") == 0) return 8; /* value (4) + runtime tag (4) */
    /* normalize common names produced by parser */
    if (strcmp(t, "long") == 0 || strcmp(t, "ulong") == 0) return 8;
    /* everything else is a word for now */
    return 4;
}

/* Вспомогательные функции */
static void add_variables_from_list(ASTNode* id_list, SymbolTable* st, const char* data_type, int is_array, int array_size);
static void analyze_statement(ASTNode* stmt, SymbolTable* st);
static Scope* create_function_scope(SymbolTable* st, const char* func_name);
static void calculate_offsets(SymbolTable* st);
static void check_unused_symbols(SymbolTable* st);


/* =========================
 * Parameter list helpers
 *
 * The parser currently builds function parameter lists by taking the first
 * AST_ARG_DEF and appending the rest as children (via addChild). That means
 * a signature like "fact(n: int, t: int)" becomes:
 *   ArgDef(n)
 *     TypeRef(int)
 *     ArgDef(t)
 *       TypeRef(int)
 *
 * Some passes expected a flat list and only registered the first parameter.
 * These helpers normalize both representations:
 *   - NULL            -> no params
 *   - AST_STATEMENT_LIST -> children are params
 *   - AST_ARG_DEF chain  -> this arg + any nested AST_ARG_DEF children
 * ========================= */

typedef struct {
    SymbolTable* st;
    int add_to_scope; /* 1 = also add as SYM_PARAMETER */
    int next_index;   /* 1-based parameter index */

    /* For function symbol signature (types) */
    char** types;
    int count;
    int cap;
} ParamCtx;

static const char* argdef_get_type(ASTNode* arg) {
    if (!arg) return "int";
    for (int i = 0; i < arg->child_count; i++) {
        ASTNode* c = arg->children[i];
        if (c && c->type == AST_TYPE_REF && c->value) {
            return c->value;
        }
    }
    return "int";
}

static void paramctx_push_type(ParamCtx* ctx, const char* t) {
    if (!ctx) return;
    if (ctx->cap <= ctx->count) {
        int new_cap = (ctx->cap == 0) ? 4 : ctx->cap * 2;
        ctx->types = (char**)realloc(ctx->types, new_cap * sizeof(char*));
        ctx->cap = new_cap;
    }
    ctx->types[ctx->count++] = strdup(t ? t : "int");
}

static void collect_params_recursive(ASTNode* node, ParamCtx* ctx) {
    if (!node || !ctx) return;

    if (node->type == AST_STATEMENT_LIST) {
        for (int i = 0; i < node->child_count; i++) {
            collect_params_recursive(node->children[i], ctx);
        }
        return;
    }

    if (node->type != AST_ARG_DEF) {
        return;
    }

    /* Current arg */
    const char* t = argdef_get_type(node);
    if (ctx->add_to_scope) {
        symbol_table_add_parameter(ctx->st, node->value, (char*)t, ctx->next_index);
    }
    paramctx_push_type(ctx, t);
    ctx->next_index++;

    /* Nested args (parser appends them as children of the first arg) */
    for (int i = 0; i < node->child_count; i++) {
        ASTNode* c = node->children[i];
        if (c && c->type == AST_ARG_DEF) {
            collect_params_recursive(c, ctx);
        }
        else if (c && c->type == AST_STATEMENT_LIST) {
            collect_params_recursive(c, ctx);
        }
    }
}

static void collect_params(ASTNode* params_node, SymbolTable* st, int add_to_scope,
    int* out_count, char*** out_types) {
    ParamCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.st = st;
    ctx.add_to_scope = add_to_scope;
    ctx.next_index = 1;

    if (params_node) {
        collect_params_recursive(params_node, &ctx);
    }

    if (out_count) *out_count = ctx.count;
    if (out_types) *out_types = ctx.types;
    else {
        /* Caller doesn't want types -> free */
        for (int i = 0; i < ctx.count; i++) free(ctx.types[i]);
        free(ctx.types);
    }
}


/* Создание таблицы символов */
SymbolTable* symbol_table_create(void) {
    SymbolTable* st = (SymbolTable*)malloc(sizeof(SymbolTable));

    /* Инициализация массива символов */
    st->max_symbols = 256;
    st->symbols = (Symbol*)malloc(st->max_symbols * sizeof(Symbol));
    st->symbol_count = 0;
    st->next_symbol_index = 1;

    /* Инициализация областей видимости */
    st->max_scopes = 64;
    st->scopes = (Scope**)malloc(st->max_scopes * sizeof(Scope*));
    st->scope_count = 0;
    st->next_scope_id = 1;

    /* Создание глобальной области видимости */
    Scope* global_scope = scope_create(st, SCOPE_GLOBAL, "global");
    scope_enter(st, global_scope);

    /* Инициализация счетчиков */
    st->global_offset = 0;

    /* Инициализация ошибок */
    st->error_count = 0;

    /* Отладка */
    st->debug_enabled = 0;

    return st;
}

/* Создание области видимости */
Scope* scope_create(SymbolTable* st, ScopeType type, const char* name) {
    if (st->scope_count >= st->max_scopes) {
        st->max_scopes *= 2;
        st->scopes = (Scope**)realloc(st->scopes, st->max_scopes * sizeof(Scope*));
    }

    Scope* scope = (Scope*)malloc(sizeof(Scope));
    scope->id = st->next_scope_id++;
    scope->type = type;
    scope->name = name ? strdup(name) : NULL;
    scope->parent = st->current_scope;
    scope->level = st->current_scope ? st->current_scope->level + 1 : 0;
    scope->local_offset = -4;  // Начинаем с -4 для локальных переменных
    scope->param_offset = 8;   // Параметры начинаются с +8

    st->scopes[st->scope_count++] = scope;
    return scope;
}

/* Вход в область видимости */
void scope_enter(SymbolTable* st, Scope* scope) {
    st->current_scope = scope;
}

/* Выход из области видимости */
void scope_exit(SymbolTable* st) {
    if (st->current_scope && st->current_scope->parent) {
        st->current_scope = st->current_scope->parent;
    }
}

/* Получение текущей области */
Scope* scope_get_current(SymbolTable* st) {
    return st->current_scope;
}

/* Получение уровня вложенности */
int scope_get_level(SymbolTable* st) {
    return st->current_scope ? st->current_scope->level : 0;
}

/* Добавление глобальной переменной */
void symbol_table_add_global(SymbolTable* st, const char* name, const char* data_type,
    int is_array, int array_size) {
    if (!st || !name) return;

    /* Проверяем, не объявлен ли символ уже */
    for (int i = 0; i < st->symbol_count; i++) {
        if (strcmp(st->symbols[i].name, name) == 0 &&
            st->symbols[i].scope->type == SCOPE_GLOBAL) {
            symbol_table_add_error(st, "Redeclaration of global variable");
            return;
        }
    }

    /* Увеличиваем размер массива при необходимости */
    if (st->symbol_count >= st->max_symbols) {
        st->max_symbols *= 2;
        st->symbols = (Symbol*)realloc(st->symbols, st->max_symbols * sizeof(Symbol));
    }

    Symbol* sym = &st->symbols[st->symbol_count];

    /* Базовые поля */
    sym->name = strdup(name);
    sym->type = SYM_GLOBAL;
    sym->data_type = data_type ? strdup(data_type) : NULL;
    sym->index = st->next_symbol_index++;

    /* Область видимости */
    sym->scope = st->current_scope;
    sym->scope_id = st->current_scope->id;
    sym->scope_level = st->current_scope->level;

    /* Информация о массиве */
    sym->is_array = is_array;
    sym->array_size = array_size;
    sym->array_dimensions = is_array ? 1 : 0;

    /* Размер и расположение */
    int base_size = data_type_size_bytes(data_type);
    sym->size = base_size;
    if (is_array && array_size > 0) {
        sym->size = base_size * array_size;
    }

    sym->offset = st->global_offset;
    sym->address = st->global_offset;  // Для глобальных offset = address
    st->global_offset += sym->size;

    /* Флаги */
    sym->is_declared = 1;
    sym->is_initialized = 0;
    sym->is_constant = 0;
    sym->is_used = 0;
    sym->is_modified = 0;
    sym->line_number = 0;

    /* Для функций (не используется для глобальных переменных) */
    sym->param_count = 0;
    sym->param_types = NULL;
    sym->return_type = NULL;

    st->symbol_count++;

    if (st->debug_enabled) {
        printf("[DEBUG] Added global: %s, offset: %d, size: %d\n",
            name, sym->offset, sym->size);
    }
}

/* Добавление локальной переменной */
void symbol_table_add_local(SymbolTable* st, const char* name, const char* data_type,
    int is_array, int array_size) {
    if (!st || !name) return;

    /* Проверяем, не объявлен ли символ уже в текущей области */
    Symbol* existing = symbol_table_lookup_current_scope(st, name);
    if (existing) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
            "Redeclaration of local variable '%s'", name);
        symbol_table_add_error(st, error_msg);
        return;
    }

    /* Увеличиваем размер массива при необходимости */
    if (st->symbol_count >= st->max_symbols) {
        st->max_symbols *= 2;
        st->symbols = (Symbol*)realloc(st->symbols, st->max_symbols * sizeof(Symbol));
    }

    Symbol* sym = &st->symbols[st->symbol_count];

    /* Базовые поля */
    sym->name = strdup(name);
    sym->type = SYM_LOCAL;
    sym->data_type = data_type ? strdup(data_type) : NULL;
    sym->index = st->next_symbol_index++;

    /* Область видимости */
    sym->scope = st->current_scope;
    sym->scope_id = st->current_scope->id;
    sym->scope_level = st->current_scope->level;

    /* Информация о массиве */
    sym->is_array = is_array;
    sym->array_size = array_size;
    sym->array_dimensions = is_array ? 1 : 0;

    /* Размер и расположение */
    int base_size = data_type_size_bytes(data_type);
    sym->size = base_size;
    if (is_array && array_size > 0) {
        sym->size = base_size * array_size;
    }

    /* Оффсет для локальных переменных отрицательный */
    sym->offset = st->current_scope->local_offset;
    st->current_scope->local_offset -= sym->size;
    sym->address = 0;  // Для локальных адрес вычисляется во время выполнения

    /* Флаги */
    sym->is_declared = 1;
    sym->is_initialized = 0;
    sym->is_constant = 0;
    sym->is_used = 0;
    sym->is_modified = 0;
    sym->line_number = 0;

    /* Для функций (не используется для локальных переменных) */
    sym->param_count = 0;
    sym->param_types = NULL;
    sym->return_type = NULL;

    st->symbol_count++;

    if (st->debug_enabled) {
        printf("[DEBUG] Added local: %s, offset: %d, size: %d, scope: %d\n",
            name, sym->offset, sym->size, sym->scope_id);
    }
}

/* Добавление параметра функции */
void symbol_table_add_parameter(SymbolTable* st, const char* name, const char* data_type,
    int param_index) {
    if (!st || !name) return;

    /* Проверяем, не объявлен ли символ уже */
    Symbol* existing = symbol_table_lookup_current_scope(st, name);
    if (existing) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
            "Redeclaration of parameter '%s'", name);
        symbol_table_add_error(st, error_msg);
        return;
    }

    /* Увеличиваем размер массива при необходимости */
    if (st->symbol_count >= st->max_symbols) {
        st->max_symbols *= 2;
        st->symbols = (Symbol*)realloc(st->symbols, st->max_symbols * sizeof(Symbol));
    }

    Symbol* sym = &st->symbols[st->symbol_count];

    /* Базовые поля */
    sym->name = strdup(name);
    sym->type = SYM_PARAMETER;
    sym->data_type = data_type ? strdup(data_type) : NULL;
    sym->index = st->next_symbol_index++;

    /* Область видимости */
    sym->scope = st->current_scope;
    sym->scope_id = st->current_scope->id;
    sym->scope_level = st->current_scope->level;

    /* Информация о массиве */
    sym->is_array = 0;
    sym->array_size = 0;
    sym->array_dimensions = 0;

    /* Размер и расположение */
    sym->size = data_type_size_bytes(data_type);

    /* Оффсет для параметров положительный */
    sym->offset = st->current_scope->param_offset;
    st->current_scope->param_offset += sym->size;
    sym->address = 0;

    /* Флаги */
    sym->is_declared = 1;
    sym->is_initialized = 1;  // Параметры считаются инициализированными
    sym->is_constant = 0;
    sym->is_used = 0;
    sym->is_modified = 0;
    sym->line_number = 0;

    /* Для функций (не используется для параметров) */
    sym->param_count = 0;
    sym->param_types = NULL;
    sym->return_type = NULL;

    st->symbol_count++;

    if (st->debug_enabled) {
        printf("[DEBUG] Added parameter: %s, offset: %d, size: %d\n",
            name, sym->offset, sym->size);
    }
}

/* Добавление функции */
void symbol_table_add_function(SymbolTable* st, const char* name, const char* return_type,
    int param_count, char** param_types) {
    if (!st || !name) return;

    /* Проверяем, не объявлена ли функция уже */
    for (int i = 0; i < st->symbol_count; i++) {
        if (st->symbols[i].type == SYM_FUNCTION &&
            strcmp(st->symbols[i].name, name) == 0) {
            symbol_table_add_error(st, "Redeclaration of function");
            return;
        }
    }

    /* Увеличиваем размер массива при необходимости */
    if (st->symbol_count >= st->max_symbols) {
        st->max_symbols *= 2;
        st->symbols = (Symbol*)realloc(st->symbols, st->max_symbols * sizeof(Symbol));
    }

    Symbol* sym = &st->symbols[st->symbol_count];

    /* Базовые поля */
    sym->name = strdup(name);
    sym->type = SYM_FUNCTION;
    sym->data_type = strdup("function");
    sym->index = st->next_symbol_index++;

    /* Область видимости */
    sym->scope = st->current_scope;  // Функции находятся в глобальной области
    sym->scope_id = st->current_scope->id;
    sym->scope_level = st->current_scope->level;

    /* Информация о массиве */
    sym->is_array = 0;
    sym->array_size = 0;
    sym->array_dimensions = 0;

    /* Размер и расположение */
    sym->size = 0;  // Функции не занимают место в стеке
    sym->offset = 0;
    sym->address = 0;  // Адрес функции будет определен при линковке

    /* Информация о функции */
    sym->param_count = param_count;
    sym->return_type = return_type ? strdup(return_type) : strdup("void");

    /* Копируем типы параметров */
    if (param_count > 0 && param_types) {
        sym->param_types = (char**)malloc(param_count * sizeof(char*));
        for (int i = 0; i < param_count; i++) {
            sym->param_types[i] = param_types[i] ? strdup(param_types[i]) : strdup("unknown");
        }
    }
    else {
        sym->param_types = NULL;
    }

    /* Флаги */
    sym->is_declared = 1;
    sym->is_initialized = 1;  // Функции считаются инициализированными
    sym->is_constant = 1;     // Функции являются константами
    sym->is_used = 0;
    sym->is_modified = 0;
    sym->line_number = 0;

    st->symbol_count++;

    if (st->debug_enabled) {
        printf("[DEBUG] Added function: %s, params: %d, return: %s\n",
            name, param_count, return_type ? return_type : "void");
    }
}

/* Добавление константы */
void symbol_table_add_constant(SymbolTable* st, const char* name, const char* data_type,
    const char* value) {
    if (!st || !name) return;

    /* Увеличиваем размер массива при необходимости */
    if (st->symbol_count >= st->max_symbols) {
        st->max_symbols *= 2;
        st->symbols = (Symbol*)realloc(st->symbols, st->max_symbols * sizeof(Symbol));
    }

    Symbol* sym = &st->symbols[st->symbol_count];

    /* Базовые поля */
    sym->name = strdup(name);
    sym->type = SYM_CONSTANT;
    sym->data_type = data_type ? strdup(data_type) : NULL;
    sym->index = st->next_symbol_index++;

    /* Область видимости */
    sym->scope = st->current_scope;
    sym->scope_id = st->current_scope->id;
    sym->scope_level = st->current_scope->level;

    /* Информация о массиве */
    sym->is_array = 0;
    sym->array_size = 0;
    sym->array_dimensions = 0;

    /* Размер и расположение */
    sym->size = data_type_size_bytes(data_type);
    sym->offset = 0;
    sym->address = 0;

    /* Для констант сохраняем значение в data_type или в отдельном поле */
    if (value) {
        /* Можно сохранить значение, но для простоты не делаем */
    }

    /* Флаги */
    sym->is_declared = 1;
    sym->is_initialized = 1;
    sym->is_constant = 1;
    sym->is_used = 0;
    sym->is_modified = 0;
    sym->line_number = 0;

    /* Для функций */
    sym->param_count = 0;
    sym->param_types = NULL;
    sym->return_type = NULL;

    st->symbol_count++;
}

/* Поиск символа в текущей и родительских областях */
Symbol* symbol_table_lookup(SymbolTable* st, const char* name) {
    if (!st || !name) return NULL;

    Scope* current = st->current_scope;

    while (current) {
        /* Ищем символ в текущей области */
        for (int i = 0; i < st->symbol_count; i++) {
            if (strcmp(st->symbols[i].name, name) == 0 &&
                st->symbols[i].scope_id == current->id) {
                return &st->symbols[i];
            }
        }

        /* Переходим к родительской области */
        current = current->parent;
    }

    return NULL;
}

/* Поиск символа только в текущей области */
Symbol* symbol_table_lookup_current_scope(SymbolTable* st, const char* name) {
    if (!st || !name || !st->current_scope) return NULL;

    for (int i = 0; i < st->symbol_count; i++) {
        if (strcmp(st->symbols[i].name, name) == 0 &&
            st->symbols[i].scope_id == st->current_scope->id) {
            return &st->symbols[i];
        }
    }

    return NULL;
}

/* Поиск глобального символа */
Symbol* symbol_table_lookup_global(SymbolTable* st, const char* name) {
    if (!st || !name) return NULL;

    for (int i = 0; i < st->symbol_count; i++) {
        if (strcmp(st->symbols[i].name, name) == 0 &&
            st->symbols[i].scope->type == SCOPE_GLOBAL) {
            return &st->symbols[i];
        }
    }

    return NULL;
}

/* Проверка, объявлен ли символ */
int symbol_is_declared(SymbolTable* st, const char* name) {
    Symbol* sym = symbol_table_lookup(st, name);
    return sym && sym->is_declared;
}

/* Отметить символ как используемый */
void symbol_set_used(Symbol* sym) {
    if (sym) {
        sym->is_used = 1;
    }
}

/* Отметить символ как измененный */
void symbol_set_modified(Symbol* sym) {
    if (sym && !sym->is_constant) {
        sym->is_modified = 1;
    }
}

/* Отметить символ как инициализированный */
void symbol_set_initialized(Symbol* sym) {
    if (sym) {
        sym->is_initialized = 1;
    }
}

/* Получить размер символа */
int symbol_get_size(Symbol* sym) {
    return sym ? sym->size : 0;
}

/* Получить оффсет символа */
int symbol_get_offset(Symbol* sym) {
    return sym ? sym->offset : 0;
}

/* Получить строковое представление типа символа */
const char* symbol_get_type_str(SymbolType type) {
    switch (type) {
    case SYM_GLOBAL: return "GLOBAL";
    case SYM_LOCAL: return "LOCAL";
    case SYM_FUNCTION: return "FUNCTION";
    case SYM_PARAMETER: return "PARAMETER";
    case SYM_CONSTANT: return "CONSTANT";
    default: return "UNKNOWN";
    }
}

/* Отметить ошибку в AST */
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

static const char* get_return_type_from_signature(ASTNode* sig) {
    if (!sig || sig->type != AST_FUNCTION_SIGNATURE) return "void";

    /* Look for return type in children */
    for (int i = 0; i < sig->child_count; i++) {
        if (sig->children[i] && sig->children[i]->type == AST_TYPE_REF) {
            return sig->children[i]->value;
        }
    }

    return "void";
}

/* Проверка выражения */
void check_expression(ASTNode* expr, SymbolTable* st, int line_num) {
    if (!expr || !st) return;

    switch (expr->type) {
    case AST_IDENTIFIER: {
        Symbol* sym = symbol_table_lookup(st, expr->value);
        if (!sym) {
            mark_ast_error(expr, "Undeclared identifier '%s'", expr->value);
        }
        else {
            /* Отмечаем символ как используемый */
            symbol_set_used(sym);
        }
        break;
    }

    case AST_ASSIGNMENT: {
        if (expr->child_count >= 2) {
            ASTNode* left = expr->children[0];
            ASTNode* right = expr->children[1];

            /* Проверяем правую часть */
            check_expression(right, st, line_num);

            /* Проверяем левую часть */
            if (left->type == AST_IDENTIFIER) {
                Symbol* sym = symbol_table_lookup(st, left->value);
                if (sym) {
                    if (sym->is_constant) {
                        mark_ast_error(expr, "Cannot assign to constant '%s'", left->value);
                    }
                    else {
                        symbol_set_modified(sym);
                        symbol_set_initialized(sym);  // ОТМЕТИТЬ КАК ИНИЦИАЛИЗИРОВАННУЮ
                    }
                }
            }
            else {
                check_expression(left, st, line_num);
            }
        }
        break;
    }

    case AST_CALL_EXPR: {
        Symbol* func_sym = symbol_table_lookup(st, expr->value);
        if (!func_sym || func_sym->type != SYM_FUNCTION) {
            mark_ast_error(expr, "Undeclared function '%s'", expr->value);
        }
        else {
            symbol_set_used(func_sym);
        }

        /* Проверяем аргументы */
        for (int i = 0; i < expr->child_count; i++) {
            check_expression(expr->children[i], st, line_num);
        }
        break;
    }

    case AST_BINARY_EXPR: {
        for (int i = 0; i < expr->child_count; i++) {
            check_expression(expr->children[i], st, line_num);
        }
        break;
    }

    case AST_UNARY_EXPR: {
        if (expr->child_count > 0) {
            check_expression(expr->children[0], st, line_num);
        }
        break;
    }

    default:
        /* Рекурсивная проверка дочерних узлов */
        for (int i = 0; i < expr->child_count; i++) {
            check_expression(expr->children[i], st, line_num);
        }
        break;
    }
}


/* Извлекаем базовый тип и информацию о массиве из typeRef.
   Для array[N] of T возвращаем base_type=T, is_array=1, array_size=N. */
static void extract_type_info(ASTNode* type_node, const char** base_type, int* is_array, int* array_size) {
    if (!base_type || !is_array || !array_size) return;
    *base_type = "int";
    *is_array = 0;
    *array_size = 0;
    if (!type_node) return;

    if (type_node->type != AST_TYPE_REF) {
        if (type_node->value) *base_type = type_node->value;
        return;
    }

    if (type_node->value && strcmp(type_node->value, "array") == 0) {
        *is_array = 1;
        if (type_node->child_count >= 2) {
            ASTNode* sz = type_node->children[0];
            ASTNode* elem = type_node->children[1];
            if (sz && sz->value) {
                int n = atoi(sz->value);
                if (n > 0) *array_size = n;
            }
            if (elem && elem->value) {
                *base_type = elem->value;
            }
        }
        return;
    }

    if (type_node->value) *base_type = type_node->value;
}

/* Добавление переменных из списка идентификаторов */
static void add_variables_from_list(ASTNode* id_list, SymbolTable* st, const char* data_type, int is_array, int array_size) {
    if (!id_list || !st) return;

    if (st->debug_enabled) {
        printf("[DEBUG] add_variables_from_list: node type=%d (%s), value=%s\n",
            id_list->type, getNodeTypeName(id_list->type),
            id_list->value ? id_list->value : "NULL");
    }

    if (id_list->type == AST_IDENTIFIER) {
        /* Одиночный идентификатор */
        if (st->current_scope->type == SCOPE_GLOBAL) {
            symbol_table_add_global(st, id_list->value, data_type, is_array, array_size);
        }
        else {
            symbol_table_add_local(st, id_list->value, data_type, is_array, array_size);
        }
    }
    else if (id_list->type == AST_ID_LIST) {
        /* Список идентификаторов через запятую */
        for (int i = 0; i < id_list->child_count; i++) {
            ASTNode* child = id_list->children[i];
            if (child && child->type == AST_IDENTIFIER) {
                if (st->current_scope->type == SCOPE_GLOBAL) {
                    symbol_table_add_global(st, child->value, data_type, is_array, array_size);
                }
                else {
                    symbol_table_add_local(st, child->value, data_type, is_array, array_size);
                }
            }
        }
    }
}

/* Анализ оператора */
static void analyze_statement(ASTNode* stmt, SymbolTable* st) {
    if (!stmt || !st) return;

    switch (stmt->type) {
    case AST_VAR_DECLARATION: {
        const char* base_type = "int";  // тип по умолчанию
        int is_array = 0;
        int array_size = 0;

        if (stmt->child_count >= 2) {
            ASTNode* id_list = stmt->children[0];
            for (int i = 1; i < stmt->child_count; i++) {
                ASTNode* child = stmt->children[i];
                if (!child) continue;
                if (child->type == AST_TYPE_REF) {
                    extract_type_info(child, &base_type, &is_array, &array_size);
                    break;
                }
                if (child->type == AST_IDENTIFIER && child->value) {
                    base_type = child->value;
                    break;
                }
            }
            add_variables_from_list(id_list, st, base_type, is_array, array_size);
        }
        else if (stmt->child_count == 1) {
            add_variables_from_list(stmt->children[0], st, base_type, 0, 0);
        }
        break;
    }

    case AST_EXPR_STATEMENT:
        if (stmt->child_count > 0) {
            check_expression(stmt->children[0], st, stmt->line_number);
        }
        break;

    case AST_IF_STATEMENT: {
        if (stmt->child_count > 0) {
            check_expression(stmt->children[0], st, stmt->line_number);
        }

        Scope* if_scope = scope_create(st, SCOPE_BLOCK, "if");
        scope_enter(st, if_scope);

        for (int i = 1; i < stmt->child_count; i++) {
            analyze_statement(stmt->children[i], st);
        }

        scope_exit(st);
        break;
    }

    case AST_WHILE_STATEMENT: {
        if (stmt->child_count > 0) {
            check_expression(stmt->children[0], st, stmt->line_number);
        }

        if (stmt->child_count > 1) {
            Scope* while_scope = scope_create(st, SCOPE_BLOCK, "while");
            scope_enter(st, while_scope);
            analyze_statement(stmt->children[1], st);
            scope_exit(st);
        }
        break;
    }

    case AST_REPEAT_STATEMENT: {
        if (stmt->child_count > 0) {
            Scope* repeat_scope = scope_create(st, SCOPE_BLOCK, "repeat");
            scope_enter(st, repeat_scope);
            analyze_statement(stmt->children[0], st);
            scope_exit(st);
        }
        if (stmt->child_count > 1) {
            check_expression(stmt->children[1], st, stmt->line_number);
        }
        break;
    }

    case AST_STATEMENT_BLOCK: {
        if (st->current_scope->type == SCOPE_FUNCTION) {
            for (int i = 0; i < stmt->child_count; i++) {
                analyze_statement(stmt->children[i], st);
            }
        }
        else {
            Scope* block_scope = scope_create(st, SCOPE_BLOCK, "block");
            scope_enter(st, block_scope);
            for (int i = 0; i < stmt->child_count; i++) {
                analyze_statement(stmt->children[i], st);
            }
            scope_exit(st);
        }
        break;
    }

    case AST_STATEMENT_LIST: {
        for (int i = 0; i < stmt->child_count; i++) {
            analyze_statement(stmt->children[i], st);
        }
        break;
    }

    default:
        break;
    }
}



void semantic_analyze(ASTNode* ast, SymbolTable* st) {
    if (!ast || !st || ast->type != AST_PROGRAM) return;

    printf("[*] Starting semantic analysis...\n");
    st->debug_enabled = 1;

    /* Первый проход: добавление функций */
    for (int i = 0; i < ast->child_count; i++) {
        ASTNode* func_def = ast->children[i];
        if (func_def->type != AST_FUNCTION_DEF) continue;

        char func_name[256] = "unknown";
        char* return_type = "void";

        if (func_def->child_count > 0 && func_def->children[0]->type == AST_FUNCTION_SIGNATURE) {
            ASTNode* sig = func_def->children[0];
            if (sig->value) {
                snprintf(func_name, sizeof(func_name), "%s", sig->value);
            }

            /* Определяем тип возвращаемого значения */
            if (sig->child_count > 1) {
                ASTNode* type_node = sig->children[1];
                if (type_node && type_node->type == AST_TYPE_REF) {
                    return_type = type_node->value ? type_node->value : "void";
                }
            }
        }

        /* Собираем параметры из сигнатуры (один или несколько) */
        int param_count = 0;
        char** param_types = NULL;
        if (func_def->child_count > 0 && func_def->children[0]->type == AST_FUNCTION_SIGNATURE) {
            ASTNode* sig = func_def->children[0];
            if (sig->child_count > 0) {
                ASTNode* params_node = sig->children[0];
                collect_params(params_node, st, 0, &param_count, &param_types);
            }
        }

        symbol_table_add_function(st, func_name, return_type, param_count, param_types);

        /* Освобождаем временный список типов */
        if (param_types) {
            for (int k = 0; k < param_count; k++) free(param_types[k]);
            free(param_types);
        }
    }

    /* Второй проход: анализ каждой функции */
    for (int i = 0; i < ast->child_count; i++) {
        ASTNode* func_def = ast->children[i];
        if (func_def->type != AST_FUNCTION_DEF) continue;

        char func_name[256] = "unknown";
        if (func_def->child_count > 0 && func_def->children[0]->type == AST_FUNCTION_SIGNATURE) {
            if (func_def->children[0]->value) {
                snprintf(func_name, sizeof(func_name), "%s", func_def->children[0]->value);
            }
        }

        /* Создаем область видимости функции */
        Scope* func_scope = scope_create(st, SCOPE_FUNCTION, func_name);
        scope_enter(st, func_scope);

        /* Добавляем параметры функции */
        if (func_def->child_count > 0) {
            ASTNode* sig = func_def->children[0];
            if (sig && sig->type == AST_FUNCTION_SIGNATURE && sig->child_count > 0) {
                /* Поддерживаем обе формы AST: список и "цепочку" ArgDef */
                collect_params(sig->children[0], st, 1, NULL, NULL);
            }
        }

        /* Анализируем тело функции - НЕ создаем дополнительную область для блока */
        if (func_def->child_count > 1) {
            ASTNode* body = func_def->children[1];
            if (body) {
                /* Анализируем содержимое тела функции в текущей области функции */
                for (int j = 0; j < body->child_count; j++) {
                    analyze_statement(body->children[j], st);
                }
            }
        }

        /* Выходим из области видимости функции */
        scope_exit(st);
    }

    /* Вычисляем оффсеты */
    calculate_offsets(st);

    /* Проверяем неиспользуемые символы */
    check_unused_symbols(st);

    printf("[+] Semantic analysis complete\n");
}

/* Вычисление оффсетов */
static void calculate_offsets(SymbolTable* st) {
    /* Оффсеты для глобальных переменных уже вычислены */
    /* Оффсеты для локальных переменных и параметров вычислены при добавлении */

    printf("[*] Calculating offsets...\n");

    for (int i = 0; i < st->symbol_count; i++) {
        Symbol* sym = &st->symbols[i];

        if (sym->type == SYM_LOCAL || sym->type == SYM_PARAMETER) {
            printf("  %s: offset = %d, size = %d, scope = %d\n",
                sym->name, sym->offset, sym->size, sym->scope_id);
        }
    }
}

/* Проверка неиспользуемых символов */
static void check_unused_symbols(SymbolTable* st) {
    printf("[*] Checking for unused symbols...\n");

    int unused_count = 0;

    for (int i = 0; i < st->symbol_count; i++) {
        Symbol* sym = &st->symbols[i];

        if (!sym->is_used && sym->type != SYM_FUNCTION &&
            !sym->is_constant && sym->scope->type != SCOPE_GLOBAL) {
            printf("  [WARNING] Unused %s: %s\n",
                symbol_get_type_str(sym->type), sym->name);
            unused_count++;
        }
    }

    if (unused_count > 0) {
        printf("  Found %d unused symbol(s)\n", unused_count);
    }
}

/* Добавление ошибки */
void symbol_table_add_error(SymbolTable* st, const char* error_message) {
    if (!st || !error_message) return;

    if (st->error_count >= 1024) {
        printf("[WARNING] Error message buffer full!\n");
        return;
    }

    st->error_messages[st->error_count] = strdup(error_message);
    st->error_count++;
}

/* Печать таблицы символов */
void symbol_table_print(SymbolTable* st) {
    if (!st) return;

    printf("\n════════════════════════════════════════════════════════════\n");
    printf("SYMBOL TABLE (%d symbols):\n", st->symbol_count);
    printf("════════════════════════════════════════════════════════════\n");

    for (int i = 0; i < st->symbol_count; i++) {
        Symbol* sym = &st->symbols[i];

        printf("%3d. %-10s %-15s ",
            sym->index, symbol_get_type_str(sym->type), sym->name);

        if (sym->data_type) {
            printf("type: %-10s ", sym->data_type);
        }

        if (sym->is_array) {
            printf("array[%d] ", sym->array_size);
        }

        if (sym->type == SYM_LOCAL || sym->type == SYM_PARAMETER ||
            sym->type == SYM_GLOBAL) {
            printf("offset: %4d ", sym->offset);
            printf("size: %3d ", sym->size);
        }

        if (sym->scope) {
            printf("scope: %d (", sym->scope_id);
            if (sym->scope->name) {
                printf("%s", sym->scope->name);
            }
            else {
                printf("level %d", sym->scope->level);
            }
            printf(") ");
        }

        /* Флаги */
        if (sym->is_constant) printf("[CONST] ");
        if (sym->is_initialized) printf("[INIT] ");
        if (sym->is_used) printf("[USED] ");
        if (sym->is_modified) printf("[MOD] ");

        printf("\n");
    }
}

/* Печать ошибок */
void symbol_table_print_errors(SymbolTable* st) {
    if (!st) return;

    printf("\n════════════════════════════════════════════════════════════\n");
    printf("SEMANTIC ERRORS (%d errors):\n", st->error_count);
    printf("════════════════════════════════════════════════════════════\n");

    if (st->error_count == 0) {
        printf("No semantic errors found.\n");
        return;
    }

    for (int i = 0; i < st->error_count; i++) {
        printf("%3d. %s\n", i + 1, st->error_messages[i]);
    }
}

/* Освобождение памяти */
void symbol_table_free(SymbolTable* st) {
    if (!st) return;

    /* Освобождаем символы */
    for (int i = 0; i < st->symbol_count; i++) {
        Symbol* sym = &st->symbols[i];

        free(sym->name);
        if (sym->data_type) free(sym->data_type);
        if (sym->return_type) free(sym->return_type);

        if (sym->param_types) {
            for (int j = 0; j < sym->param_count; j++) {
                free(sym->param_types[j]);
            }
            free(sym->param_types);
        }
    }

    free(st->symbols);

    /* Освобождаем области видимости */
    for (int i = 0; i < st->scope_count; i++) {
        if (st->scopes[i]->name) free(st->scopes[i]->name);
        free(st->scopes[i]);
    }
    free(st->scopes);

    /* Освобождаем ошибки */
    for (int i = 0; i < st->error_count; i++) {
        free(st->error_messages[i]);
    }

    free(st);
}