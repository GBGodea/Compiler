#include "codegen_cfg.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define MAX_REGISTERS        8
#define MAX_VARIABLES        256
#define MAX_INSTRUCTIONS     4096
#define MAX_TYPE_TABLE       512

/* ================================================================
   ТАБЛИЦА ТИПОВ - отслеживание типов переменных
   ================================================================ */

typedef struct type_info {
    char var_name[64];
    int type_id;        /* 0=unknown, 1=int, 2=float, 3=bool */
    int is_constant;
    int constant_value; /* для константных значений */
} VariableTypeInfo;

typedef struct {
    VariableTypeInfo types[MAX_TYPE_TABLE];
    int count;
} TypeTable;

static TypeTable* type_table = NULL;

/* Прототипы для функций работы со стеком */
static int get_local_variable_offset(CodeGenerator* gen, const char* var_name);
static void add_local_variable(CodeGenerator* gen, const char* var_name, int type_id);
static int codegen_load_variable(CodeGenerator* gen, const char* var_name, int type_id);
static void codegen_store_variable(CodeGenerator* gen, const char* var_name,
    int value_reg, int type_id);
static int get_global_variable_address(CodeGenerator* gen, const char* var_name);

/* Прототипы для функций работы с инструкциями стековой памяти */
static const char* get_load_stack_instr(int type_id);
static const char* get_store_stack_instr(int type_id);

void type_table_init(void)
{
    type_table = (TypeTable*)malloc(sizeof(TypeTable));
    type_table->count = 0;
}

void type_table_free(void)
{
    if (type_table) {
        free(type_table);
        type_table = NULL;
    }
}

int get_type_id(const char* var_name)
{
    if (!type_table || !var_name) {
        return 0; /* unknown */
    }

    for (int i = 0; i < type_table->count; i++) {
        if (strcmp(type_table->types[i].var_name, var_name) == 0) {
            return type_table->types[i].type_id;
        }
    }
    return 0; /* unknown */
}

void set_type_id(const char* var_name, int type_id)
{
    if (!type_table || !var_name || type_table->count >= MAX_TYPE_TABLE) {
        return;
    }

    /* проверяем, есть ли уже такая переменная */
    for (int i = 0; i < type_table->count; i++) {
        if (strcmp(type_table->types[i].var_name, var_name) == 0) {
            type_table->types[i].type_id = type_id;
            return;
        }
    }

    /* добавляем новую */
    strcpy(type_table->types[type_table->count].var_name, var_name);
    type_table->types[type_table->count].type_id = type_id;
    type_table->types[type_table->count].is_constant = 0;
    type_table->count++;
}

const char* get_type_suffix(int type_id)
{
    switch (type_id) {
    case 1: return "i"; /* int */
    case 2: return "f"; /* float */
    case 3: return "b"; /* bool */
    default: return "";
    }
}

int infer_type_from_expr(ASTNode* expr)
{
    if (!expr) {
        return 0; /* unknown */
    }

    if (expr->type == AST_LITERAL) {
        if (expr->value) {
            /* определяем тип по содержимому */
            /* если содержит точку - float */
            if (strchr(expr->value, '.') != NULL) {
                return 2; /* float */
            }
            /* если true/false - bool */
            else if (strcmp(expr->value, "true") == 0 ||
                strcmp(expr->value, "false") == 0) {
                return 3; /* bool */
            }
            /* если число - int */
            else if (isdigit((unsigned char)expr->value[0]) ||
                (expr->value[0] == '-' && isdigit((unsigned char)expr->value[1]))) {
                return 1; /* int */
            }
        }
        return 0;
    }

    if (expr->type == AST_IDENTIFIER) {
        return get_type_id(expr->value);
    }

    /* для бинарных операций - тип определяется по типам операндов */
    if (expr->type == AST_BINARY_EXPR && expr->child_count > 0) {
        int type1 = infer_type_from_expr(expr->children[0]);
        int type2 = expr->child_count > 1 ? infer_type_from_expr(expr->children[1]) : 0;

        /* если типы совпадают, возвращаем этот тип */
        if (type1 == type2 && type1 != 0) {
            return type1;
        }

        /* если один из операндов float, результат float */
        if (type1 == 2 || type2 == 2) {
            return 2;
        }

        /* если один из операндов int, результат int */
        if (type1 == 1 || type2 == 1) {
            return 1;
        }

        /* если один из операндов bool, результат bool */
        if (type1 == 3 || type2 == 3) {
            return 3;
        }

        return type1; /* возвращаем тип первого операнда */
    }

    return 0; /* unknown */
}

/* ================================================================
   ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ - УПРАВЛЕНИЕ РЕГИСТРАМИ
   ================================================================ */

static int next_free_register(CodeGenerator* gen, int start_from)
{
    for (int i = start_from; i < MAX_REGISTERS; i++) {
        if (!gen->reg_in_use[i]) {
            gen->reg_in_use[i] = 1;
            return i;
        }
    }
    return -1;
}

static void free_register(CodeGenerator* gen, int reg_id)
{
    if (reg_id >= 0 && reg_id < MAX_REGISTERS) {
        gen->reg_in_use[reg_id] = 0;
    }
}

/* ================================================================
   ГЕНЕРАЦИЯ ИНСТРУКЦИЙ
   ================================================================ */

void codegen_emit_instruction(CodeGenerator* gen,
    const char* mnemonic,
    const char* op1,
    const char* op2,
    const char* op3,
    const char* comment)
{
    if (!gen || !mnemonic) {
        return;
    }

    if (gen->instr_count >= gen->allocated) {
        gen->allocated = gen->allocated * 2;
        gen->instructions = (Instruction*)realloc(
            gen->instructions,
            gen->allocated * sizeof(Instruction)
        );
    }

    Instruction* instr = &gen->instructions[gen->instr_count++];

    instr->mnemonic = (char*)malloc(strlen(mnemonic) + 1);
    strcpy(instr->mnemonic, mnemonic);

    instr->operand1 = op1 ? (char*)malloc(strlen(op1) + 1) : NULL;
    if (op1) strcpy(instr->operand1, op1);

    instr->operand2 = op2 ? (char*)malloc(strlen(op2) + 1) : NULL;
    if (op2) strcpy(instr->operand2, op2);

    instr->operand3 = op3 ? (char*)malloc(strlen(op3) + 1) : NULL;
    if (op3) strcpy(instr->operand3, op3);

    instr->comment = comment ? (char*)malloc(strlen(comment) + 1) : NULL;
    if (comment) strcpy(instr->comment, comment);

    gen->total_instructions++;

    if (strstr(mnemonic, "J") != NULL || strcmp(mnemonic, "CALL") == 0) {
        gen->branch_instructions++;
    }

    if (strcmp(mnemonic, "LD") == 0 || strcmp(mnemonic, "ST") == 0 ||
        strcmp(mnemonic, "LDB") == 0 || strcmp(mnemonic, "STB") == 0 ||
        strcmp(mnemonic, "LDW") == 0 || strcmp(mnemonic, "STW") == 0 ||
        strcmp(mnemonic, "LDC") == 0) {
        gen->memory_instructions++;
    }
}

