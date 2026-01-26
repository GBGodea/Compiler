#pragma once
#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "cfg.h"
#include "semantic.h"

#ifdef __cplusplus
extern "C" {
#endif
    typedef struct {
        int emit_comments;      /* 1: добавлять комментарии в asm */
        int emit_start_stub;    /* 1: добавить _start: CALL _func_main; HLT */
    } CodegenOptions;

    CodegenOptions codegen_default_options(void);

    /* Генерация в FILE* (1 = ok, 0 = error) */
    int codegen_generate_stream(const CFG* cfg, const SymbolTable* st,
        FILE* out,
        CodegenOptions opt);

    /* Генерация в файл (1 = ok, 0 = error) */
    int codegen_generate_file(const CFG* cfg, const SymbolTable* st,
        const char* output_path,
        CodegenOptions opt);

#ifdef __cplusplus
}
#endif

#endif
