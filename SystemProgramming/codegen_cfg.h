#ifndef CODEGEN_CFG_H
#define CODEGEN_CFG_H

#include "cfg.h"
#include "semantic.h"
#include <stdio.h>

/* ================================================================
   CODEGEN_CFG.H - КОДОГЕНЕРАТОР ДЛЯ ВАШЕГО CFG ГРАФА

   Работает НЕПОСРЕДСТВЕННО с вашим CFG:
   - Обходит граф через defaultNext и conditionalNext
   - Генерирует код из op_tree в каждом узле
   - Создаёт правильные метки и переходы
   ================================================================ */

typedef struct {
    char* mnemonic;      /* ADD, SUB, LOAD, STORE, JMP и т.д. */
    char* operand1;
    char* operand2;
    char* operand3;
    char* comment;       /* Для отладки и документирования */
} Instruction;

typedef struct {
    Instruction* instructions;
    int instr_count;
    int allocated;

    /* Управление метками узлов */
    char** labels;
    int label_count;
    int next_label_id;

    /* Управление регистрами (r0-r7) */
    int* reg_in_use;      /* 0 = свободен, >0 = занят */
    int next_temp_reg;

    /* Статистика для отладки */
    int total_instructions;
    int branch_instructions;
    int memory_instructions;

    /* Управление стеком */
    int stack_offset;      /* Текущее смещение от fp для локальных переменных */
    int max_stack_offset;  /* Максимальный размер стека для функции */
    int param_offset;      /* Смещение для параметров (положительное от fp) */

    char* current_function;

    struct {
        char** var_names;
        int* var_offsets;  /* Смещение относительно fp (отрицательное для локальных) */
        int* var_types;    /* Тип переменной (1=int, 2=float, 3=bool) */
        int var_count;
    } locals;

    /* Глобальные переменные (в DRAM) */
    struct {
        char** var_names;
        int* var_addresses;
        int* var_types;
        int var_count;
    } globals;

} CodeGenerator;

/* ================================================================
   ОСНОВНОЙ API - РАБОТА С CFG ГРАФОМ
   ================================================================ */

   /* Создать новый кодогенератор */
CodeGenerator* codegen_create(void);

/* ГЛАВНАЯ ФУНКЦИЯ
   Передаёте уже построенный CFG граф
   Генератор обходит его и создаёт ассемблер */
void codegen_from_cfg(CodeGenerator* gen,
    CFG* cfg,
    SymbolTable* symtab);

/* Освободить ресурсы */
void codegen_free(CodeGenerator* gen);

/* ================================================================
   ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
   ================================================================ */

   /* Генерация кода для одного узла CFG */
void codegen_generate_cfg_node(CodeGenerator* gen,
    CFGNode* node,
    SymbolTable* symtab);

/* Генерация инструкций из операционного дерева (op_tree) */
int codegen_emit_expression(CodeGenerator* gen,
    ASTNode* expr,
    SymbolTable* symtab);

/* Генерация присваивания */
void codegen_emit_assignment(CodeGenerator* gen,
    ASTNode* expr,
    SymbolTable* symtab);

/* Добавление переменной в таблицу памяти */
void codegen_add_global_variable(CodeGenerator* gen, const char* var_name, int type_id);
void codegen_add_local_variable(CodeGenerator* gen, const char* var_name, int type_id);

/* Сбор переменных из CFG */
void collect_variables_from_cfg(CodeGenerator* gen, CFG* cfg);

/* Сбор информации о типах из CFG */
void collect_type_information(CFG* cfg);

/* Безусловный переход - генерирует JMP */
void codegen_emit_jump(CodeGenerator* gen, CFGNode* target);

/* Генерирует метку для узла */
void codegen_emit_label(CodeGenerator* gen, CFGNode* node);

/* Генерирует одну инструкцию */
void codegen_emit_instruction(CodeGenerator* gen,
    const char* mnemonic,
    const char* op1,
    const char* op2,
    const char* op3,
    const char* comment);

/* Генерация идентификатора (ЗДЕСЬ ИСПРАВЛЕНО - убрано static) */
int codegen_emit_identifier(CodeGenerator* gen,
    ASTNode* expr,
    SymbolTable* symtab);

/* ================================================================
   ФУНКЦИИ РАБОТЫ С ТИПАМИ
   ================================================================ */

int infer_type_from_expr(ASTNode* expr);
int get_type_id(const char* var_name);
void set_type_id(const char* var_name, int type_id);

/* ================================================================
   ЭКСПОРТ И ВЫВОД
   ================================================================ */

   /* Экспортировать сгенерированный код в файл ассемблера */
void codegen_export_assembly(CodeGenerator* gen, const char* filename);

/* Вывести все инструкции в консоль */
void codegen_print_instructions(CodeGenerator* gen);

/* Вывести статистику генерации */
void codegen_print_stats(CodeGenerator* gen);

#endif /* CODEGEN_CFG_H */