void codegen_emit_label(CodeGenerator* gen, CFGNode* node)
{
    if (!gen || !node) {
        return;
    }

    char label[64];

    if (node->type == CFG_START) {
        sprintf(label, "entry_node%d", node->id);
    }
    else if (node->type == CFG_END) {
        sprintf(label, "exit_node%d", node->id);
    }
    else if (node->type == CFG_CONDITION) {
        sprintf(label, "cond_node%d", node->id);
    }
    else if (node->type == CFG_MERGE) {
        sprintf(label, "merge_node%d", node->id);
    }
    else {
        sprintf(label, "block_node%d", node->id);
    }

    if (gen->instr_count >= gen->allocated) {
        gen->allocated = gen->allocated * 2;
        gen->instructions = (Instruction*)realloc(
            gen->instructions,
            gen->allocated * sizeof(Instruction)
        );
    }

    Instruction* instr = &gen->instructions[gen->instr_count++];
    instr->mnemonic = (char*)malloc(strlen(label) + 2);
    sprintf(instr->mnemonic, "%s:", label);
    instr->operand1 = NULL;
    instr->operand2 = NULL;
    instr->operand3 = NULL;
    instr->comment = (char*)malloc(8);
    strcpy(instr->comment, "[LABEL]");
}

void codegen_emit_jump(CodeGenerator* gen, CFGNode* target)
{
    if (!gen || !target) {
        return;
    }

    char target_label[64];

    if (target->type == CFG_START) {
        sprintf(target_label, "entry_node%d", target->id);
    }
    else if (target->type == CFG_END) {
        sprintf(target_label, "exit_node%d", target->id);
    }
    else if (target->type == CFG_CONDITION) {
        sprintf(target_label, "cond_node%d", target->id);
    }
    else if (target->type == CFG_MERGE) {
        sprintf(target_label, "merge_node%d", target->id);
    }
    else {
        sprintf(target_label, "block_node%d", target->id);
    }

    codegen_emit_instruction(gen, "JMP", target_label, NULL, NULL,
        "unconditional jump");
}

/* ================================================================
   ВСПОМОГАТЕЛЬНАЯ ФУНКЦИЯ ДЛЯ ПОЛУЧЕНИЯ АДРЕСА ГЛОБАЛЬНОЙ ПЕРЕМЕННОЙ
   ================================================================ */

static int get_global_variable_address(CodeGenerator* gen, const char* var_name)
{
    if (!gen || !var_name) {
        return 0x8000; /* адрес по умолчанию в DRAM */
    }

    for (int i = 0; i < gen->globals.var_count; i++) {
        if (strcmp(gen->globals.var_names[i], var_name) == 0) {
            return gen->globals.var_addresses[i];
        }
    }

    /* если переменная не найдена, добавляем её как глобальную */
    codegen_add_global_variable(gen, var_name, 1); /* по умолчанию int */

    /* возвращаем адрес только что добавленной переменной */
    return gen->globals.var_addresses[gen->globals.var_count - 1];
}

/* ================================================================
   ФУНКЦИИ ДЛЯ РАБОТЫ С ТИПАМИ ВО ВРЕМЯ ГЕНЕРАЦИИ КОДА
   ================================================================ */

   /* Возвращает мнемонику для операции в зависимости от типа */
static const char* get_arith_mnemonic(const char* op, int type_id)
{
    if (type_id == 2) { /* float */
        /* В архитектуре нет отдельных инструкций для float,
           используем те же инструкции, что и для int */
        if (strcmp(op, "+") == 0) return "ADD";
        if (strcmp(op, "-") == 0) return "SUB";
        if (strcmp(op, "*") == 0) return "MUL";
        if (strcmp(op, "/") == 0) return "DIV";
        if (strcmp(op, "%") == 0) return "MOD";
    }
    else { /* int или bool */
        if (strcmp(op, "+") == 0) return "ADD";
        if (strcmp(op, "-") == 0) return "SUB";
        if (strcmp(op, "*") == 0) return "MUL";
        if (strcmp(op, "/") == 0) return "DIV";
        if (strcmp(op, "%") == 0) return "MOD";
        if (strcmp(op, "&") == 0) return "AND";
        if (strcmp(op, "|") == 0) return "OR";
        if (strcmp(op, "^") == 0) return "XOR";
        if (strcmp(op, "<<") == 0) return "SHL";
        if (strcmp(op, ">>") == 0) return "SHR";
    }
    return "ADD"; /* по умолчанию */
}

/* Возвращает инструкцию загрузки в зависимости от типа */
static const char* get_load_instr(int type_id)
{
    switch (type_id) {
    case 1: return "LD";  /* int - 32-битная загрузка */
    case 2: return "LD";  /* float - 32-битная загрузка */
    case 3: return "LDB"; /* bool - 8-битная загрузка */
    default: return "LD";
    }
}

/* Возвращает инструкцию сохранения в зависимости от типа */
static const char* get_store_instr(int type_id)
{
    switch (type_id) {
    case 1: return "ST";  /* int - 32-битное сохранение */
    case 2: return "ST";  /* float - 32-битное сохранение */
    case 3: return "STB"; /* bool - 8-битное сохранение */
    default: return "ST";
    }
}

/* Возвращает инструкцию загрузки из стека (SRAM) */
static const char* get_load_stack_instr(int type_id)
{
    /* Для стека (SRAM) используем LDS для всех типов 32-битных данных */
    (void)type_id; /* Не используется, но убираем предупреждение */
    return "LDS";
}

/* Возвращает инструкцию сохранения в стек (SRAM) */
static const char* get_store_stack_instr(int type_id)
{
    /* Для стека (SRAM) используем STS для всех типов 32-битных данных */
    (void)type_id; /* Не используется, но убираем предупреждение */
    return "STS";
}

/* Возвращает инструкцию загрузки константы в зависимости от типа */
static const char* get_move_instr(int type_id)
{
    switch (type_id) {
    case 1: return "MOVI"; /* int */
    case 2: return "MOVF"; /* float */
    case 3: return "MOVI"; /* bool (0 или 1) */
    default: return "MOVI";
    }
}

/* ================================================================
   ГЕНЕРАЦИЯ ВЫРАЖЕНИЙ С УЧЁТОМ ТИПОВ
   ================================================================ */

