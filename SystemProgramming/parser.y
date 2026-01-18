%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

int yylex();
void yyerror(const char *s);

extern char *yytext;

int line_num = 1;
ASTNode* root_ast = NULL;

%}

%union {
    char *str;
    int num;
    struct ASTNode *node;
}

%token <str> IDENTIFIER
%token <num> INT_LITERAL
%token <str> STRING_LITERAL
%token <str> CHAR_LITERAL
%token <str> HEX_LITERAL
%token <str> BITS_LITERAL
%token <str> BOOL_LITERAL

%token METHOD VAR BEGIN_KW END IF THEN ELSE WHILE DO REPEAT UNTIL BREAK
%token BOOL_TYPE BYTE_TYPE INT_TYPE UINT_TYPE LONG_TYPE ULONG_TYPE FLOAT_TYPE CHAR_TYPE STRING_TYPE
%token ARRAY OF

%token ASSIGN
%token EQ NE LE GE LT GT
%token AND OR NOT
%token PLUS MINUS MUL DIV MOD
%token LPAREN RPAREN
%token LBRACKET RBRACKET
%token COMMA COLON SEMICOLON

%type <node> source sourceItem funcDef funcSignature argDef argDefList argDefListNonEmpty
%type <node> typeRef body varDeclarations identifierList statementBlock statementList statement
%type <node> expr logic_expr comp_expr add_expr mul_expr unary_expr postfix_expr primary_expr exprList exprListNonEmpty
%type <node> optionalType

%right ASSIGN
%left OR
%left AND
%left EQ NE
%left LT LE GT GE
%left PLUS MINUS
%left MUL DIV MOD
%right NOT UMINUS
%left LPAREN RPAREN LBRACKET RBRACKET

%start source

%%

source:
    { 
        $$ = createASTNode(AST_PROGRAM, "Program", line_num);
        root_ast = $$;
    }
    | source sourceItem {
        $$ = addChild($1, $2);
        root_ast = $$;
    }
    ;

sourceItem:
    funcDef { $$ = $1; }
    ;

funcDef:
    METHOD funcSignature body {
        $$ = createASTNode(AST_FUNCTION_DEF, "function", line_num);
        addChild($$, $2);
        addChild($$, $3);
    }
    | METHOD funcSignature SEMICOLON {
        $$ = createASTNode(AST_FUNCTION_DEF, "declaration", line_num);
        addChild($$, $2);
    }
    ;

funcSignature:
    IDENTIFIER LPAREN argDefList RPAREN {
        $$ = createASTNode(AST_FUNCTION_SIGNATURE, $1, line_num);
        addChild($$, $3);
        free($1);
    }
    | IDENTIFIER LPAREN argDefList RPAREN COLON typeRef {
        $$ = createASTNode(AST_FUNCTION_SIGNATURE, $1, line_num);
        addChild($$, $3);
        addChild($$, $6);
        free($1);
    }
    ;

argDefList:
    { $$ = NULL; }
    | argDefListNonEmpty { $$ = $1; }
    ;

argDefListNonEmpty:
    /*
     * Build a *flat* parameter list.
     *
     * Old behavior:
     *   argDefListNonEmpty: argDefListNonEmpty COMMA argDef { $$ = addChild($1,$3); }
     * created a *chain* where the first AST_ARG_DEF became the parent of the next.
     * That made parameters like "(n: int, t: int)" look like:
     *   ArgDef(n)
     *     TypeRef(int)
     *     ArgDef(t)
     *       TypeRef(int)
     *
     * New behavior:
     *   StatementList("params")
     *     ArgDef(n)
     *     ArgDef(t)
     */
    argDef {
        $$ = createASTNode(AST_STATEMENT_LIST, "params", line_num);
        addChild($$, $1);
    }
    | argDefListNonEmpty COMMA argDef {
        $$ = addChild($1, $3);
    }
    ;

argDef:
    IDENTIFIER {
        $$ = createASTNode(AST_ARG_DEF, $1, line_num);
        free($1);
    }
    | IDENTIFIER COLON typeRef {
        $$ = createASTNode(AST_ARG_DEF, $1, line_num);
        addChild($$, $3);
        free($1);
    }
    ;

