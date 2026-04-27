%{
#include <cstdlib>
#include <string>
#include "ast.h"
#include "parser.tab.h"

extern int yylineno;
%}

%option noyywrap
%option yylineno

/* Regular expression definitions */
DIGIT       [0-9]
LETTER      [a-zA-Z_]
ID          {LETTER}({LETTER}|{DIGIT})*
NUMBER      (0|[1-9][0-9]*)
WHITESPACE  [ \t\r\n]+

/* Comment handling states */
%x COMMENT

%%

    /* Skip whitespace */
{WHITESPACE}    { /* ignore */ }

    /* Single-line comment */
"//".*          { /* ignore single-line comment */ }

    /* Multi-line comment */
"/*"            { BEGIN(COMMENT); }
<COMMENT>"*/"   { BEGIN(INITIAL); }
<COMMENT>.      { /* ignore comment content */ }
<COMMENT>\n     { /* ignore newline in comment */ }

    /* Keywords */
"int"           { return INT; }
"void"          { return VOID; }
"const"         { return CONST; }
"if"            { return IF; }
"else"          { return ELSE; }
"while"         { return WHILE; }
"break"         { return BREAK; }
"continue"      { return CONTINUE; }
"return"        { return RETURN; }

    /* Operators */
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

    /* Delimiters */
"("             { return LPAREN; }
")"             { return RPAREN; }
"{"             { return LBRACE; }
"}"             { return RBRACE; }
";"             { return SEMICOLON; }
","             { return COMMA; }

    /* Identifiers */
{ID}            { 
                    yylval.str = new std::string(yytext); 
                    return IDENTIFIER; 
                }

    /* Numbers */
{NUMBER}        { 
                    yylval.num = atoi(yytext); 
                    return NUMBER; 
                }

    /* Error handling */
.               { 
                    fprintf(stderr, "Lexical error: unexpected character '%s' at line %d\n", 
                            yytext, yylineno); 
                    exit(1);
                }

%%