static int codegen_emit_binary_expr(CodeGenerator* gen,
    ASTNode* expr,
    SymbolTable* symtab)
{
    if (!expr || expr->child_count < 2) {
        return -1;
    }

    /* Специальная обработка присваивания */
    if (expr->value && strcmp(expr->value, ":=") == 0) {
        codegen_emit_assignment(gen, expr, symtab);
        return -1; /* присваивание не возвращает значение */
    }

    /* Генерируем левую часть */
    int left_reg = codegen_emit_expression(gen, expr->children[0], symtab);
    if (left_reg < 0) return -1;

    /* Генерируем правую часть */
    int right_reg = codegen_emit_expression(gen, expr->children[1], symtab);
    if (right_reg < 0) {
        free_register(gen, left_reg);
        return -1;
    }

    char left_str[8], right_str[8], result_str[8];
    sprintf(left_str, "r%d", left_reg);
    sprintf(right_str, "r%d", right_reg);
    sprintf(result_str, "r%d", left_reg);

    /* Генерируем инструкцию */
    if (strcmp(expr->value, "+") == 0) {
        codegen_emit_instruction(gen, "ADD", result_str, left_str, right_str,
            "addition");
    }
    else if (strcmp(expr->value, "-") == 0) {
        codegen_emit_instruction(gen, "SUB", result_str, left_str, right_str,
            "subtraction");
    }
    else if (strcmp(expr->value, "*") == 0) {
        codegen_emit_instruction(gen, "MUL", result_str, left_str, right_str,
            "multiplication");
    }
    else if (strcmp(expr->value, "/") == 0) {
        codegen_emit_instruction(gen, "DIV", result_str, left_str, right_str,
            "division");
    }
    else if (strcmp(expr->value, ">") == 0 ||
        strcmp(expr->value, "<") == 0 ||
        strcmp(expr->value, "==") == 0 ||
        strcmp(expr->value, "!=") == 0) {

        /* Общий обработчик сравнений с глобальными уникальными метками */

        /* Генерация уникальных меток */
        static int cmp_counter = 0;
        int current_label = cmp_counter++;

        char true_label[64], false_label[64], end_label[64];
        sprintf(true_label, "cmp_true_%d", current_label);
        sprintf(false_label, "cmp_false_%d", current_label);
        sprintf(end_label, "cmp_end_%d", current_label);

        /* Сравнение */
        codegen_emit_instruction(gen, "CMP", left_str, right_str, NULL,
            "compare");

        /* Установка результата в 0 (false) по умолчанию */
        codegen_emit_instruction(gen, "MOVI", result_str, "#0", NULL,
            "default to false");

        /* Условный переход в зависимости от операции */
        if (strcmp(expr->value, ">") == 0) {
            codegen_emit_instruction(gen, "JGT", true_label, NULL, NULL,
                "jump if greater");
        }
        else if (strcmp(expr->value, "<") == 0) {
            codegen_emit_instruction(gen, "JLT", true_label, NULL, NULL,
                "jump if less");
        }
        else if (strcmp(expr->value, "==") == 0) {
            codegen_emit_instruction(gen, "JEQ", true_label, NULL, NULL,
                "jump if equal");
        }
        else if (strcmp(expr->value, "!=") == 0) {
            codegen_emit_instruction(gen, "JNE", true_label, NULL, NULL,
                "jump if not equal");
        }

        /* Переход к false случаю */
        codegen_emit_instruction(gen, "JMP", false_label, NULL, NULL,
            "jump to false");

        /* True case - устанавливаем результат в 1 */
        codegen_emit_instruction(gen, true_label, NULL, NULL, NULL,
            "true case");
        codegen_emit_instruction(gen, "MOVI", result_str, "#1", NULL,
            "set to true");
        codegen_emit_instruction(gen, "JMP", end_label, NULL, NULL,
            "jump to end");

        /* False case - результат уже 0 */
        codegen_emit_instruction(gen, false_label, NULL, NULL, NULL,
            "false case");

        /* End of comparison */
        codegen_emit_instruction(gen, end_label, NULL, NULL, NULL,
            "end comparison");
    }

    free_register(gen, right_reg);
    return left_reg;
}

/* ================================================================
   ГЕНЕРАЦИЯ ИДЕНТИФИКАТОРА (ИСПРАВЛЕННАЯ ВЕРСИЯ - НЕСТАТИЧЕСКАЯ)
   ================================================================ */

int codegen_emit_identifier(CodeGenerator* gen,
    ASTNode* expr,
    SymbolTable* symtab)
{
    (void)symtab;

    if (!expr || !expr->value) {
        return -1;
    }

    /* Получаем тип переменной */
    int type_id = get_type_id(expr->value);

    /* Проверяем, есть ли переменная в локальных (в стеке) */
    int is_local = 0;
    for (int i = 0; i < gen->locals.var_count; i++) {
        if (strcmp(gen->locals.var_names[i], expr->value) == 0) {
            is_local = 1;
            break;
        }
    }

    if (is_local) {
        /* Загружаем локальную переменную из стека (SRAM) */
        return codegen_load_variable(gen, expr->value, type_id);
    }
    else {
        /* Переменная не найдена в локальных - возможно, это глобальная переменная */
        /* Выделяем регистр для адреса переменной */
        int addr_reg = next_free_register(gen, 0);
        if (addr_reg < 0) {
            return -1;
        }

        /* Выделяем регистр для значения переменной */
        int value_reg = next_free_register(gen, 0);
        if (value_reg < 0) {
            free_register(gen, addr_reg);
            return -1;
        }

        char addr_reg_str[8], value_reg_str[8];
        sprintf(addr_reg_str, "r%d", addr_reg);
        sprintf(value_reg_str, "r%d", value_reg);

        const char* ld_instr = get_load_instr(type_id);

        /* Добавляем как глобальную переменную в DRAM */
        int var_address = get_global_variable_address(gen, expr->value);
        char var_addr[64];
        sprintf(var_addr, "#0x%x", var_address);

        char comment[128];
        sprintf(comment, "load global variable address (0x%x)", var_address);

        /* Загружаем адрес переменной в регистр */
        codegen_emit_instruction(gen, "MOVI", addr_reg_str, var_addr, NULL,
            comment);

        /* Загружаем значение переменной по адресу из DRAM */
        codegen_emit_instruction(gen, ld_instr, value_reg_str, addr_reg_str, NULL,
            "load global variable value from DRAM");

        free_register(gen, addr_reg);
        return value_reg;
    }
}

