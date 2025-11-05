/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_PARSER_TAB_H_INCLUDED
# define YY_YY_PARSER_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    IDENTIFIER = 258,              /* IDENTIFIER  */
    INT_LITERAL = 259,             /* INT_LITERAL  */
    STRING_LITERAL = 260,          /* STRING_LITERAL  */
    CHAR_LITERAL = 261,            /* CHAR_LITERAL  */
    HEX_LITERAL = 262,             /* HEX_LITERAL  */
    BITS_LITERAL = 263,            /* BITS_LITERAL  */
    BOOL_LITERAL = 264,            /* BOOL_LITERAL  */
    METHOD = 265,                  /* METHOD  */
    VAR = 266,                     /* VAR  */
    BEGIN_KW = 267,                /* BEGIN_KW  */
    END = 268,                     /* END  */
    IF = 269,                      /* IF  */
    THEN = 270,                    /* THEN  */
    ELSE = 271,                    /* ELSE  */
    WHILE = 272,                   /* WHILE  */
    DO = 273,                      /* DO  */
    REPEAT = 274,                  /* REPEAT  */
    UNTIL = 275,                   /* UNTIL  */
    BREAK = 276,                   /* BREAK  */
    BOOL_TYPE = 277,               /* BOOL_TYPE  */
    BYTE_TYPE = 278,               /* BYTE_TYPE  */
    INT_TYPE = 279,                /* INT_TYPE  */
    UINT_TYPE = 280,               /* UINT_TYPE  */
    LONG_TYPE = 281,               /* LONG_TYPE  */
    ULONG_TYPE = 282,              /* ULONG_TYPE  */
    CHAR_TYPE = 283,               /* CHAR_TYPE  */
    STRING_TYPE = 284,             /* STRING_TYPE  */
    ARRAY = 285,                   /* ARRAY  */
    OF = 286,                      /* OF  */
    ASSIGN = 287,                  /* ASSIGN  */
    EQ = 288,                      /* EQ  */
    NE = 289,                      /* NE  */
    LE = 290,                      /* LE  */
    GE = 291,                      /* GE  */
    LT = 292,                      /* LT  */
    GT = 293,                      /* GT  */
    AND = 294,                     /* AND  */
    OR = 295,                      /* OR  */
    NOT = 296,                     /* NOT  */
    PLUS = 297,                    /* PLUS  */
    MINUS = 298,                   /* MINUS  */
    MUL = 299,                     /* MUL  */
    DIV = 300,                     /* DIV  */
    MOD = 301,                     /* MOD  */
    LPAREN = 302,                  /* LPAREN  */
    RPAREN = 303,                  /* RPAREN  */
    LBRACKET = 304,                /* LBRACKET  */
    RBRACKET = 305,                /* RBRACKET  */
    COMMA = 306,                   /* COMMA  */
    COLON = 307,                   /* COLON  */
    SEMICOLON = 308,               /* SEMICOLON  */
    UMINUS = 309                   /* UMINUS  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 14 "parser.y"

    char *str;
    int num;
    struct ASTNode *node;

#line 124 "parser.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_PARSER_TAB_H_INCLUDED  */
