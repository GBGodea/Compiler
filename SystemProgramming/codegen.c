#include "codegen.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* =========================
 * String builder
 * ========================= */

typedef struct {
    char* buf;
    size_t len;
    size_t cap;
} Str;

static void sb_init(Str* s) {
    s->buf = NULL;
    s->len = 0;
    s->cap = 0;
}

static void sb_free(Str* s) {
    free(s->buf);
    s->buf = NULL;
    s->len = 0;
    s->cap = 0;
}

static int sb_reserve(Str* s, size_t add) {
    size_t need = s->len + add + 1;
    if (need <= s->cap) return 1;
    size_t new_cap = (s->cap == 0) ? 256 : s->cap;
    while (new_cap < need) new_cap *= 2;
    char* n = (char*)realloc(s->buf, new_cap);
    if (!n) return 0;
    s->buf = n;
    s->cap = new_cap;
    return 1;
}

static int sb_append(Str* s, const char* text) {
    if (!text) return 1;
    size_t n = strlen(text);
    if (!sb_reserve(s, n)) return 0;
    memcpy(s->buf + s->len, text, n);
    s->len += n;
    s->buf[s->len] = '\0';
    return 1;
}

static int sb_appendf(Str* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[4096];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if ((size_t)n < sizeof(tmp)) {
        return sb_append(s, tmp);
    }

    /* slow path */
    char* dyn = (char*)malloc((size_t)n + 1);
    if (!dyn) return 0;
    va_start(ap, fmt);
    vsnprintf(dyn, (size_t)n + 1, fmt, ap);
    va_end(ap);
    int ok = sb_append(s, dyn);
    free(dyn);
    return ok;
}

/* =========================
 * Register allocator
 * ========================= */

typedef struct {
    int used[8];
} RegPool;

static void reg_init(RegPool* rp) {
    memset(rp->used, 0, sizeof(rp->used));
    /* r7 is scratch for addresses; do not allocate */
    rp->used[7] = 1;
    /* reserve r0 for CALL return / special cases */
    rp->used[0] = 1;
}

static int reg_alloc(RegPool* rp) {
    /* allocate r1..r6 only (r0 reserved, r7 scratch) */
    for (int i = 1; i <= 6; i++) {
        if (!rp->used[i]) {
            rp->used[i] = 1;
            return i;
        }
    }
    return -1;
}

static void reg_free(RegPool* rp, int r) {
    if (r < 1 || r > 6) return;
    rp->used[r] = 0;
}

static const char* rname(int r) {
    static const char* names[] = { "r0","r1","r2","r3","r4","r5","r6","r7" };
    if (r < 0) return "r?";
    if (r > 7) return "r?";
    return names[r];
}

/* =========================
 * CFG function discovery
 * ========================= */

typedef struct {
    char name[256];
    int scope_id;
    const CFGNode* entry;
} FunctionInfo;

static int starts_with(const char* s, const char* pref) {
    if (!s || !pref) return 0;
    size_t n = strlen(pref);
    return strncmp(s, pref, n) == 0;
}

static void trim_spaces(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) { s[n - 1] = 0; n--; }
    char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

/* label вида: "entry: main (scope:2)" */
static int parse_entry_label(const char* label, char* out_name, size_t out_cap, int* out_scope) {
    if (!label || !starts_with(label, "entry:")) return 0;

    const char* p = label + strlen("entry:");
    while (*p && isspace((unsigned char)*p)) p++;

    const char* par = strstr(p, "(");
    size_t name_len = par ? (size_t)(par - p) : strlen(p);
    if (name_len >= out_cap) name_len = out_cap - 1;

    memcpy(out_name, p, name_len);
    out_name[name_len] = 0;
    trim_spaces(out_name);

    int scope = 1;
    if (par) {
        const char* sc = strstr(par, "scope:");
        if (sc) {
            sc += strlen("scope:");
            scope = atoi(sc);
        }
    }
    if (out_scope) *out_scope = scope;
    return 1;
}

static int collect_functions(const CFG* cfg, FunctionInfo** out_funcs, int* out_count) {
    if (!cfg || !out_funcs || !out_count) return 0;

    int cap = 8;
    int count = 0;
    FunctionInfo* funcs = (FunctionInfo*)calloc((size_t)cap, sizeof(FunctionInfo));
    if (!funcs) return 0;

    for (int i = 0; i < cfg->node_count; i++) {
        const CFGNode* n = cfg->nodes[i];
        if (!n) continue;
        if (n->type != CFG_START) continue;

        char fname[256];
        int sid = 1;
        if (!parse_entry_label(n->label, fname, sizeof(fname), &sid)) continue;

        if (count >= cap) {
            cap *= 2;
            FunctionInfo* nf = (FunctionInfo*)realloc(funcs, (size_t)cap * sizeof(FunctionInfo));
            if (!nf) { free(funcs); return 0; }
            funcs = nf;
        }

        memset(&funcs[count], 0, sizeof(FunctionInfo));
        snprintf(funcs[count].name, sizeof(funcs[count].name), "%s", fname);
        funcs[count].scope_id = sid;
        funcs[count].entry = n;
        count++;
    }

    *out_funcs = funcs;
    *out_count = count;
    return 1;
}

/* =========================
 * Symbol lookup in a given scope chain
 * ========================= */

static Scope* find_scope_by_id(const SymbolTable* st, int id) {
    if (!st) return NULL;
    for (int i = 0; i < st->scope_count; i++) {
        if (st->scopes[i] && st->scopes[i]->id == id) return st->scopes[i];
    }
    return NULL;
}