static int codegen_emit_literal(CodeGenerator* gen, ASTNode* expr)
{
    if (!expr || !expr->value) {
        return -1;
    }

    int reg_id = next_free_register(gen, 0);
    if (reg_id < 0) {
        return -1;
    }

    char reg_str[8];
    sprintf(reg_str, "r%d", reg_id);

    /* определяем тип константы */
    int type_id = infer_type_from_expr(expr);

    const char* mov_instr = get_move_instr(type_id);

    char const_str[64];

    /* Обработка bool значений */
    if (type_id == 3) {
        if (strcmp(expr->value, "true") == 0) {
            sprintf(const_str, "#1");
        }
        else {
            sprintf(const_str, "#0");
        }
    }
    else {
        /* Для чисел используем # перед значением */
        sprintf(const_str, "#%s", expr->value);
    }

    codegen_emit_instruction(gen, mov_instr, reg_str, const_str, NULL,
        "load constant");

    return reg_id;
}

int codegen_emit_expression(CodeGenerator* gen,
    ASTNode* expr,
    SymbolTable* symtab)
{
    if (!expr) {
        return -1;
    }

    switch (expr->type) {
    case AST_BINARY_EXPR:
        return codegen_emit_binary_expr(gen, expr, symtab);

    case AST_IDENTIFIER:
        return codegen_emit_identifier(gen, expr, symtab);

    case AST_LITERAL:
        return codegen_emit_literal(gen, expr);

    default:
        return -1;
    }
}

/* ================================================================
   ДОБАВЛЕНИЕ ПЕРЕМЕННЫХ В ПАМЯТЬ ГЕНЕРАТОРА
   ================================================================ */

void codegen_add_global_variable(CodeGenerator* gen, const char* var_name, int type_id)
{
    if (!gen || !var_name || gen->globals.var_count >= MAX_VARIABLES) {
        return;
    }

    /* проверяем, есть ли уже такая переменная */
    for (int i = 0; i < gen->globals.var_count; i++) {
        if (strcmp(gen->globals.var_names[i], var_name) == 0) {
            printf("[DEBUG] Global variable '%s' already exists at address 0x%x\n",
                var_name, gen->globals.var_addresses[i]);
            return;
        }
    }

    /* добавляем новую глобальную переменную */
    gen->globals.var_names[gen->globals.var_count] =
        (char*)malloc(strlen(var_name) + 1);
    strcpy(gen->globals.var_names[gen->globals.var_count], var_name);
    gen->globals.var_addresses[gen->globals.var_count] =
        0x8000 + (gen->globals.var_count * 4);
    gen->globals.var_types[gen->globals.var_count] = type_id;
    gen->globals.var_count++;

    /* также добавляем в таблицу типов */
    set_type_id(var_name, type_id);

    printf("[DEBUG] Added global variable '%s' (type_id=%d) at address 0x%x\n",
        var_name, type_id, gen->globals.var_addresses[gen->globals.var_count - 1]);
}

void codegen_add_local_variable(CodeGenerator* gen, const char* var_name, int type_id)
{
    if (!gen || !var_name || gen->locals.var_count >= MAX_VARIABLES) {
        return;
    }

    /* Проверяем, есть ли уже такая переменная */
    for (int i = 0; i < gen->locals.var_count; i++) {
        if (strcmp(gen->locals.var_names[i], var_name) == 0) {
            return; /* уже есть */
        }
    }

    /* Выделяем место в стеке (отрицательное смещение от fp) */
    int offset = -(gen->stack_offset + 4);  /* 4 байта на переменную */
    gen->stack_offset += 4;

    if (gen->stack_offset > gen->max_stack_offset) {
        gen->max_stack_offset = gen->stack_offset;
    }

    /* Сохраняем переменную */
    int idx = gen->locals.var_count++;
    gen->locals.var_names[idx] = strdup(var_name);
    gen->locals.var_offsets[idx] = offset;
    gen->locals.var_types[idx] = type_id;

    /* также добавляем в таблицу типов */
    set_type_id(var_name, type_id);

    printf("[DEBUG] Added local variable '%s' at offset %d (type=%d)\n",
        var_name, offset, type_id);
}

/* ================================================================
   ВСПОМОГАТЕЛЬНАЯ ФУНКЦИЯ ДЛЯ ИЗВЛЕЧЕНИЯ ПЕРЕМЕННЫХ ИЗ ОБЪЯВЛЕНИЯ
   ================================================================ */

static void extract_variables_from_declaration(ASTNode* decl, CodeGenerator* gen)
{
    if (!decl || decl->type != AST_VAR_DECLARATION) {
        return;
    }

    if (decl->child_count >= 2) {
        ASTNode* id_node = decl->children[0];
        ASTNode* type_node = decl->children[1];

        int type_id = 1; /* int по умолчанию */
        if (type_node && type_node->value) {
            if (strcmp(type_node->value, "int") == 0) type_id = 1;
            else if (strcmp(type_node->value, "float") == 0) type_id = 2;
            else if (strcmp(type_node->value, "bool") == 0) type_id = 3;
            else if (strcmp(type_node->value, "char") == 0) type_id = 4;
        }

        if (id_node->type == AST_IDENTIFIER) {
            codegen_add_local_variable(gen, id_node->value, type_id);
        }
        else if (id_node->type == AST_ID_LIST) {
            for (int i = 0; i < id_node->child_count; i++) {
                if (id_node->children[i]->type == AST_IDENTIFIER) {
                    codegen_add_local_variable(gen, id_node->children[i]->value, type_id);
                }
            }
        }
    }
}

/* ================================================================
   СОБРАНИЕ ИНФОРМАЦИИ О ТИПАХ ИЗ CFG
   ================================================================ */

void collect_type_information(CFG* cfg)
{
    if (!cfg) {
        return;
    }

    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];

        if (!node || !node->ast_node) {
            continue;
        }

        /* Если это узел объявления переменной (VAR_DECLARATION) */
        if (node->ast_node->type == AST_VAR_DECLARATION) {
            if (node->ast_node->child_count >= 2) {
                ASTNode* id_node = node->ast_node->children[0];
                ASTNode* type_node = node->ast_node->children[1];

                if (type_node && type_node->value) {
                    int type_id = 0;

                    /* Определяем ID типа */
                    if (strcmp(type_node->value, "int") == 0) {
                        type_id = 1;
                    }
                    else if (strcmp(type_node->value, "float") == 0) {
                        type_id = 2;
                    }
                    else if (strcmp(type_node->value, "bool") == 0) {
                        type_id = 3;
                    }
                    else if (strcmp(type_node->value, "char") == 0) {
                        type_id = 4;
                    }
                    else if (strcmp(type_node->value, "string") == 0) {
                        type_id = 5;
                    }

                    /* Добавляем переменную/переменные в таблицу типов */
                    if (id_node->type == AST_IDENTIFIER) {
                        set_type_id(id_node->value, type_id);
                        printf("[TYPE INFO] Variable '%s' : type_id=%d\n",
                            id_node->value, type_id);
                    }
                    else if (id_node->type == AST_ID_LIST) {
                        for (int j = 0; j < id_node->child_count; j++) {
                            if (id_node->children[j]->type == AST_IDENTIFIER) {
                                set_type_id(id_node->children[j]->value, type_id);
                                printf("[TYPE INFO] Variable '%s' : type_id=%d\n",
                                    id_node->children[j]->value, type_id);
                            }
                        }
                    }
                }
            }
        }

        /* если это присваивание - сохраняем тип */
        if (node->op_tree && node->op_tree->type == AST_ASSIGNMENT &&
            node->op_tree->child_count > 0) {

            ASTNode* var = node->op_tree->children[0];
            ASTNode* value = node->op_tree->child_count > 1 ?
                node->op_tree->children[1] : NULL;

            if (var && var->type == AST_IDENTIFIER && value) {
                int inferred_type = infer_type_from_expr(value);
                if (inferred_type > 0) {
                    set_type_id(var->value, inferred_type);
                }
            }
        }
    }
}

