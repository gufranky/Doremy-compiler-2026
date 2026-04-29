%{
#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <cstdio>
#include <io.h>
extern "C" int fileno(FILE*);
#define isatty _isatty
#endif
#include "ast.h"
#include "parser.tab.h"

extern int yylineno;
%}

%option noyywrap
%option yylineno

DIGIT       [0-9]
LETTER      [a-zA-Z_]
ID          {LETTER}({LETTER}|{DIGIT})*
HEXNUMBER   0[xX][0-9a-fA-F]+
OCTNUMBER   0[0-7]*
DECNUMBER   [1-9][0-9]*
WHITESPACE  [ \t\r\n]+

%x COMMENT

%%

{WHITESPACE}    { /* ignore */ }

"//".*          { /* ignore single-line comment */ }
"/*"            { BEGIN(COMMENT); }
<COMMENT>"*/"   { BEGIN(INITIAL); }
<COMMENT>.      { /* ignore comment content */ }
<COMMENT>\n     { /* ignore newline in comment */ }

"int"           { return INT; }
"void"          { return VOID; }
"float"         { return FLOAT; }
"const"         { return CONST; }
"if"            { return IF; }
"else"          { return ELSE; }
"while"         { return WHILE; }
"break"         { return BREAK; }
"continue"      { return CONTINUE; }
"return"        { return RETURN; }

"+"             { return PLUS; }
"-"             { return MINUS; }
"*"             { return STAR; }
"/"             { return SLASH; }
"%"             { return PERCENT; }

"<"             { return LT; }
">"             { return GT; }
"<="            { return LE; }
">="            { return GE; }
"=="            { return EQ; }
"!="            { return NE; }

"&&"            { return AND; }
"||"            { return OR; }
"!"             { return NOT; }

"="             { return ASSIGN; }

"("             { return LPAREN; }
")"             { return RPAREN; }
"{"             { return LBRACE; }
"}"             { return RBRACE; }
";"             { return SEMICOLON; }
","             { return COMMA; }

{ID}            {
                    yylval.str = new std::string(yytext);
                    return IDENTIFIER;
                }

{HEXNUMBER}     {
                    yylval.num = static_cast<int>(strtol(yytext, nullptr, 0));
                    return NUMBER;
                }

{OCTNUMBER}     {
                    yylval.num = static_cast<int>(strtol(yytext, nullptr, 0));
                    return NUMBER;
                }

{DECNUMBER}     {
                    yylval.num = static_cast<int>(strtol(yytext, nullptr, 10));
                    return NUMBER;
                }

.               {
                    fprintf(stderr, "Lexical error: unexpected character '%s' at line %d\n",
                            yytext, yylineno);
                    exit(1);
                }

%%