static Symbol* find_symbol_in_scope(const SymbolTable* st, const char* name, int scope_id) {
    if (!st || !name) return NULL;
    for (int i = 0; i < st->symbol_count; i++) {
        Symbol* sym = &st->symbols[i];
        if (!sym->name) continue;
        if (sym->scope_id != scope_id) continue;
        if (strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

static Symbol* cg_lookup_symbol(const SymbolTable* st, const char* name, int scope_id) {
    if (!st || !name) return NULL;

    Scope* sc = find_scope_by_id(st, scope_id);
    while (sc) {
        Symbol* s = find_symbol_in_scope(st, name, sc->id);
        if (s) return s;
        sc = sc->parent;
    }

    /* fallback: global */
    return find_symbol_in_scope(st, name, 1);
}

static int symbol_is_stack_resident(const Symbol* s) {
    if (!s) return 0;
    return (s->type == SYM_LOCAL || s->type == SYM_PARAMETER);
}

/*
 * Вычислить размер фрейма функции: берем все локальные символы/параметры внутри поддерева scopes.
 * Для простоты: учитываем все SYM_LOCAL у которых scope лежит в цепочке родительства от function scope.
 */
static int scope_is_descendant_of(Scope* candidate, Scope* ancestor) {
    if (!candidate || !ancestor) return 0;
    Scope* cur = candidate;
    while (cur) {
        if (cur->id == ancestor->id) return 1;
        cur = cur->parent;
    }
    return 0;
}

static int compute_frame_size_bytes(const SymbolTable* st, int function_scope_id) {
    if (!st) return 0;
    Scope* func_scope = find_scope_by_id(st, function_scope_id);
    if (!func_scope) return 0;

    /*
     * IMPORTANT: our Symbol.offset convention (set in semantic.c) is the *top* address
     * of an object relative to FP, and objects grow "down" in memory.
     * The Scope.local_offset already tracks the next free top-address after allocating
     * all locals in that scope. Therefore the required frame size is simply
     * -local_offset (and it includes the initial -4 slot needed for word stores).
     */
    int lo = func_scope->local_offset;
    if (lo >= 0) return 0;
    return -lo;
}

/* =========================
 * Codegen context
 * ========================= */

typedef struct {
    const CFG* cfg;
    const SymbolTable* st;
    CodegenOptions opt;

    Str out;

    /* текущая функция */
    char func_name[256];
    int func_scope_id;

    /* метки */
    int label_seq;

    /* регистры */
    RegPool regs;

    /* эпилог метка (куда прыгают return) */
    char epilog_label[300];

    /* множество reachable nodes текущей функции */


    /* label storage (avoid static-buffer aliasing) */
    int max_node_id;
    char** node_label_map; /* by node id */
    char** temp_labels;
    int temp_count;
    int temp_cap;

    /* function symbol info */
    const Symbol* func_sym;
    const Symbol* return_sym; /* heuristic: variable holding return value */
    int has_return_value;

    unsigned char* reachable; /* cfg->node_count */
} CG;

static void cg_comment(CG* cg, const char* fmt, ...) {
    if (!cg || !cg->opt.emit_comments) return;
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sb_appendf(&cg->out, "; %s\n", buf);
}

static char* xstrdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (!p) return NULL;
    memcpy(p, s, n);
    return p;
}

static void cg_labels_init(CG* cg) {
    if (!cg || !cg->cfg) return;
    int max_id = 0;
    for (int i = 0; i < cg->cfg->node_count; i++) {
        if (cg->cfg->nodes[i] && cg->cfg->nodes[i]->id > max_id) {
            max_id = cg->cfg->nodes[i]->id;
        }
    }
    cg->max_node_id = max_id;
    cg->node_label_map = (char**)calloc((size_t)(max_id + 1), sizeof(char*));
    cg->temp_labels = NULL;
    cg->temp_count = 0;
    cg->temp_cap = 0;
}

static void cg_labels_free(CG* cg) {
    if (!cg) return;
    if (cg->node_label_map) {
        for (int i = 0; i <= cg->max_node_id; i++) {
            free(cg->node_label_map[i]);
        }
        free(cg->node_label_map);
        cg->node_label_map = NULL;
    }
    for (int i = 0; i < cg->temp_count; i++) {
        free(cg->temp_labels[i]);
    }
    free(cg->temp_labels);
    cg->temp_labels = NULL;
    cg->temp_count = 0;
    cg->temp_cap = 0;
}

static const char* cg_node_label(CG* cg, const CFGNode* n) {
    if (!cg || !n) return "_L_invalid";
    int id = n->id;
    if (id < 0 || id > cg->max_node_id || !cg->node_label_map) return "_L_invalid";
    if (!cg->node_label_map[id]) {
        char buf[256];
        snprintf(buf, sizeof(buf), "_L_%s_%d", cg->func_name, id);
        cg->node_label_map[id] = xstrdup(buf);
    }
    return cg->node_label_map[id] ? cg->node_label_map[id] : "_L_invalid";
}

static const char* cg_new_label(CG* cg, const char* prefix) {
    if (!cg) return "_T_fn_L_0";
    int id = cg->label_seq++;
    char buf[256];
    snprintf(buf, sizeof(buf), "_T_%s_%s_%d", cg->func_name, prefix ? prefix : "L", id);

    if (cg->temp_count + 1 > cg->temp_cap) {
        int nc = cg->temp_cap ? cg->temp_cap * 2 : 32;
        char** nn = (char**)realloc(cg->temp_labels, (size_t)nc * sizeof(char*));
        if (!nn) return "_T_oom";
        cg->temp_labels = nn;
        cg->temp_cap = nc;
    }
    cg->temp_labels[cg->temp_count] = xstrdup(buf);
    if (!cg->temp_labels[cg->temp_count]) return "_T_oom";
    cg->temp_count++;
    return cg->temp_labels[cg->temp_count - 1];
}


/* =========================
 * Emit helpers
 * ========================= */

static void emit(CG* cg, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sb_append(&cg->out, buf);
}

static void emit_ins2(CG* cg, const char* op, const char* a, const char* b) {
    emit(cg, "    %s %s, %s\n", op, a, b);
}

static void emit_ins3(CG* cg, const char* op, const char* a, const char* b, const char* c) {
    emit(cg, "    %s %s, %s, %s\n", op, a, b, c);
}

static void emit_label(CG* cg, const char* lbl) {
    emit(cg, "%s:\n", lbl);
}

/* compute address of stack-resident symbol into r7 */
static void emit_addr_stack_sym(CG* cg, const Symbol* sym) {
    int off = sym ? sym->offset : 0;

    /* IMPORTANT: MOVI immediate is zero-extended in the simulator.
       So we never use negative immediates for stack addressing. */
    if (off == 0) {
        emit_ins2(cg, "MOV", "r7", "fp");
        return;
    }

    if (off > 0) {
        emit(cg, "    MOVI r7, #%d\n", off);
        emit_ins3(cg, "ADD", "r7", "fp", "r7");
        return;
    }

    /* off < 0 */
    emit(cg, "    MOVI r7, #%d\n", -off);
    emit_ins3(cg, "SUB", "r7", "fp", "r7");
}


/* вычислить адрес global/const символа sym в r7 */
static void emit_addr_abs(CG* cg, const Symbol* sym) {
    /* предполагаем что sym->address уже абсолютный */
    emit(cg, "    LA r7, #%d\n", sym->address);
}

static int emit_load_symbol(CG* cg, const Symbol* sym) {
    int r = reg_alloc(&cg->regs);
    if (r < 0) {
        cg_comment(cg, "REG_ALLOC failed");
        r = 1; /* best effort */
    }

    /* arrays evaluate to their base address */
    if (sym && sym->is_array) {
        if (symbol_is_stack_resident(sym)) {
            emit_addr_stack_sym(cg, sym);
            emit_ins2(cg, "MOV", rname(r), "r7");
            return r;
        }
        if (sym->type == SYM_GLOBAL) {
            emit_addr_abs(cg, sym);
            emit_ins2(cg, "MOV", rname(r), "r7");
            return r;
        }
    }

    if (symbol_is_stack_resident(sym)) {
        emit_addr_stack_sym(cg, sym);
        emit_ins2(cg, "LDS", rname(r), "r7");
        return r;
    }

    if (sym->type == SYM_GLOBAL) {
        emit_addr_abs(cg, sym);
        emit_ins2(cg, "LD", rname(r), "r7");
        return r;
    }

    if (sym->type == SYM_CONSTANT) {
        emit_addr_abs(cg, sym);
        emit_ins2(cg, "LDC", rname(r), "r7");
        return r;
    }

    /* неизвестное - 0 */
    emit(cg, "    MOVI %s, #0\n", rname(r));
    return r;
}

static void emit_store_symbol(CG* cg, const Symbol* sym, int r_value) {
    if (!sym) return;
    if (symbol_is_stack_resident(sym)) {
        emit_addr_stack_sym(cg, sym);
        emit_ins2(cg, "STS", "r7", rname(r_value));
        return;
    }
    if (sym->type == SYM_GLOBAL) {
        emit_addr_abs(cg, sym);
        emit_ins2(cg, "ST", "r7", rname(r_value));
        return;
    }
    /* constants не пишем */
}

/* =========================
 * Expression evaluation
 * ========================= */

static int cg_eval_expr(CG* cg, const ASTNode* e);
static void cg_emit_branch_on_expr(CG* cg, const ASTNode* e, const char* lbl_true, const char* lbl_false);

static long parse_int_literal(const char* s, int* ok) {
    if (ok) *ok = 0;
    if (!s) return 0;
    char* end = NULL;
    long v = strtol(s, &end, 0);
    if (end != s) {
        if (ok) *ok = 1;
        return v;
    }
    return 0;
}

static int parse_bool_literal(const char* s, int* ok) {
    if (ok) *ok = 0;
    if (!s) return 0;
    if (strcmp(s, "true") == 0) { if (ok) *ok = 1; return 1; }
    if (strcmp(s, "false") == 0) { if (ok) *ok = 1; return 0; }
    return 0;
}

static long parse_char_literal(const char* s, int* ok) {
    if (ok) *ok = 0;
    if (!s) return 0;
    size_t n = strlen(s);
    if (n < 3 || s[0] != '\'' || s[n - 1] != '\'') return 0;

    const char* inner = s + 1;
    size_t in_len = n - 2;

    if (in_len == 1) {
        if (ok) *ok = 1;
        return (unsigned char)inner[0];
    }

    if (in_len == 2 && inner[0] == '\\') {
        char c = inner[1];
        long v = 0;
        switch (c) {
        case 'n': v = 10; break;
        case 'r': v = 13; break;
        case 't': v = 9; break;
        case '0': v = 0; break;
        case '\\': v = '\\'; break;
        case '\'': v = '\''; break;
        default: v = (unsigned char)c; break;
        }
        if (ok) *ok = 1;
        return v;
    }

    return 0;
}

static int cg_eval_unary(CG* cg, const ASTNode* e) {
    if (!e || e->child_count < 1) {
        int r = reg_alloc(&cg->regs);
        emit(cg, "    MOVI %s, #0\n", rname(r));
        return r;
    }

    const char* op = e->value ? e->value : "";
    if (strcmp(op, "!") == 0) {
        /* bool not: r = (expr == 0) ? 1 : 0 */
        int rv = cg_eval_expr(cg, e->children[0]);
        const char* l_set1 = cg_new_label(cg, "not1");
        const char* l_end = cg_new_label(cg, "not_end");

        emit(cg, "    CMPI %s, #0\n", rname(rv));
        emit(cg, "    JEQ %s\n", l_set1);
        emit(cg, "    MOVI %s, #0\n", rname(rv));
        emit(cg, "    JMP %s\n", l_end);
        emit_label(cg, l_set1);
        emit(cg, "    MOVI %s, #1\n", rname(rv));
        emit_label(cg, l_end);
        return rv;
    }

    if (strcmp(op, "-") == 0) {
        int rv = cg_eval_expr(cg, e->children[0]);
        emit_ins2(cg, "NEG", rname(rv), rname(rv));
        return rv;
    }

    if (strcmp(op, "~") == 0) {
        int rv = cg_eval_expr(cg, e->children[0]);
        emit_ins2(cg, "NOT", rname(rv), rname(rv));
        return rv;
    }

    /* unary + : no-op */
    return cg_eval_expr(cg, e->children[0]);
}

static void emit_cmp_to_bool(CG* cg, const char* op, int rl, int rr, int dest) {
    const char* l_true = cg_new_label(cg, "cmp_true");
    const char* l_end = cg_new_label(cg, "cmp_end");

    emit_ins2(cg, "CMP", rname(rl), rname(rr));
    if (strcmp(op, "==") == 0) emit(cg, "    JEQ %s\n", l_true);
    else if (strcmp(op, "!=") == 0) emit(cg, "    JNE %s\n", l_true);
    else if (strcmp(op, "<") == 0) emit(cg, "    JLT %s\n", l_true);
    else if (strcmp(op, "<=") == 0) emit(cg, "    JLE %s\n", l_true);
    else if (strcmp(op, ">") == 0) emit(cg, "    JGT %s\n", l_true);
    else if (strcmp(op, ">=") == 0) emit(cg, "    JGE %s\n", l_true);
    else {
        /* unknown -> false */
    }

    emit(cg, "    MOVI %s, #0\n", rname(dest));
    emit(cg, "    JMP %s\n", l_end);
    emit_label(cg, l_true);
    emit(cg, "    MOVI %s, #1\n", rname(dest));
    emit_label(cg, l_end);
}

static int cg_eval_binary(CG* cg, const ASTNode* e) {
    if (!e || e->child_count < 2) {
        int r = reg_alloc(&cg->regs);
        emit(cg, "    MOVI %s, #0\n", rname(r));
        return r;
    }

    const char* op = e->value ? e->value : "";

    /* comparisons produce boolean */
    if (!strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") || !strcmp(op, "<=") ||
        !strcmp(op, ">") || !strcmp(op, ">=")) {
        int rl = cg_eval_expr(cg, e->children[0]);
        int rr = cg_eval_expr(cg, e->children[1]);
        int dest = rl; /* reuse */
        emit_cmp_to_bool(cg, op, rl, rr, dest);
        reg_free(&cg->regs, rr);
        return dest;
    }

    /* logical ops (не короткое замыкание в value, а в bool-результат) */
    if (!strcmp(op, "&&") || !strcmp(op, "||")) {
        int dest = reg_alloc(&cg->regs);
        if (dest < 0) dest = 0;

        const char* l_true = cg_new_label(cg, "logic_true");
        const char* l_false = cg_new_label(cg, "logic_false");
        const char* l_end = cg_new_label(cg, "logic_end");

        cg_emit_branch_on_expr(cg, e, l_true, l_false);

        emit_label(cg, l_true);
        emit(cg, "    MOVI %s, #1\n", rname(dest));
        emit(cg, "    JMP %s\n", l_end);

        emit_label(cg, l_false);
        emit(cg, "    MOVI %s, #0\n", rname(dest));

        emit_label(cg, l_end);
        return dest;
    }

    /* arithmetic / bitwise */
    int rl = cg_eval_expr(cg, e->children[0]);
    int rr = cg_eval_expr(cg, e->children[1]);

    if (!strcmp(op, "+")) emit_ins3(cg, "ADD", rname(rl), rname(rl), rname(rr));
    else if (!strcmp(op, "-")) emit_ins3(cg, "SUB", rname(rl), rname(rl), rname(rr));
    else if (!strcmp(op, "*")) emit_ins3(cg, "MUL", rname(rl), rname(rl), rname(rr));
    else if (!strcmp(op, "/")) emit_ins3(cg, "DIV", rname(rl), rname(rl), rname(rr));
    else if (!strcmp(op, "%")) emit_ins3(cg, "MOD", rname(rl), rname(rl), rname(rr));
    else if (!strcmp(op, "&")) emit_ins3(cg, "AND", rname(rl), rname(rl), rname(rr));
    else if (!strcmp(op, "|")) emit_ins3(cg, "OR", rname(rl), rname(rl), rname(rr));
    else if (!strcmp(op, "^")) emit_ins3(cg, "XOR", rname(rl), rname(rl), rname(rr));
    else if (!strcmp(op, "<<")) emit_ins3(cg, "SHL", rname(rl), rname(rl), rname(rr));
    else if (!strcmp(op, ">>")) emit_ins3(cg, "SHR", rname(rl), rname(rl), rname(rr));
    else {
        cg_comment(cg, "Unknown binary op '%s'", op);
    }

    reg_free(&cg->regs, rr);
    return rl;
}

static int cg_eval_call(CG* cg, const ASTNode* e) {
    const char* fname = (e && e->child_count > 0 && e->children[0]) ? e->children[0]->value : NULL;
    const ASTNode* args_node = NULL;
    if (e && e->child_count > 1) args_node = e->children[1];
    int argc = (args_node) ? args_node->child_count : 0;

    /* Save caller-live regs (r1..r6) because callee may clobber anything.
       We push them and mark free so arg-eval can reuse them. */
    int saved[8];
    int saved_count = 0;
    for (int r = 1; r <= 6; r++) {
        if (cg->regs.used[r]) {
            emit(cg, "    PUSH %s\n", rname(r));
            saved[saved_count++] = r;
            cg->regs.used[r] = 0;
        }
    }

    /* Push args right-to-left */
    for (int i = argc - 1; i >= 0; i--) {
        int ra = cg_eval_expr(cg, args_node->children[i]);
        emit(cg, "    PUSH %s\n", rname(ra));
        reg_free(&cg->regs, ra);
    }

    /* Call */
    if (!fname) fname = "<anon>";
    emit(cg, "    CALL _func_%s\n", fname);

    /* Pop args (discard) */
    for (int i = 0; i < argc; i++) {
        emit(cg, "    POP r7\n");
    }

    /* Restore saved regs in reverse */
    for (int i = saved_count - 1; i >= 0; i--) {
        emit(cg, "    POP %s\n", rname(saved[i]));
        cg->regs.used[saved[i]] = 1;
    }

    /* Return value is always in r0 */
    cg->regs.used[0] = 1;
    return 0;
}

static int cg_eval_assignment(CG* cg, const ASTNode* e);

static int cg_eval_expr(CG* cg, const ASTNode* e) {
    if (!e) {
        int r = reg_alloc(&cg->regs);
        emit(cg, "    MOVI %s, #0\n", rname(r));
        return r;
    }

    switch (e->type) {
    case AST_LITERAL: {
        /* integer literal (signed) */
        int r = reg_alloc(&cg->regs);
        if (r < 0) r = 1;
        long v = 0;
        if (e->value) v = strtol(e->value, NULL, 0);
        if (v >= 0 && v <= 65535) {
            emit(cg, "    MOVI %s, #%ld\n", rname(r), v);
            return r;
        }
        if (v < 0 && (-v) <= 65535) {
            int tmp = reg_alloc(&cg->regs);
            if (tmp < 0) tmp = 2;
            emit(cg, "    MOVI %s, #0\n", rname(r));
            emit(cg, "    MOVI %s, #%ld\n", rname(tmp), -v);
            emit_ins3(cg, "SUB", rname(r), rname(r), rname(tmp));
            reg_free(&cg->regs, tmp);
            return r;
        }
        /* fallback: clamp to 0 */
        emit(cg, "    MOVI %s, #0\n", rname(r));
        return r;
    }


    case AST_BOOL_LITERAL: {
        int r = reg_alloc(&cg->regs);
        if (r < 0) r = 1;
        int ok = 0;
        int v = parse_bool_literal(e->value, &ok);
        if (!ok) v = 0;
        emit(cg, "    MOVI %s, #%d\n", rname(r), v);
        return r;
    }

    case AST_CHAR_LITERAL: {
        int r = reg_alloc(&cg->regs);
        if (r < 0) r = 1;
        int ok = 0;
        long v = parse_char_literal(e->value, &ok);
        if (!ok) v = 0;
        emit(cg, "    MOVI %s, #%ld\n", rname(r), v);
        return r;
    }

    case AST_INDEX_EXPR: {
        /* array indexing: base[index] (only 1D) */
        const ASTNode* base = (e->child_count > 0) ? e->children[0] : NULL;
        const ASTNode* idxList = (e->child_count > 1) ? e->children[1] : NULL;
        const ASTNode* idxExpr = (idxList && idxList->child_count > 0) ? idxList->children[0] : NULL;

        const Symbol* sym = NULL;
        int is_stack = 1;

        int r_addr = reg_alloc(&cg->regs);
        if (r_addr < 0) { r_addr = 1; cg->regs.used[r_addr] = 1; }

        if (base && base->type == AST_IDENTIFIER && base->value) {
            sym = cg_lookup_symbol((SymbolTable*)cg->st, base->value, cg->func_scope_id);
        }

        if (sym && symbol_is_stack_resident(sym)) {
            emit_addr_stack_sym(cg, sym);
            emit_ins2(cg, "MOV", rname(r_addr), "r7");
            is_stack = 1;
        }
        else if (sym && sym->type == SYM_GLOBAL) {
            emit_addr_abs(cg, sym);
            emit_ins2(cg, "MOV", rname(r_addr), "r7");
            is_stack = 0;
        }
        else {
            emit(cg, "    MOVI %s, #0\n", rname(r_addr));
            is_stack = 1;
        }

        int r_idx;
        if (idxExpr) {
            r_idx = cg_eval_expr(cg, idxExpr);
        }
        else {
            r_idx = reg_alloc(&cg->regs);
            if (r_idx < 0) { r_idx = 2; cg->regs.used[r_idx] = 1; }
            emit(cg, "    MOVI %s, #0\n", rname(r_idx));
        }

        /* scale index by element size (default: 4 bytes) */
        int elem_sz = 4;
        if (sym && sym->is_array && sym->array_size > 0 && sym->size > 0) {
            int es = sym->size / sym->array_size;
            if (es > 0) elem_sz = es;
        }

        if (elem_sz == 1) {
            /* no-op */
        }
        else if (elem_sz == 2 || elem_sz == 4 || elem_sz == 8 || elem_sz == 16) {
            int sh = 0;
            if (elem_sz == 2) sh = 1;
            else if (elem_sz == 4) sh = 2;
            else if (elem_sz == 8) sh = 3;
            else if (elem_sz == 16) sh = 4;

            int r_sh = reg_alloc(&cg->regs);
            if (r_sh < 0) { r_sh = 3; cg->regs.used[r_sh] = 1; }
            emit(cg, "    MOVI %s, #%d\n", rname(r_sh), sh);
            emit_ins3(cg, "SHL", rname(r_idx), rname(r_idx), rname(r_sh));
            reg_free(&cg->regs, r_sh);
        }
        else {
            int r_mul = reg_alloc(&cg->regs);
            if (r_mul < 0) { r_mul = 3; cg->regs.used[r_mul] = 1; }
            emit(cg, "    MOVI %s, #%d\n", rname(r_mul), elem_sz);
            emit_ins3(cg, "MUL", rname(r_idx), rname(r_idx), rname(r_mul));
            reg_free(&cg->regs, r_mul);
        }

        /* IMPORTANT: stack-allocated arrays grow downward from their top offset.
           For stack arrays we subtract the scaled index; for globals we add. */
        if (is_stack) emit_ins3(cg, "SUB", rname(r_addr), rname(r_addr), rname(r_idx));
        else emit_ins3(cg, "ADD", rname(r_addr), rname(r_addr), rname(r_idx));
        reg_free(&cg->regs, r_idx);

        /* load element */
        if (is_stack) {
            emit_ins2(cg, "LDS", rname(r_addr), rname(r_addr));
        }
        else {
            emit_ins2(cg, "LD", rname(r_addr), rname(r_addr));
        }

        return r_addr;
    }

    case AST_IDENTIFIER: {
        Symbol* sym = cg_lookup_symbol(cg->st, e->value, cg->func_scope_id);
        if (!sym) {
            int r = reg_alloc(&cg->regs);
            if (r < 0) r = 1;
            emit(cg, "    MOVI %s, #0\n", rname(r));
            cg_comment(cg, "Unknown identifier '%s'", e->value ? e->value : "?");
            return r;
        }
        return emit_load_symbol(cg, sym);
    }

    case AST_UNARY_EXPR:
        return cg_eval_unary(cg, e);

    case AST_BINARY_EXPR:
    case AST_ARITHMETIC_EXPR:
        return cg_eval_binary(cg, e);

    case AST_ASSIGNMENT:
        return cg_eval_assignment(cg, e);

    case AST_CALL_EXPR:
        return cg_eval_call(cg, e);

    case AST_ADDR_OF: {
        /* адрес переменной */
        if (e->child_count > 0 && e->children[0] && e->children[0]->type == AST_IDENTIFIER) {
            Symbol* sym = cg_lookup_symbol(cg->st, e->children[0]->value, cg->func_scope_id);
            int r = reg_alloc(&cg->regs);
            if (r < 0) r = 1;
            if (sym && symbol_is_stack_resident(sym)) {
                int off = sym->offset;
                if (off >= 0) {
                    emit(cg, "    MOVI %s, #%d\n", rname(r), off);
                    emit_ins3(cg, "ADD", rname(r), "fp", rname(r));
                }
                else {
                    emit(cg, "    MOVI %s, #%d\n", rname(r), -off);
                    emit_ins3(cg, "SUB", rname(r), "fp", rname(r));
                }
                return r;
            }
            if (sym && sym->type == SYM_GLOBAL) {
                emit(cg, "    LA %s, #%d\n", rname(r), sym->address);
                return r;
            }
            emit(cg, "    MOVI %s, #0\n", rname(r));
            return r;
        }
        int r = reg_alloc(&cg->regs);
        emit(cg, "    MOVI %s, #0\n", rname(r));
        return r;
    }

    case AST_DEREF: {
        if (e->child_count > 0) {
            int addr = cg_eval_expr(cg, e->children[0]);
            emit_ins2(cg, "LD", rname(addr), rname(addr));
            return addr;
        }
        int r = reg_alloc(&cg->regs);
        emit(cg, "    MOVI %s, #0\n", rname(r));
        return r;
    }

    default: {
        int r = reg_alloc(&cg->regs);
        if (r < 0) r = 1;
        emit(cg, "    MOVI %s, #0\n", rname(r));
        cg_comment(cg, "Unsupported AST node type %d", (int)e->type);
        return r;
    }
    }
}

/* =========================
 * Branching on expressions (for CFG_CONDITION)
 * ========================= */

static void cg_emit_branch_on_expr(CG* cg, const ASTNode* e, const char* lbl_true, const char* lbl_false) {
    if (!e) {
        emit(cg, "    JMP %s\n", lbl_false);
        return;
    }

    /* short-circuit for && and || */
    if (e->type == AST_BINARY_EXPR && e->value) {
        const char* op = e->value;
        if (!strcmp(op, "&&")) {
            const char* mid = cg_new_label(cg, "and_mid");
            cg_emit_branch_on_expr(cg, e->children[0], mid, lbl_false);
            emit_label(cg, mid);
            cg_emit_branch_on_expr(cg, e->children[1], lbl_true, lbl_false);
            return;
        }
        if (!strcmp(op, "||")) {
            const char* mid = cg_new_label(cg, "or_mid");
            cg_emit_branch_on_expr(cg, e->children[0], lbl_true, mid);
            emit_label(cg, mid);
            cg_emit_branch_on_expr(cg, e->children[1], lbl_true, lbl_false);
            return;
        }

        /* direct comparisons */
        if (!strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") || !strcmp(op, "<=") ||
            !strcmp(op, ">") || !strcmp(op, ">=")) {

            int rl = cg_eval_expr(cg, e->children[0]);
            int rr = cg_eval_expr(cg, e->children[1]);
            emit_ins2(cg, "CMP", rname(rl), rname(rr));

            if (!strcmp(op, "==")) emit(cg, "    JEQ %s\n", lbl_true);
            else if (!strcmp(op, "!=")) emit(cg, "    JNE %s\n", lbl_true);
            else if (!strcmp(op, "<")) emit(cg, "    JLT %s\n", lbl_true);
            else if (!strcmp(op, "<=")) emit(cg, "    JLE %s\n", lbl_true);
            else if (!strcmp(op, ">")) emit(cg, "    JGT %s\n", lbl_true);
            else if (!strcmp(op, ">=")) emit(cg, "    JGE %s\n", lbl_true);

            emit(cg, "    JMP %s\n", lbl_false);

            reg_free(&cg->regs, rr);
            reg_free(&cg->regs, rl);
            return;
        }
    }

    /* unary ! */
    if (e->type == AST_UNARY_EXPR && e->value && strcmp(e->value, "!") == 0 && e->child_count > 0) {
        cg_emit_branch_on_expr(cg, e->children[0], lbl_false, lbl_true);
        return;
    }

    /* fallback: compute value and compare with 0 */
    int rv = cg_eval_expr(cg, e);
    emit(cg, "    CMPI %s, #0\n", rname(rv));
    emit(cg, "    JNE %s\n", lbl_true);
    emit(cg, "    JMP %s\n", lbl_false);
    reg_free(&cg->regs, rv);
}

/* =========================
 * Reachability (per-function)
 * ========================= */

static int node_index(const CFG* cfg, const CFGNode* n) {
    if (!cfg || !n) return -1;
    for (int i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i] == n) return i;
    }
    return -1;
}