/* ================================================================
   СОБИРАНИЕ ПЕРЕМЕННЫХ ИЗ CFG
   ================================================================ */

static void collect_from_ast(ASTNode* ast, CodeGenerator* gen)
{
    if (!ast) return;

    if (ast->type == AST_IDENTIFIER && ast->value) {
        /* Эта функция теперь только собирает информацию о типах,
           но не добавляет переменные автоматически */
        int type_id = get_type_id(ast->value);
        if (type_id == 0) {
            type_id = 1; /* по умолчанию int */
            set_type_id(ast->value, type_id);
        }
    }

    for (int j = 0; j < ast->child_count; j++) {
        collect_from_ast(ast->children[j], gen);
    }
}

void collect_variables_from_cfg(CodeGenerator* gen, CFG* cfg)
{
    if (!gen || !cfg) {
        return;
    }

    /* Проходим по всем узлам CFG */
    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];

        if (!node) {
            continue;
        }

        /* Собираем из ast_node (объявления переменных) */
        if (node->ast_node && node->ast_node->type == AST_VAR_DECLARATION) {
            extract_variables_from_declaration(node->ast_node, gen);
        }

        /* Также собираем из op_tree (использование переменных) */
        if (node->op_tree) {
            collect_from_ast(node->op_tree, gen);
        }
    }
}

/* ================================================================
   ГЕНЕРАЦИЯ ПРИСВАИВАНИЯ (ИСПРАВЛЕННАЯ ВЕРСИЯ)
   ================================================================ */

void codegen_emit_assignment(CodeGenerator* gen,
    ASTNode* expr,
    SymbolTable* symtab)
{
    (void)symtab;

    if (!expr || expr->child_count < 2) {
        return;
    }

    ASTNode* var_node = expr->children[0];
    ASTNode* value_node = expr->children[1];

    if (!var_node || var_node->type != AST_IDENTIFIER || !value_node) {
        return;
    }

    /* Генерируем код для правой части */
    int value_reg = codegen_emit_expression(gen, value_node, symtab);
    if (value_reg < 0) {
        return;
    }

    /* Определяем тип значения */
    int type_id = infer_type_from_expr(value_node);
    if (type_id == 0) {
        type_id = 1; /* int по умолчанию */
    }

    /* Проверяем, является ли переменная локальной */
    int is_local = 0;
    for (int i = 0; i < gen->locals.var_count; i++) {
        if (strcmp(gen->locals.var_names[i], var_node->value) == 0) {
            is_local = 1;
            break;
        }
    }

    if (is_local) {
        /* Сохраняем значение в локальную переменную в стеке */
        codegen_store_variable(gen, var_node->value, value_reg, type_id);
    }
    else {
        /* Глобальная переменная: сохраняем в DRAM */
        /* Выделяем регистр для адреса переменной */
        int addr_reg = next_free_register(gen, 0);
        if (addr_reg < 0) {
            free_register(gen, value_reg);
            return;
        }

        char addr_reg_str[8], value_reg_str[8];
        sprintf(addr_reg_str, "r%d", addr_reg);
        sprintf(value_reg_str, "r%d", value_reg);

        /* Получаем реальный адрес переменной в DRAM */
        int var_address = get_global_variable_address(gen, var_node->value);
        char var_addr[64];
        sprintf(var_addr, "#0x%x", var_address);

        /* Формируем комментарий с адресом */
        char comment[128];
        sprintf(comment, "load global variable address (0x%x)", var_address);

        /* Загружаем адрес переменной в регистр */
        codegen_emit_instruction(gen, "MOVI", addr_reg_str, var_addr, NULL,
            comment);

        /* Сохраняем значение по адресу */
        const char* store_instr = get_store_instr(type_id);
        codegen_emit_instruction(gen, store_instr, addr_reg_str, value_reg_str, NULL,
            "store to global variable");

        free_register(gen, addr_reg);
    }

    /* Освобождаем регистр */
    free_register(gen, value_reg);
}

/* ================================================================
   ГЕНЕРАЦИЯ КОДА ДЛЯ УЗЛА CFG
   ================================================================ */