typeRef:
    BOOL_TYPE { $$ = createASTNode(AST_TYPE_REF, "bool", line_num); }
    | BYTE_TYPE { $$ = createASTNode(AST_TYPE_REF, "byte", line_num); }
    | INT_TYPE { $$ = createASTNode(AST_TYPE_REF, "int", line_num); }
    | UINT_TYPE { $$ = createASTNode(AST_TYPE_REF, "uint", line_num); }
    | LONG_TYPE { $$ = createASTNode(AST_TYPE_REF, "long", line_num); }
    | ULONG_TYPE { $$ = createASTNode(AST_TYPE_REF, "ulong", line_num); }
    | FLOAT_TYPE { $$ = createASTNode(AST_TYPE_REF, "float", line_num); }
    | CHAR_TYPE { $$ = createASTNode(AST_TYPE_REF, "char", line_num); }
    | STRING_TYPE { $$ = createASTNode(AST_TYPE_REF, "string", line_num); }
    | IDENTIFIER { 
        $$ = createASTNode(AST_TYPE_REF, $1, line_num);
        free($1);
    }
    | ARRAY LBRACKET INT_LITERAL RBRACKET OF typeRef {
        $$ = createASTNode(AST_TYPE_REF, "array", line_num);
        char buf[32];
        sprintf(buf, "%d", $3);
        ASTNode* sz = createASTNode(AST_LITERAL, buf, line_num);
        addChild($$, sz);
        addChild($$, $6);
    }
;

/* ��������� 1: �������� body - ������� ������ ���� ������ ���������� */
body:
    varDeclarations BEGIN_KW statementList END SEMICOLON {
        $$ = createASTNode(AST_STATEMENT_BLOCK, "body", line_num);
        if ($1) {
            /* ��������� ���������� ���������� ��� ����� ���� */
            addChild($$, $1);
        }
        if ($3) {
            /* ��������� ��������� */
            addChild($$, $3);
        }
    }
    ;

/* ��������� 2: �������� varDeclarations */
varDeclarations:
    { $$ = NULL; }
    | varDeclarations VAR identifierList optionalType SEMICOLON {
        ASTNode* varDecl = createASTNode(AST_VAR_DECLARATION, "var", line_num);
        addChild(varDecl, $3);  /* identifierList */
        if ($4) {  /* optionalType, ���� ���� */
            addChild(varDecl, $4);
        }

        if ($1 == NULL) {
            /* ������� ���� ������ ��� ���� ���������� */
            $$ = createASTNode(AST_STATEMENT_LIST, "var_declarations", line_num);
            addChild($$, varDecl);
        } else {
            /* ��������� ����� ���������� � ������������ ������ */
            $$ = addChild($1, varDecl);
        }
    }
    ;

optionalType:
    { $$ = NULL; }  /* ������ ��� */
    | COLON typeRef { $$ = $2; }  /* ����� ��� */
    ;

identifierList:
    IDENTIFIER { 
        $$ = createASTNode(AST_IDENTIFIER, $1, line_num);
        free($1);
    }
    | identifierList COMMA IDENTIFIER {
        $$ = addChild($1, createASTNode(AST_IDENTIFIER, $3, line_num));
        free($3);
    }
    ;

/* ��������� 3: �������� statementBlock - ������ ��� ����� begin-end */
statementBlock:
    BEGIN_KW statementList END {
        $$ = createASTNode(AST_STATEMENT_BLOCK, "begin", line_num);
        if ($2) {
            addChild($$, $2);
        }
    }
    | BEGIN_KW statementList END SEMICOLON {
        $$ = createASTNode(AST_STATEMENT_BLOCK, "begin", line_num);
        if ($2) {
            addChild($$, $2);
        }
    }
    ;

/* ��������� 4: �������� statementList */
statementList:
    { 
        $$ = NULL; 
    }
    | statement {
        $$ = createASTNode(AST_STATEMENT_LIST, "statements", line_num);
        addChild($$, $1);
    }
    | statementList statement {
        if ($1 == NULL) {
            $$ = createASTNode(AST_STATEMENT_LIST, "statements", line_num);
            addChild($$, $2);
        } else {
            $$ = addChild($1, $2);
        }
    }
    ;

statement:
    IF expr THEN statement {
        $$ = createASTNode(AST_IF_STATEMENT, "if", line_num);
        addChild($$, $2);
        addChild($$, $4);
    }
    | IF expr THEN statement ELSE statement {
        $$ = createASTNode(AST_IF_STATEMENT, "if-else", line_num);
        addChild($$, $2);
        addChild($$, $4);
        addChild($$, $6);
    }
    | statementBlock { $$ = $1; }
    | WHILE expr DO statement {
        $$ = createASTNode(AST_WHILE_STATEMENT, "while", line_num);
        addChild($$, $2);
        addChild($$, $4);
    }
    | REPEAT statement UNTIL expr SEMICOLON {
        $$ = createASTNode(AST_REPEAT_STATEMENT, "repeat-until", line_num);
        addChild($$, $2);
        addChild($$, $4);
    }
    | REPEAT statement WHILE expr SEMICOLON {
        $$ = createASTNode(AST_REPEAT_STATEMENT, "repeat-while", line_num);
        addChild($$, $2);
        addChild($$, $4);
    }
    | BREAK SEMICOLON { 
        $$ = createASTNode(AST_BREAK_STATEMENT, "break", line_num);
    }
    | expr SEMICOLON {
        $$ = createASTNode(AST_EXPR_STATEMENT, "expression", line_num);
        addChild($$, $1);
    }
    ;

/* ���������� ��������� � ������ ����������� */