static void mark_reachable(CG* cg, const CFGNode* start) {
    int ncount = cg->cfg->node_count;
    memset(cg->reachable, 0, (size_t)ncount);

    const CFGNode** stack = (const CFGNode**)malloc((size_t)ncount * sizeof(CFGNode*));
    int sp = 0;
    stack[sp++] = start;

    while (sp > 0) {
        const CFGNode* cur = stack[--sp];
        int idx = node_index(cg->cfg, cur);
        if (idx < 0) continue;
        if (cg->reachable[idx]) continue;
        cg->reachable[idx] = 1;

        if (cur->defaultNext) stack[sp++] = cur->defaultNext;
        if (cur->conditionalNext) stack[sp++] = cur->conditionalNext;
    }

    free((void*)stack);
}

/* =========================
 * Node emission
 * ========================= */


static int cg_eval_assignment(CG* cg, const ASTNode* e) {
    if (!cg || !e || e->child_count < 2) {
        int r = reg_alloc(&cg->regs);
        if (r < 0) r = 1;
        emit(cg, "    MOVI %s, #0\n", rname(r));
        return r;
    }


    const ASTNode* lhs = e->children[0];
    const ASTNode* rhs = e->children[1];

    /* indexed assignment: a[i] := rhs */
    if (lhs && lhs->type == AST_INDEX_EXPR) {
        const ASTNode* base = (lhs->child_count > 0) ? lhs->children[0] : NULL;
        const ASTNode* idxList = (lhs->child_count > 1) ? lhs->children[1] : NULL;
        const ASTNode* idxExpr = (idxList && idxList->child_count > 0) ? idxList->children[0] : NULL;

        const Symbol* sym = NULL;
        int is_stack = 1;

        int r_addr = reg_alloc(&cg->regs);
        if (r_addr < 0) { r_addr = 1; cg->regs.used[r_addr] = 1; }

        if (base && base->type == AST_IDENTIFIER && base->value) {
            sym = cg_lookup_symbol((SymbolTable*)cg->st, base->value, cg->func_scope_id);
        }

        if (sym && symbol_is_stack_resident(sym)) {
            emit_addr_stack_sym(cg, sym);
            emit_ins2(cg, "MOV", rname(r_addr), "r7");
            is_stack = 1;
        }
        else if (sym && sym->type == SYM_GLOBAL) {
            emit_addr_abs(cg, sym);
            emit_ins2(cg, "MOV", rname(r_addr), "r7");
            is_stack = 0;
        }
        else {
            emit(cg, "    MOVI %s, #0\n", rname(r_addr));
            is_stack = 1;
        }

        int r_idx;
        if (idxExpr) r_idx = cg_eval_expr(cg, idxExpr);
        else {
            r_idx = reg_alloc(&cg->regs);
            if (r_idx < 0) { r_idx = 2; cg->regs.used[r_idx] = 1; }
            emit(cg, "    MOVI %s, #0\n", rname(r_idx));
        }

        /* scale index by element size (default: 4 bytes) */
        int elem_sz = 4;
        if (sym && sym->is_array && sym->array_size > 0 && sym->size > 0) {
            int es = sym->size / sym->array_size;
            if (es > 0) elem_sz = es;
        }

        if (elem_sz == 1) {
            /* no-op */
        }
        else if (elem_sz == 2 || elem_sz == 4 || elem_sz == 8 || elem_sz == 16) {
            int sh = 0;
            if (elem_sz == 2) sh = 1;
            else if (elem_sz == 4) sh = 2;
            else if (elem_sz == 8) sh = 3;
            else if (elem_sz == 16) sh = 4;

            int r_sh = reg_alloc(&cg->regs);
            if (r_sh < 0) { r_sh = 3; cg->regs.used[r_sh] = 1; }
            emit(cg, "    MOVI %s, #%d\n", rname(r_sh), sh);
            emit_ins3(cg, "SHL", rname(r_idx), rname(r_idx), rname(r_sh));
            reg_free(&cg->regs, r_sh);
        }
        else {
            int r_mul = reg_alloc(&cg->regs);
            if (r_mul < 0) { r_mul = 3; cg->regs.used[r_mul] = 1; }
            emit(cg, "    MOVI %s, #%d\n", rname(r_mul), elem_sz);
            emit_ins3(cg, "MUL", rname(r_idx), rname(r_idx), rname(r_mul));
            reg_free(&cg->regs, r_mul);
        }

        /* IMPORTANT: stack-allocated arrays grow downward from their top offset.
           For stack arrays we subtract the scaled index; for globals we add. */
        if (is_stack) emit_ins3(cg, "SUB", rname(r_addr), rname(r_addr), rname(r_idx));
        else emit_ins3(cg, "ADD", rname(r_addr), rname(r_addr), rname(r_idx));
        reg_free(&cg->regs, r_idx);

        int rv = cg_eval_expr(cg, rhs);
        if (is_stack) emit_ins2(cg, "STS", rname(r_addr), rname(rv));
        else emit_ins2(cg, "ST", rname(r_addr), rname(rv));

        reg_free(&cg->regs, r_addr);
        return rv;
    }

    if (!lhs || lhs->type != AST_IDENTIFIER || !lhs->value) {
        return cg_eval_expr(cg, rhs);
    }
    const Symbol* sym = cg_lookup_symbol((SymbolTable*)cg->st, lhs->value, cg->func_scope_id);
    int rv = cg_eval_expr(cg, rhs);

    if (sym) {
        if (sym->type == SYM_GLOBAL) {
            emit_addr_abs(cg, sym);
            emit_ins2(cg, "ST", "r7", rname(rv));
        }
        else if (symbol_is_stack_resident(sym)) {
            emit_addr_stack_sym(cg, sym);
            emit_ins2(cg, "STS", "r7", rname(rv));
        }
    }

    return rv;
}