void codegen_generate_cfg_node(CodeGenerator* gen,
    CFGNode* node,
    SymbolTable* symtab)
{
    if (!gen || !node) {
        return;
    }

    codegen_emit_label(gen, node);

    switch (node->type) {

    case CFG_START:
        /* Определяем имя функции */
        if (node->label) {
            /* Формат label: "entry: main" или просто "main" */
            char* colon = strchr(node->label, ':');
            if (colon) {
                /* Извлекаем имя после двоеточия */
                char* func_name = colon + 1;
                while (*func_name == ' ') func_name++; /* Пропускаем пробелы */

                if (gen->current_function) {
                    free(gen->current_function);
                }
                gen->current_function = strdup(func_name);
            }
            else {
                if (gen->current_function) {
                    free(gen->current_function);
                }
                gen->current_function = strdup(node->label);
            }
        }

        /* Пролог функции: настройка стека */
        printf("[DEBUG] Generating function prologue for %s\n",
            gen->current_function ? gen->current_function : "unknown");

        /* Инициализация указателя стека */
        codegen_emit_instruction(gen, "MOVI", "r7", "#0xfff0", NULL,
            "initialize stack top (sram end)");
        codegen_emit_instruction(gen, "MOV", "sp", "r7", NULL,
            "setup stack pointer");
        codegen_emit_instruction(gen, "MOV", "fp", "sp", NULL,
            "setup frame pointer");

        /* Резервирование места для локальных переменных */
        if (gen->max_stack_offset > 0) {
            char stack_size_str[16];
            sprintf(stack_size_str, "#%d", gen->max_stack_offset);
            codegen_emit_instruction(gen, "MOVI", "r0", stack_size_str, NULL,
                "load stack frame size");
            codegen_emit_instruction(gen, "SUB", "sp", "sp", "r0",
                "allocate space for local variables");
        }
        break;

    case CFG_END:
        /* Эпилог функции */
        printf("[DEBUG] Generating function epilogue\n");

        /* Восстановление стека */
        if (gen->max_stack_offset > 0) {
            char stack_size_str[16];
            sprintf(stack_size_str, "#%d", gen->max_stack_offset);
            codegen_emit_instruction(gen, "MOVI", "r0", stack_size_str, NULL,
                "load stack frame size");
            codegen_emit_instruction(gen, "ADD", "sp", "sp", "r0",
                "deallocate local variables");
        }

        /* ВАЖНО: Для функции main используем HLT, а не RET */
        if (gen->current_function && strcmp(gen->current_function, "main") == 0) {
            codegen_emit_instruction(gen, "HLT", NULL, NULL, NULL,
                "halt program");
        }
        else {
            codegen_emit_instruction(gen, "RET", NULL, NULL, NULL,
                "return from function");
        }
        break;

    case CFG_BLOCK:
        if (node->ast_node && node->ast_node->type == AST_VAR_DECLARATION) {
            /* Объявление переменной - резервируем место в стеке */
            ASTNode* decl = node->ast_node;

            if (decl->child_count >= 2) {
                ASTNode* id_node = decl->children[0];
                ASTNode* type_node = decl->children[1];

                int type_id = 1; /* int по умолчанию */
                if (type_node && type_node->value) {
                    if (strcmp(type_node->value, "int") == 0) type_id = 1;
                    else if (strcmp(type_node->value, "float") == 0) type_id = 2;
                    else if (strcmp(type_node->value, "bool") == 0) type_id = 3;
                }

                if (id_node->type == AST_IDENTIFIER) {
                    codegen_add_local_variable(gen, id_node->value, type_id);
                }
                else if (id_node->type == AST_ID_LIST) {
                    for (int i = 0; i < id_node->child_count; i++) {
                        if (id_node->children[i]->type == AST_IDENTIFIER) {
                            codegen_add_local_variable(gen, id_node->children[i]->value, type_id);
                        }
                    }
                }

                /* Инициализация переменной нулем */
                char comment[256];
                snprintf(comment, sizeof(comment),
                    "Initialize variable(s) from declaration");
                codegen_emit_instruction(gen, "NOP", NULL, NULL, NULL, comment);
            }
        }
        else if (node->op_tree) {
            /* Генерация кода для выражения */
            if (node->op_tree->type == AST_ASSIGNMENT) {
                printf("[DEBUG] Node %d: Generating assignment\n", node->id);
                codegen_emit_assignment(gen, node->op_tree, symtab);
            }
            else if (node->op_tree->type == AST_BINARY_EXPR) {
                /* Для условий в if/while */
                printf("[DEBUG] Node %d: Generating condition\n", node->id);
                int reg_id = codegen_emit_expression(gen, node->op_tree, symtab);
                if (reg_id >= 0) {
                    free_register(gen, reg_id);
                }
            }
        }
        break;

    case CFG_CONDITION:
        if (node->op_tree) {
            /* Генерируем код для условия */
            int reg_id = codegen_emit_expression(gen, node->op_tree, symtab);
            if (reg_id >= 0) {
                char reg_str[8];
                sprintf(reg_str, "r%d", reg_id);

                /* Условие уже сгенерировано в виде 0/1 в регистре */
                /* Просто проверяем, равно ли 0 */

                /* Сравниваем с нулем */
                codegen_emit_instruction(gen, "CMPI", reg_str, "#0", NULL,
                    "compare condition result with zero");

                free_register(gen, reg_id);

                if (node->conditionalNext && node->defaultNext) {
                    char true_label[64], false_label[64];

                    if (node->conditionalNext->type == CFG_BLOCK) {
                        sprintf(true_label, "block_node%d", node->conditionalNext->id);
                    }
                    else if (node->conditionalNext->type == CFG_MERGE) {
                        sprintf(true_label, "merge_node%d", node->conditionalNext->id);
                    }
                    else {
                        sprintf(true_label, "cond_node%d", node->conditionalNext->id);
                    }

                    if (node->defaultNext->type == CFG_BLOCK) {
                        sprintf(false_label, "block_node%d", node->defaultNext->id);
                    }
                    else if (node->defaultNext->type == CFG_MERGE) {
                        sprintf(false_label, "merge_node%d", node->defaultNext->id);
                    }
                    else {
                        sprintf(false_label, "block_node%d", node->defaultNext->id);
                    }

                    /* генерируем условный переход */
                    codegen_emit_instruction(gen, "JNE", true_label, NULL, NULL,
                        "conditional jump if condition is true (not zero)");
                    codegen_emit_instruction(gen, "JMP", false_label, NULL, NULL,
                        "jump to false branch");
                }
            }
        }
        break;

    case CFG_MERGE:
        /* merge узел ничего не делает */
        break;

    case CFG_ERROR:
        if (node->error_message) {
            char comment[256];
            snprintf(comment, sizeof(comment),
                "ERROR: %s", node->error_message);
            codegen_emit_instruction(gen, "NOP", NULL, NULL, NULL,
                comment);
        }
        break;
    }
}

/* ================================================================
   ГЛАВНАЯ ФУНКЦИЯ - ОБХОД CFG И ГЕНЕРАЦИЯ КОДА
   ================================================================ */

void codegen_from_cfg(CodeGenerator* gen,
    CFG* cfg,
    SymbolTable* symtab)
{
    if (!gen || !cfg) {
        return;
    }

    printf("[*] Collecting type information from CFG...\n");
    collect_type_information(cfg);

    printf("[*] Collecting variables from CFG...\n");
    collect_variables_from_cfg(gen, cfg);

    printf("[+] Type and variable collection complete\n");
    printf("[+] Found %d local variables, %d global variables\n",
        gen->locals.var_count, gen->globals.var_count);

    printf("[+] Starting code generation from CFG (%d nodes)\n",
        cfg->node_count);

    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];

        if (!node) {
            continue;
        }

        codegen_generate_cfg_node(gen, node, symtab);

        /* Генерация безусловного перехода для блоков, которые не являются условиями */
        if (node->type != CFG_END && node->type != CFG_CONDITION && node->defaultNext) {
            codegen_emit_jump(gen, node->defaultNext);
        }
    }

    printf("[+] Code generation complete. %d instructions generated\n",
        gen->total_instructions);
    printf("[+] Local variables: %d, Global variables: %d\n",
        gen->locals.var_count, gen->globals.var_count);
}

/* ================================================================
   СОЗДАНИЕ И ОСВОБОЖДЕНИЕ РЕСУРСОВ
   ================================================================ */

