#pragma once
#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

typedef enum {
    SYM_GLOBAL = 0,    // Глобальная переменная
    SYM_LOCAL = 1,     // Локальная переменная
    SYM_FUNCTION = 2,  // Функция
    SYM_PARAMETER = 3, // Параметр функции
    SYM_CONSTANT = 4   // Константа
} SymbolType;

typedef enum {
    SCOPE_GLOBAL = 0,   // Глобальная область видимости
    SCOPE_FUNCTION = 1, // Область видимости функции
    SCOPE_BLOCK = 2     // Область видимости блока
} ScopeType;

typedef struct Scope {
    int id;                   // ID области видимости
    ScopeType type;           // Тип области
    char* name;               // Имя области (имя функции или NULL)
    struct Scope* parent;     // Родительская область
    int level;                // Уровень вложенности
    int local_offset;         // Текущий оффсет для локальных переменных
    int param_offset;         // Текущий оффсет для параметров
} Scope;

typedef struct {
    char* name;               // Имя символа
    SymbolType type;          // Тип символа
    char* data_type;          // Тип данных (int, string и т.д.)

    // Информация о расположении
    int offset;               // Смещение в стеке/глобальной памяти
    int size;                 // Размер в байтах
    int index;                // Индекс в таблице символов
    int address;              // Абсолютный адрес (для глобальных)

    // Информация о массивах
    int is_array;             // Является ли массивом
    int array_size;           // Размер массива (если массив)
    int array_dimensions;     // Количество измерений

    // Область видимости
    int scope_id;             // ID области видимости
    Scope* scope;             // Ссылка на область видимости
    int scope_level;          // Уровень вложенности области

    // Флаги и метаданные
    int is_declared;          // Объявлен ли символ
    int is_initialized;       // Инициализирован ли
    int is_constant;          // Является ли константой
    int line_number;          // Номер строки объявления
    int is_used;              // Используется ли символ
    int is_modified;          // Изменяется ли значение

    // Для функций
    int param_count;          // Количество параметров
    char** param_types;       // Типы параметров
    char* return_type;        // Тип возвращаемого значения
} Symbol;

typedef struct {
    Symbol* symbols;          // Массив символов
    int symbol_count;         // Количество символов
    int max_symbols;          // Максимальное количество символов

    // Области видимости
    Scope* current_scope;     // Текущая область видимости
    Scope** scopes;           // Массив областей видимости
    int scope_count;          // Количество областей
    int max_scopes;           // Максимальное количество областей
    int next_scope_id;        // Следующий ID области

    // Счетчики для оффсетов
    int global_offset;        // Текущий оффсет для глобальных переменных
    int next_symbol_index;    // Следующий индекс символа

    // Ошибки
    char* error_messages[1024];
    int error_count;

    // Отладочная информация
    int debug_enabled;        // Включен ли отладочный вывод
} SymbolTable;

/* Основные функции таблицы символов */
SymbolTable* symbol_table_create(void);
void symbol_table_free(SymbolTable* st);

/* Управление областями видимости */
Scope* scope_create(SymbolTable* st, ScopeType type, const char* name);
void scope_enter(SymbolTable* st, Scope* scope);
void scope_exit(SymbolTable* st);
Scope* scope_get_current(SymbolTable* st);
int scope_get_level(SymbolTable* st);

/* Функции добавления символов */
void symbol_table_add_global(SymbolTable* st, const char* name, const char* data_type,
    int is_array, int array_size);
void symbol_table_add_local(SymbolTable* st, const char* name, const char* data_type,
    int is_array, int array_size);
void symbol_table_add_parameter(SymbolTable* st, const char* name, const char* data_type,
    int param_index);
void symbol_table_add_function(SymbolTable* st, const char* name, const char* return_type,
    int param_count, char** param_types);
void symbol_table_add_constant(SymbolTable* st, const char* name, const char* data_type,
    const char* value);

/* Поиск символов */
Symbol* symbol_table_lookup(SymbolTable* st, const char* name);
Symbol* symbol_table_lookup_current_scope(SymbolTable* st, const char* name);
Symbol* symbol_table_lookup_global(SymbolTable* st, const char* name);
int symbol_is_declared(SymbolTable* st, const char* name);

/* Информация о символах */
void symbol_set_used(Symbol* sym);
void symbol_set_modified(Symbol* sym);
void symbol_set_initialized(Symbol* sym);
int symbol_get_size(Symbol* sym);
int symbol_get_offset(Symbol* sym);
const char* symbol_get_type_str(SymbolType type);

/* Функции семантического анализа */
void semantic_analyze(ASTNode* ast, SymbolTable* symbol_table);
void check_expression(ASTNode* expr, SymbolTable* st, int line_num);
void mark_ast_error(ASTNode* node, const char* format, ...);

/* Отладочный вывод */
void symbol_table_print(SymbolTable* st);
void symbol_table_print_scope(SymbolTable* st, Scope* scope);
void symbol_table_print_errors(SymbolTable* st);

/* Функции для работы с ошибками */
void symbol_table_add_error(SymbolTable* st, const char* error_message);
void semantic_check_expression(ASTNode* node, SymbolTable* table, int* has_error);

#endif // SEMANTIC_H