static void emit_function_prolog(CG* cg) {
    int frame = compute_frame_size_bytes(cg->st, cg->func_scope_id);

    cg_comment(cg, "function %s, scope %d, frame=%d", cg->func_name, cg->func_scope_id, frame);

    emit(cg, "    PUSH fp\n");
    emit(cg, "    MOV fp, sp\n");

    if (frame > 0) {
        emit(cg, "    MOVI r7, #%d\n", frame);
        emit_ins3(cg, "SUB", "sp", "sp", "r7");
    }
}

static void emit_function_epilog(CG* cg) {
    emit_label(cg, cg->epilog_label);

    /* If function returns a value and there is an implicit return variable (e.g. 'result'),
       load it into r0 before tearing down the frame. */
    if (cg->has_return_value && cg->return_sym) {
        if (cg->return_sym->type == SYM_GLOBAL) {
            emit_addr_abs(cg, cg->return_sym);
            emit_ins2(cg, "LD", "r0", "r7");
        }
        else if (cg->return_sym->type == SYM_CONSTANT) {
            emit_addr_abs(cg, cg->return_sym);
            emit_ins2(cg, "LDC", "r0", "r7");
        }
        else if (symbol_is_stack_resident(cg->return_sym)) {
            emit_addr_stack_sym(cg, cg->return_sym);
            emit_ins2(cg, "LDS", "r0", "r7");
        }
    }

    emit(cg, "    MOV sp, fp\n");
    emit(cg, "    POP fp\n");
    emit(cg, "    RET\n");
}