CodeGenerator* codegen_create(void)
{
    type_table_init();

    CodeGenerator* gen = (CodeGenerator*)malloc(sizeof(CodeGenerator));

    gen->current_function = NULL;
    gen->allocated = 1024;
    gen->instr_count = 0;
    gen->instructions = (Instruction*)malloc(
        gen->allocated * sizeof(Instruction)
    );

    gen->label_count = 0;
    gen->next_label_id = 0;
    gen->labels = (char**)malloc(256 * sizeof(char*));

    gen->reg_in_use = (int*)malloc(MAX_REGISTERS * sizeof(int));
    for (int i = 0; i < MAX_REGISTERS; i++) {
        gen->reg_in_use[i] = 0;
    }
    gen->next_temp_reg = 0;

    /* Инициализация глобальных переменных */
    gen->globals.var_names = (char**)malloc(
        MAX_VARIABLES * sizeof(char*)
    );
    gen->globals.var_addresses = (int*)malloc(
        MAX_VARIABLES * sizeof(int)
    );
    gen->globals.var_types = (int*)malloc(
        MAX_VARIABLES * sizeof(int)
    );
    gen->globals.var_count = 0;

    gen->total_instructions = 0;
    gen->branch_instructions = 0;
    gen->memory_instructions = 0;

    /* ИНИЦИАЛИЗАЦИЯ СТЕКА И ЛОКАЛЬНЫХ ПЕРЕМЕННЫХ */
    gen->stack_offset = 0;
    gen->max_stack_offset = 0;
    gen->param_offset = 0;

    gen->locals.var_names = (char**)malloc(MAX_VARIABLES * sizeof(char*));
    gen->locals.var_offsets = (int*)malloc(MAX_VARIABLES * sizeof(int));
    gen->locals.var_types = (int*)malloc(MAX_VARIABLES * sizeof(int));
    gen->locals.var_count = 0;

    return gen;
}

void codegen_free(CodeGenerator* gen)
{
    if (!gen) {
        return;
    }

    /* Освобождение инструкций */
    for (int i = 0; i < gen->instr_count; i++) {
        if (gen->instructions[i].mnemonic) free(gen->instructions[i].mnemonic);
        if (gen->instructions[i].operand1) free(gen->instructions[i].operand1);
        if (gen->instructions[i].operand2) free(gen->instructions[i].operand2);
        if (gen->instructions[i].operand3) free(gen->instructions[i].operand3);
        if (gen->instructions[i].comment) free(gen->instructions[i].comment);
    }
    free(gen->instructions);

    /* Освобождение меток */
    for (int i = 0; i < gen->label_count; i++) {
        if (gen->labels[i]) free(gen->labels[i]);
    }
    free(gen->labels);

    /* Освобождение регистров */
    free(gen->reg_in_use);

    /* Освобождение глобальных переменных */
    for (int i = 0; i < gen->globals.var_count; i++) {
        free(gen->globals.var_names[i]);
    }
    free(gen->globals.var_names);
    free(gen->globals.var_addresses);
    free(gen->globals.var_types);

    /* Освобождение локальных переменных */
    for (int i = 0; i < gen->locals.var_count; i++) {
        free(gen->locals.var_names[i]);
    }
    free(gen->locals.var_names);
    free(gen->locals.var_offsets);
    free(gen->locals.var_types);

    if (gen->current_function) {
        free(gen->current_function);
    }
    type_table_free();
    free(gen);
}

/* ================================================================
   ИСПРАВЛЕННЫЕ ФУНКЦИИ ДЛЯ РАБОТЫ С ЛОКАЛЬНЫМИ ПЕРЕМЕННЫМИ В СТЕКЕ (SRAM)
   ================================================================ */

   /* Получить смещение переменной в стеке */
static int get_local_variable_offset(CodeGenerator* gen, const char* var_name) {
    for (int i = 0; i < gen->locals.var_count; i++) {
        if (strcmp(gen->locals.var_names[i], var_name) == 0) {
            return gen->locals.var_offsets[i];
        }
    }
    return 0; /* не найдена */
}

/* Загрузка значения переменной из стека (SRAM) в регистр */
static int codegen_load_variable(CodeGenerator* gen, const char* var_name, int type_id) {
    int value_reg = next_free_register(gen, 0);
    if (value_reg < 0) return -1;

    char value_reg_str[8];
    sprintf(value_reg_str, "r%d", value_reg);

    /* Получаем смещение переменной */
    int offset = get_local_variable_offset(gen, var_name);

    /* Вычисляем адрес переменной в стеке: fp + offset (offset отрицательный) */
    int addr_reg = next_free_register(gen, 0);
    if (addr_reg < 0) {
        free_register(gen, value_reg);
        return -1;
    }

    char addr_reg_str[8];
    sprintf(addr_reg_str, "r%d", addr_reg);

    /* Загружаем смещение в регистр */
    char offset_str[16];

    /* Поскольку у нас отрицательные смещения от fp для локальных переменных */
    if (offset >= 0) {
        /* Если offset положительный (для параметров функции) */
        sprintf(offset_str, "#%d", offset);
        codegen_emit_instruction(gen, "MOVI", addr_reg_str, offset_str, NULL,
            "load positive offset");
        codegen_emit_instruction(gen, "ADD", addr_reg_str, "fp", addr_reg_str,
            "compute address: fp + offset");
    }
    else {
        /* offset отрицательный - для локальных переменных */
        sprintf(offset_str, "#%d", -offset);  /* загружаем положительное число */
        codegen_emit_instruction(gen, "MOVI", addr_reg_str, offset_str, NULL,
            "load negative offset (positive value)");
        codegen_emit_instruction(gen, "SUB", addr_reg_str, "fp", addr_reg_str,
            "compute address: fp - |offset|");
    }

    /* Загружаем значение из стека (SRAM) */
    const char* load_instr = get_load_stack_instr(type_id);
    codegen_emit_instruction(gen, load_instr, value_reg_str, addr_reg_str, NULL,
        "load variable from stack (SRAM)");

    free_register(gen, addr_reg);
    return value_reg;
}

