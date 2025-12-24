#ifndef CALLGRAPH_H
#define CALLGRAPH_H

#include <stdio.h>

typedef struct {
    char* caller_func;      
    char* callee_func;      
    int call_count;         
} FunctionCall;

typedef struct {
    FunctionCall* calls;
    int call_count;
    int max_calls;
} CallGraph;

CallGraph* callgraph_create(void);
void callgraph_add_call(CallGraph* cg, const char* caller, const char* callee);
void callgraph_export_dot(CallGraph* cg, const char* filename);
void callgraph_print_summary(CallGraph* cg);
void callgraph_free(CallGraph* cg);

#endif