static void emit_one_node(CG* cg, const CFGNode* n) {
    if (!cg || !n) return;

    reg_init(&cg->regs);

    if (n->type == CFG_START) {
        /* CFG_START: сам узел кода не содержит, но здесь ставим пролог */
        emit_function_prolog(cg);
        if (n->defaultNext) {
            emit(cg, "    JMP %s\n", cg_node_label(cg, n->defaultNext));
        }
        return;
    }

    if (n->type == CFG_END) {
        /* если попали сюда напрямую без явного return - просто эпилог */
        emit(cg, "    JMP %s\n", cg->epilog_label);
        return;
    }

    if (n->type == CFG_ERROR) {
        cg_comment(cg, "CFG_ERROR: %s", n->label ? n->label : "(no label)");
        if (n->defaultNext) emit(cg, "    JMP %s\n", cg_node_label(cg, n->defaultNext));
        return;
    }

    if (n->type == CFG_CONDITION) {
        const char* t = n->conditionalNext ? cg_node_label(cg, n->conditionalNext) : cg_new_label(cg, "cond_true");
        const char* f = n->defaultNext ? cg_node_label(cg, n->defaultNext) : cg_new_label(cg, "cond_false");

        const ASTNode* expr = (n->expr_tree_count > 0) ? n->expr_trees[0] : n->op_tree;
        cg_emit_branch_on_expr(cg, expr, t, f);
        return;
    }

    /* merge / block etc */
    if (n->type == CFG_MERGE) {
        if (n->defaultNext) {
            emit(cg, "    JMP %s\n", cg_node_label(cg, n->defaultNext));
        }
        return;
    }

    /* здесь обычные блоки */
    const ASTNode* stmt = n->ast_node;

    /* поддержка return на уровне AST (если CFG builder начнёт его создавать) */
    if (n->type == CFG_RETURN || (stmt && stmt->type == AST_RETURN_STATEMENT)) {
        const ASTNode* retexpr = NULL;
        if (stmt && stmt->child_count > 0) retexpr = stmt->children[0];
        if (retexpr) {
            int rv = cg_eval_expr(cg, retexpr);
            if (rv != 0) emit(cg, "    MOV r0, %s\n", rname(rv));
            reg_free(&cg->regs, rv);
        }
        emit(cg, "    JMP %s\n", cg->epilog_label);
        return;
    }

    if (stmt && stmt->type == AST_VAR_DECLARATION) {
        /* var decl: место в стеке уже зарезервировано в фрейме */
        if (n->defaultNext) emit(cg, "    JMP %s\n", cg_node_label(cg, n->defaultNext));
        return;
    }

    if (n->is_break) {
        /* break: просто переход */
        if (n->defaultNext) emit(cg, "    JMP %s\n", cg_node_label(cg, n->defaultNext));
        return;
    }

    /* выражения */
    if (n->expr_tree_count > 0 && n->expr_trees[0]) {
        int rv = cg_eval_expr(cg, n->expr_trees[0]);
        /* результат можно выкинуть */
        reg_free(&cg->regs, rv);
    }

    if (n->defaultNext) {
        emit(cg, "    JMP %s\n", cg_node_label(cg, n->defaultNext));
    }
}