/* Сохранение значения в переменную в стеке (SRAM) */
static void codegen_store_variable(CodeGenerator* gen, const char* var_name,
    int value_reg, int type_id) {
    char value_reg_str[8];
    sprintf(value_reg_str, "r%d", value_reg);

    /* Получаем смещение переменной */
    int offset = get_local_variable_offset(gen, var_name);

    /* Вычисляем адрес переменной */
    int addr_reg = next_free_register(gen, 0);
    if (addr_reg < 0) return;

    char addr_reg_str[8];
    sprintf(addr_reg_str, "r%d", addr_reg);

    /* Загружаем смещение в регистр */
    char offset_str[16];
    if (offset >= 0) {
        sprintf(offset_str, "#%d", offset);
        codegen_emit_instruction(gen, "MOVI", addr_reg_str, offset_str, NULL,
            "load positive offset");
        codegen_emit_instruction(gen, "ADD", addr_reg_str, "fp", addr_reg_str,
            "compute address: fp + offset");
    }
    else {
        sprintf(offset_str, "#%d", -offset);
        codegen_emit_instruction(gen, "MOVI", addr_reg_str, offset_str, NULL,
            "load negative offset (positive value)");
        codegen_emit_instruction(gen, "SUB", addr_reg_str, "fp", addr_reg_str,
            "compute address: fp - offset");
    }

    /* Сохраняем значение в стек (SRAM) */
    const char* store_instr = get_store_stack_instr(type_id);
    codegen_emit_instruction(gen, store_instr, addr_reg_str, value_reg_str, NULL,
        "store variable to stack (SRAM)");

    free_register(gen, addr_reg);
}

/* ================================================================
   ЭКСПОРТ И СТАТИСТИКА
   ================================================================ */

void codegen_export_assembly(CodeGenerator* gen, const char* filename)
{
    if (!gen || !filename) {
        return;
    }

    FILE* f = fopen(filename, "w");
    if (!f) {
        perror("fopen");
        return;
    }

    fprintf(f, "[section cram, sram]\n\n");
    fprintf(f, "; Generated Assembly Code for noobik VM\n");
    fprintf(f, "; Auto-generated from CFG with type information\n");
    fprintf(f, "; Uses MOVI for int, MOVF for float, LDB for bool\n\n");

    for (int i = 0; i < gen->instr_count; i++) {
        Instruction* instr = &gen->instructions[i];

        if (strcmp(instr->comment, "[LABEL]") == 0) {
            fprintf(f, "%s\n", instr->mnemonic);
        }
        else {
            fprintf(f, "    %-8s", instr->mnemonic);

            if (instr->operand1) {
                char op1_buf[64];
                strcpy(op1_buf, instr->operand1);
                fprintf(f, " %s", op1_buf);
            }

            if (instr->operand2) {
                char op2_buf[64];
                strcpy(op2_buf, instr->operand2);
                fprintf(f, ", %s", op2_buf);
            }

            if (instr->operand3) {
                char op3_buf[64];
                strcpy(op3_buf, instr->operand3);
                fprintf(f, ", %s", op3_buf);
            }

            if (instr->comment && strcmp(instr->comment, "[LABEL]") != 0) {
                char clean_comment[256];
                strcpy(clean_comment, instr->comment);
                char* percent = strchr(clean_comment, '(');
                if (percent) *percent = '\0';
                fprintf(f, " ; %s", clean_comment);
            }

            fprintf(f, "\n");
        }
    }

    fprintf(f, "\n[section name=dram, bank=dram, start=0x8000]\n");
    fprintf(f, "; Data section - Global variable declarations\n");
    fprintf(f, "; Address Type Name\n");
    fprintf(f, "; -------- -------- -----------------\n");

    for (int i = 0; i < gen->globals.var_count; i++) {
        const char* type_str = "";

        switch (gen->globals.var_types[i]) {
        case 1: type_str = "int"; break;
        case 2: type_str = "float"; break;
        case 3: type_str = "bool"; break;
        case 4: type_str = "char"; break;
        default: type_str = "unknown"; break;
        }

        fprintf(f, "; 0x%04x   %-8s %s\n",
            gen->globals.var_addresses[i],
            type_str,
            gen->globals.var_names[i]);
    }

    fprintf(f, "\n");

    for (int i = 0; i < gen->globals.var_count; i++) {
        switch (gen->globals.var_types[i]) {
        case 1: /* int */
            fprintf(f, "%-10s: dd 0        ; int (0x%x)\n",
                gen->globals.var_names[i], gen->globals.var_addresses[i]);
            break;
        case 2: /* float */
            fprintf(f, "%-10s: dd 0.0      ; float (0x%x)\n",
                gen->globals.var_names[i], gen->globals.var_addresses[i]);
            break;
        case 3: /* bool */
            fprintf(f, "%-10s: db 0        ; bool (0x%x)\n",
                gen->globals.var_names[i], gen->globals.var_addresses[i]);
            break;
        default:
            fprintf(f, "%-10s: dd 0        ; global variable (0x%x)\n",
                gen->globals.var_names[i], gen->globals.var_addresses[i]);
            break;
        }
    }

    fprintf(f, "\n[section name=kram, bank=kram, start=0x4000]\n");
    fprintf(f, "; Constant section (read-only)\n");
    fprintf(f, "; Floating point constants stored here\n");

    fclose(f);
    printf("[+] Assembly exported to %s\n", filename);
}

void codegen_print_instructions(CodeGenerator* gen)
{
    if (!gen) {
        return;
    }

    printf("\n");
    printf("================================\n");
    printf("      GENERATED INSTRUCTIONS\n");
    printf("================================\n\n");

    for (int i = 0; i < gen->instr_count; i++) {
        Instruction* instr = &gen->instructions[i];

        printf("%3d: ", i);
        printf("%-8s", instr->mnemonic);

        if (instr->operand1) {
            printf(" %s", instr->operand1);
        }
        if (instr->operand2) {
            printf(", %s", instr->operand2);
        }
        if (instr->operand3) {
            printf(", %s", instr->operand3);
        }

        if (instr->comment) {
            printf("   ; %s", instr->comment);
        }

        printf("\n");
    }

    printf("\n================================\n");
}

void codegen_print_stats(CodeGenerator* gen)
{
    if (!gen) {
        return;
    }

    printf("\n");
    printf("================================\n");
    printf("      CODE GENERATION STATS\n");
    printf("================================\n");
    printf("Total Instructions:    %d\n", gen->total_instructions);
    printf("Branch Instructions:   %d\n", gen->branch_instructions);
    printf("Memory Instructions:   %d\n", gen->memory_instructions);
    printf("Local Variables:       %d\n", gen->locals.var_count);
    printf("Global Variables:      %d\n", gen->globals.var_count);
    printf("Stack Frame Size:      %d bytes\n", gen->max_stack_offset);
    printf("================================\n\n");

    if (type_table && type_table->count > 0) {
        printf("\nVariable Types Collected:\n");
        printf("================================\n");
        for (int i = 0; i < type_table->count; i++) {
            const char* type_name = "";
            switch (type_table->types[i].type_id) {
            case 1: type_name = "int"; break;
            case 2: type_name = "float"; break;
            case 3: type_name = "bool"; break;
            default: type_name = "unknown"; break;
            }
            printf("%-20s : %s\n",
                type_table->types[i].var_name,
                type_name);
        }
        printf("================================\n\n");
    }
}