expr:
    logic_expr { $$ = $1; }
    | logic_expr ASSIGN expr {
        $$ = createASTNode(AST_ASSIGNMENT, ":=", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    ;

logic_expr:
    comp_expr { $$ = $1; }
    | logic_expr OR comp_expr {
        $$ = createASTNode(AST_BINARY_EXPR, "||", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    | logic_expr AND comp_expr {
        $$ = createASTNode(AST_BINARY_EXPR, "&&", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    ;

comp_expr:
    add_expr { $$ = $1; }
    | comp_expr EQ add_expr {
        $$ = createASTNode(AST_BINARY_EXPR, "==", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    | comp_expr NE add_expr {
        $$ = createASTNode(AST_BINARY_EXPR, "!=", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    | comp_expr LT add_expr {
        $$ = createASTNode(AST_BINARY_EXPR, "<", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    | comp_expr LE add_expr {
        $$ = createASTNode(AST_BINARY_EXPR, "<=", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    | comp_expr GT add_expr {
        $$ = createASTNode(AST_BINARY_EXPR, ">", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    | comp_expr GE add_expr {
        $$ = createASTNode(AST_BINARY_EXPR, ">=", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    ;

add_expr:
    mul_expr { $$ = $1; }
    | add_expr PLUS mul_expr {
        $$ = createASTNode(AST_BINARY_EXPR, "+", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    | add_expr MINUS mul_expr {
        $$ = createASTNode(AST_BINARY_EXPR, "-", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    ;

mul_expr:
    unary_expr { $$ = $1; }
    | mul_expr MUL unary_expr {
        $$ = createASTNode(AST_BINARY_EXPR, "*", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    | mul_expr DIV unary_expr {
        $$ = createASTNode(AST_BINARY_EXPR, "/", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    | mul_expr MOD unary_expr {
        $$ = createASTNode(AST_BINARY_EXPR, "%", line_num);
        addChild($$, $1);
        addChild($$, $3);
    }
    ;

unary_expr:
    postfix_expr { $$ = $1; }
    | NOT unary_expr {
        $$ = createASTNode(AST_UNARY_EXPR, "!", line_num);
        addChild($$, $2);
    }
    | MINUS unary_expr %prec UMINUS {
        $$ = createASTNode(AST_UNARY_EXPR, "-", line_num);
        addChild($$, $2);
    }
    | PLUS unary_expr %prec UMINUS {
        $$ = createASTNode(AST_UNARY_EXPR, "+", line_num);
        addChild($$, $2);
    }
    ;

postfix_expr:
    primary_expr { $$ = $1; }
    | postfix_expr LPAREN exprList RPAREN {
        const char* func_name = "call";
        if ($1->type == AST_IDENTIFIER && $1->value) {
            func_name = $1->value;
        }
    
        $$ = createASTNode(AST_CALL_EXPR, func_name, line_num);
        addChild($$, $1);
        if ($3) {
            addChild($$, $3);
        } else {
            ASTNode* empty_args = createASTNode(AST_STATEMENT_LIST, "args", line_num);
            addChild($$, empty_args);
        }
    }
    | postfix_expr LBRACKET exprList RBRACKET {
        $$ = createASTNode(AST_INDEX_EXPR, "index", line_num);
        addChild($$, $1);
        if ($3) addChild($$, $3);
    }
    ;

primary_expr:
    IDENTIFIER { 
        $$ = createASTNode(AST_IDENTIFIER, $1, line_num);
        free($1);
    }
    | INT_LITERAL {
        char buf[32];
        sprintf(buf, "%d", $1);
        $$ = createASTNode(AST_LITERAL, buf, line_num);
    }
    | STRING_LITERAL { 
        $$ = createASTNode(AST_STRING_LITERAL, $1, line_num);
        free($1);
    }
    | CHAR_LITERAL { 
        $$ = createASTNode(AST_CHAR_LITERAL, $1, line_num);
        free($1);
    }
    | HEX_LITERAL { 
        $$ = createASTNode(AST_LITERAL, $1, line_num);
        free($1);
    }
    | BITS_LITERAL { 
        $$ = createASTNode(AST_LITERAL, $1, line_num);
        free($1);
    }
    | BOOL_LITERAL { 
        $$ = createASTNode(AST_BOOL_LITERAL, $1, line_num);
        free($1);
    }
    | LPAREN expr RPAREN {
        $$ = $2;
    }
    ;

exprList:
    { $$ = NULL; }
    | exprListNonEmpty { $$ = $1; }
    ;

exprListNonEmpty:
    expr { 
        ASTNode* list = createASTNode(AST_STATEMENT_LIST, "args", line_num);
        addChild(list, $1);
        $$ = list;
    }
    | exprListNonEmpty COMMA expr {
        addChild($1, $3);
        $$ = $1;
    }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Parse error: (%s) at token '%s' line %d\n", s, yytext, line_num);
}