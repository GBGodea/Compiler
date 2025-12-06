%{
#include "parser.tab.h"
#include <stdlib.h>
#include <string.h>

// ÍÅ ÎÏÐÅÄÅËßÅÌ çäåñü - áåðåì èç parser.y
extern int line_num;

%}

%option noyywrap
%x COMMENT

DIGIT       [0-9]
LETTER      [a-zA-Z_]
IDENTIFIER  {LETTER}({LETTER}|{DIGIT})*
INTEGER     {DIGIT}+
HEX         0[xX][0-9a-fA-F]+
BINARY      0[bB][01]+
WHITESPACE  [ \t]+
NEWLINE     [\r\n]

%%

{WHITESPACE}        ;
{NEWLINE}           { line_num++; }
"//".*              ;
"/*"                { BEGIN(COMMENT); }

<COMMENT>{
    [^*\n]+         ;
    "*"[^/\n]       ;
    "*/"             { BEGIN(INITIAL); }
    "\n"             { line_num++; }
}

"method"            { return METHOD; }
"var"               { return VAR; }
"begin"             { return BEGIN_KW; }
"end"               { return END; }
"if"                { return IF; }
"then"              { return THEN; }
"else"              { return ELSE; }
"while"             { return WHILE; }
"do"                { return DO; }
"repeat"            { return REPEAT; }
"until"             { return UNTIL; }
"break"             { return BREAK; }

"bool"              { return BOOL_TYPE; }
"byte"              { return BYTE_TYPE; }
"int"               { return INT_TYPE; }
"uint"              { return UINT_TYPE; }
"long"              { return LONG_TYPE; }
"ulong"             { return ULONG_TYPE; }
"char"              { return CHAR_TYPE; }
"string"            { return STRING_TYPE; }
"array"             { return ARRAY; }
"of"                { return OF; }

"true"|"false"      { yylval.str = strdup(yytext); return BOOL_LITERAL; }

":="                { return ASSIGN; }
"=="                { return EQ; }
"!="                { return NE; }
"<="                { return LE; }
">="                { return GE; }
"<"                 { return LT; }
">"                 { return GT; }

"&&"                { return AND; }
"||"                { return OR; }
"!"                 { return NOT; }

"+"                 { return PLUS; }
"-"                 { return MINUS; }
"*"                 { return MUL; }
"/"                 { return DIV; }
"%"                 { return MOD; }

"("                 { return LPAREN; }
")"                 { return RPAREN; }
"["                 { return LBRACKET; }
"]"                 { return RBRACKET; }
","                 { return COMMA; }
":"                 { return COLON; }
";"                 { return SEMICOLON; }

{INTEGER}           { yylval.num = atoi(yytext); return INT_LITERAL; }
{HEX}               { yylval.str = strdup(yytext); return HEX_LITERAL; }
{BINARY}            { yylval.str = strdup(yytext); return BITS_LITERAL; }

\"([^\"\\\n]|\\.)*\" { yylval.str = strdup(yytext); return STRING_LITERAL; }
'([^'\\\n]|\\.)'    { yylval.str = strdup(yytext); return CHAR_LITERAL; }

{IDENTIFIER}        { yylval.str = strdup(yytext); return IDENTIFIER; }

.                   { 
    fprintf(stderr, "Unknown character '%c' at line %d\n", yytext[0], line_num);
    return yytext[0]; 
}

%%