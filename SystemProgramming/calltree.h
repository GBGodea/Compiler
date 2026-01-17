#ifndef CALLTREE_H
#define CALLTREE_H

#include "ast.h"
#include <stdio.h>


typedef struct CallTreeNode {
    int id;
    char* function_name;
    struct CallTreeNode** children;
    int child_count;
    int max_children;
} CallTreeNode;

typedef struct {
    CallTreeNode** roots;
    int root_count;
    int next_id;
} CallTree;


CallTree* calltree_create(void);
CallTreeNode* calltree_create_node(CallTree* ct, const char* func_name);
void calltree_add_call(CallTree* ct, const char* caller, const char* callee);
void calltree_export_dot(CallTree* ct, const char* filename);
void calltree_free(CallTree* ct);

#endif