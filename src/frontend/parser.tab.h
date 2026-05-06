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

#ifndef YY_YY_GENERATED_PARSER_TAB_H_INCLUDED
# define YY_YY_GENERATED_PARSER_TAB_H_INCLUDED
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
    NUMBER = 258,                  /* NUMBER  */
    FLOAT_NUMBER = 259,            /* FLOAT_NUMBER  */
    IDENTIFIER = 260,              /* IDENTIFIER  */
    INT = 261,                     /* INT  */
    VOID = 262,                    /* VOID  */
    FLOAT = 263,                   /* FLOAT  */
    IF = 264,                      /* IF  */
    ELSE = 265,                    /* ELSE  */
    WHILE = 266,                   /* WHILE  */
    BREAK = 267,                   /* BREAK  */
    CONTINUE = 268,                /* CONTINUE  */
    RETURN = 269,                  /* RETURN  */
    CONST = 270,                   /* CONST  */
    PLUS = 271,                    /* PLUS  */
    MINUS = 272,                   /* MINUS  */
    STAR = 273,                    /* STAR  */
    SLASH = 274,                   /* SLASH  */
    PERCENT = 275,                 /* PERCENT  */
    LT = 276,                      /* LT  */
    GT = 277,                      /* GT  */
    LE = 278,                      /* LE  */
    GE = 279,                      /* GE  */
    EQ = 280,                      /* EQ  */
    NE = 281,                      /* NE  */
    AND = 282,                     /* AND  */
    OR = 283,                      /* OR  */
    NOT = 284,                     /* NOT  */
    ASSIGN = 285,                  /* ASSIGN  */
    LPAREN = 286,                  /* LPAREN  */
    RPAREN = 287,                  /* RPAREN  */
    LBRACE = 288,                  /* LBRACE  */
    RBRACE = 289,                  /* RBRACE  */
    LBRACKET = 290,                /* LBRACKET  */
    RBRACKET = 291,                /* RBRACKET  */
    SEMICOLON = 292,               /* SEMICOLON  */
    COMMA = 293,                   /* COMMA  */
    UNARY_MINUS = 294,             /* UNARY_MINUS  */
    UNARY_PLUS = 295               /* UNARY_PLUS  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 18 "grammar/parser.y"

    int num;
    float fnum;
    std::string* str;
    Type* type;

    ASTNode* astNode;
    CompUnit* compUnit;
    FuncDef* funcDef;
    Param* param;
    std::vector<Param*>* paramList;

    Stmt* stmt;
    Block* block;
    std::vector<Stmt*>* stmtList;
    VarDeclStmt* declStmt;
    VarDef* varDef;
    std::vector<VarDef*>* varDefList;

    Expr* expr;
    std::vector<Expr*>* exprList;
    std::vector<Expr*>* dimList;
    InitList* initList;

#line 129 "generated/parser.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_GENERATED_PARSER_TAB_H_INCLUDED  */