/* =========================
 * Function emission
 * ========================= */

static int cmp_node_id_ptr(const void* a, const void* b) {
    const CFGNode* na = *(const CFGNode* const*)a;
    const CFGNode* nb = *(const CFGNode* const*)b;
    if (!na || !nb) return 0;
    return (na->id < nb->id) ? -1 : (na->id > nb->id ? 1 : 0);
}

static int emit_function(CG* cg, const FunctionInfo* fn) {
    if (!cg || !fn || !fn->entry) return 0;

    snprintf(cg->func_name, sizeof(cg->func_name), "%s", fn->name);
    cg->func_scope_id = fn->scope_id;
    cg->label_seq = 0;
    reg_init(&cg->regs);
    snprintf(cg->epilog_label, sizeof(cg->epilog_label), "_EPILOG_%s", cg->func_name);

    /* return information for this function (heuristics: implicit return var 'result') */
    cg->func_sym = NULL;
    cg->return_sym = NULL;
    cg->has_return_value = 0;
    if (cg->st) {
        for (int i = 0; i < cg->st->symbol_count; i++) {
            Symbol* s = (Symbol*)&cg->st->symbols[i];
            if (s->type == SYM_FUNCTION && s->name && strcmp(s->name, cg->func_name) == 0) {
                cg->func_sym = s;
                if (s->return_type && strcmp(s->return_type, "void") != 0) {
                    cg->has_return_value = 1;
                }
                break;
            }
        }
        if (cg->has_return_value) {
            cg->return_sym = cg_lookup_symbol((SymbolTable*)cg->st, "result", cg->func_scope_id);
            if (!cg->return_sym) {
                cg->return_sym = cg_lookup_symbol((SymbolTable*)cg->st, cg->func_name, cg->func_scope_id);
            }
        }
    }

    mark_reachable(cg, fn->entry);

    /* собрать список reachable nodes */
    int ncount = 0;
    for (int i = 0; i < cg->cfg->node_count; i++) {
        if (cg->reachable[i]) ncount++;
    }

    const CFGNode** nodes = (const CFGNode**)malloc((size_t)ncount * sizeof(CFGNode*));
    if (!nodes) return 0;

    int k = 0;
    for (int i = 0; i < cg->cfg->node_count; i++) {
        if (cg->reachable[i]) nodes[k++] = cg->cfg->nodes[i];
    }
    qsort(nodes, (size_t)ncount, sizeof(CFGNode*), cmp_node_id_ptr);

    /* function label */
    {
        char fl[300];
        snprintf(fl, sizeof(fl), "_func_%s", cg->func_name);
        emit_label(cg, fl);
    }

    cg_comment(cg, "CFG nodes reachable: %d", ncount);

    /* emit each node with its internal label */
    for (int i = 0; i < ncount; i++) {
        const CFGNode* n = nodes[i];
        emit_label(cg, cg_node_label(cg, n));
        if (cg->opt.emit_comments && n->label) {
            cg_comment(cg, "node %d: %s", n->id, n->label);
        }
        emit_one_node(cg, n);
        emit(cg, "\n");
    }

    /* shared epilog */
    emit_function_epilog(cg);
    emit(cg, "\n");

    free((void*)nodes);
    return 1;
}

