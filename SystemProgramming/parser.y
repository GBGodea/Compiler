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
%token BOOL_TYPE BYTE_TYPE INT_TYPE UINT_TYPE LONG_TYPE ULONG_TYPE CHAR_TYPE STRING_TYPE
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
%type <node> expr exprList exprListNonEmpty binOp unOp commaList commaListNonEmpty

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
    argDef { $$ = $1; }
    | argDefListNonEmpty COMMA argDef {
        if ($1 == NULL) {
            $$ = $3;
        } else {
            $$ = addChild($1, $3);
        }
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
    | CHAR_TYPE { $$ = createASTNode(AST_TYPE_REF, "char", line_num); }
    | STRING_TYPE { $$ = createASTNode(AST_TYPE_REF, "string", line_num); }
    | IDENTIFIER { 
        $$ = createASTNode(AST_TYPE_REF, $1, line_num);
        free($1);
    }
    | ARRAY LBRACKET commaList RBRACKET OF typeRef {
        $$ = createASTNode(AST_TYPE_REF, "array", line_num);
        if ($3) addChild($$, $3);
        addChild($$, $6);
    }
    ;

commaList:
    { $$ = NULL; }
    | commaListNonEmpty { $$ = $1; }
    ;

commaListNonEmpty:
    COMMA { $$ = createASTNode(AST_LITERAL, ",", line_num); }
    | commaListNonEmpty COMMA {
        $$ = addChild($1, createASTNode(AST_LITERAL, ",", line_num));
    }
    ;

body:
    varDeclarations statementBlock {
        $$ = createASTNode(AST_STATEMENT_BLOCK, "body", line_num);
        if ($1) addChild($$, $1);
        addChild($$, $2);
    }
    | statementBlock {
        $$ = $1;
    }
    ;

varDeclarations:
    { $$ = NULL; }
    | varDeclarations VAR identifierList COLON typeRef SEMICOLON {
        ASTNode* varDecl = createASTNode(AST_VAR_DECLARATION, "var", line_num);
        addChild(varDecl, $3);
        addChild(varDecl, $5);

        if ($1 == NULL) {
            $$ = varDecl;
        } else {
            $$ = addChild($1, varDecl);
        }
    }
    | varDeclarations VAR identifierList SEMICOLON {
        ASTNode* varDecl = createASTNode(AST_VAR_DECLARATION, "var", line_num);
        addChild(varDecl, $3);

        if ($1 == NULL) {
            $$ = varDecl;
        } else {
            $$ = addChild($1, varDecl);
        }
    }
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

// ИСПРАВЛЕНИЕ: statementBlock создаёт правильную иерархию
statementBlock:
    BEGIN_KW statementList END SEMICOLON {
        $$ = createASTNode(AST_STATEMENT_BLOCK, "begin", line_num);
        if ($2) {
            // Если есть список statements, добавляем его
            addChild($$, $2);
        }
    }
    ;

statementList:
    { 
        // Пустой список = NULL
        $$ = NULL; 
    }
    | statement {
        ASTNode* list = createASTNode(AST_STATEMENT_LIST, "statements", line_num);
        addChild(list, $1);
        $$ = list;
    }
    | statementList statement {
        // Последующие statements: добавляем к существующему списку
        // $1 уже является AST_STATEMENT_LIST
        if ($1 == NULL) {
            ASTNode* list = createASTNode(AST_STATEMENT_LIST, "statements", line_num);
            addChild(list, $2);
            $$ = list;
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
    | REPEAT statement WHILE expr SEMICOLON {
        $$ = createASTNode(AST_REPEAT_STATEMENT, "repeat-while", line_num);
        addChild($$, $2);
        addChild($$, $4);
    }
    | REPEAT statement UNTIL expr SEMICOLON {
        $$ = createASTNode(AST_REPEAT_STATEMENT, "repeat-until", line_num);
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

expr:
    expr binOp expr {
        $$ = addChild($2, $1);
        addChild($$, $3);
    }
    | unOp expr %prec UMINUS {
        $$ = addChild($1, $2);
    }
    | LPAREN expr RPAREN {
        $$ = $2;
    }
    | expr LPAREN exprList RPAREN {
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
    | expr LBRACKET exprList RBRACKET {
        $$ = createASTNode(AST_INDEX_EXPR, "index", line_num);
        addChild($$, $1);
        if ($3) addChild($$, $3);
    }
    | IDENTIFIER { 
        $$ = createASTNode(AST_IDENTIFIER, $1, line_num);
        free($1);
    }
    | INT_LITERAL {
        char buf[32];
        sprintf(buf, "%d", $1);
        $$ = createASTNode(AST_LITERAL, buf, line_num);
    }
    | STRING_LITERAL { 
        $$ = createASTNode(AST_LITERAL, $1, line_num);
        free($1);
    }
    | CHAR_LITERAL { 
        $$ = createASTNode(AST_LITERAL, $1, line_num);
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
        $$ = createASTNode(AST_LITERAL, $1, line_num);
        free($1);
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

binOp:
    ASSIGN { $$ = createASTNode(AST_BINARY_EXPR, ":=", line_num); }
    | PLUS { $$ = createASTNode(AST_BINARY_EXPR, "+", line_num); }
    | MINUS { $$ = createASTNode(AST_BINARY_EXPR, "-", line_num); }
    | MUL { $$ = createASTNode(AST_BINARY_EXPR, "*", line_num); }
    | DIV { $$ = createASTNode(AST_BINARY_EXPR, "/", line_num); }
    | MOD { $$ = createASTNode(AST_BINARY_EXPR, "%", line_num); }
    | EQ { $$ = createASTNode(AST_BINARY_EXPR, "==", line_num); }
    | NE { $$ = createASTNode(AST_BINARY_EXPR, "!=", line_num); }
    | LT { $$ = createASTNode(AST_BINARY_EXPR, "<", line_num); }
    | LE { $$ = createASTNode(AST_BINARY_EXPR, "<=", line_num); }
    | GT { $$ = createASTNode(AST_BINARY_EXPR, ">", line_num); }
    | GE { $$ = createASTNode(AST_BINARY_EXPR, ">=", line_num); }
    | AND { $$ = createASTNode(AST_BINARY_EXPR, "&&", line_num); }
    | OR { $$ = createASTNode(AST_BINARY_EXPR, "||", line_num); }
    ;

unOp:
    NOT { $$ = createASTNode(AST_UNARY_EXPR, "!", line_num); }
    | MINUS { $$ = createASTNode(AST_UNARY_EXPR, "-", line_num); }
    | PLUS { $$ = createASTNode(AST_UNARY_EXPR, "+", line_num); }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Parse error: (%s) at token '%s' line %d\n", s, yytext, line_num);
}