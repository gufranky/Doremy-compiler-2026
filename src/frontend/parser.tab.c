/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "grammar/parser.y"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "ast.h"

extern int yylex();
extern int yylineno;
extern char* yytext;
extern FILE* yyin;

void yyerror(const char* s);

CompUnit* root = nullptr;

#line 88 "generated/parser.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "parser.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_NUMBER = 3,                     /* NUMBER  */
  YYSYMBOL_FLOAT_NUMBER = 4,               /* FLOAT_NUMBER  */
  YYSYMBOL_IDENTIFIER = 5,                 /* IDENTIFIER  */
  YYSYMBOL_INT = 6,                        /* INT  */
  YYSYMBOL_VOID = 7,                       /* VOID  */
  YYSYMBOL_FLOAT = 8,                      /* FLOAT  */
  YYSYMBOL_IF = 9,                         /* IF  */
  YYSYMBOL_ELSE = 10,                      /* ELSE  */
  YYSYMBOL_WHILE = 11,                     /* WHILE  */
  YYSYMBOL_BREAK = 12,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 13,                  /* CONTINUE  */
  YYSYMBOL_RETURN = 14,                    /* RETURN  */
  YYSYMBOL_CONST = 15,                     /* CONST  */
  YYSYMBOL_PLUS = 16,                      /* PLUS  */
  YYSYMBOL_MINUS = 17,                     /* MINUS  */
  YYSYMBOL_STAR = 18,                      /* STAR  */
  YYSYMBOL_SLASH = 19,                     /* SLASH  */
  YYSYMBOL_PERCENT = 20,                   /* PERCENT  */
  YYSYMBOL_LT = 21,                        /* LT  */
  YYSYMBOL_GT = 22,                        /* GT  */
  YYSYMBOL_LE = 23,                        /* LE  */
  YYSYMBOL_GE = 24,                        /* GE  */
  YYSYMBOL_EQ = 25,                        /* EQ  */
  YYSYMBOL_NE = 26,                        /* NE  */
  YYSYMBOL_AND = 27,                       /* AND  */
  YYSYMBOL_OR = 28,                        /* OR  */
  YYSYMBOL_NOT = 29,                       /* NOT  */
  YYSYMBOL_ASSIGN = 30,                    /* ASSIGN  */
  YYSYMBOL_LPAREN = 31,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 32,                    /* RPAREN  */
  YYSYMBOL_LBRACE = 33,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 34,                    /* RBRACE  */
  YYSYMBOL_LBRACKET = 35,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 36,                  /* RBRACKET  */
  YYSYMBOL_SEMICOLON = 37,                 /* SEMICOLON  */
  YYSYMBOL_COMMA = 38,                     /* COMMA  */
  YYSYMBOL_UNARY_MINUS = 39,               /* UNARY_MINUS  */
  YYSYMBOL_UNARY_PLUS = 40,                /* UNARY_PLUS  */
  YYSYMBOL_YYACCEPT = 41,                  /* $accept  */
  YYSYMBOL_CompUnit = 42,                  /* CompUnit  */
  YYSYMBOL_BType = 43,                     /* BType  */
  YYSYMBOL_GlobalItem = 44,                /* GlobalItem  */
  YYSYMBOL_OptInitVal = 45,                /* OptInitVal  */
  YYSYMBOL_ArrayDims = 46,                 /* ArrayDims  */
  YYSYMBOL_OptMoreVarDefs = 47,            /* OptMoreVarDefs  */
  YYSYMBOL_ParamListOpt = 48,              /* ParamListOpt  */
  YYSYMBOL_ParamList = 49,                 /* ParamList  */
  YYSYMBOL_Param = 50,                     /* Param  */
  YYSYMBOL_ArrayDimsExpr = 51,             /* ArrayDimsExpr  */
  YYSYMBOL_Decl = 52,                      /* Decl  */
  YYSYMBOL_VarDecl = 53,                   /* VarDecl  */
  YYSYMBOL_ConstDecl = 54,                 /* ConstDecl  */
  YYSYMBOL_VarDefList = 55,                /* VarDefList  */
  YYSYMBOL_ConstDefList = 56,              /* ConstDefList  */
  YYSYMBOL_VarDef = 57,                    /* VarDef  */
  YYSYMBOL_ConstDef = 58,                  /* ConstDef  */
  YYSYMBOL_InitVal = 59,                   /* InitVal  */
  YYSYMBOL_InitValList = 60,               /* InitValList  */
  YYSYMBOL_ConstInitVal = 61,              /* ConstInitVal  */
  YYSYMBOL_ConstInitValList = 62,          /* ConstInitValList  */
  YYSYMBOL_Block = 63,                     /* Block  */
  YYSYMBOL_BlockItemList = 64,             /* BlockItemList  */
  YYSYMBOL_BlockItem = 65,                 /* BlockItem  */
  YYSYMBOL_Stmt = 66,                      /* Stmt  */
  YYSYMBOL_LVal = 67,                      /* LVal  */
  YYSYMBOL_ConstExpr = 68,                 /* ConstExpr  */
  YYSYMBOL_Expr = 69,                      /* Expr  */
  YYSYMBOL_LOrExpr = 70,                   /* LOrExpr  */
  YYSYMBOL_LAndExpr = 71,                  /* LAndExpr  */
  YYSYMBOL_EqExpr = 72,                    /* EqExpr  */
  YYSYMBOL_RelExpr = 73,                   /* RelExpr  */
  YYSYMBOL_AddExpr = 74,                   /* AddExpr  */
  YYSYMBOL_MulExpr = 75,                   /* MulExpr  */
  YYSYMBOL_UnaryExpr = 76,                 /* UnaryExpr  */
  YYSYMBOL_UnaryOp = 77,                   /* UnaryOp  */
  YYSYMBOL_PrimaryExpr = 78,               /* PrimaryExpr  */
  YYSYMBOL_ArgListOpt = 79,                /* ArgListOpt  */
  YYSYMBOL_ArgList = 80                    /* ArgList  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_uint8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  11
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   260

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  41
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  40
/* YYNRULES -- Number of rules.  */
#define YYNRULES  101
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  188

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   295


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    85,    85,    90,    97,   100,   107,   118,   130,   162,
     168,   169,   173,   174,   185,   186,   190,   191,   195,   199,
     206,   211,   218,   234,   238,   245,   246,   250,   261,   274,
     278,   285,   289,   296,   307,   318,   337,   357,   380,   385,
     389,   395,   400,   407,   412,   416,   422,   427,   434,   446,
     447,   458,   459,   463,   466,   469,   472,   475,   478,   481,
     484,   487,   490,   493,   499,   503,   507,   513,   517,   521,
     522,   528,   529,   535,   536,   539,   545,   546,   549,   552,
     555,   561,   562,   565,   571,   572,   575,   578,   584,   585,
     595,   596,   597,   601,   604,   607,   610,   613,   626,   627,
     631,   635
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "NUMBER",
  "FLOAT_NUMBER", "IDENTIFIER", "INT", "VOID", "FLOAT", "IF", "ELSE",
  "WHILE", "BREAK", "CONTINUE", "RETURN", "CONST", "PLUS", "MINUS", "STAR",
  "SLASH", "PERCENT", "LT", "GT", "LE", "GE", "EQ", "NE", "AND", "OR",
  "NOT", "ASSIGN", "LPAREN", "RPAREN", "LBRACE", "RBRACE", "LBRACKET",
  "RBRACKET", "SEMICOLON", "COMMA", "UNARY_MINUS", "UNARY_PLUS", "$accept",
  "CompUnit", "BType", "GlobalItem", "OptInitVal", "ArrayDims",
  "OptMoreVarDefs", "ParamListOpt", "ParamList", "Param", "ArrayDimsExpr",
  "Decl", "VarDecl", "ConstDecl", "VarDefList", "ConstDefList", "VarDef",
  "ConstDef", "InitVal", "InitValList", "ConstInitVal", "ConstInitValList",
  "Block", "BlockItemList", "BlockItem", "Stmt", "LVal", "ConstExpr",
  "Expr", "LOrExpr", "LAndExpr", "EqExpr", "RelExpr", "AddExpr", "MulExpr",
  "UnaryExpr", "UnaryOp", "PrimaryExpr", "ArgListOpt", "ArgList", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-154)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      14,  -154,     7,  -154,    48,     9,    23,  -154,  -154,     0,
      57,  -154,  -154,    22,    48,  -154,    78,  -154,    48,   -12,
      59,    36,    42,  -154,    40,  -154,    57,    46,   192,   229,
      53,    61,    65,    48,   195,  -154,    65,  -154,  -154,    63,
    -154,  -154,  -154,   213,   154,  -154,    76,  -154,    91,    99,
      92,    85,   106,    83,  -154,   229,  -154,    95,   106,     8,
      96,    98,  -154,  -154,  -154,   173,  -154,  -154,  -154,   229,
     114,   103,  -154,  -154,   -24,   229,   229,   229,   229,   229,
     229,   229,   229,   229,   229,   229,   229,   229,   229,  -154,
    -154,  -154,   131,   104,  -154,  -154,   111,    32,  -154,  -154,
      -4,  -154,   119,   116,   123,  -154,  -154,   192,   120,    99,
      92,    85,    85,   106,   106,   106,   106,    83,    83,  -154,
    -154,  -154,    44,   142,     8,   229,   127,   132,   133,   129,
     135,    68,  -154,  -154,     8,  -154,  -154,  -154,  -154,  -154,
    -154,    47,   137,  -154,   195,  -154,   229,   163,  -154,  -154,
     192,   143,  -154,   144,   229,   229,   229,  -154,  -154,  -154,
     145,    87,   229,  -154,  -154,  -154,  -154,  -154,   174,  -154,
     148,   149,   159,  -154,  -154,   155,  -154,  -154,   136,   136,
    -154,    60,   176,  -154,   192,   136,  -154,  -154
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     4,     0,     5,     0,     0,     0,     2,     9,     0,
       0,     1,     3,    12,    16,    12,     0,    31,    16,    10,
       0,     0,    17,    18,     0,    28,     0,     0,     0,     0,
      14,    20,     0,     0,     0,    32,     0,    94,    95,    64,
      90,    91,    92,     0,     0,    11,    93,    38,    68,    69,
      71,    73,    76,    81,    84,     0,    88,     0,    67,     0,
       0,     0,    49,     6,    19,     0,    37,    43,     7,    98,
       0,     0,    39,    41,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    89,
      13,    12,     0,    15,    29,     8,    21,     0,    44,    46,
       0,   100,     0,    99,     0,    96,    40,     0,     0,    70,
      72,    74,    75,    77,    78,    79,    80,    82,    83,    85,
      86,    87,    33,     0,     0,     0,    22,     0,     0,     0,
       0,     0,    48,    54,     0,    51,    25,    26,    53,    50,
      52,    93,     0,    45,     0,    97,     0,     0,    42,    66,
       0,     0,    30,     0,     0,     0,     0,    60,    61,    63,
       0,     0,     0,    55,    47,   101,    65,    35,     0,    23,
       0,     0,     0,    62,    27,     0,    12,    24,     0,     0,
      56,    34,    57,    59,     0,     0,    36,    58
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -154,  -154,     3,   188,  -154,   -15,  -154,   183,  -154,   161,
    -154,  -154,  -154,   108,    69,  -154,    86,   187,   -40,  -154,
     -63,  -154,    56,  -154,  -154,  -153,   -92,   185,   -42,  -154,
     139,   150,    49,   -23,    45,   -36,  -154,  -154,  -154,  -154
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     5,    20,     7,    30,    19,    60,    21,    22,    23,
     126,   135,   136,     8,    93,    16,    94,    17,    45,    74,
      66,   100,   138,    97,   139,   140,    46,    67,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,   102,   103
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
      24,    71,    99,     6,    73,   141,    58,    10,     6,    11,
     106,    58,     9,    91,   107,     1,     2,     3,    28,    89,
       1,     2,     3,    29,     4,   182,   183,   101,    13,     4,
     143,    14,   187,   108,   144,    37,    38,    39,     1,    92,
       3,   127,    58,   128,   129,   130,   131,     4,    40,    41,
     119,   120,   121,    18,     1,   142,     3,   113,   114,   115,
     116,    42,    15,    43,    31,    62,   132,   148,    32,   133,
      34,    37,    38,    39,   150,    29,   122,   162,    36,    29,
      33,   164,    75,   153,    40,    41,   141,   141,    63,   160,
     184,    59,    68,   141,    69,    29,    61,    42,    62,    43,
     134,    86,    87,    88,   165,   159,    80,    81,    82,    83,
     167,    75,   170,   171,   172,    25,    26,    78,    79,    76,
     175,    58,    84,    85,   174,   124,    77,   111,   112,   117,
     118,    90,   104,    95,    96,   105,   142,   142,   123,    37,
      38,    39,   124,   142,   186,   127,   125,   128,   129,   130,
     131,   145,    40,    41,   146,   147,   149,    37,    38,    39,
     151,   181,   154,   155,   156,    42,   157,    43,   166,    62,
      40,    41,   158,   133,   163,   168,    37,    38,    39,   176,
     169,   178,   173,    42,   177,    43,   185,    44,    72,    40,
      41,   179,   180,    12,    64,    37,    38,    39,    37,    38,
      39,    27,    42,   161,    43,   137,    65,    98,    40,    41,
     152,    40,    41,    35,    57,   109,    37,    38,    39,     0,
      70,    42,     0,    43,    42,    44,    43,   110,    65,    40,
      41,     0,    37,    38,    39,     0,     0,     0,     0,     0,
       0,     0,    42,     0,    43,    40,    41,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    42,     0,
      43
};

