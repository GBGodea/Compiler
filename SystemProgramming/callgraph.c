#include "callgraph.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================
// СОЗДАНИЕ ГРАФА ВЫЗОВОВ
// ============================================================

CallGraph* callgraph_create(void) {
    CallGraph* cg = (CallGraph*)malloc(sizeof(CallGraph));
    cg->max_calls = 256;
    cg->calls = (FunctionCall*)malloc(cg->max_calls * sizeof(FunctionCall));
    cg->call_count = 0;
    return cg;
}

// ============================================================
// ДОБАВЛЕНИЕ ВЫЗОВА
// ============================================================

void callgraph_add_call(CallGraph* cg, const char* caller, const char* callee) {
    if (!cg || !caller || !callee) return;

    // Игнорируем встроенные функции
    if (strcmp(callee, "unknown") == 0) return;

    // Расширяем массив, если нужно
    if (cg->call_count >= cg->max_calls) {
        cg->max_calls *= 2;
        cg->calls = (FunctionCall*)realloc(cg->calls, cg->max_calls * sizeof(FunctionCall));
    }

    // Проверяем, есть ли уже такой вызов
    for (int i = 0; i < cg->call_count; i++) {
        if (strcmp(cg->calls[i].caller_func, caller) == 0 &&
            strcmp(cg->calls[i].callee_func, callee) == 0) {
            cg->calls[i].call_count++;
            return;
        }
    }

    // Добавляем новый вызов
    FunctionCall* call = &cg->calls[cg->call_count];
    call->caller_func = (char*)malloc(strlen(caller) + 1);
    strcpy(call->caller_func, caller);
    call->callee_func = (char*)malloc(strlen(callee) + 1);
    strcpy(call->callee_func, callee);
    call->call_count = 1;

    cg->call_count++;
}

// ============================================================
// ЭКСПОРТ В DOT
// ============================================================

void callgraph_export_dot(CallGraph* cg, const char* filename) {
    if (!cg || !filename) return;

    FILE* f = fopen(filename, "w");
    if (!f) {
        perror("fopen (callgraph DOT)");
        return;
    }

    fprintf(f, "digraph CallGraph {\n");
    fprintf(f, "  rankdir=LR;\n");
    fprintf(f, "  node [shape=box, fontname=\"Courier\", fontsize=10];\n");
    fprintf(f, "  edge [fontname=\"Courier\", fontsize=9];\n\n");

    // Собираем уникальные функции
    const char** functions = (const char**)malloc(256 * sizeof(char*));
    int func_count = 0;

    for (int i = 0; i < cg->call_count; i++) {
        // Добавляем caller
        int found = 0;
        for (int j = 0; j < func_count; j++) {
            if (strcmp(functions[j], cg->calls[i].caller_func) == 0) {
                found = 1;
                break;
            }
        }
        if (!found && func_count < 256) {
            functions[func_count++] = cg->calls[i].caller_func;
        }

        // Добавляем callee
        found = 0;
        for (int j = 0; j < func_count; j++) {
            if (strcmp(functions[j], cg->calls[i].callee_func) == 0) {
                found = 1;
                break;
            }
        }
        if (!found && func_count < 256) {
            functions[func_count++] = cg->calls[i].callee_func;
        }
    }

    // Выводим узлы
    fprintf(f, "  // Functions\n");
    for (int i = 0; i < func_count; i++) {
        // Раскрашиваем main в зелёный, остальные в белый
        const char* color = strcmp(functions[i], "main") == 0 ? "lightgreen" : "white";
        fprintf(f, "  \"%s\" [fillcolor=%s, style=filled];\n", functions[i], color);
    }

    fprintf(f, "\n  // Calls\n");
    // Выводим рёбра
    for (int i = 0; i < cg->call_count; i++) {
        if (cg->calls[i].call_count == 1) {
            fprintf(f, "  \"%s\" -> \"%s\";\n",
                cg->calls[i].caller_func, cg->calls[i].callee_func);
        }
        else {
            fprintf(f, "  \"%s\" -> \"%s\" [label=\"%d\"];\n",
                cg->calls[i].caller_func, cg->calls[i].callee_func,
                cg->calls[i].call_count);
        }
    }

    fprintf(f, "}\n");
    fclose(f);
}

// ============================================================
// ПЕЧАТЬ РЕЗЮМЕ
// ============================================================

void callgraph_print_summary(CallGraph* cg) {
    if (!cg) return;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║              CALL GRAPH SUMMARY                           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("Total function calls: %d\n\n", cg->call_count);

    // Группируем по вызывающим функциям
    const char** callers = (const char**)malloc(256 * sizeof(char*));
    int caller_count = 0;

    for (int i = 0; i < cg->call_count; i++) {
        int found = 0;
        for (int j = 0; j < caller_count; j++) {
            if (strcmp(callers[j], cg->calls[i].caller_func) == 0) {
                found = 1;
                break;
            }
        }
        if (!found && caller_count < 256) {
            callers[caller_count++] = cg->calls[i].caller_func;
        }
    }

    // Выводим по вызывающим функциям
    for (int i = 0; i < caller_count; i++) {
        printf("  %s() calls:\n", callers[i]);
        for (int j = 0; j < cg->call_count; j++) {
            if (strcmp(cg->calls[j].caller_func, callers[i]) == 0) {
                printf("    - %s() [%d times]\n", cg->calls[j].callee_func, cg->calls[j].call_count);
            }
        }
    }

    printf("\n");
    free(callers);
}

// ============================================================
// ОСВОБОЖДЕНИЕ ПАМЯТИ
// ============================================================

void callgraph_free(CallGraph* cg) {
    if (!cg) return;

    for (int i = 0; i < cg->call_count; i++) {
        free(cg->calls[i].caller_func);
        free(cg->calls[i].callee_func);
    }

    free(cg->calls);
    free(cg);
}
