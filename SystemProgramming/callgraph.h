#ifndef CALLGRAPH_H
#define CALLGRAPH_H

#include <stdio.h>

// ============================================================
// ТИП ВЫЗОВА ФУНКЦИИ
// ============================================================

typedef struct {
    char* caller_func;      // Функция, которая вызывает
    char* callee_func;      // Функция, которая вызывается
    int call_count;         // Количество вызовов
} FunctionCall;

// ============================================================
// ГРАФ ВЫЗОВОВ
// ============================================================

typedef struct {
    FunctionCall* calls;
    int call_count;
    int max_calls;
} CallGraph;

// ============================================================
// ФУНКЦИИ
// ============================================================

CallGraph* callgraph_create(void);
void callgraph_add_call(CallGraph* cg, const char* caller, const char* callee);
void callgraph_export_dot(CallGraph* cg, const char* filename);
void callgraph_print_summary(CallGraph* cg);
void callgraph_free(CallGraph* cg);

#endif