static const yytype_int16 yycheck[] =
{
      15,    43,    65,     0,    44,    97,    29,     4,     5,     0,
      34,    34,     5,     5,    38,     6,     7,     8,    30,    55,
       6,     7,     8,    35,    15,   178,   179,    69,     5,    15,
      34,    31,   185,    75,    38,     3,     4,     5,     6,    31,
       8,     9,    65,    11,    12,    13,    14,    15,    16,    17,
      86,    87,    88,    31,     6,    97,     8,    80,    81,    82,
      83,    29,     5,    31,     5,    33,    34,   107,    32,    37,
      30,     3,     4,     5,    30,    35,    91,    30,    32,    35,
      38,   144,    35,   125,    16,    17,   178,   179,    32,   131,
      30,    38,    36,   185,    31,    35,    35,    29,    33,    31,
      97,    18,    19,    20,   146,    37,    21,    22,    23,    24,
     150,    35,   154,   155,   156,    37,    38,    25,    26,    28,
     162,   144,    16,    17,    37,    38,    27,    78,    79,    84,
      85,    36,    18,    37,    36,    32,   178,   179,     7,     3,
       4,     5,    38,   185,   184,     9,    35,    11,    12,    13,
      14,    32,    16,    17,    38,    32,    36,     3,     4,     5,
      18,   176,    35,    31,    31,    29,    37,    31,     5,    33,
      16,    17,    37,    37,    37,    32,     3,     4,     5,     5,
      36,    32,    37,    29,    36,    31,    10,    33,    34,    16,
      17,    32,    37,     5,    33,     3,     4,     5,     3,     4,
       5,    18,    29,   134,    31,    97,    33,    34,    16,    17,
     124,    16,    17,    26,    29,    76,     3,     4,     5,    -1,
       7,    29,    -1,    31,    29,    33,    31,    77,    33,    16,
      17,    -1,     3,     4,     5,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    29,    -1,    31,    16,    17,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    29,    -1,
      31
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     6,     7,     8,    15,    42,    43,    44,    54,     5,
      43,     0,    44,     5,    31,     5,    56,    58,    31,    46,
      43,    48,    49,    50,    46,    37,    38,    48,    30,    35,
      45,     5,    32,    38,    30,    58,    32,     3,     4,     5,
      16,    17,    29,    31,    33,    59,    67,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    68,    74,    38,
      47,    35,    33,    63,    50,    33,    61,    68,    63,    31,
       7,    69,    34,    59,    60,    35,    28,    27,    25,    26,
      21,    22,    23,    24,    16,    17,    18,    19,    20,    76,
      36,     5,    31,    55,    57,    37,    36,    64,    34,    61,
      62,    69,    79,    80,    18,    32,    34,    38,    69,    71,
      72,    73,    73,    74,    74,    74,    74,    75,    75,    76,
      76,    76,    46,     7,    38,    35,    51,     9,    11,    12,
      13,    14,    34,    37,    43,    52,    53,    54,    63,    65,
      66,    67,    69,    34,    38,    32,    38,    32,    59,    36,
      30,    18,    57,    69,    35,    31,    31,    37,    37,    37,
      69,    55,    30,    37,    61,    69,     5,    59,    32,    36,
      69,    69,    69,    37,    37,    69,     5,    36,    32,    32,
      37,    46,    66,    66,    30,    10,    59,    66
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    41,    42,    42,    43,    43,    44,    44,    44,    44,
      45,    45,    46,    46,    47,    47,    48,    48,    49,    49,
      50,    50,    50,    51,    51,    52,    52,    53,    54,    55,
      55,    56,    56,    57,    57,    57,    57,    58,    59,    59,
      59,    60,    60,    61,    61,    61,    62,    62,    63,    64,
      64,    65,    65,    66,    66,    66,    66,    66,    66,    66,
      66,    66,    66,    66,    67,    67,    67,    68,    69,    70,
      70,    71,    71,    72,    72,    72,    73,    73,    73,    73,
      73,    74,    74,    74,    75,    75,    75,    75,    76,    76,
      77,    77,    77,    78,    78,    78,    78,    78,    79,    79,
      80,    80
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     6,     6,     6,     1,
       0,     2,     0,     4,     0,     2,     0,     1,     1,     3,
       2,     4,     5,     3,     4,     1,     1,     3,     4,     1,
       3,     1,     3,     2,     6,     4,     8,     4,     1,     2,
       3,     1,     3,     1,     2,     3,     1,     3,     3,     0,
       2,     1,     1,     1,     1,     2,     4,     5,     7,     5,
       2,     2,     3,     2,     1,     5,     4,     1,     1,     1,
       3,     1,     3,     1,     3,     3,     1,     3,     3,     3,
       3,     1,     3,     3,     1,     3,     3,     3,     1,     2,
       1,     1,     1,     1,     1,     1,     3,     4,     0,     1,
       1,     3
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* CompUnit: GlobalItem  */
#line 85 "grammar/parser.y"
                 {
        (yyval.compUnit) = new CompUnit();
        (yyval.compUnit)->items.emplace_back((yyvsp[0].astNode));
        root = (yyval.compUnit);
    }
#line 1302 "generated/parser.tab.c"
    break;

  case 3: /* CompUnit: CompUnit GlobalItem  */
#line 90 "grammar/parser.y"
                          {
        (yyval.compUnit) = (yyvsp[-1].compUnit);
        (yyval.compUnit)->items.emplace_back((yyvsp[0].astNode));
    }
#line 1311 "generated/parser.tab.c"
    break;

  case 4: /* BType: INT  */
#line 97 "grammar/parser.y"
          {
        (yyval.type) = new Type(Type::Int());
    }
#line 1319 "generated/parser.tab.c"
    break;

  case 5: /* BType: FLOAT  */
#line 100 "grammar/parser.y"
            {
        (yyval.type) = new Type(Type::Float());
    }
#line 1327 "generated/parser.tab.c"
    break;

  case 6: /* GlobalItem: VOID IDENTIFIER LPAREN ParamListOpt RPAREN Block  */
#line 107 "grammar/parser.y"
                                                       {
        FuncDef* func = new FuncDef(Type::Void(), *(yyvsp[-4].str), (yyvsp[0].block));
        if ((yyvsp[-2].paramList)) {
            for (auto* param : *(yyvsp[-2].paramList)) {
                func->params.emplace_back(param);
            }
            delete (yyvsp[-2].paramList);
        }
        delete (yyvsp[-4].str);
        (yyval.astNode) = static_cast<ASTNode*>(func);
    }
#line 1343 "generated/parser.tab.c"
    break;

  case 7: /* GlobalItem: BType IDENTIFIER LPAREN ParamListOpt RPAREN Block  */
#line 118 "grammar/parser.y"
                                                        {
        FuncDef* func = new FuncDef(*(yyvsp[-5].type), *(yyvsp[-4].str), (yyvsp[0].block));
        if ((yyvsp[-2].paramList)) {
            for (auto* param : *(yyvsp[-2].paramList)) {
                func->params.emplace_back(param);
            }
            delete (yyvsp[-2].paramList);
        }
        delete (yyvsp[-5].type);
        delete (yyvsp[-4].str);
        (yyval.astNode) = static_cast<ASTNode*>(func);
    }
#line 1360 "generated/parser.tab.c"
    break;

  case 8: /* GlobalItem: BType IDENTIFIER ArrayDims OptInitVal OptMoreVarDefs SEMICOLON  */
#line 130 "grammar/parser.y"
                                                                     {
        VarDeclStmt* decl = new VarDeclStmt(*(yyvsp[-5].type));
        VarDef* first = new VarDef(*(yyvsp[-4].str));
        first->isArray = ((yyvsp[-3].dimList) != nullptr && !(yyvsp[-3].dimList)->empty());
        if ((yyvsp[-3].dimList)) {
            for (auto* dim : *(yyvsp[-3].dimList)) {
                first->arrayDimExprs.emplace_back(dim);
            }
            delete (yyvsp[-3].dimList);
        }
        if ((yyvsp[-2].initList)) {
            first->hasInit = true;
            first->hasInitList = !((yyvsp[-2].initList)->isScalar);
            if ((yyvsp[-2].initList)->isScalar && (yyvsp[-2].initList)->elements.size() == 1) {
                // 标量初始化：提取表达式
                first->initExpr.reset(dynamic_cast<Expr*>((yyvsp[-2].initList)->elements[0].release()));
            } else {
                // 数组初始化列表
                first->initList.reset((yyvsp[-2].initList));
            }
        }
        decl->defs.emplace_back(first);
        if ((yyvsp[-1].varDefList)) {
            for (auto* def : *(yyvsp[-1].varDefList)) {
                decl->defs.emplace_back(def);
            }
            delete (yyvsp[-1].varDefList);
        }
        delete (yyvsp[-5].type);
        delete (yyvsp[-4].str);
        (yyval.astNode) = static_cast<ASTNode*>(decl);
    }
#line 1397 "generated/parser.tab.c"
    break;

  case 9: /* GlobalItem: ConstDecl  */
#line 162 "grammar/parser.y"
                {
        (yyval.astNode) = static_cast<ASTNode*>((yyvsp[0].declStmt));
    }
#line 1405 "generated/parser.tab.c"
    break;

  case 10: /* OptInitVal: %empty  */
#line 168 "grammar/parser.y"
                  { (yyval.initList) = nullptr; }
#line 1411 "generated/parser.tab.c"
    break;

  case 11: /* OptInitVal: ASSIGN InitVal  */
#line 169 "grammar/parser.y"
                     { (yyval.initList) = (yyvsp[0].initList); }
#line 1417 "generated/parser.tab.c"
    break;

  case 12: /* ArrayDims: %empty  */
#line 173 "grammar/parser.y"
                  { (yyval.dimList) = nullptr; }
#line 1423 "generated/parser.tab.c"
    break;

  case 13: /* ArrayDims: ArrayDims LBRACKET ConstExpr RBRACKET  */
#line 174 "grammar/parser.y"
                                            {
        if ((yyvsp[-3].dimList) == nullptr) {
            (yyval.dimList) = new std::vector<Expr*>();
        } else {
            (yyval.dimList) = (yyvsp[-3].dimList);
        }
        (yyval.dimList)->push_back((yyvsp[-1].expr));
    }
#line 1436 "generated/parser.tab.c"
    break;

  case 14: /* OptMoreVarDefs: %empty  */
#line 185 "grammar/parser.y"
                  { (yyval.varDefList) = nullptr; }
#line 1442 "generated/parser.tab.c"
    break;

  case 15: /* OptMoreVarDefs: COMMA VarDefList  */
#line 186 "grammar/parser.y"
                       { (yyval.varDefList) = (yyvsp[0].varDefList); }
#line 1448 "generated/parser.tab.c"
    break;

  case 16: /* ParamListOpt: %empty  */
#line 190 "grammar/parser.y"
                  { (yyval.paramList) = nullptr; }
#line 1454 "generated/parser.tab.c"
    break;

  case 17: /* ParamListOpt: ParamList  */
#line 191 "grammar/parser.y"
                { (yyval.paramList) = (yyvsp[0].paramList); }
#line 1460 "generated/parser.tab.c"
    break;

  case 18: /* ParamList: Param  */
#line 195 "grammar/parser.y"
            {
        (yyval.paramList) = new std::vector<Param*>();
        (yyval.paramList)->push_back((yyvsp[0].param));
    }
#line 1469 "generated/parser.tab.c"
    break;

  case 19: /* ParamList: ParamList COMMA Param  */
#line 199 "grammar/parser.y"
                            {
        (yyval.paramList) = (yyvsp[-2].paramList);
        (yyval.paramList)->push_back((yyvsp[0].param));
    }
#line 1478 "generated/parser.tab.c"
    break;

  case 20: /* Param: BType IDENTIFIER  */
#line 206 "grammar/parser.y"
                       {
        (yyval.param) = new Param(*(yyvsp[-1].type), *(yyvsp[0].str));
        delete (yyvsp[-1].type);
        delete (yyvsp[0].str);
    }
#line 1488 "generated/parser.tab.c"
    break;

  case 21: /* Param: BType IDENTIFIER LBRACKET RBRACKET  */
#line 211 "grammar/parser.y"
                                         {
        (yyval.param) = new Param(*(yyvsp[-3].type), *(yyvsp[-2].str));
        (yyval.param)->type.isArray = true;
        (yyval.param)->type.firstDimUnsized = true;
        delete (yyvsp[-3].type);
        delete (yyvsp[-2].str);
    }
#line 1500 "generated/parser.tab.c"
    break;

  case 22: /* Param: BType IDENTIFIER LBRACKET RBRACKET ArrayDimsExpr  */
#line 218 "grammar/parser.y"
                                                       {
        (yyval.param) = new Param(*(yyvsp[-4].type), *(yyvsp[-3].str));
        (yyval.param)->type.isArray = true;
        (yyval.param)->type.firstDimUnsized = true;
        if ((yyvsp[0].exprList)) {
            for (auto* dim : *(yyvsp[0].exprList)) {
                (yyval.param)->arrayDimExprs.emplace_back(dim);
            }
            delete (yyvsp[0].exprList);
        }
        delete (yyvsp[-4].type);
        delete (yyvsp[-3].str);
    }
#line 1518 "generated/parser.tab.c"
    break;

  case 23: /* ArrayDimsExpr: LBRACKET Expr RBRACKET  */
#line 234 "grammar/parser.y"
                             {
        (yyval.exprList) = new std::vector<Expr*>();
        (yyval.exprList)->push_back((yyvsp[-1].expr));
    }
#line 1527 "generated/parser.tab.c"
    break;

  case 24: /* ArrayDimsExpr: ArrayDimsExpr LBRACKET Expr RBRACKET  */
#line 238 "grammar/parser.y"
                                           {
        (yyval.exprList) = (yyvsp[-3].exprList);
        (yyval.exprList)->push_back((yyvsp[-1].expr));
    }
#line 1536 "generated/parser.tab.c"
    break;

  case 25: /* Decl: VarDecl  */
#line 245 "grammar/parser.y"
              { (yyval.stmt) = (yyvsp[0].declStmt); }
#line 1542 "generated/parser.tab.c"
    break;

  case 26: /* Decl: ConstDecl  */
#line 246 "grammar/parser.y"
                { (yyval.stmt) = (yyvsp[0].declStmt); }
#line 1548 "generated/parser.tab.c"
    break;

  case 27: /* VarDecl: BType VarDefList SEMICOLON  */
#line 250 "grammar/parser.y"
                                 {
        (yyval.declStmt) = new VarDeclStmt(*(yyvsp[-2].type));
        for (auto* def : *(yyvsp[-1].varDefList)) {
            (yyval.declStmt)->defs.emplace_back(def);
        }
        delete (yyvsp[-2].type);
        delete (yyvsp[-1].varDefList);
    }
#line 1561 "generated/parser.tab.c"
    break;

  case 28: /* ConstDecl: CONST BType ConstDefList SEMICOLON  */
#line 261 "grammar/parser.y"
                                         {
        Type constType = *(yyvsp[-2].type);
        constType.isConst = true;
        (yyval.declStmt) = new VarDeclStmt(constType);
        for (auto* def : *(yyvsp[-1].varDefList)) {
            (yyval.declStmt)->defs.emplace_back(def);
        }
        delete (yyvsp[-2].type);
        delete (yyvsp[-1].varDefList);
    }
#line 1576 "generated/parser.tab.c"
    break;

  case 29: /* VarDefList: VarDef  */
#line 274 "grammar/parser.y"
             {
        (yyval.varDefList) = new std::vector<VarDef*>();
        (yyval.varDefList)->push_back((yyvsp[0].varDef));
    }
#line 1585 "generated/parser.tab.c"
    break;

  case 30: /* VarDefList: VarDefList COMMA VarDef  */
#line 278 "grammar/parser.y"
                              {
        (yyval.varDefList) = (yyvsp[-2].varDefList);
        (yyval.varDefList)->push_back((yyvsp[0].varDef));
    }
#line 1594 "generated/parser.tab.c"
    break;

  case 31: /* ConstDefList: ConstDef  */
#line 285 "grammar/parser.y"
               {
        (yyval.varDefList) = new std::vector<VarDef*>();
        (yyval.varDefList)->push_back((yyvsp[0].varDef));
    }
#line 1603 "generated/parser.tab.c"
    break;

  case 32: /* ConstDefList: ConstDefList COMMA ConstDef  */
#line 289 "grammar/parser.y"
                                  {
        (yyval.varDefList) = (yyvsp[-2].varDefList);
        (yyval.varDefList)->push_back((yyvsp[0].varDef));
    }
#line 1612 "generated/parser.tab.c"
    break;

  case 33: /* VarDef: IDENTIFIER ArrayDims  */
#line 296 "grammar/parser.y"
                           {
        (yyval.varDef) = new VarDef(*(yyvsp[-1].str));
        (yyval.varDef)->isArray = ((yyvsp[0].dimList) != nullptr && !(yyvsp[0].dimList)->empty());
        if ((yyvsp[0].dimList)) {
            for (auto* dim : *(yyvsp[0].dimList)) {
                (yyval.varDef)->arrayDimExprs.emplace_back(dim);
            }
            delete (yyvsp[0].dimList);
        }
        delete (yyvsp[-1].str);
    }
#line 1628 "generated/parser.tab.c"
    break;

  case 34: /* VarDef: LPAREN VOID STAR RPAREN IDENTIFIER ArrayDims  */
#line 307 "grammar/parser.y"
                                                   {
        (yyval.varDef) = new VarDef(*(yyvsp[-1].str));
        (yyval.varDef)->isArray = ((yyvsp[0].dimList) != nullptr && !(yyvsp[0].dimList)->empty());
        if ((yyvsp[0].dimList)) {
            for (auto* dim : *(yyvsp[0].dimList)) {
                (yyval.varDef)->arrayDimExprs.emplace_back(dim);
            }
            delete (yyvsp[0].dimList);
        }
        delete (yyvsp[-1].str);
    }
#line 1644 "generated/parser.tab.c"
    break;

  case 35: /* VarDef: IDENTIFIER ArrayDims ASSIGN InitVal  */
#line 318 "grammar/parser.y"
                                          {
        if ((yyvsp[0].initList)->isScalar && (yyvsp[0].initList)->elements.size() == 1) {
            // 标量初始化：提取表达式
            (yyval.varDef) = new VarDef(*(yyvsp[-3].str), dynamic_cast<Expr*>((yyvsp[0].initList)->elements[0].release()));
        } else {
            // 数组初始化列表
            (yyval.varDef) = new VarDef(*(yyvsp[-3].str), (yyvsp[0].initList));
        }
        (yyval.varDef)->isArray = ((yyvsp[-2].dimList) != nullptr && !(yyvsp[-2].dimList)->empty());
        (yyval.varDef)->hasInit = true;
        (yyval.varDef)->hasInitList = !((yyvsp[0].initList)->isScalar);
        if ((yyvsp[-2].dimList)) {
            for (auto* dim : *(yyvsp[-2].dimList)) {
                (yyval.varDef)->arrayDimExprs.emplace_back(dim);
            }
            delete (yyvsp[-2].dimList);
        }
        delete (yyvsp[-3].str);
    }
#line 1668 "generated/parser.tab.c"
    break;

  case 36: /* VarDef: LPAREN VOID STAR RPAREN IDENTIFIER ArrayDims ASSIGN InitVal  */
#line 337 "grammar/parser.y"
                                                                  {
        if ((yyvsp[0].initList)->isScalar && (yyvsp[0].initList)->elements.size() == 1) {
            (yyval.varDef) = new VarDef(*(yyvsp[-3].str), dynamic_cast<Expr*>((yyvsp[0].initList)->elements[0].release()));
        } else {
            (yyval.varDef) = new VarDef(*(yyvsp[-3].str), (yyvsp[0].initList));
        }
        (yyval.varDef)->isArray = ((yyvsp[-2].dimList) != nullptr && !(yyvsp[-2].dimList)->empty());
        (yyval.varDef)->hasInit = true;
        (yyval.varDef)->hasInitList = !((yyvsp[0].initList)->isScalar);
        if ((yyvsp[-2].dimList)) {
            for (auto* dim : *(yyvsp[-2].dimList)) {
                (yyval.varDef)->arrayDimExprs.emplace_back(dim);
            }
            delete (yyvsp[-2].dimList);
        }
        delete (yyvsp[-3].str);
    }
#line 1690 "generated/parser.tab.c"
    break;

  case 37: /* ConstDef: IDENTIFIER ArrayDims ASSIGN ConstInitVal  */
#line 357 "grammar/parser.y"
                                               {
        if ((yyvsp[0].initList)->isScalar && (yyvsp[0].initList)->elements.size() == 1) {
            // 标量初始化：提取表达式
            (yyval.varDef) = new VarDef(*(yyvsp[-3].str), dynamic_cast<Expr*>((yyvsp[0].initList)->elements[0].release()));
        } else {
            // 数组初始化列表
            (yyval.varDef) = new VarDef(*(yyvsp[-3].str), (yyvsp[0].initList));
        }
        (yyval.varDef)->isArray = ((yyvsp[-2].dimList) != nullptr && !(yyvsp[-2].dimList)->empty());
        (yyval.varDef)->initIsConst = true;
        (yyval.varDef)->hasInit = true;
        (yyval.varDef)->hasInitList = !((yyvsp[0].initList)->isScalar);
        if ((yyvsp[-2].dimList)) {
            for (auto* dim : *(yyvsp[-2].dimList)) {
                (yyval.varDef)->arrayDimExprs.emplace_back(dim);
            }
            delete (yyvsp[-2].dimList);
        }
        delete (yyvsp[-3].str);
    }
#line 1715 "generated/parser.tab.c"
    break;

  case 38: /* InitVal: Expr  */
#line 380 "grammar/parser.y"
           {
        (yyval.initList) = new InitList();
        (yyval.initList)->isScalar = true;
        (yyval.initList)->elements.emplace_back((yyvsp[0].expr));
    }
#line 1725 "generated/parser.tab.c"
    break;

  case 39: /* InitVal: LBRACE RBRACE  */
#line 385 "grammar/parser.y"
                    {
        (yyval.initList) = new InitList();
        (yyval.initList)->isScalar = false;
    }
#line 1734 "generated/parser.tab.c"
    break;

  case 40: /* InitVal: LBRACE InitValList RBRACE  */
#line 389 "grammar/parser.y"
                                {
        (yyval.initList) = (yyvsp[-1].initList);
    }
#line 1742 "generated/parser.tab.c"
    break;

  case 41: /* InitValList: InitVal  */
#line 395 "grammar/parser.y"
              {
        (yyval.initList) = new InitList();
        (yyval.initList)->isScalar = false;
        (yyval.initList)->elements.emplace_back((yyvsp[0].initList));
    }
#line 1752 "generated/parser.tab.c"
    break;

  case 42: /* InitValList: InitValList COMMA InitVal  */
#line 400 "grammar/parser.y"
                                {
        (yyval.initList) = (yyvsp[-2].initList);
        (yyval.initList)->elements.emplace_back((yyvsp[0].initList));
    }
#line 1761 "generated/parser.tab.c"
    break;

  case 43: /* ConstInitVal: ConstExpr  */
#line 407 "grammar/parser.y"
                {
        (yyval.initList) = new InitList();
        (yyval.initList)->isScalar = true;
        (yyval.initList)->elements.emplace_back((yyvsp[0].expr));
    }
#line 1771 "generated/parser.tab.c"
    break;

  case 44: /* ConstInitVal: LBRACE RBRACE  */
#line 412 "grammar/parser.y"
                    {
        (yyval.initList) = new InitList();
        (yyval.initList)->isScalar = false;
    }
#line 1780 "generated/parser.tab.c"
    break;

  case 45: /* ConstInitVal: LBRACE ConstInitValList RBRACE  */
#line 416 "grammar/parser.y"
                                     {
        (yyval.initList) = (yyvsp[-1].initList);
    }
#line 1788 "generated/parser.tab.c"
    break;

  case 46: /* ConstInitValList: ConstInitVal  */
#line 422 "grammar/parser.y"
                   {
        (yyval.initList) = new InitList();
        (yyval.initList)->isScalar = false;
        (yyval.initList)->elements.emplace_back((yyvsp[0].initList));
    }
#line 1798 "generated/parser.tab.c"
    break;

  case 47: /* ConstInitValList: ConstInitValList COMMA ConstInitVal  */
#line 427 "grammar/parser.y"
                                          {
        (yyval.initList) = (yyvsp[-2].initList);
        (yyval.initList)->elements.emplace_back((yyvsp[0].initList));
    }
#line 1807 "generated/parser.tab.c"
    break;

  case 48: /* Block: LBRACE BlockItemList RBRACE  */
#line 434 "grammar/parser.y"
                                  {
        (yyval.block) = new Block();
        if ((yyvsp[-1].stmtList)) {
            for (auto* stmt : *(yyvsp[-1].stmtList)) {
                (yyval.block)->stmts.emplace_back(stmt);
            }
            delete (yyvsp[-1].stmtList);
        }
    }
#line 1821 "generated/parser.tab.c"
    break;

  case 49: /* BlockItemList: %empty  */
#line 446 "grammar/parser.y"
                  { (yyval.stmtList) = nullptr; }
#line 1827 "generated/parser.tab.c"
    break;

  case 50: /* BlockItemList: BlockItemList BlockItem  */
#line 447 "grammar/parser.y"
                              {
        if ((yyvsp[-1].stmtList) == nullptr) {
            (yyval.stmtList) = new std::vector<Stmt*>();
        } else {
            (yyval.stmtList) = (yyvsp[-1].stmtList);
        }
        (yyval.stmtList)->push_back((yyvsp[0].stmt));
    }
#line 1840 "generated/parser.tab.c"
    break;

  case 51: /* BlockItem: Decl  */
#line 458 "grammar/parser.y"
           { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1846 "generated/parser.tab.c"
    break;

  case 52: /* BlockItem: Stmt  */
#line 459 "grammar/parser.y"
           { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1852 "generated/parser.tab.c"
    break;

  case 53: /* Stmt: Block  */
#line 463 "grammar/parser.y"
            {
        (yyval.stmt) = (yyvsp[0].block);
    }
#line 1860 "generated/parser.tab.c"
    break;

  case 54: /* Stmt: SEMICOLON  */
#line 466 "grammar/parser.y"
                {
        (yyval.stmt) = new EmptyStmt();
    }
#line 1868 "generated/parser.tab.c"
    break;

  case 55: /* Stmt: Expr SEMICOLON  */
#line 469 "grammar/parser.y"
                     {
        (yyval.stmt) = new ExprStmt((yyvsp[-1].expr));
    }
#line 1876 "generated/parser.tab.c"
    break;

  case 56: /* Stmt: LVal ASSIGN Expr SEMICOLON  */
#line 472 "grammar/parser.y"
                                 {
        (yyval.stmt) = new AssignStmt((yyvsp[-3].expr), (yyvsp[-1].expr));
    }
#line 1884 "generated/parser.tab.c"
    break;

  case 57: /* Stmt: IF LPAREN Expr RPAREN Stmt  */
#line 475 "grammar/parser.y"
                                 {
        (yyval.stmt) = new IfStmt((yyvsp[-2].expr), (yyvsp[0].stmt));
    }
#line 1892 "generated/parser.tab.c"
    break;

  case 58: /* Stmt: IF LPAREN Expr RPAREN Stmt ELSE Stmt  */
#line 478 "grammar/parser.y"
                                           {
        (yyval.stmt) = new IfStmt((yyvsp[-4].expr), (yyvsp[-2].stmt), (yyvsp[0].stmt));
    }
#line 1900 "generated/parser.tab.c"
    break;

  case 59: /* Stmt: WHILE LPAREN Expr RPAREN Stmt  */
#line 481 "grammar/parser.y"
                                    {
        (yyval.stmt) = new WhileStmt((yyvsp[-2].expr), (yyvsp[0].stmt));
    }
#line 1908 "generated/parser.tab.c"
    break;

  case 60: /* Stmt: BREAK SEMICOLON  */
#line 484 "grammar/parser.y"
                      {
        (yyval.stmt) = new BreakStmt();
    }
#line 1916 "generated/parser.tab.c"
    break;

  case 61: /* Stmt: CONTINUE SEMICOLON  */
#line 487 "grammar/parser.y"
                         {
        (yyval.stmt) = new ContinueStmt();
    }
#line 1924 "generated/parser.tab.c"
    break;

  case 62: /* Stmt: RETURN Expr SEMICOLON  */
#line 490 "grammar/parser.y"
                            {
        (yyval.stmt) = new ReturnStmt((yyvsp[-1].expr));
    }
#line 1932 "generated/parser.tab.c"
    break;

  case 63: /* Stmt: RETURN SEMICOLON  */
#line 493 "grammar/parser.y"
                       {
        (yyval.stmt) = new ReturnStmt(nullptr);
    }
#line 1940 "generated/parser.tab.c"
    break;

  case 64: /* LVal: IDENTIFIER  */
#line 499 "grammar/parser.y"
                 {
        (yyval.expr) = new IdentifierExpr(*(yyvsp[0].str));
        delete (yyvsp[0].str);
    }
#line 1949 "generated/parser.tab.c"
    break;

  case 65: /* LVal: LPAREN VOID STAR RPAREN IDENTIFIER  */
#line 503 "grammar/parser.y"
                                         {
        (yyval.expr) = new IdentifierExpr(*(yyvsp[0].str));
        delete (yyvsp[0].str);
    }
#line 1958 "generated/parser.tab.c"
    break;

  case 66: /* LVal: LVal LBRACKET Expr RBRACKET  */
#line 507 "grammar/parser.y"
                                  {
        (yyval.expr) = new ArrayAccessExpr((yyvsp[-3].expr), (yyvsp[-1].expr));
    }
#line 1966 "generated/parser.tab.c"
    break;

  case 67: /* ConstExpr: AddExpr  */
#line 513 "grammar/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 1972 "generated/parser.tab.c"
    break;

  case 68: /* Expr: LOrExpr  */
#line 517 "grammar/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 1978 "generated/parser.tab.c"
    break;

  case 69: /* LOrExpr: LAndExpr  */
#line 521 "grammar/parser.y"
               { (yyval.expr) = (yyvsp[0].expr); }
#line 1984 "generated/parser.tab.c"
    break;

  case 70: /* LOrExpr: LOrExpr OR LAndExpr  */
#line 522 "grammar/parser.y"
                          {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_OR, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 1992 "generated/parser.tab.c"
    break;

  case 71: /* LAndExpr: EqExpr  */
#line 528 "grammar/parser.y"
             { (yyval.expr) = (yyvsp[0].expr); }
#line 1998 "generated/parser.tab.c"
    break;

  case 72: /* LAndExpr: LAndExpr AND EqExpr  */
#line 529 "grammar/parser.y"
                          {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_AND, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2006 "generated/parser.tab.c"
    break;

  case 73: /* EqExpr: RelExpr  */
#line 535 "grammar/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 2012 "generated/parser.tab.c"
    break;

  case 74: /* EqExpr: EqExpr EQ RelExpr  */
#line 536 "grammar/parser.y"
                        {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_EQ, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2020 "generated/parser.tab.c"
    break;

  case 75: /* EqExpr: EqExpr NE RelExpr  */
#line 539 "grammar/parser.y"
                        {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_NE, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2028 "generated/parser.tab.c"
    break;

  case 76: /* RelExpr: AddExpr  */
#line 545 "grammar/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 2034 "generated/parser.tab.c"
    break;

  case 77: /* RelExpr: RelExpr LT AddExpr  */
#line 546 "grammar/parser.y"
                         {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_LT, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2042 "generated/parser.tab.c"
    break;

  case 78: /* RelExpr: RelExpr GT AddExpr  */
#line 549 "grammar/parser.y"
                         {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_GT, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2050 "generated/parser.tab.c"
    break;

  case 79: /* RelExpr: RelExpr LE AddExpr  */
#line 552 "grammar/parser.y"
                         {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_LE, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2058 "generated/parser.tab.c"
    break;

  case 80: /* RelExpr: RelExpr GE AddExpr  */
#line 555 "grammar/parser.y"
                         {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_GE, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2066 "generated/parser.tab.c"
    break;

  case 81: /* AddExpr: MulExpr  */
#line 561 "grammar/parser.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 2072 "generated/parser.tab.c"
    break;

  case 82: /* AddExpr: AddExpr PLUS MulExpr  */
#line 562 "grammar/parser.y"
                           {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_ADD, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2080 "generated/parser.tab.c"
    break;

  case 83: /* AddExpr: AddExpr MINUS MulExpr  */
#line 565 "grammar/parser.y"
                            {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_SUB, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2088 "generated/parser.tab.c"
    break;

  case 84: /* MulExpr: UnaryExpr  */
#line 571 "grammar/parser.y"
                { (yyval.expr) = (yyvsp[0].expr); }
#line 2094 "generated/parser.tab.c"
    break;

  case 85: /* MulExpr: MulExpr STAR UnaryExpr  */
#line 572 "grammar/parser.y"
                             {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_MUL, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2102 "generated/parser.tab.c"
    break;

  case 86: /* MulExpr: MulExpr SLASH UnaryExpr  */
#line 575 "grammar/parser.y"
                              {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_DIV, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2110 "generated/parser.tab.c"
    break;

  case 87: /* MulExpr: MulExpr PERCENT UnaryExpr  */
#line 578 "grammar/parser.y"
                                {
        (yyval.expr) = new BinaryExpr(BinaryExpr::B_MOD, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2118 "generated/parser.tab.c"
    break;

  case 88: /* UnaryExpr: PrimaryExpr  */
#line 584 "grammar/parser.y"
                  { (yyval.expr) = (yyvsp[0].expr); }
#line 2124 "generated/parser.tab.c"
    break;

  case 89: /* UnaryExpr: UnaryOp UnaryExpr  */
#line 585 "grammar/parser.y"
                        {
        UnaryExpr::OpType op;
        if ((yyvsp[-1].num) == 0) op = UnaryExpr::U_PLUS;
        else if ((yyvsp[-1].num) == 1) op = UnaryExpr::U_MINUS;
        else op = UnaryExpr::U_NOT;
        (yyval.expr) = new UnaryExpr(op, (yyvsp[0].expr));
    }
#line 2136 "generated/parser.tab.c"
    break;

  case 90: /* UnaryOp: PLUS  */
#line 595 "grammar/parser.y"
            { (yyval.num) = 0; }
#line 2142 "generated/parser.tab.c"
    break;

  case 91: /* UnaryOp: MINUS  */
#line 596 "grammar/parser.y"
            { (yyval.num) = 1; }
#line 2148 "generated/parser.tab.c"
    break;

  case 92: /* UnaryOp: NOT  */
#line 597 "grammar/parser.y"
            { (yyval.num) = 2; }
#line 2154 "generated/parser.tab.c"
    break;

  case 93: /* PrimaryExpr: LVal  */
#line 601 "grammar/parser.y"
           {
        (yyval.expr) = (yyvsp[0].expr);
    }
#line 2162 "generated/parser.tab.c"
    break;

  case 94: /* PrimaryExpr: NUMBER  */
#line 604 "grammar/parser.y"
             {
        (yyval.expr) = new NumberExpr((yyvsp[0].num));
    }
#line 2170 "generated/parser.tab.c"
    break;

  case 95: /* PrimaryExpr: FLOAT_NUMBER  */
#line 607 "grammar/parser.y"
                   {
        (yyval.expr) = new NumberExpr((yyvsp[0].fnum));
    }
#line 2178 "generated/parser.tab.c"
    break;

  case 96: /* PrimaryExpr: LPAREN Expr RPAREN  */
#line 610 "grammar/parser.y"
                         {
        (yyval.expr) = new ParenExpr((yyvsp[-1].expr));
    }
#line 2186 "generated/parser.tab.c"
    break;

  case 97: /* PrimaryExpr: IDENTIFIER LPAREN ArgListOpt RPAREN  */
#line 613 "grammar/parser.y"
                                          {
        (yyval.expr) = new FunctionCallExpr(*(yyvsp[-3].str));
        if ((yyvsp[-1].exprList)) {
            for (auto* arg : *(yyvsp[-1].exprList)) {
                static_cast<FunctionCallExpr*>((yyval.expr))->args.emplace_back(arg);
            }
            delete (yyvsp[-1].exprList);
        }
        delete (yyvsp[-3].str);
    }
#line 2201 "generated/parser.tab.c"
    break;

  case 98: /* ArgListOpt: %empty  */
#line 626 "grammar/parser.y"
                  { (yyval.exprList) = nullptr; }
#line 2207 "generated/parser.tab.c"
    break;

  case 99: /* ArgListOpt: ArgList  */
#line 627 "grammar/parser.y"
              { (yyval.exprList) = (yyvsp[0].exprList); }
#line 2213 "generated/parser.tab.c"
    break;

  case 100: /* ArgList: Expr  */
#line 631 "grammar/parser.y"
           {
        (yyval.exprList) = new std::vector<Expr*>();
        (yyval.exprList)->push_back((yyvsp[0].expr));
    }
#line 2222 "generated/parser.tab.c"
    break;

  case 101: /* ArgList: ArgList COMMA Expr  */
#line 635 "grammar/parser.y"
                         {
        (yyval.exprList) = (yyvsp[-2].exprList);
        (yyval.exprList)->push_back((yyvsp[0].expr));
    }
#line 2231 "generated/parser.tab.c"
    break;


#line 2235 "generated/parser.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 641 "grammar/parser.y"


void yyerror(const char* s) {
    fprintf(stderr, "Parse error at line %d near '%s': %s\n", yylineno,
            yytext ? yytext : "<eof>", s);
    exit(1);
}