/* =========================
 * Public API
 * ========================= */

CodegenOptions codegen_default_options(void) {
    CodegenOptions o;
    o.emit_comments = 1;
    o.emit_start_stub = 1;
    return o;
}

int codegen_generate_stream(const CFG* cfg, const SymbolTable* st, FILE* out, CodegenOptions opt) {
    if (!cfg || !out) return 0;

    FunctionInfo* funcs = NULL;
    int fcount = 0;
    if (!collect_functions(cfg, &funcs, &fcount)) return 0;

    CG cg;
    memset(&cg, 0, sizeof(cg));
    cg.cfg = cfg;
    cg.st = st;
    cg.opt = opt;
    sb_init(&cg.out);

    cg_labels_init(&cg);

    cg.reachable = (unsigned char*)malloc((size_t)cfg->node_count);
    if (!cg.reachable) {
        free(funcs);
        sb_free(&cg.out);
        return 0;
    }

    /* header */
    sb_append(&cg.out, "; ---- Noobik assembly generated from CFG ----\n\n");
    sb_append(&cg.out, "[section cram]\n\n");

    if (opt.emit_start_stub) {
        sb_append(&cg.out, "_start:\n");
        sb_append(&cg.out, "    MOVI sp, #0xFFFC\n");
        sb_append(&cg.out, "    MOVI fp, #0xFFFC\n");
        sb_append(&cg.out, "    CALL _func_main\n");
        sb_append(&cg.out, "    HLT\n\n");
    }

    for (int i = 0; i < fcount; i++) {
        emit_function(&cg, &funcs[i]);
    }

    sb_append(&cg.out, "[section name=dram, bank=dram, start=0x8000]\n");

    int ok = 1;
    if (cg.out.buf) {
        if (fwrite(cg.out.buf, 1, cg.out.len, out) != cg.out.len) ok = 0;
    }

    cg_labels_free(&cg);

    free(cg.reachable);
    free(funcs);
    sb_free(&cg.out);
    return ok;
}

int codegen_generate_file(const CFG* cfg, const SymbolTable* st, const char* output_path, CodegenOptions opt) {
    if (!output_path) return 0;
    FILE* f = fopen(output_path, "wb");
    if (!f) return 0;
    int ok = codegen_generate_stream(cfg, st, f, opt);
    fclose(f);
    return ok;
}
