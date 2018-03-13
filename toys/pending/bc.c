/* bc.c - An implementation of POSIX bc.
 *
 * Copyright 2018 Gavin D. Howard <yzena.tech@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/bc.html

USE_BC(NEWTOY(bc, "cilqsw", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_LOCALE))

config BC
  bool "bc"
  default n
  help
    usage: bc [-cilqsw] [file ...]

    bc is a command-line calculator with a Turing-complete language.

    options:

      -c  print generated code (for debugging)
      -i  force interactive mode
      -l  use predefined math routines:

          s(expr)  =  sine of expr in radians
          c(expr)  =  cosine of expr in radians
          a(expr)  =  arctangent of expr, returning radians
          l(expr)  =  natural log of expr
          e(expr)  =  raises e to the power of expr
          j(n, x)  =  Bessel function of integer order n of x

      -q  don't print version and copyright
      -s  error if any non-POSIX extensions are used
      -w  warn if any non-POSIX extensions are used

*/

#include <assert.h>
#include <stdbool.h>

#define FOR_bc
#include "toys.h"

GLOBALS(
  long bc_code;
  long bc_interactive;
  long bc_std;
  long bc_warn;

  long bc_signal;
)

#define BC_BASE_MAX_DEF (99)
#define BC_DIM_MAX_DEF (2048)
#define BC_SCALE_MAX_DEF (99)
#define BC_STRING_MAX_DEF (1024)

typedef enum BcStatus {

  BC_STATUS_SUCCESS,

  BC_STATUS_MALLOC_FAIL,
  BC_STATUS_IO_ERR,

  BC_STATUS_INVALID_PARAM,

  BC_STATUS_INVALID_OPTION,

  BC_STATUS_NO_LIMIT,
  BC_STATUS_INVALID_LIMIT,

  BC_STATUS_VEC_OUT_OF_BOUNDS,

  BC_STATUS_VECO_OUT_OF_BOUNDS,
  BC_STATUS_VECO_ITEM_EXISTS,

  BC_STATUS_LEX_INVALID_TOKEN,
  BC_STATUS_LEX_NO_STRING_END,
  BC_STATUS_LEX_NO_COMMENT_END,
  BC_STATUS_LEX_EOF,

  BC_STATUS_PARSE_INVALID_TOKEN,
  BC_STATUS_PARSE_INVALID_EXPR,
  BC_STATUS_PARSE_INVALID_PRINT,
  BC_STATUS_PARSE_INVALID_FUNC,
  BC_STATUS_PARSE_INVALID_ASSIGN,
  BC_STATUS_PARSE_NO_AUTO,
  BC_STATUS_PARSE_LIMITS,
  BC_STATUS_PARSE_QUIT,
  BC_STATUS_PARSE_MISMATCH_NUM_FUNCS,
  BC_STATUS_PARSE_DUPLICATE_LOCAL,
  BC_STATUS_PARSE_BUG,

  BC_STATUS_MATH_NEGATIVE,
  BC_STATUS_MATH_NON_INTEGER,
  BC_STATUS_MATH_OVERFLOW,
  BC_STATUS_MATH_DIVIDE_BY_ZERO,
  BC_STATUS_MATH_NEG_SQRT,
  BC_STATUS_MATH_INVALID_STRING,
  BC_STATUS_MATH_INVALID_TRUNCATE,

  BC_STATUS_EXEC_FILE_ERR,
  BC_STATUS_EXEC_MISMATCHED_PARAMS,
  BC_STATUS_EXEC_UNDEFINED_FUNC,
  BC_STATUS_EXEC_UNDEFINED_VAR,
  BC_STATUS_EXEC_UNDEFINED_ARRAY,
  BC_STATUS_EXEC_FILE_NOT_EXECUTABLE,
  BC_STATUS_EXEC_SIGACTION_FAIL,
  BC_STATUS_EXEC_INVALID_SCALE,
  BC_STATUS_EXEC_INVALID_IBASE,
  BC_STATUS_EXEC_INVALID_OBASE,
  BC_STATUS_EXEC_INVALID_STMT,
  BC_STATUS_EXEC_INVALID_EXPR,
  BC_STATUS_EXEC_INVALID_STRING,
  BC_STATUS_EXEC_STRING_LEN,
  BC_STATUS_EXEC_INVALID_NAME,
  BC_STATUS_EXEC_ARRAY_LENGTH,
  BC_STATUS_EXEC_INVALID_READ_EXPR,
  BC_STATUS_EXEC_RECURSIVE_READ,
  BC_STATUS_EXEC_PRINT_ERR,
  BC_STATUS_EXEC_INVALID_CONSTANT,
  BC_STATUS_EXEC_INVALID_LVALUE,
  BC_STATUS_EXEC_INVALID_RETURN,
  BC_STATUS_EXEC_INVALID_LABEL,
  BC_STATUS_EXEC_INVALID_TYPE,
  BC_STATUS_EXEC_INVALID_STACK,
  BC_STATUS_EXEC_HALT,

  BC_STATUS_POSIX_NAME_LEN,
  BC_STATUS_POSIX_SCRIPT_COMMENT,
  BC_STATUS_POSIX_INVALID_KEYWORD,
  BC_STATUS_POSIX_DOT_LAST,
  BC_STATUS_POSIX_RETURN_PARENS,
  BC_STATUS_POSIX_BOOL_OPS,
  BC_STATUS_POSIX_REL_OUTSIDE,
  BC_STATUS_POSIX_MULTIPLE_REL,
  BC_STATUS_POSIX_MISSING_FOR_INIT,
  BC_STATUS_POSIX_MISSING_FOR_COND,
  BC_STATUS_POSIX_MISSING_FOR_UPDATE,
  BC_STATUS_POSIX_FUNC_HEADER_LEFT_BRACE,

} BcStatus;

typedef void (*BcFreeFunc)(void*);
typedef BcStatus (*BcCopyFunc)(void*, void*);

void bc_error(BcStatus status);
void bc_error_file(BcStatus status, const char *file, uint32_t line);

BcStatus bc_posix_error(BcStatus status, const char *file,
                        uint32_t line, const char *msg);

#define BC_VEC_INITIAL_CAP (32)

typedef int (*BcVecCmpFunc)(void*, void*);

typedef struct BcVec {

  uint8_t *array;
  size_t len;
  size_t cap;
  size_t size;

  BcFreeFunc dtor;

} BcVec;

typedef struct BcVecO {

  BcVec vec;
  BcVecCmpFunc cmp;

} BcVecO;

typedef signed char BcDigit;

typedef struct BcNum {

  BcDigit *num;
  size_t rdx;
  size_t len;
  size_t cap;
  bool neg;

} BcNum;

#define BC_NUM_MIN_BASE (2)

#define BC_NUM_MAX_INPUT_BASE (16)

#define BC_NUM_MAX_OUTPUT_BASE (99)

#define BC_NUM_DEF_SIZE (16)

#define BC_NUM_FROM_CHAR(c) ((c) -'0')

#define BC_NUM_TO_CHAR(n) ((n) + '0')

#define BC_NUM_PRINT_WIDTH (69)

#define BC_NUM_ONE(n) ((n)->len == 1 && (n)->rdx == 0 && (n)->num[0] == 1)

typedef BcStatus (*BcNumUnaryFunc)(BcNum*, BcNum*, size_t);
typedef BcStatus (*BcNumBinaryFunc)(BcNum*, BcNum*, BcNum*, size_t);

typedef BcStatus (*BcNumDigitFunc)(unsigned long, size_t, size_t*, FILE*);

BcStatus bc_num_init(BcNum *n, size_t request);

BcStatus bc_num_expand(BcNum *n, size_t request);

void bc_num_free(void *num);

BcStatus bc_num_copy(void *dest, void *src);

BcStatus bc_num_long(BcNum *n, long *result);
BcStatus bc_num_ulong(BcNum *n, unsigned long *result);

BcStatus bc_num_long2num(BcNum *n, long val);
BcStatus bc_num_ulong2num(BcNum *n, unsigned long val);

BcStatus bc_num_truncate(BcNum *n);

BcStatus bc_num_add(BcNum *a, BcNum *b, BcNum *result, size_t scale);
BcStatus bc_num_sub(BcNum *a, BcNum *b, BcNum *result, size_t scale);
BcStatus bc_num_mul(BcNum *a, BcNum *b, BcNum *result, size_t scale);
BcStatus bc_num_div(BcNum *a, BcNum *b, BcNum *result, size_t scale);
BcStatus bc_num_mod(BcNum *a, BcNum *b, BcNum *result, size_t scale);

int bc_num_compare(BcNum *a, BcNum *b);

void bc_num_zero(BcNum *n);
void bc_num_one(BcNum *n);
void bc_num_ten(BcNum *n);

#define BC_PROGRAM_MAX_STMTS (128)

#define BC_PROGRAM_DEF_SIZE (16)

typedef enum BcExprType {

  BC_EXPR_INC_PRE,
  BC_EXPR_DEC_PRE,

  BC_EXPR_INC_POST,
  BC_EXPR_DEC_POST,

  BC_EXPR_NEGATE,

  BC_EXPR_POWER,

  BC_EXPR_MULTIPLY,
  BC_EXPR_DIVIDE,
  BC_EXPR_MODULUS,

  BC_EXPR_PLUS,
  BC_EXPR_MINUS,

  BC_EXPR_REL_EQUAL,
  BC_EXPR_REL_LESS_EQ,
  BC_EXPR_REL_GREATER_EQ,
  BC_EXPR_REL_NOT_EQ,
  BC_EXPR_REL_LESS,
  BC_EXPR_REL_GREATER,

  BC_EXPR_BOOL_NOT,

  BC_EXPR_BOOL_OR,
  BC_EXPR_BOOL_AND,

  BC_EXPR_ASSIGN_POWER,
  BC_EXPR_ASSIGN_MULTIPLY,
  BC_EXPR_ASSIGN_DIVIDE,
  BC_EXPR_ASSIGN_MODULUS,
  BC_EXPR_ASSIGN_PLUS,
  BC_EXPR_ASSIGN_MINUS,
  BC_EXPR_ASSIGN,

  BC_EXPR_NUMBER,
  BC_EXPR_VAR,
  BC_EXPR_ARRAY_ELEM,

  BC_EXPR_FUNC_CALL,

  BC_EXPR_SCALE_FUNC,
  BC_EXPR_SCALE,
  BC_EXPR_IBASE,
  BC_EXPR_OBASE,
  BC_EXPR_LAST,
  BC_EXPR_LENGTH,
  BC_EXPR_READ,
  BC_EXPR_SQRT,

  BC_EXPR_PRINT,

} BcExprType;

typedef struct BcEntry {

  char *name;
  size_t idx;

} BcEntry;

typedef struct BcAuto {

  char *name;
  bool var;

  union {

    BcNum num;
    BcVec array;

  } data;

} BcAuto;

typedef struct BcFunc {

  BcVec code;

  BcVec labels;

  BcVec params;

  BcVec autos;

} BcFunc;

typedef BcNum BcVar;

typedef BcVec BcArray;

typedef enum BcResultType {

  BC_RESULT_INTERMEDIATE,

  BC_RESULT_CONSTANT,

  BC_RESULT_VAR,
  BC_RESULT_ARRAY,

  BC_RESULT_SCALE,
  BC_RESULT_LAST,
  BC_RESULT_IBASE,
  BC_RESULT_OBASE,

  BC_RESULT_ONE,

} BcResultType;

typedef struct BcResult {

  BcResultType type;

  union {

    BcNum num;

    struct {

      char *name;
      size_t idx;

    } id;

  } data;

} BcResult;

typedef struct BcInstPtr {

  size_t func;
  size_t idx;
  size_t len;

} BcInstPtr;

typedef BcStatus (*BcDataInitFunc)(void*);

BcStatus bc_auto_init(void *auto1, char *name, bool var);
void bc_auto_free(void *auto1);

#define BC_INST_CALL ((uint8_t) 'C')
#define BC_INST_RETURN ((uint8_t) 'R')
#define BC_INST_RETURN_ZERO ((uint8_t) '$')

#define BC_INST_READ ((uint8_t) 'r')

#define BC_INST_JUMP ((uint8_t) 'J')
#define BC_INST_JUMP_NOT_ZERO ((uint8_t) 'n')
#define BC_INST_JUMP_ZERO ((uint8_t) 'z')

#define BC_INST_PUSH_VAR ((uint8_t) 'V')
#define BC_INST_PUSH_ARRAY ((uint8_t) 'A')

#define BC_INST_PUSH_LAST ((uint8_t) 'L')
#define BC_INST_PUSH_SCALE ((uint8_t) '.')
#define BC_INST_PUSH_IBASE ((uint8_t) 'I')
#define BC_INST_PUSH_OBASE ((uint8_t) 'O')

#define BC_INST_SCALE_FUNC ((uint8_t) 'a')
#define BC_INST_LENGTH ((uint8_t) 'l')
#define BC_INST_SQRT ((uint8_t) 'q')

#define BC_INST_PUSH_NUM ((uint8_t) 'N')
#define BC_INST_POP ((uint8_t) 'P')
#define BC_INST_INC_DUP ((uint8_t) 'E')
#define BC_INST_DEC_DUP ((uint8_t) 'D')

#define BC_INST_INC ((uint8_t) 'e')
#define BC_INST_DEC ((uint8_t) 'd')

#define BC_INST_HALT ((uint8_t) 'H')

#define BC_INST_PRINT ((uint8_t) 'p')
#define BC_INST_PRINT_EXPR ((uint8_t) 'Q')
#define BC_INST_STR ((uint8_t) 's')
#define BC_INST_PRINT_STR ((uint8_t) 'S')

#define BC_INST_OP_POWER ((uint8_t) '^')
#define BC_INST_OP_MULTIPLY ((uint8_t) '*')
#define BC_INST_OP_DIVIDE ((uint8_t) '/')
#define BC_INST_OP_MODULUS ((uint8_t) '%')
#define BC_INST_OP_PLUS ((uint8_t) '+')
#define BC_INST_OP_MINUS ((uint8_t) '-')

#define BC_INST_OP_REL_EQUAL ((uint8_t) '=')
#define BC_INST_OP_REL_LESS_EQ ((uint8_t) ';')
#define BC_INST_OP_REL_GREATER_EQ ((uint8_t) '?')
#define BC_INST_OP_REL_NOT_EQ ((uint8_t) '~')
#define BC_INST_OP_REL_LESS ((uint8_t) '<')
#define BC_INST_OP_REL_GREATER ((uint8_t) '>')

#define BC_INST_OP_BOOL_NOT ((uint8_t) '!')

#define BC_INST_OP_BOOL_OR ((uint8_t) '|')
#define BC_INST_OP_BOOL_AND ((uint8_t) '&')

#define BC_INST_OP_NEGATE ((uint8_t) '_')

#define BC_INST_OP_ASSIGN_POWER ((uint8_t) '`')
#define BC_INST_OP_ASSIGN_MULTIPLY ((uint8_t) '{')
#define BC_INST_OP_ASSIGN_DIVIDE ((uint8_t) '}')
#define BC_INST_OP_ASSIGN_MODULUS ((uint8_t) '@')
#define BC_INST_OP_ASSIGN_PLUS ((uint8_t) '[')
#define BC_INST_OP_ASSIGN_MINUS ((uint8_t) ']')
#define BC_INST_OP_ASSIGN ((uint8_t) ',')

typedef int (*BcIoGetc)(void*);

#define bc_io_gets(buf, n) bc_io_fgets((buf), (n), stdin)
#define bc_io_getline(p, n) bc_io_fgetline((p), (n), stdin)

#define BC_LEX_GEN_ENUM(ENUM) ENUM,
#define BC_LEX_GEN_STR(STRING) #STRING,

// BC_LEX_OP_NEGATE is not used in lexing;
// it is only for parsing.
#define BC_LEX_TOKEN_FOREACH(TOKEN) \
  TOKEN(BC_LEX_OP_INC)  \
  TOKEN(BC_LEX_OP_DEC)  \
                        \
  TOKEN(BC_LEX_OP_NEGATE)  \
                           \
  TOKEN(BC_LEX_OP_POWER)  \
                          \
  TOKEN(BC_LEX_OP_MULTIPLY)  \
  TOKEN(BC_LEX_OP_DIVIDE)    \
  TOKEN(BC_LEX_OP_MODULUS)   \
                             \
  TOKEN(BC_LEX_OP_PLUS)   \
  TOKEN(BC_LEX_OP_MINUS)  \
                          \
  TOKEN(BC_LEX_OP_REL_EQUAL)       \
  TOKEN(BC_LEX_OP_REL_LESS_EQ)     \
  TOKEN(BC_LEX_OP_REL_GREATER_EQ)  \
  TOKEN(BC_LEX_OP_REL_NOT_EQ)      \
  TOKEN(BC_LEX_OP_REL_LESS)        \
  TOKEN(BC_LEX_OP_REL_GREATER)     \
                                   \
  TOKEN(BC_LEX_OP_BOOL_NOT)  \
                             \
  TOKEN(BC_LEX_OP_BOOL_OR)   \
  TOKEN(BC_LEX_OP_BOOL_AND)  \
                             \
  TOKEN(BC_LEX_OP_ASSIGN_POWER)     \
  TOKEN(BC_LEX_OP_ASSIGN_MULTIPLY)  \
  TOKEN(BC_LEX_OP_ASSIGN_DIVIDE)    \
  TOKEN(BC_LEX_OP_ASSIGN_MODULUS)   \
  TOKEN(BC_LEX_OP_ASSIGN_PLUS)      \
  TOKEN(BC_LEX_OP_ASSIGN_MINUS)     \
  TOKEN(BC_LEX_OP_ASSIGN)           \
                                    \
  TOKEN(BC_LEX_NEWLINE)  \
                         \
  TOKEN(BC_LEX_WHITESPACE)  \
                            \
  TOKEN(BC_LEX_LEFT_PAREN)   \
  TOKEN(BC_LEX_RIGHT_PAREN)  \
                             \
  TOKEN(BC_LEX_LEFT_BRACKET)   \
  TOKEN(BC_LEX_RIGHT_BRACKET)  \
                               \
  TOKEN(BC_LEX_LEFT_BRACE)   \
  TOKEN(BC_LEX_RIGHT_BRACE)  \
                             \
  TOKEN(BC_LEX_COMMA)      \
  TOKEN(BC_LEX_SEMICOLON)  \
                           \
  TOKEN(BC_LEX_STRING)  \
  TOKEN(BC_LEX_NAME)    \
  TOKEN(BC_LEX_NUMBER)  \
                        \
  TOKEN(BC_LEX_KEY_AUTO)      \
  TOKEN(BC_LEX_KEY_BREAK)     \
  TOKEN(BC_LEX_KEY_CONTINUE)  \
  TOKEN(BC_LEX_KEY_DEFINE)    \
  TOKEN(BC_LEX_KEY_ELSE)      \
  TOKEN(BC_LEX_KEY_FOR)       \
  TOKEN(BC_LEX_KEY_HALT)      \
  TOKEN(BC_LEX_KEY_IBASE)     \
  TOKEN(BC_LEX_KEY_IF)        \
  TOKEN(BC_LEX_KEY_LAST)      \
  TOKEN(BC_LEX_KEY_LENGTH)    \
  TOKEN(BC_LEX_KEY_LIMITS)    \
  TOKEN(BC_LEX_KEY_OBASE)     \
  TOKEN(BC_LEX_KEY_PRINT)     \
  TOKEN(BC_LEX_KEY_QUIT)      \
  TOKEN(BC_LEX_KEY_READ)      \
  TOKEN(BC_LEX_KEY_RETURN)    \
  TOKEN(BC_LEX_KEY_SCALE)     \
  TOKEN(BC_LEX_KEY_SQRT)      \
  TOKEN(BC_LEX_KEY_WHILE)     \
                              \
  TOKEN(BC_LEX_EOF)           \
                              \
  TOKEN(BC_LEX_INVALID)       \

typedef enum BcLexTokenType {
  BC_LEX_TOKEN_FOREACH(BC_LEX_GEN_ENUM)
} BcLexTokenType;

typedef struct BcLexToken {

  BcLexTokenType type;
  char *string;

} BcLexToken;

typedef struct BcLex {

  const char *buffer;
  size_t idx;
  size_t line;
  bool newline;
  const char *file;
  size_t len;

} BcLex;

typedef struct BcLexKeyword {

  const char name[9];
  const char len;
  const bool posix;

} BcLexKeyword;

#define KW_TABLE_ENTRY(a, b, c) { .name = a, .len = b, .posix = c }

#define BC_PROGRAM_BUF_SIZE (1024)

typedef struct BcProgram {

  BcVec ip_stack;

  size_t scale;

  BcNum ibase;
  size_t ibase_t;
  BcNum obase;
  size_t obase_t;

  long base_max;
  long dim_max;
  long scale_max;
  long string_max;

  BcVec expr_stack;

  BcVec stack;

  BcVec funcs;

  BcVecO func_map;

  BcVec vars;

  BcVecO var_map;

  BcVec arrays;

  BcVecO array_map;

  BcVec strings;

  BcVec constants;

  const char *file;

  BcNum last;

  BcNum zero;
  BcNum one;

  char *num_buf;
  size_t buf_size;

} BcProgram;

#define BC_PROGRAM_CHECK_STACK(p) ((p)->stack.len > 1)
#define BC_PROGRAM_CHECK_EXPR_STACK(p, l) ((p)->expr_stack.len >= (l))

#define BC_PROGRAM_MAIN_FUNC (0)
#define BC_PROGRAM_READ_FUNC (1)

#define BC_PROGRAM_SEARCH_VAR (1<<0)
#define BC_PROGRAM_SEARCH_ARRAY_ONLY (1<<1)

typedef BcStatus (*BcProgramExecFunc)(BcProgram*);
typedef unsigned long (*BcProgramBuiltInFunc)(BcNum*);
typedef void (*BcNumInitFunc)(BcNum*);

BcStatus bc_program_func_add(BcProgram *p, char *name, size_t *idx);
BcStatus bc_program_exec(BcProgram *p);

#define BC_PARSE_TOP_FLAG_PTR(parse)  \
  ((uint8_t*) bc_vec_top(&(parse)->flags))

#define BC_PARSE_TOP_FLAG(parse)  \
  (*(BC_PARSE_TOP_FLAG_PTR(parse)))

#define BC_PARSE_FLAG_FUNC_INNER (0x01)

#define BC_PARSE_FUNC_INNER(parse)  \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_FUNC_INNER)

#define BC_PARSE_FLAG_FUNC (0x02)

#define BC_PARSE_FUNC(parse)  \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_FUNC)

#define BC_PARSE_FLAG_BODY (0x04)

#define BC_PARSE_BODY(parse)  \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_BODY)

#define BC_PARSE_FLAG_LOOP (0x08)

#define BC_PARSE_LOOP(parse)  \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_LOOP)

#define BC_PARSE_FLAG_LOOP_INNER (0x10)

#define BC_PARSE_LOOP_INNER(parse) \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_LOOP_INNER)

#define BC_PARSE_FLAG_IF (0x20)

#define BC_PARSE_IF(parse)  \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_IF)

#define BC_PARSE_FLAG_ELSE (0x40)

#define BC_PARSE_ELSE(parse)  \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_ELSE)

#define BC_PARSE_FLAG_IF_END (0x80)

#define BC_PARSE_IF_END(parse)  \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_IF_END)

#define BC_PARSE_CAN_EXEC(parse)  \
  (!(BC_PARSE_TOP_FLAG(parse) & (BC_PARSE_FLAG_FUNC_INNER |  \
                                 BC_PARSE_FLAG_FUNC |        \
                                 BC_PARSE_FLAG_BODY |  \
                                 BC_PARSE_FLAG_LOOP |        \
                                 BC_PARSE_FLAG_LOOP_INNER |  \
                                 BC_PARSE_FLAG_IF |          \
                                 BC_PARSE_FLAG_ELSE |        \
                                 BC_PARSE_FLAG_IF_END)))

// We can calculate the conversion between tokens and exprs
// by subtracting the position of the first operator in the
// lex enum and adding the position of the first in the expr
// enum. Note: This only works for binary operators.
#define BC_PARSE_TOKEN_TO_EXPR(type) ((type) - BC_LEX_OP_POWER + BC_EXPR_POWER)

typedef struct BcOp {

  uint8_t prec;
  bool left;

} BcOp;

typedef struct BcParse {

  BcLex lex;
  BcLexToken token;

  BcVec flags;

  BcVec exit_labels;

  BcVec cond_labels;

  BcVec ops;

  BcProgram *program;
  size_t func;

  uint32_t num_braces;

  bool auto_part;

} BcParse;

#define BC_PARSE_EXPR_POSIX_REL (1<<0)
#define BC_PARSE_EXPR_PRINT (1<<1)
#define BC_PARSE_EXPR_NO_CALL (1<<2)
#define BC_PARSE_EXPR_NO_READ (1<<3)

BcStatus bc_parse_expr(BcParse *parse, BcVec *code, uint8_t flags);

#define BC_VM_BUF_SIZE (1024)

typedef struct BcVm {

  BcProgram program;
  BcParse parse;

  int filec;
  char** filev;

} BcVm;

const char *bc_version = "0.1";

const char *bc_copyright =
  "bc copyright (c) 2018 Gavin D. Howard and contributors\n"
  "Report bugs at: https://github.com/gavinhoward/bc";

const char *bc_warranty_short =
  "This is free software with ABSOLUTELY NO WARRANTY.";

const char *bc_version_fmt = "bc %s\n%s\n\n%s\n\n";

const char *bc_err_types[] = {

  NULL,

  "bc",
  "bc",

  "bc",

  "bc",

  "bc",
  "bc",

  "vector",

  "ordered vector",
  "ordered vector",

  "Lex",
  "Lex",
  "Lex",
  "Lex",

  "Parse",
  "Parse",
  "Parse",
  "Parse",
  "Parse",
  "Parse",
  "Parse",
  "Parse",
  "Parse",
  "Parse",
  "Parse",

  "Math",
  "Math",
  "Math",
  "Math",
  "Math",
  "Math",
  "Math",

  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",
  "Runtime",

  "POSIX",
  "POSIX",
  "POSIX",
  "POSIX",
  "POSIX",
  "POSIX",
  "POSIX",
  "POSIX",
  "POSIX",
  "POSIX",
  "POSIX",
  "POSIX",

};

const char *bc_err_descs[] = {

  NULL,

  "memory allocation error",
  "I/O error",

  "invalid parameter",

  "invalid option",

  "one or more limits not specified",
  "invalid limit; this is a bug in bc",

  "index is out of bounds for the vector and error was not caught; "
    "this is probably a bug in bc",

  "index is out of bounds for the ordered vector and error was not caught; "
    "this is probably a bug in bc",
  "item already exists in ordered vector and error was not caught; "
    "this is probably a bug in bc",

  "invalid token",
  "string end could not be found",
  "comment end could not be found",
  "end of file",

  "invalid token",
  "invalid expression",
  "invalid print statement",
  "invalid function definition",
  "invalid assignment: must assign to scale, "
    "ibase, obase, last, a variable, or an array element",
  "no auto variable found",
  "limits statement in file not handled correctly; "
    "this is most likely a bug in bc",
  "quit statement in file not exited correctly; "
    "this is most likely a bug in bc",
  "number of functions does not match the number of entries "
    "in the function map; this is most likely a bug in bc",
  "function parameter or auto var has the same name as another",
  "bug in parser",

  "negative number",
  "non integer number",
  "overflow",
  "divide by zero",
  "negative square root",
  "invalid number string",
  "cannot truncate more places than exist after the decimal point",

  "couldn't open file",
  "mismatched parameters",
  "undefined function",
  "undefined variable",
  "undefined array",
  "file is not executable",
  "could not install signal handler",
  "bad scale; must be [0, BC_SCALE_MAX]",
  "bad ibase; must be [2, 16]",
  "bad obase; must be [2, BC_BASE_MAX]",
  "invalid statement; this is a bug in bc",
  "invalid expression; this is a bug in bc",
  "invalid string",
  "string too long: length must be [0, BC_STRING_MAX]",
  "invalid name/identifier",
  "invalid array length; must be [1, BC_DIM_MAX]",
  "invalid read() expression",
  "read() call inside of a read() call",
  "print error",
  "invalid constant",
  "invalid lvalue; cannot assign to constants or intermediate values",
  "cannot return from function; no function to return from",
  "invalid label; this is probably a bug in bc",
  "variable is wrong type",
  "invalid stack; this is a bug in bc",
  "bc stopped; this is a bug in bc",

  "POSIX only allows one character names; the following is invalid:",
  "POSIX does not allow '#' script comments",
  "POSIX does not allow the following keyword:",
  "POSIX does not allow a period ('.') as a shortcut for the last result",
  "POSIX requires parentheses around return expressions",
  "POSIX does not allow boolean operators; the following is invalid:",
  "POSIX does not allow comparison operators outside if or loops",
  "POSIX does not allow more than one comparison operator per condition",
  "POSIX does not allow an empty init expression in a for loop",
  "POSIX does not allow an empty condition expression in a for loop",
  "POSIX does not allow an empty update expression in a for loop",
  "POSIX requires the left brace be on the same line as the function header",

};

const char *bc_lang_func_main = "(main)";
const char *bc_lang_func_read = "(read)";

const char *bc_lex_token_type_strs[] = {
  BC_LEX_TOKEN_FOREACH(BC_LEX_GEN_STR)
};

const BcLexKeyword bc_lex_keywords[20] = {
  KW_TABLE_ENTRY("auto", 4, true),
  KW_TABLE_ENTRY("break", 5, true),
  KW_TABLE_ENTRY("continue", 8, false),
  KW_TABLE_ENTRY("define", 6, true),
  KW_TABLE_ENTRY("else", 4, false),
  KW_TABLE_ENTRY("for", 3, true),
  KW_TABLE_ENTRY("halt", 4, false),
  KW_TABLE_ENTRY("ibase", 5, true),
  KW_TABLE_ENTRY("if", 2, true),
  KW_TABLE_ENTRY("last", 4, false),
  KW_TABLE_ENTRY("length", 6, true),
  KW_TABLE_ENTRY("limits", 6, false),
  KW_TABLE_ENTRY("obase", 5, true),
  KW_TABLE_ENTRY("print", 5, false),
  KW_TABLE_ENTRY("quit", 4, true),
  KW_TABLE_ENTRY("read", 4, false),
  KW_TABLE_ENTRY("return", 6, true),
  KW_TABLE_ENTRY("scale", 5, true),
  KW_TABLE_ENTRY("sqrt", 4, true),
  KW_TABLE_ENTRY("while", 5, true),
};

const char bc_num_hex_digits[] = "0123456789ABCDEF";

// This is an array that corresponds to token types. An entry is
// true if the token is valid in an expression, false otherwise.
const bool bc_parse_token_exprs[] = {

  true,
  true,

  true,

  true,
  true,
  true,

  true,
  true,

  true,
  true,
  true,
  true,
  true,
  true,
  true,

  true,
  true,
  true,
  true,
  true,
  true,

  true,

  true,
  true,

  true,

  false,

  false,

  true,
  true,

  false,
  false,

  false,
  false,

  false,
  false,

  false,
  true,
  true,

  false,
  false,
  false,
  false,
  false,
  false,
  false,
  true,
  false,
  true,
  true,
  true,
  true,
  false,
  false,
  true,
  false,
  true,
  true,
  false,

  false,

  false,
};

// This is an array of data for operators that correspond to token types.
// The last corresponds to BC_PARSE_OP_NEGATE_IDX since it doesn't have
// its own token type (it is the same token at the binary minus operator).
const BcOp bc_parse_ops[] = {

  { 0, false },
  { 0, false },

  { 1, false },

  { 2, false },

  { 3, true },
  { 3, true },
  { 3, true },

  { 4, true },
  { 4, true },

  { 6, true },
  { 6, true },
  { 6, true },
  { 6, true },
  { 6, true },
  { 6, true },

  { 7, false },

  { 8, true },
  { 8, true },

  { 5, false },
  { 5, false },
  { 5, false },
  { 5, false },
  { 5, false },
  { 5, false },
  { 5, false },

};

const uint8_t bc_parse_insts[] = {

  BC_INST_OP_NEGATE,

  BC_INST_OP_POWER,

  BC_INST_OP_MULTIPLY,
  BC_INST_OP_DIVIDE,
  BC_INST_OP_MODULUS,

  BC_INST_OP_PLUS,
  BC_INST_OP_MINUS,

  BC_INST_OP_REL_EQUAL,
  BC_INST_OP_REL_LESS_EQ,
  BC_INST_OP_REL_GREATER_EQ,
  BC_INST_OP_REL_NOT_EQ,
  BC_INST_OP_REL_LESS,
  BC_INST_OP_REL_GREATER,

  BC_INST_OP_BOOL_NOT,

  BC_INST_OP_BOOL_NOT,
  BC_INST_OP_BOOL_AND,

  BC_INST_OP_ASSIGN_POWER,
  BC_INST_OP_ASSIGN_MULTIPLY,
  BC_INST_OP_ASSIGN_DIVIDE,
  BC_INST_OP_ASSIGN_MODULUS,
  BC_INST_OP_ASSIGN_PLUS,
  BC_INST_OP_ASSIGN_MINUS,
  BC_INST_OP_ASSIGN,

};

const char *bc_program_byte_fmt = "%02x";

const BcNumBinaryFunc bc_program_math_ops[] = {

  bc_num_mod,
  NULL, // &
  NULL, // '
  NULL, // (
  NULL, // )
  bc_num_mul,
  bc_num_add,
  NULL, // ,
  bc_num_sub,
  NULL, // .
  bc_num_div,

};

const char *bc_program_stdin_name = "<stdin>";

const char *bc_program_ready_prompt = "ready for more input\n\n";

const char *bc_program_sigint_msg = "\n\ninterrupt (type \"quit\" to exit)\n\n";
const char *bc_lib_name = "lib.bc";

const char bc_lib[] = {
  115,99,97,108,101,61,50,48,10,100,101,102,105,110,101,32,101,40,120,41,123,
  10,9,97,117,116,111,32,98,44,115,44,110,44,114,44,100,44,105,44,112,44,102,
  44,118,10,9,98,61,105,98,97,115,101,10,9,105,98,97,115,101,61,65,10,9,105,102,
  40,120,60,48,41,123,10,9,9,110,61,49,10,9,9,120,61,45,120,10,9,125,10,9,115,
  61,115,99,97,108,101,10,9,114,61,115,43,55,43,48,46,52,53,42,120,10,9,115,99,
  97,108,101,61,115,99,97,108,101,40,120,41,43,49,10,9,119,104,105,108,101,40,
  120,62,49,41,123,10,9,9,100,43,61,49,10,9,9,120,47,61,50,10,9,9,115,99,97,108,
  101,43,61,49,10,9,125,10,9,115,99,97,108,101,61,114,10,9,114,61,120,43,49,10,
  9,112,61,120,10,9,102,61,49,10,9,102,111,114,40,105,61,50,59,49,59,43,43,105,
  41,123,10,9,9,112,42,61,120,59,10,9,9,102,42,61,105,10,9,9,118,61,112,47,102,
  10,9,9,105,102,40,101,61,61,48,41,98,114,101,97,107,10,9,9,114,43,61,118,10,
  9,125,10,9,119,104,105,108,101,40,102,45,45,41,114,42,61,114,10,9,115,99,97,
  108,101,61,115,10,9,105,98,97,115,101,61,98,10,9,105,102,40,109,41,114,101,
  116,117,114,110,40,49,47,114,41,10,9,114,101,116,117,114,110,40,114,47,49,41,
  10,125,10,100,101,102,105,110,101,32,108,40,120,41,123,10,9,97,117,116,111,
  32,98,44,115,44,114,44,112,44,97,44,113,44,105,44,118,10,9,98,61,105,98,97,
  115,101,10,9,105,98,97,115,101,61,65,10,9,105,102,40,120,60,61,48,41,123,10,
  9,9,114,61,40,49,45,49,48,94,115,99,97,108,101,41,47,49,10,9,9,105,98,97,115,
  101,61,98,10,9,9,114,101,116,117,114,110,40,114,41,10,9,125,10,9,115,61,115,
  99,97,108,101,10,9,115,99,97,108,101,43,61,55,10,9,112,61,50,10,9,119,104,105,
  108,101,40,120,62,61,50,41,123,10,9,9,112,42,61,50,10,9,9,120,61,115,113,114,
  116,40,120,41,10,9,125,10,9,119,104,105,108,101,40,120,60,61,48,46,53,41,123,
  10,9,9,112,42,61,50,10,9,9,120,61,115,113,114,116,40,120,41,10,9,125,10,9,114,
  61,97,61,40,120,45,49,41,47,40,120,43,49,41,10,9,113,61,97,42,97,10,9,102,111,
  114,40,105,61,51,59,49,59,105,43,61,50,41,123,10,9,9,110,42,61,109,10,9,9,118,
  61,110,47,105,10,9,9,105,102,40,101,61,61,48,41,98,114,101,97,107,10,9,9,114,
  43,61,118,10,9,125,10,9,114,42,61,112,10,9,115,99,97,108,101,61,115,10,9,105,
  98,97,115,101,61,98,10,9,114,101,116,117,114,110,40,114,47,49,41,10,125,10,
  100,101,102,105,110,101,32,115,40,120,41,123,10,9,97,117,116,111,32,98,44,115,
  44,114,44,110,44,97,44,113,44,105,10,9,98,61,105,98,97,115,101,10,9,105,98,
  97,115,101,61,65,10,9,115,61,115,99,97,108,101,10,9,115,99,97,108,101,61,49,
  46,51,42,115,43,50,10,9,97,61,97,40,49,41,10,9,105,102,40,120,60,48,41,123,
  10,9,9,110,61,49,10,9,9,120,61,45,120,10,9,125,10,9,115,99,97,108,101,61,48,
  10,9,113,61,40,120,47,97,43,50,41,47,52,10,9,120,45,61,52,42,113,42,97,10,9,
  105,102,40,113,37,50,41,120,61,45,120,10,9,115,99,97,108,101,61,115,43,50,10,
  9,114,61,97,61,120,10,9,113,61,45,120,42,120,10,9,102,111,114,40,105,61,51,
  59,49,59,105,43,61,50,41,123,10,9,9,97,42,61,113,47,40,105,42,40,105,45,49,
  41,41,10,9,9,105,102,40,97,61,61,48,41,98,114,101,97,107,10,9,9,114,43,61,97,
  10,9,125,10,9,115,99,97,108,101,61,115,10,9,105,98,97,115,101,61,98,10,9,105,
  102,40,110,41,114,101,116,117,114,110,40,45,114,47,49,41,10,9,114,101,116,117,
  114,110,40,114,47,49,41,10,125,10,100,101,102,105,110,101,32,99,40,120,41,123,
  10,9,97,117,116,111,32,98,44,115,10,9,98,61,105,98,97,115,101,10,9,105,98,97,
  115,101,61,65,10,9,115,61,115,99,97,108,101,10,9,115,99,97,108,101,43,61,49,
  10,9,120,61,115,40,50,42,97,40,49,41,43,120,41,10,9,115,99,97,108,101,61,115,
  10,9,105,98,97,115,101,61,98,10,9,114,101,116,117,114,110,40,120,47,49,41,10,
  125,10,100,101,102,105,110,101,32,97,40,120,41,123,10,9,97,117,116,111,32,98,
  44,115,44,114,44,110,44,97,44,109,44,116,44,102,44,105,44,117,10,9,98,61,105,
  98,97,115,101,10,9,105,98,97,115,101,61,65,10,9,110,61,49,10,9,105,102,40,120,
  60,48,41,123,10,9,9,110,61,45,49,10,9,9,120,61,45,120,10,9,125,10,9,105,102,
  40,120,61,61,49,41,123,10,9,9,105,102,40,115,99,97,108,101,60,61,54,52,41,123,
  10,9,9,9,114,101,116,117,114,110,40,46,55,56,53,51,57,56,49,54,51,51,57,55,
  52,52,56,51,48,57,54,49,53,54,54,48,56,52,53,56,49,57,56,55,53,55,50,49,48,
  52,57,50,57,50,51,52,57,56,52,51,55,55,54,52,53,53,50,52,51,55,51,54,49,52,
  56,48,47,110,41,10,9,9,125,10,9,125,10,9,105,102,40,120,61,61,46,50,54,55,41,
  123,10,9,9,105,102,40,115,99,97,108,101,60,61,54,52,41,123,10,9,9,9,114,101,
  116,117,114,110,40,46,50,54,48,57,49,51,53,54,57,50,51,50,57,52,48,53,55,57,
  53,57,54,55,56,53,50,54,55,55,55,55,57,56,54,53,54,51,57,55,55,52,55,52,48,
  50,51,57,56,56,50,52,52,53,56,50,50,51,50,57,56,56,50,57,49,55,47,110,41,10,
  9,9,125,10,9,125,10,9,115,61,115,99,97,108,101,10,9,105,102,40,120,62,46,50,
  54,55,41,123,10,9,9,115,99,97,108,101,43,61,53,10,9,9,97,61,97,40,46,50,54,
  55,41,10,9,125,10,9,115,99,97,108,101,61,115,43,51,10,9,119,104,105,108,101,
  40,120,62,46,50,54,55,41,123,10,9,9,109,43,61,49,10,9,9,120,61,40,120,45,46,
  50,54,55,41,47,40,49,43,46,50,54,55,42,120,41,10,9,125,10,9,114,61,117,61,120,
  10,9,102,61,45,120,42,120,10,9,102,111,114,40,105,61,51,59,49,59,105,43,61,
  50,41,123,10,9,9,117,42,61,102,10,9,9,116,61,117,47,105,10,9,9,105,102,40,116,
  61,61,48,41,98,114,101,97,107,10,9,9,114,43,61,116,10,9,125,10,9,115,99,97,
  108,101,61,115,10,9,105,98,97,115,101,61,98,10,9,114,101,116,117,114,110,40,
  40,109,42,97,43,114,41,47,110,41,10,125,10,100,101,102,105,110,101,32,106,40,
  110,44,120,41,123,10,9,97,117,116,111,32,98,44,115,44,111,44,97,44,105,44,118,
  44,102,10,9,98,61,105,98,97,115,101,10,9,105,98,97,115,101,61,65,10,9,115,61,
  115,99,97,108,101,10,9,115,99,97,108,101,61,48,10,9,110,47,61,49,10,9,105,102,
  40,110,60,48,41,123,10,9,9,110,61,45,110,10,9,9,105,102,40,110,37,50,61,61,
  49,41,111,61,49,10,9,125,10,9,97,61,49,10,9,102,111,114,40,105,61,50,59,105,
  60,61,110,59,43,43,105,41,102,42,61,105,10,9,115,99,97,108,101,61,49,46,53,
  42,115,10,9,97,61,40,120,94,110,41,47,40,50,94,110,42,97,41,10,9,114,61,118,
  61,49,10,9,102,61,45,120,42,120,47,52,10,9,115,99,97,108,101,43,61,108,101,
  110,103,116,104,40,97,41,45,115,99,97,108,101,40,97,41,10,9,102,111,114,40,
  105,61,49,59,49,59,43,43,105,41,123,10,9,9,118,61,118,42,115,47,105,47,40,110,
  43,105,41,10,9,9,105,102,40,118,61,61,48,41,98,114,101,97,107,10,9,9,114,43,
  61,118,10,9,125,10,9,115,99,97,108,101,61,115,10,9,105,98,97,115,101,61,98,
  10,9,105,102,40,111,41,114,101,116,117,114,110,40,45,97,42,114,47,49,41,10,
  9,114,101,116,117,114,110,40,97,42,114,47,49,41,10,125,10,0
};

BcStatus bc_vec_double(BcVec *vec) {

  uint8_t *ptr;

  ptr = realloc(vec->array, vec->size * (vec->cap * 2));

  if (!ptr) return BC_STATUS_MALLOC_FAIL;

  vec->array = ptr;
  vec->cap *= 2;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_vec_init(BcVec *vec, size_t esize, BcFreeFunc dtor) {

  if (vec == NULL || esize == 0) return BC_STATUS_INVALID_PARAM;

  vec->size = esize;
  vec->cap = BC_VEC_INITIAL_CAP;
  vec->len = 0;
  vec->dtor = dtor;

  vec->array = malloc(esize * BC_VEC_INITIAL_CAP);

  if (!vec->array) return BC_STATUS_MALLOC_FAIL;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_vec_expand(BcVec *vec, size_t request) {

  uint8_t *ptr;

  if (!vec) return BC_STATUS_INVALID_PARAM;

  if (vec->cap >= request) return BC_STATUS_SUCCESS;

  ptr = realloc(vec->array, vec->size * request);

  if (!ptr) return BC_STATUS_MALLOC_FAIL;

  vec->array = ptr;
  vec->cap = request;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_vec_push(BcVec *vec, void *data) {

  BcStatus status;
  size_t size;

  if (vec == NULL || data == NULL) return BC_STATUS_INVALID_PARAM;

  if (vec->len == vec->cap) {

    status = bc_vec_double(vec);

    if (status) return status;
  }

  size = vec->size;
  memmove(vec->array + (size * vec->len), data, size);

  ++vec->len;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_vec_pushByte(BcVec *vec, uint8_t data) {

  BcStatus status;

  if (vec == NULL || vec->size != sizeof(uint8_t))
    return BC_STATUS_INVALID_PARAM;

  if (vec->len == vec->cap) {

    status = bc_vec_double(vec);

    if (status) return status;
  }

  vec->array[vec->len] = data;

  ++vec->len;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_vec_pushAt(BcVec *vec, void *data, size_t idx) {

  BcStatus status;
  uint8_t *ptr;
  size_t size;

  if (vec == NULL || data == NULL || idx > vec->len)
    return BC_STATUS_INVALID_PARAM;

  if (idx == vec->len) return bc_vec_push(vec, data);

  if (vec->len == vec->cap) {

    status = bc_vec_double(vec);

    if (status) return status;
  }

  size = vec->size;
  ptr = vec->array + size * idx;

  memmove(ptr + size, ptr, size * (vec->len - idx));
  memmove(ptr, data, size);

  ++vec->len;

  return BC_STATUS_SUCCESS;
}

void* bc_vec_top(const BcVec *vec) {

  if (vec == NULL || vec->len == 0) return NULL;

  return vec->array + vec->size * (vec->len - 1);
}

void* bc_vec_item(const BcVec *vec, size_t idx) {

  if (vec == NULL || vec->len == 0 || idx >= vec->len) return NULL;

  return vec->array + vec->size * idx;
}

void* bc_vec_item_rev(const BcVec *vec, size_t idx) {

  if (vec == NULL || vec->len == 0 || idx >= vec->len) return NULL;

  return vec->array + vec->size * (vec->len - idx - 1);
}

BcStatus bc_vec_pop(BcVec *vec) {

  if (vec == NULL) return BC_STATUS_INVALID_PARAM;

  if (!vec->len) return BC_STATUS_VEC_OUT_OF_BOUNDS;

  --vec->len;

  if (vec->dtor) vec->dtor(vec->array + (vec->size * vec->len));

  return BC_STATUS_SUCCESS;
}

void bc_vec_free(void *vec) {

  BcVec *s;
  size_t esize, len, i;
  BcFreeFunc sfree;
  uint8_t *array;

  s = (BcVec*) vec;

  if (s == NULL) return;

  sfree = s->dtor;

  if (sfree) {

    len = s->len;
    array = s->array;
    esize = s->size;

    for (i = 0; i < len; ++i) sfree(array + (i * esize));
  }

  free(s->array);

  s->size = 0;
  s->array = NULL;
  s->len = 0;
  s->cap = 0;
}

size_t bc_veco_find(const BcVecO* vec, void *data) {

  BcVecCmpFunc cmp;
  size_t low, high;

  cmp = vec->cmp;

  low = 0;
  high = vec->vec.len;

  while (low < high) {

    size_t mid;
    int result;
    uint8_t *ptr;

    mid = (low + high) / 2;

    ptr = bc_vec_item(&vec->vec, mid);

    result = cmp(data, ptr);

    if (!result) return mid;

    if (result < 0) high = mid;
    else low = mid + 1;
  }

  return low;
}

BcStatus bc_veco_init(BcVecO* vec, size_t esize,
                      BcFreeFunc dtor, BcVecCmpFunc cmp)
{
  if (!vec || esize == 0 || !cmp) return BC_STATUS_INVALID_PARAM;

  vec->cmp = cmp;

  return bc_vec_init(&vec->vec, esize, dtor);
}

BcStatus bc_veco_insert(BcVecO* vec, void *data, size_t *idx) {

  BcStatus status;

  if (!vec || !data) return BC_STATUS_INVALID_PARAM;

  *idx = bc_veco_find(vec, data);

  if (*idx > vec->vec.len) return BC_STATUS_VECO_OUT_OF_BOUNDS;

  if (*idx != vec->vec.len && !vec->cmp(data, bc_vec_item(&vec->vec, *idx)))
    return BC_STATUS_VECO_ITEM_EXISTS;

  if (*idx >= vec->vec.len) {
    *idx = vec->vec.len;
    status = bc_vec_push(&vec->vec, data);
  }
  else status = bc_vec_pushAt(&vec->vec, data, *idx);

  return status;
}

size_t bc_veco_index(const BcVecO* vec, void *data) {

  size_t idx;

  if (!vec || !data) return BC_STATUS_INVALID_PARAM;

  idx = bc_veco_find(vec, data);

  if (idx >= vec->vec.len || vec->cmp(data, bc_vec_item(&vec->vec, idx)))
    return -1;

  return idx;
}

void* bc_veco_item(const BcVecO* vec, size_t idx) {
  return bc_vec_item(&vec->vec, idx);
}

void bc_veco_free(BcVecO* vec) {
  bc_vec_free(&vec->vec);
}

int bc_num_compareDigits(BcNum *a, BcNum *b, size_t *digits) {

  size_t i;
  size_t min;
  BcDigit *max_num;
  BcDigit *min_num;
  bool a_max;
  bool neg;
  size_t a_int;
  size_t b_int;
  BcDigit *ptr_a;
  BcDigit *ptr_b;
  size_t diff;
  int cmp;
  BcDigit c;

  *digits = 0;

  if (!a) {

    if (!b) return 0;
    else return b->neg ? 1 : -1;
  }
  else if (!b) return a->neg ? -1 : 1;

  neg = false;

  if (a->neg) {

    if (b->neg) neg = true;
    else return -1;
  }
  else if (b->neg) return 1;

  if (!a->len) {
    cmp = b->neg ? 1 : -1;
    return !b->len ? 0 : cmp;
  }
  else if (!b->len) return a->neg ? -1 : 1;

  a_int = a->len - a->rdx;
  b_int = b->len - b->rdx;

  if (a_int > b_int) return 1;
  else if (b_int > a_int) return -1;

  ptr_a = a->num + a->rdx;
  ptr_b = b->num + b->rdx;

  for (i = a_int - 1; i < a_int; --i, ++(*digits)) {
    c = ptr_a[i] - ptr_b[i];
    if (c) return neg ? -c : c;
  }

  a_max = a->rdx > b->rdx;

  if (a_max) {

    min = b->rdx;

    diff = a->rdx - b->rdx;

    max_num = a->num + diff;
    min_num = b->num;

    for (i = min - 1; i < min; --i, ++(*digits)) {
      c = max_num[i] - min_num[i];
      if (c) return neg ? -c : c;
    }

    max_num -= diff;

    for (i = diff - 1; i < diff; --i) {
      if (max_num[i]) return neg ? -1 : 1;
    }
  }
  else {

    min = a->rdx;

    diff = b->rdx - a->rdx;

    max_num = b->num + diff;
    min_num = a->num;

    for (i = min - 1; i < min; --i, ++(*digits)) {
      c = max_num[i] - min_num[i];
      if (c) return neg ? c : -c;
    }

    max_num -= diff;

    for (i = diff - 1; i < diff; --i) {
      if (max_num[i]) return neg ? 1 : -1;
    }
  }

  return 0;
}

int bc_num_compareArrays(BcDigit *array1, BcDigit *array2, size_t len) {

  size_t i;
  BcDigit c;

  if (array1[len]) return 1;

  for (i = len - 1; i < len; --i) {
    c = array1[i] - array2[i];
    if (c) return c;
  }

  return 0;
}

BcStatus bc_num_trunc(BcNum *n, size_t places) {

  BcDigit *ptr;

  if (places > n->rdx) return BC_STATUS_MATH_INVALID_TRUNCATE;

  if (places == 0) return BC_STATUS_SUCCESS;

  ptr = n->num + places;

  n->len -= places;
  n->rdx -= places;

  memmove(n->num, ptr, n->len * sizeof(BcDigit));

  memset(n->num + n->len, 0, sizeof(BcDigit) * (n->cap - n->len));

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_extend(BcNum *n, size_t places) {

  BcStatus status;
  BcDigit *ptr;
  size_t len;

  if (places == 0) return BC_STATUS_SUCCESS;

  len = n->len + places;

  if (n->cap < len) {

    status = bc_num_expand(n, len);

    if (status) return status;
  }

  ptr = n->num + places;

  memmove(ptr, n->num, sizeof(BcDigit) * n->len);

  memset(n->num, 0, sizeof(BcDigit) * places);

  n->len += places;
  n->rdx += places;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_alg_a(BcNum *a, BcNum *b, BcNum *c, size_t scale) {

  BcDigit *ptr;
  BcDigit *ptr_a;
  BcDigit *ptr_b;
  BcDigit *ptr_c;
  size_t i;
  size_t max;
  size_t min;
  size_t diff;
  size_t a_whole;
  size_t b_whole;
  BcDigit carry;

  (void) scale;

  c->neg = a->neg;

  memset(c->num, 0, c->cap * sizeof(BcDigit));

  c->rdx = maxof(a->rdx, b->rdx);

  min = minof(a->rdx, b->rdx);

  c->len = 0;

  if (a->rdx > b->rdx) {

    diff = a->rdx - b->rdx;

    ptr = a->num;
    ptr_a = a->num + diff;
    ptr_b = b->num;
  }
  else {

    diff = b->rdx - a->rdx;

    ptr = b->num;
    ptr_a = a->num;
    ptr_b = b->num + diff;
  }

  ptr_c = c->num;

  for (i = 0; i < diff; ++i) {
    ptr_c[i] = ptr[i];
    ++c->len;
  }

  ptr_c += diff;

  carry = 0;

  for (i = 0; i < min; ++i) {

    ptr_c[i] = ptr_a[i] + ptr_b[i] + carry;
    ++c->len;

    if (ptr_c[i] >= 10) {
      carry = ptr_c[i] / 10;
      ptr_c[i] %= 10;
    }
    else carry = 0;
  }

  c->rdx = c->len;

  a_whole = a->len - a->rdx;
  b_whole = b->len - b->rdx;

  min = minof(a_whole, b_whole);

  ptr_a = a->num + a->rdx;
  ptr_b = b->num + b->rdx;
  ptr_c = c->num + c->rdx;

  for (i = 0; i < min; ++i) {

    ptr_c[i] = ptr_a[i] + ptr_b[i] + carry;
    ++c->len;

    if (ptr_c[i] >= 10) {
      carry = ptr_c[i] / 10;
      ptr_c[i] %= 10;
    }
    else carry = 0;
  }

  if (a_whole > b_whole) {
    max = a_whole;
    ptr = ptr_a;
  }
  else {
    max = b_whole;
    ptr = ptr_b;
  }

  for (; i < max; ++i) {

    ptr_c[i] += ptr[i] + carry;
    ++c->len;

    if (ptr_c[i] >= 10) {
      carry = ptr_c[i] / 10;
      ptr_c[i] %= 10;
    }
    else carry = 0;
  }

  if (carry) {
    ++c->len;
    ptr_c[i] = carry;
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_alg_s(BcNum *a, BcNum *b, BcNum *c, size_t sub) {

  BcStatus status;
  int cmp;
  BcNum *minuend;
  BcNum *subtrahend;
  size_t i, j, start;
  bool aneg, bneg, neg;

  // Because this function doesn't need to use scale (per the bc spec),
  // I am hijacking it to tell this function whether it is doing an add
  // or a subtract.

  if (!a->len) {
    status = bc_num_copy(c, b);
    c->neg = !b->neg;
    return status;
  }
  else if (!b->len) return bc_num_copy(c, a);

  aneg = a->neg;
  bneg = b->neg;

  a->neg = b->neg = false;

  cmp = bc_num_compare(a, b);

  a->neg = aneg;
  b->neg = bneg;

  if (!cmp) {
    bc_num_zero(c);
    return BC_STATUS_SUCCESS;
  }
  else if (cmp > 0) {
    neg = sub ? a->neg : !a->neg;
    minuend = a;
    subtrahend = b;
  }
  else {
    neg = sub ? !b->neg : b->neg;
    minuend = b;
    subtrahend = a;
  }

  status = bc_num_copy(c, minuend);

  if (status) return status;

  c->neg = neg;

  if (c->rdx < subtrahend->rdx) {
    status = bc_num_extend(c, subtrahend->rdx - c->rdx);
    if (status) return status;
    start = 0;
  }
  else start = c->rdx - subtrahend->rdx;

  for (i = 0; i < subtrahend->len; ++i) {

    c->num[i + start] -= subtrahend->num[i];

    for (j = 0; c->num[i + j + start] < 0;) {

      c->num[i + j + start] += 10;
      ++j;

      if (j >= c->len - start) return BC_STATUS_MATH_OVERFLOW;

      c->num[i + j + start] -= 1;
    }
  }

  // Remove leading zeros.
  while (c->len > c->rdx && !c->num[c->len - 1]) --c->len;

  return status;
}

BcStatus bc_num_alg_m(BcNum *a, BcNum *b, BcNum *c, size_t scale) {

  BcStatus status;
  BcDigit carry;
  size_t i;
  size_t j;
  size_t len;

  if (!a->len || !b->len) {
    bc_num_zero(c);
    return BC_STATUS_SUCCESS;
  }
  else if (BC_NUM_ONE(a)) {
    status = bc_num_copy(c, b);
    if (a->neg) c->neg = !c->neg;
    return status;
  }
  else if (BC_NUM_ONE(b)) {
    status = bc_num_copy(c, a);
    if (b->neg) c->neg = !c->neg;
    return status;
  }

  scale = maxof(scale, a->rdx);
  scale = maxof(scale, b->rdx);
  c->rdx = a->rdx + b->rdx;

  memset(c->num, 0, sizeof(BcDigit) * c->cap);
  c->len = 0;

  carry = 0;
  len = 0;

  for (i = 0; i < b->len; ++i) {

    for (j = 0; j < a->len; ++j) {

      c->num[i + j] += a->num[j] * b->num[i] + carry;

      carry = c->num[i + j] / 10;
      c->num[i + j] %= 10;
    }

    if (carry) {
      c->num[i + j] += carry;
      carry = 0;
      len = maxof(len, i + j + 1);
    }
    else len = maxof(len, i + j);
  }

  c->len = maxof(len, c->rdx);

  c->neg = !a->neg != !b->neg;

  if (scale < c->rdx) status = bc_num_trunc(c, c->rdx - scale);
  else status = BC_STATUS_SUCCESS;

  // Remove leading zeros.
  while (c->len > c->rdx && !c->num[c->len - 1]) --c->len;

  return status;
}

BcStatus bc_num_alg_d(BcNum *a, BcNum *b, BcNum *c, size_t scale) {

  BcStatus status;
  BcDigit *ptr;
  BcDigit *bptr;
  size_t len;
  size_t end;
  size_t i;
  BcNum copy;

  if (!b->len) return BC_STATUS_MATH_DIVIDE_BY_ZERO;
  else if (!a->len) {
    bc_num_zero(c);
    return BC_STATUS_SUCCESS;
  }
  else if (BC_NUM_ONE(b)) {
    status = bc_num_copy(c, a);
    if (b->neg) c->neg = !c->neg;
    status = bc_num_extend(c, scale);
    return status;
  }

  status = bc_num_init(&copy, a->len + b->rdx + scale + 1);

  if (status) return status;

  status = bc_num_copy(&copy, a);

  if (status) goto err;

  len = b->len;

  if (len > copy.len) {

    status = bc_num_expand(&copy, len + 2);

    if (status) goto err;

    status = bc_num_extend(&copy, len - copy.len);

    if (status) goto err;
  }

  if (b->rdx > copy.rdx) {
    status = bc_num_extend(&copy, b->rdx - copy.rdx);
    if (status) goto err;
  }

  copy.rdx -= b->rdx;

  if (scale > copy.rdx) {
    status = bc_num_extend(&copy, scale - copy.rdx);
    if (status) goto err;
  }

  if (b->rdx == b->len) {

    bool zero;

    zero = true;

    for (i = 0; zero && i < len; ++i) zero = b->num[len - i - 1] == 0;

    if (i == len) return BC_STATUS_MATH_DIVIDE_BY_ZERO;

    len -= i - 1;
  }

  if (copy.cap == copy.len) {
    status = bc_num_expand(&copy, copy.len + 1);
    if (status) goto err;
  }

  // We want an extra zero in front to make things simpler.
  copy.num[copy.len] = 0;
  ++copy.len;

  end = copy.len - len;

  status = bc_num_expand(c, copy.len);

  if (status) goto err;

  bc_num_zero(c);
  c->rdx = copy.rdx;
  c->len = copy.len;

  bptr = b->num;

  for (i = end - 1; i < end; --i) {

    size_t j;
    size_t k;
    BcDigit quotient;

    ptr = copy.num + i;

    quotient = 0;

    while (bc_num_compareArrays(ptr, bptr, len) >= 0) {

      for (j = 0; j < len; ++j) {

        ptr[j] -= bptr[j];

        k = j;

        while (ptr[k] < 0) {

          ptr[k] += 10;
          ++k;

          if (k > len) return BC_STATUS_MATH_OVERFLOW;

          ptr[k] -= 1;
        }
      }

      ++quotient;
    }

    c->num[i] = quotient;
  }

  c->neg = !a->neg != !b->neg;

  // Remove leading zeros.
  while (c->len > c->rdx && !c->num[c->len - 1]) --c->len;

  if (c->rdx > scale) status = bc_num_trunc(c, c->rdx - scale);

err:

  bc_num_free(&copy);

  return status;
}

BcStatus bc_num_alg_mod(BcNum *a, BcNum *b, BcNum *c, size_t scale) {

  BcStatus status;
  BcNum c1;
  BcNum c2;
  size_t len;

  len = a->len + b->len + scale;

  status = bc_num_init(&c1, len);

  if (status) return status;

  status = bc_num_init(&c2, len);

  if (status) goto c2_err;

  status = bc_num_div(a, b, &c1, scale);

  if (status) goto err;

  c->rdx = maxof(scale + b->rdx, a->rdx);

  status = bc_num_mul(&c1, b, &c2, scale);

  if (status) goto err;

  status = bc_num_sub(a, &c2, c, scale);

err:

  bc_num_free(&c2);

c2_err:

  bc_num_free(&c1);

  return status;
}

BcStatus bc_num_alg_p(BcNum *a, BcNum *b, BcNum *c, size_t scale) {

  BcStatus status;
  BcNum copy;
  BcNum one;
  long pow;
  unsigned long upow;
  size_t i;
  size_t powrdx;
  size_t resrdx;
  bool neg;
  bool zero;

  if (b->rdx) return BC_STATUS_MATH_NON_INTEGER;

  status = bc_num_long(b, &pow);

  if (status) return status;

  if (pow == 0) {
    bc_num_one(c);
    return BC_STATUS_SUCCESS;
  }
  else if (!a->len) {
    bc_num_zero(c);
    return BC_STATUS_SUCCESS;
  }
  else if (pow == 1) {
    return bc_num_copy(c, a);
  }
  else if (pow == -1) {

    status = bc_num_init(&one, BC_NUM_DEF_SIZE);

    if (status) return status;

    bc_num_one(&one);

    status = bc_num_div(&one, a, c, scale);

    bc_num_free(&one);

    return status;
  }
  else if (pow < 0) {
    neg = true;
    upow = -pow;
  }
  else {
    neg = false;
    upow = pow;
    scale = minof(a->rdx * upow, maxof(scale, a->rdx));
  }

  status = bc_num_init(&copy, a->len);

  if (status) return status;

  status = bc_num_copy(&copy, a);

  if (status) goto err;

  powrdx = a->rdx;

  while (!(upow & 1)) {

    powrdx <<= 1;

    status = bc_num_mul(&copy, &copy, &copy, powrdx);

    if (status) goto err;

    upow >>= 1;
  }

  status = bc_num_copy(c, &copy);

  if (status) goto err;

  resrdx = powrdx;
  upow >>= 1;

  while (upow != 0) {

    powrdx <<= 1;

    status = bc_num_mul(&copy, &copy, &copy, powrdx);

    if (status) goto err;

    if (upow & 1) {
      resrdx += powrdx;
      bc_num_mul(c, &copy, c, resrdx);
    }

    upow >>= 1;
  }

  if (neg) {

    status = bc_num_init(&one, BC_NUM_DEF_SIZE);

    if (status) goto err;

    bc_num_one(&one);

    status = bc_num_div(&one, c, c, scale);

    bc_num_free(&one);

    if (status) goto err;
  }

  if (c->rdx > scale) {
    status = bc_num_trunc(c, c->rdx - scale);
    if (status) goto err;
  }

  for (zero = true, i = 0; zero && i < c->len; ++i) zero = c->num[i] == 0;

  if (zero) bc_num_zero(c);

err:

  bc_num_free(&copy);

  return status;
}

BcStatus bc_num_sqrt_newton(BcNum *a, BcNum *b, size_t scale) {

  BcStatus status;
  BcNum num1;
  BcNum num2;
  BcNum two;
  BcNum *x0;
  BcNum *x1;
  BcNum *temp;
  size_t pow;
  BcNum f;
  BcNum fprime;
  size_t len;
  size_t digits;
  size_t resrdx;
  int cmp;

  if (!a->len) {
    bc_num_zero(b);
    return BC_STATUS_SUCCESS;
  }
  else if (BC_NUM_ONE(a) && !(a)->neg) {
    bc_num_one(b);
    return bc_num_extend(b, scale);
  }
  else if (a->neg) return BC_STATUS_MATH_NEG_SQRT;

  memset(b->num, 0, b->cap * sizeof(BcDigit));

  scale = maxof(scale, a->rdx) + 1;

  len = a->len;

  status = bc_num_init(&num1, len);

  if (status) return status;

  status = bc_num_init(&num2, num1.len);

  if (status) goto num2_err;

  status = bc_num_init(&two, BC_NUM_DEF_SIZE);

  if (status) goto two_err;

  bc_num_one(&two);
  two.num[0] = 2;

  len += scale;

  status = bc_num_init(&f, len);

  if (status) goto f_err;

  status = bc_num_init(&fprime, len + scale);

  if (status) goto fprime_err;

  x0 = &num1;
  x1 = &num2;

  bc_num_one(x0);

  pow = a->len - a->rdx;

  if (pow) {

    if (pow & 1) {
      x0->num[0] = 2;
      pow -= 1;
    }
    else {
      x0->num[0] = 6;
      pow -= 2;
    }

    status = bc_num_extend(x0, pow);

    if (status) goto err;
  }

  cmp = 1;
  x0->rdx = 0;
  digits = 0;
  resrdx = scale + 1;
  len = (x0->len - x0->rdx) + resrdx;

  while (cmp && digits <= len) {

    status = bc_num_mul(x0, x0, &f, resrdx);

    if (status) goto err;

    status = bc_num_sub(&f, a, &f, resrdx);

    if (status) goto err;

    status = bc_num_mul(x0, &two, &fprime, resrdx);

    if (status) goto err;

    status = bc_num_div(&f, &fprime, &f, resrdx);

    if (status) goto err;

    status = bc_num_sub(x0, &f, x1, resrdx);

    if (status) goto err;

    cmp = bc_num_compareDigits(x1, x0, &digits);

    temp = x0;
    x0 = x1;
    x1 = temp;
  }

  status = bc_num_copy(b, x0);

  if (status) goto err;

  --scale;

  if (b->rdx > scale) status = bc_num_trunc(b, b->rdx - scale);
  else if (b->rdx < scale) status = bc_num_extend(b, scale - b->rdx);

err:

  bc_num_free(&fprime);

fprime_err:

  bc_num_free(&f);

f_err:

  bc_num_free(&two);

two_err:

  bc_num_free(&num2);

num2_err:

  bc_num_free(&num1);

  return status;
}

BcStatus bc_num_binary(BcNum *a, BcNum *b, BcNum *c,  size_t scale,
                              BcNumBinaryFunc op, size_t req)
{
  BcStatus status;
  BcNum num2;
  BcNum *ptr_a;
  BcNum *ptr_b;
  bool init;

  if (!a || !b || !c || !op) return BC_STATUS_INVALID_PARAM;

  init = false;

  if (c == a) {
    memcpy(&num2, c, sizeof(BcNum));
    ptr_a = &num2;
    init = true;
  }
  else ptr_a = a;

  if (c == b) {

    if (c == a) {
      ptr_b = ptr_a;
    }
    else {
      memcpy(&num2, c, sizeof(BcNum));
      ptr_b = &num2;
      init = true;
    }
  }
  else ptr_b = b;

  if (init) status = bc_num_init(c, req);
  else status = bc_num_expand(c, req);

  if (status) return status;

  status = op(ptr_a, ptr_b, c, scale);

  if (c == a || c == b) bc_num_free(&num2);

  return status;
}

BcStatus bc_num_unary(BcNum *a, BcNum *b, size_t scale,
                             BcNumUnaryFunc op, size_t req)
{
  BcStatus status;
  BcNum a2;
  BcNum *ptr_a;

  if (!a || !b || !op) return BC_STATUS_INVALID_PARAM;

  if (b == a) {

    memcpy(&a2, b, sizeof(BcNum));
    ptr_a = &a2;

    status = bc_num_init(b, req);
  }
  else {
    ptr_a = a;
    status = bc_num_expand(b, req);
  }

  if (status) return status;

  status = op(ptr_a, b, scale);

  if (b == a) bc_num_free(&a2);

  return status;
}

bool bc_num_strValid(const char *val, size_t base) {

  size_t len;
  size_t i;
  BcDigit c;
  BcDigit b;
  bool radix;

  radix = false;

  len = strlen(val);

  if (!len) return true;

  if (base <= 10) {

    b = base + '0';

    for (i = 0; i < len; ++i) {

      c = val[i];

      if (c == '.') {

        if (radix) return false;

        radix = true;
        continue;
      }

      if (c < '0' || c >= b) return false;
    }
  }
  else {

    b = base - 9 + 'A';

    for (i = 0; i < len; ++i) {

      c = val[i];

      if (c == '.') {

        if (radix) return false;

        radix = true;
        continue;
      }

      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= b))) return false;
    }
  }

  return true;
}

BcStatus bc_num_parseDecimal(BcNum *n, const char *val) {

  BcStatus status;
  size_t len;
  size_t i;
  const char *ptr;
  size_t radix;
  size_t end;
  BcDigit *num;

  for (i = 0; val[i] == '0'; ++i);

  val += i;

  len = strlen(val);

  bc_num_zero(n);

  if (len) {

    bool zero;

    zero = true;

    for (i = 0; zero && i < len; ++i) {
      if (val[i] != '0' && val[i] != '.') zero = false;
    }

    if (zero) {
      memset(n->num, 0, sizeof(BcDigit) * n->cap);
      n->neg = false;
      return BC_STATUS_SUCCESS;
    }

    status = bc_num_expand(n, len);

    if (status) return status;

  }
  else {
    memset(n->num, 0, sizeof(BcDigit) * n->cap);
    n->neg = false;
    return BC_STATUS_SUCCESS;
  }

  ptr = strchr(val, '.');

  if (ptr) {
    radix = ptr - val;
    ++ptr;
    n->rdx = (val + len) - ptr;
  }
  else {
    radix = len;
    n->rdx = 0;
  }

  end = n->rdx - 1;

  for (i = 0; i < n->rdx; ++i) {
    n->num[i] = BC_NUM_FROM_CHAR(ptr[end - i]);
    n->len += 1;
  }

  if (i >= len) return BC_STATUS_SUCCESS;

  num = n->num + n->len;
  end = radix - 1;

  for (i = 0; i < radix; ++i) {
    num[i] = BC_NUM_FROM_CHAR(val[end - i]);
    ++n->len;
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_parseBase(BcNum *n, const char *val, BcNum *base) {

  BcStatus status;
  BcNum temp;
  BcNum mult;
  BcNum result;
  size_t i;
  size_t len;
  size_t digits;
  BcDigit c;
  bool zero;

  len = strlen(val);

  zero = true;

  for (i = 0; zero && i < len; ++i) {
    c = val[i];
    zero = (c == '.' || c == '0');
  }

  if (zero) {
    bc_num_zero(n);
    return BC_STATUS_SUCCESS;
  }

  status = bc_num_init(&temp, BC_NUM_DEF_SIZE);

  if (status) return status;

  status = bc_num_init(&mult, BC_NUM_DEF_SIZE);

  if (status) goto mult_err;

  bc_num_zero(n);

  for (i = 0; i < len && (c = val[i]) != '.'; ++i) {

    long v;

    status = bc_num_mul(n, base, &mult, 0);

    if (status) goto int_err;

    if (c <= '9') v = c - '0';
    else v = c - 'A' + 10;

    status = bc_num_long2num(&temp, v);

    if (status) goto int_err;

    status = bc_num_add(&mult, &temp, n, 0);

    if (status) goto int_err;
  }

  if (i == len) c = val[i];

  if (c == '\0') goto int_err;

  assert(c == '.');

  status = bc_num_init(&result, base->len);

  if (status) goto int_err;

  ++i;
  bc_num_zero(&result);
  bc_num_one(&mult);

  for (digits = 0; i < len; ++i, ++digits) {

    c = val[i];

    status = bc_num_mul(&result, base, &result, 0);

    if (status) goto err;

    status = bc_num_long2num(&temp, (long) c);

    if (status) goto err;

    status = bc_num_add(&result, &temp, &result, 0);

    if (status) goto err;

    status = bc_num_mul(&mult, base, &mult, 0);

    if (status) goto err;
  }

  status = bc_num_div(&result, &mult, &result, digits);

  if (status) goto err;

  status = bc_num_add(n, &result, n, 0);

err:

  bc_num_free(&result);

int_err:

  bc_num_free(&mult);

mult_err:

  bc_num_free(&temp);

  return status;
}

BcStatus bc_num_printRadix(size_t *nchars, FILE *f) {

  if (*nchars + 1 >= BC_NUM_PRINT_WIDTH) {
    if (fputc('\\', f) == EOF) return BC_STATUS_IO_ERR;
    if (fputc('\n', f) == EOF) return BC_STATUS_IO_ERR;
    *nchars = 0;
  }

  if (fputc('.', f) == EOF) return BC_STATUS_IO_ERR;

  *nchars = *nchars + 1;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_printDigits(unsigned long num, size_t width,
                                   size_t *nchars, FILE *f)
{
  if (*nchars + width + 1 >= BC_NUM_PRINT_WIDTH) {
    if (fputc('\\', f) == EOF) return BC_STATUS_IO_ERR;
    if (fputc('\n', f) == EOF) return BC_STATUS_IO_ERR;
    *nchars = 0;
  }
  else {
    if (fputc(' ', f) == EOF) return BC_STATUS_IO_ERR;
    ++(*nchars);
  }

  if (fprintf(f, "%0*lu", (unsigned int) width, num) < 0)
    return BC_STATUS_IO_ERR;

  *nchars = *nchars + width;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_printHex(unsigned long num, size_t width,
                                size_t *nchars, FILE *f)
{
  if (*nchars + width >= BC_NUM_PRINT_WIDTH) {
    if (fputc('\\', f) == EOF) return BC_STATUS_IO_ERR;
    if (fputc('\n', f) == EOF) return BC_STATUS_IO_ERR;
    *nchars = 0;
  }

  if (fputc(bc_num_hex_digits[num], f) == EOF) return BC_STATUS_IO_ERR;

  *nchars = *nchars + width;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_printDecimal(BcNum *n, FILE *f) {

  BcStatus status;
  size_t i;
  size_t nchars;

  nchars = 0;

  if (n->neg) {
    if (fputc('-', f) == EOF) return BC_STATUS_IO_ERR;
    ++nchars;
  }

  status = BC_STATUS_SUCCESS;

  for (i = n->len - 1; !status && i >= n->rdx && i < n->len; --i)
    status = bc_num_printHex(n->num[i], 1, &nchars, f);

  if (status || !n->rdx) return status;

  status = bc_num_printRadix(&nchars, f);

  if (status) return status;

  for (; !status && i < n->len; --i)
    status = bc_num_printHex(n->num[i], 1, &nchars, f);

  return status;
}

BcStatus bc_num_printBase(BcNum *n, BcNum *base, size_t base_t, FILE* f) {

  BcStatus status;
  BcVec stack;
  BcNum intp;
  BcNum fracp;
  BcNum digit;
  BcNum frac_len;
  size_t nchars;
  size_t width;
  BcNumDigitFunc print;
  size_t i;

  nchars = 0;

  if (n->neg) {
    if (fputc('-', f) == EOF) return BC_STATUS_IO_ERR;
    ++nchars;
  }

  if (base_t <= 16) {
    width = 1;
    print = bc_num_printHex;
  }
  else {
    width = (size_t) floor(log10((double) (base_t - 1)) + 1.0);
    print = bc_num_printDigits;
  }

  status = bc_vec_init(&stack, sizeof(unsigned long), NULL);

  if (status) return status;

  status = bc_num_init(&intp, n->len);

  if (status) goto int_err;

  status = bc_num_init(&fracp, n->rdx);

  if (status) goto frac_err;

  status = bc_num_init(&digit, width);

  if (status) goto digit_err;

  status = bc_num_copy(&intp, n);

  if (status) goto frac_len_err;

  status = bc_num_truncate(&intp);

  if (status) goto frac_len_err;

  status = bc_num_sub(n, &intp, &fracp, 0);

  if (status) goto frac_len_err;

  while (!intp.len) {

    unsigned long dig;

    status = bc_num_mod(&intp, base, &digit, 0);

    if (status) goto frac_len_err;

    status = bc_num_ulong(&digit, &dig);

    if (status) goto frac_len_err;

    status = bc_vec_push(&stack, &dig);

    if (status) goto frac_len_err;

    status = bc_num_div(&intp, base, &intp, 0);

    if (status) goto frac_len_err;
  }

  for (i = 0; i < stack.len; ++i) {

    unsigned long *ptr;

    ptr = bc_vec_item_rev(&stack, i);

    status = print(*ptr, width, &nchars, f);

    if (status) goto frac_len_err;
  }

  if (!n->rdx) goto frac_len_err;

  status = bc_num_printRadix(&nchars, f);

  if (status) goto frac_len_err;

  status = bc_num_init(&frac_len, n->len - n->rdx);

  if (status) goto frac_len_err;

  bc_num_one(&frac_len);

  while (frac_len.len <= n->len) {

    unsigned long fdigit;

    status = bc_num_mul(&fracp, base, &fracp, n->rdx);

    if (status) goto err;

    status = bc_num_ulong(&fracp, &fdigit);

    if (status) goto err;

    status = bc_num_ulong2num(&intp, fdigit);

    if (status) goto err;

    status = bc_num_sub(&fracp, &intp, &fracp, 0);

    if (status) goto err;

    status = print(fdigit, width, &nchars, f);

    if (status) goto err;

    status = bc_num_mul(&frac_len, base, &frac_len, 0);

    if (status) goto err;
  }

err:

  bc_num_free(&frac_len);

frac_len_err:

  bc_num_free(&digit);

digit_err:

  bc_num_free(&fracp);

frac_err:

  bc_num_free(&intp);

int_err:

  bc_vec_free(&stack);

  return status;
}

BcStatus bc_num_init(BcNum *n, size_t request) {

  if (!n) return BC_STATUS_INVALID_PARAM;

  memset(n, 0, sizeof(BcNum));

  request = request >= BC_NUM_DEF_SIZE ? request : BC_NUM_DEF_SIZE;

  n->num = malloc(request);

  if (!n->num) return BC_STATUS_MALLOC_FAIL;

  n->cap = request;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_expand(BcNum *n, size_t request) {

  if (!n || !request) return BC_STATUS_INVALID_PARAM;

  if (request > n->cap) {

    BcDigit *temp;

    temp = realloc(n->num, request);

    if (!temp) return BC_STATUS_MALLOC_FAIL;

    memset(temp + n->cap, 0, sizeof(char) * (request - n->cap));

    n->num = temp;
    n->cap = request;
  }

  return BC_STATUS_SUCCESS;
}

void bc_num_free(void *num) {

  BcNum *n;

  if (!num) return;

  n = (BcNum*) num;

  if (n->num) free(n->num);

  memset(n, 0, sizeof(BcNum));
}

BcStatus bc_num_copy(void *dest, void *src) {

  BcStatus status;

  BcNum *d;
  BcNum *s;

  if (!dest || !src) return BC_STATUS_INVALID_PARAM;

  if (dest == src) return BC_STATUS_SUCCESS;

  d = (BcNum*) dest;
  s = (BcNum*) src;

  status = bc_num_expand(d, s->cap);

  if (status) return status;

  d->len = s->len;
  d->neg = s->neg;
  d->rdx = s->rdx;

  memcpy(d->num, s->num, sizeof(char) * d->len);
  memset(d->num + d->len, 0, sizeof(char) * (d->cap - d->len));

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_parse(BcNum *n, const char *val, BcNum *base, size_t base_t) {

  BcStatus status;

  if (!n || !val) return BC_STATUS_INVALID_PARAM;

  if (base_t < BC_NUM_MIN_BASE || base_t > BC_NUM_MAX_INPUT_BASE)
    return BC_STATUS_EXEC_INVALID_IBASE;

  if (!bc_num_strValid(val, base_t)) return BC_STATUS_MATH_INVALID_STRING;

  if (base_t == 10) status = bc_num_parseDecimal(n, val);
  else status = bc_num_parseBase(n, val, base);

  return status;
}

BcStatus bc_num_fprint(BcNum *n, BcNum *base, size_t base_t,
                       bool newline, FILE *f)
{
  BcStatus status;

  if (!n || !f) return BC_STATUS_INVALID_PARAM;

  if (base_t < BC_NUM_MIN_BASE || base_t > BC_NUM_MAX_OUTPUT_BASE)
    return BC_STATUS_EXEC_INVALID_OBASE;

  if (!n->len) {
    if (fputc('0', f) == EOF) return BC_STATUS_IO_ERR;
    status = BC_STATUS_SUCCESS;
  }
  else if (base_t == 10) status = bc_num_printDecimal(n, f);
  else status = bc_num_printBase(n, base, base_t, f);

  if (status) return status;

  if (newline) {
    if (fputc('\n', f) == EOF) return BC_STATUS_IO_ERR;
  }

  return status;
}

BcStatus bc_num_print(BcNum *n, BcNum *base, size_t base_t, bool newline) {
  return bc_num_fprint(n, base, base_t, newline, stdout);
}

BcStatus bc_num_long(BcNum *n, long *result) {

  size_t i;
  unsigned long temp;
  unsigned long prev;
  unsigned long pow;

  if (!n || !result) return BC_STATUS_INVALID_PARAM;

  temp = 0;
  pow = 1;

  for (i = n->rdx; i < n->len; ++i) {

    prev = temp;

    temp += n->num[i] * pow;

    pow *= 10;

    if (temp < prev) return BC_STATUS_MATH_OVERFLOW;
  }

  if (n->neg) temp = -temp;

  *result = temp;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_ulong(BcNum *n, unsigned long *result) {

  size_t i;
  unsigned long prev;
  unsigned long pow;

  if (!n || !result) return BC_STATUS_INVALID_PARAM;

  if (n->neg) return BC_STATUS_MATH_NEGATIVE;

  *result = 0;
  pow = 1;

  for (i = n->rdx; i < n->len; ++i) {

    prev = *result;

    *result += n->num[i] * pow;

    pow *= 10;

    if (*result < prev) return BC_STATUS_MATH_OVERFLOW;
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_long2num(BcNum *n, long val) {

  BcStatus status;
  size_t len;
  size_t i;
  BcDigit *ptr;
  BcDigit carry;

  if (!n) return BC_STATUS_INVALID_PARAM;

  bc_num_zero(n);

  if (!val) {
    memset(n->num, 0, sizeof(char) * n->cap);
    return BC_STATUS_SUCCESS;
  }

  carry = 0;

  if (val < 0) {

    if (val == LONG_MIN) {
      carry = 1;
      val += 1;
    }

    val = -val;
    n->neg = true;
  }

  len = (size_t) ceil(log10(((double) ULONG_MAX) + 1.0f));

  status = bc_num_expand(n, len);

  if (status) return status;

  ptr = n->num;

  for (i = 0; val; ++i) {
    ++n->len;
    ptr[i] = (char) (val % 10);
    val /= 10;
  }

  if (carry) ptr[i - 1] += carry;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_ulong2num(BcNum *n, unsigned long val) {

  BcStatus status;
  size_t len;
  size_t i;
  BcDigit *ptr;

  if (!n) return BC_STATUS_INVALID_PARAM;

  bc_num_zero(n);

  if (!val) {
    memset(n->num, 0, sizeof(char) * n->cap);
    return BC_STATUS_SUCCESS;
  }

  len = (size_t) ceil(log10(((double) ULONG_MAX) + 1.0f));

  status = bc_num_expand(n, len);

  if (status) return status;

  ptr = n->num;

  for (i = 0; val; ++i) {
    ++n->len;
    ptr[i] = (char) (val % 10);
    val /= 10;
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_truncate(BcNum *n) {
  if (!n) return BC_STATUS_INVALID_PARAM;
  return bc_num_trunc(n, n->rdx);
}

BcStatus bc_num_add(BcNum *a, BcNum *b, BcNum *result, size_t scale) {

  BcNumBinaryFunc op;

  (void) scale;

  if ((a->neg && b->neg) || (!a->neg && !b->neg)) op = bc_num_alg_a;
  else op = bc_num_alg_s;

  return bc_num_binary(a, b, result, false, op, a->len + b->len + 1);
}

BcStatus bc_num_sub(BcNum *a, BcNum *b, BcNum *result, size_t scale) {

  BcNumBinaryFunc op;

  (void) scale;

  if (a->neg && b->neg) op = bc_num_alg_s;
  else if (a->neg || b->neg) op = bc_num_alg_a;
  else op = bc_num_alg_s;

  return bc_num_binary(a, b, result, true, op, a->len + b->len + 1);
}

BcStatus bc_num_mul(BcNum *a, BcNum *b, BcNum *result, size_t scale) {
  return bc_num_binary(a, b, result, scale, bc_num_alg_m,
                       a->len + b->len + scale + 1);
}

BcStatus bc_num_div(BcNum *a, BcNum *b, BcNum *result, size_t scale) {
  return bc_num_binary(a, b, result, scale, bc_num_alg_d,
                       a->len + b->len + scale + 1);
}

BcStatus bc_num_mod(BcNum *a, BcNum *b, BcNum *result, size_t scale) {
  return bc_num_binary(a, b, result, scale, bc_num_alg_mod,
                       a->len + b->len + scale + 1);
}

BcStatus bc_num_pow(BcNum *a, BcNum *b, BcNum *result, size_t scale) {
  return bc_num_binary(a, b, result, scale, bc_num_alg_p,
                       (a->len + 1) * (b->len + 1));
}

BcStatus bc_num_sqrt(BcNum *a, BcNum *result, size_t scale) {
  return bc_num_unary(a, result, scale, bc_num_sqrt_newton,
                      a->rdx + (a->len - a->rdx) * 2 + 1);
}

int bc_num_compare(BcNum *a, BcNum *b) {
  size_t digits;
  return bc_num_compareDigits(a, b, &digits);
}

void bc_num_zero(BcNum *n) {

  if (!n) return;

  memset(n->num, 0, n->cap * sizeof(char));

  n->neg = false;
  n->len = 0;
  n->rdx = 0;
}

void bc_num_one(BcNum *n) {

  if (!n) return;

  bc_num_zero(n);

  n->len = 1;
  n->num[0] = 1;
}

void bc_num_ten(BcNum *n) {

  if (!n) return;

  bc_num_zero(n);

  n->len = 2;
  n->num[0] = 0;
  n->num[1] = 1;
}

BcStatus bc_func_insert(BcFunc *func, char *name, bool var, BcVec *vec) {

  BcStatus status;
  BcAuto a;
  size_t i;
  BcAuto *ptr;

  if (!func || !name || !vec) return BC_STATUS_INVALID_PARAM;

  for (i = 0; i < func->params.len; ++i) {
    ptr = bc_vec_item(&func->params, i);
    if (!strcmp(name, ptr->name))
      return BC_STATUS_PARSE_DUPLICATE_LOCAL;
  }

  for (i = 0; i < func->autos.len; ++i) {
    ptr = bc_vec_item(&func->autos, i);
    if (!strcmp(name, ptr->name))
      return BC_STATUS_PARSE_DUPLICATE_LOCAL;
  }

  status = bc_auto_init(&a, name, var);

  if (status) return status;

  status = bc_vec_push(vec, &a);

  if (status) return status;

  return status;
}

BcStatus bc_func_init(BcFunc *func) {

  BcStatus status;

  if (!func) return BC_STATUS_INVALID_PARAM;

  status = bc_vec_init(&func->code, sizeof(uint8_t), NULL);

  if (status) return status;

  status = bc_vec_init(&func->params, sizeof(BcAuto), bc_auto_free);

  if (status) goto param_err;

  status = bc_vec_init(&func->autos, sizeof(BcAuto), bc_auto_free);

  if (status) goto auto_err;

  status = bc_vec_init(&func->labels, sizeof(size_t), NULL);

  if (status) goto label_err;

  return BC_STATUS_SUCCESS;

label_err:

  bc_vec_free(&func->autos);

auto_err:

  bc_vec_free(&func->params);

param_err:

  bc_vec_free(&func->code);

  return status;
}

void bc_func_free(void *func) {

  BcFunc *f;

  f = (BcFunc*) func;

  if (f == NULL) return;

  bc_vec_free(&f->code);
  bc_vec_free(&f->params);
  bc_vec_free(&f->autos);
  bc_vec_free(&f->labels);
}

BcStatus bc_var_init(void *var) {

  if (!var) return BC_STATUS_INVALID_PARAM;

  return bc_num_init((BcVar*) var, BC_NUM_DEF_SIZE);
}

void bc_var_free(void *var) {

  BcVar *v;

  v = (BcVar*) var;

  if (v == NULL) return;

  bc_num_free(v);
}

BcStatus bc_array_init(void *array) {

  if (!array) return BC_STATUS_INVALID_PARAM;

  return bc_vec_init((BcArray*) array, sizeof(BcNum), bc_num_free);
}

BcStatus bc_array_copy(void *dest, void *src) {

  BcStatus status;
  size_t i;
  BcNum *dnum;
  BcNum *snum;

  BcArray *d;
  BcArray *s;

  d = (BcArray*) dest;
  s = (BcArray*) src;

  if (!d || !s || d == s || d->size != s->size || d->dtor != s->dtor)
    return BC_STATUS_INVALID_PARAM;

  while (d->len) {
    status = bc_vec_pop(d);
    if (status) return status;
  }

  status = bc_vec_expand(d, s->cap);

  if (status) return status;

  d->len = s->len;

  for (i = 0; i < s->len; ++i) {

    dnum = bc_vec_item(d, i);
    snum = bc_vec_item(s, i);

    if (!dnum || !snum) return BC_STATUS_VEC_OUT_OF_BOUNDS;

    status = bc_num_init(dnum, snum->len);

    if (status) return status;

    status = bc_num_copy(dnum, snum);

    if (status) {
      bc_num_free(dnum);
      return status;
    }
  }

  return status;
}

BcStatus bc_array_zero(BcArray *a) {

  BcStatus status;

  status = BC_STATUS_SUCCESS;

  while (!status && a->len) status = bc_vec_pop(a);

  return status;
}

BcStatus bc_array_expand(BcArray *a, size_t len) {

  BcStatus status;

  status = BC_STATUS_SUCCESS;

  while (len > a->len) {

    BcNum num;

    status = bc_num_init(&num, BC_NUM_DEF_SIZE);

    if (status) return status;

    bc_num_zero(&num);

    status = bc_vec_push(a, &num);

    if (status) {
      bc_num_free(&num);
      return status;
    }
  }

  return status;
}

void bc_array_free(void *array) {

  BcArray *a;

  a = (BcArray*) array;

  if (a == NULL) return;

  bc_vec_free(a);
}

void bc_string_free(void *string) {

  char *s;

  s = *((char**) string);

  free(s);
}

int bc_entry_cmp(void *entry1, void *entry2) {

  BcEntry *e1;
  BcEntry *e2;
  int cmp;

  e1 = (BcEntry*) entry1;
  e2 = (BcEntry*) entry2;

  if (!strcmp(e1->name, bc_lang_func_main)) {
    if (!strcmp(e2->name, bc_lang_func_main)) cmp = 0;
    else cmp = -1;
  }
  else if (!strcmp(e1->name, bc_lang_func_read)) {
    if (!strcmp(e2->name, bc_lang_func_main)) cmp = 1;
    else if (!strcmp(e2->name, bc_lang_func_read)) cmp = 0;
    else cmp = -1;
  }
  else if (!strcmp(e2->name, bc_lang_func_main)) cmp = 1;
  else cmp = strcmp(e1->name, e2->name);

  return cmp;
}

void bc_entry_free(void *entry) {

  BcEntry *e;

  if (!entry) return;

  e = (BcEntry*) entry;

  free(e->name);
}

BcStatus bc_auto_init(void *auto1, char *name, bool var) {

  BcStatus status;
  BcAuto *a;

  if (!auto1) return BC_STATUS_INVALID_PARAM;

  a = (BcAuto*) auto1;

  a->var = var;
  a->name = name;

  if (var) status = bc_num_init(&a->data.num, BC_NUM_DEF_SIZE);
  else status = bc_vec_init(&a->data.array, sizeof(BcNum), bc_num_free);

  return status;
}

void bc_auto_free(void *auto1) {

  BcAuto *a;

  if (!auto1) return;

  a = (BcAuto*) auto1;

  if (a->name) free(a->name);

  if (a->var) bc_num_free(&a->data.num);
  else bc_vec_free(&a->data.array);
}

void bc_result_free(void *result) {

  BcResult *r;

  if (!result) return;

  r = (BcResult*) result;

  switch (r->type) {

    case BC_RESULT_INTERMEDIATE:
    case BC_RESULT_SCALE:
    {
      bc_num_free(&r->data.num);
      break;
    }

    case BC_RESULT_VAR:
    case BC_RESULT_ARRAY:
    {
      if (r->data.id.name) free(r->data.id.name);
      break;
    }

    default:
    {
      // Do nothing.
      break;
    }
  }
}

void bc_constant_free(void *constant) {

  char *c;

  if (!constant) return;

  c = *((char**) constant);

  free(c);
}

long bc_io_frag(char *buf, long len, int term, BcIoGetc bc_getc, void *ctx) {

  long i;
  int c;

  if (!buf || len < 0 || !bc_getc) return -1;

  for (c = (~term) | 1, i = 0; i < len; i++) {

    if (c == (int) '\0' || c == term || (c = bc_getc(ctx)) == EOF) {
      buf[i] = '\0';
      break;
    }

    buf[i] = (char) c;
  }

  return i;
}

int bc_io_xfgetc(void *ctx) {
  return fgetc((FILE *) ctx);
}

long bc_io_fgets(char * buf, int n, FILE* fp) {

  long len;

  if (!buf) return -1;

  if (n == 1) {
    buf[0] = '\0';
    return 0;
  }

  if (n < 1 || !fp) return -1;

  len = bc_io_frag(buf, n - 1, (int) '\n', bc_io_xfgetc, fp);

  if (len >= 0) buf[len] = '\0';

  return len;
}

BcStatus bc_io_fgetline(char** p, size_t *n, FILE* fp) {

  size_t mlen, slen, dlen, len;
  char *s;
  char *t;

  if (!p || !n || !fp) return BC_STATUS_INVALID_PARAM;

  if (!p) {

    char blk[64];

    for (slen = 0; ; slen += 64) {

      len = (size_t) bc_io_frag(blk, 64, (int) '\n', bc_io_xfgetc, fp);

      if (len != 64 || blk[len - 1] == '\n' || blk[len - 1] == '\0')
        return slen + len;
    }
  }

  mlen = 8;
  slen = 0;

  s = *p;

  for (;;) {

    mlen += mlen;

    if ((t = realloc(s, mlen)) == NULL) break;

    s = t;
    dlen = mlen - slen - 1;

    len = (size_t) bc_io_frag(s + slen, dlen, (int) '\n', bc_io_xfgetc, fp);

    slen += len;

    if (len < dlen || t[slen - 1] == '\n' || t[slen - 1] == '\0') {

      s[slen] = '\0';
      *p = s;
      *n = slen;

      return BC_STATUS_SUCCESS;
    }
  }

  return BC_STATUS_IO_ERR;
}

BcStatus bc_io_fread(const char *path, char** buf) {

  BcStatus status;
  FILE* f;
  size_t size;
  size_t read;

  f = fopen(path, "r");

  if (!f) return BC_STATUS_EXEC_FILE_ERR;

  fseek(f, 0, SEEK_END);
  size = ftell(f);

  fseek(f, 0, SEEK_SET);

  *buf = malloc(size + 1);

  if (!*buf) {
    status = BC_STATUS_MALLOC_FAIL;
    goto malloc_err;
  }

  read = fread(*buf, 1, size, f);

  if (read != size) {
    status = BC_STATUS_IO_ERR;
    goto read_err;
  }

  (*buf)[size] = '\0';

  fclose(f);

  return BC_STATUS_SUCCESS;

read_err:

  free(*buf);

malloc_err:

  fclose(f);

  return status;
}

BcStatus bc_lex_whitespace(BcLex *lex, BcLexToken *token) {

  char c;

  token->type = BC_LEX_WHITESPACE;

  c = lex->buffer[lex->idx];

  while ((isspace(c) && c != '\n') || c == '\\') {
    ++lex->idx;
    c = lex->buffer[lex->idx];
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_string(BcLex *lex, BcLexToken *token) {

  const char *start;
  size_t newlines, len, i, j;
  char c;

  newlines = 0;

  token->type = BC_LEX_STRING;

  i = lex->idx;
  c = lex->buffer[i];

  while (c != '"' && c != '\0') {
    if (c == '\n') ++newlines;
    c = lex->buffer[++i];
  }

  if (c == '\0') {
    lex->idx = i;
    return BC_STATUS_LEX_NO_STRING_END;
  }

  len = i - lex->idx;

  token->string = malloc(len + 1);

  if (!token->string) return BC_STATUS_MALLOC_FAIL;

  start = lex->buffer + lex->idx;

  for (j = 0; j < len; ++j) token->string[j] = start[j];

  token->string[len] = '\0';

  lex->idx = i + 1;
  lex->line += newlines;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_comment(BcLex *lex, BcLexToken *token) {

  uint32_t newlines;
  bool end;
  size_t i;
  const char *buffer;
  char c;

  newlines = 0;

  token->type = BC_LEX_WHITESPACE;

  ++lex->idx;

  i = lex->idx;
  buffer = lex->buffer;

  end = false;

  while (!end) {

    c = buffer[i];

    while (c != '*' && c != '\0') {
      if (c == '\n') ++newlines;
      c = buffer[++i];
    }

    if (c == '\0' || buffer[i + 1] == '\0') {
      lex->idx = i;
      return BC_STATUS_LEX_NO_COMMENT_END;
    }

    end = buffer[i + 1] == '/';
    i += end ? 0 : 1;
  }

  lex->idx = i + 2;
  lex->line += newlines;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_number(BcLex *lex, BcLexToken *token, char start) {

  const char *buffer;
  const char *buf;
  size_t backslashes;
  size_t len;
  size_t hits;
  size_t i, j;
  char c;
  bool point;

  token->type = BC_LEX_NUMBER;

  point = start == '.';

  buffer = lex->buffer + lex->idx;

  backslashes = 0;
  i = 0;
  c = buffer[i];

  while (c && ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
               (c == '.' && !point) || (c == '\\' && buffer[i + 1] == '\n')))
  {
    if (c == '\\') {
      ++i;
      backslashes += 1;
    }

    c = buffer[++i];
  }

  len = i + 1;

  token->string = malloc(len - backslashes + 1);

  if (!token->string) return BC_STATUS_MALLOC_FAIL;

  token->string[0] = start;

  buf = buffer - 1;
  hits = 0;

  for (j = 1; j < len; ++j) {

    c = buf[j];

    // If we have hit a backslash, skip it.
    // We don't have to check for a newline
    // because it's guaranteed.
    if (hits < backslashes && c == '\\') {
      ++hits;
      ++j;
      continue;
    }

    token->string[j - (hits * 2)] = c;
  }

  token->string[len] = '\0';

  lex->idx += i;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_name(BcLex *lex, BcLexToken *token) {

  BcStatus status;
  const char *buffer;
  size_t i;
  char c;

  buffer = lex->buffer + lex->idx - 1;

  for (i = 0; i < sizeof(bc_lex_keywords) / sizeof(bc_lex_keywords[0]); ++i) {

    if (!strncmp(buffer, bc_lex_keywords[i].name, bc_lex_keywords[i].len)) {

      token->type = BC_LEX_KEY_AUTO + i;

      if (!bc_lex_keywords[i].posix &&
          (status = bc_posix_error(BC_STATUS_POSIX_INVALID_KEYWORD,
                                   lex->file, lex->line,
                                   bc_lex_keywords[i].name)))
      {
        return status;
      }

      // We need to minus one because the
      // index has already been incremented.
      lex->idx += bc_lex_keywords[i].len - 1;

      return BC_STATUS_SUCCESS;
    }
  }

  token->type = BC_LEX_NAME;

  i = 0;
  c = buffer[i];

  while ((c >= 'a' && c<= 'z') || (c >= '0' && c <= '9') || c == '_') {
    ++i;
    c = buffer[i];
  }

  if (i > 1 && (status = bc_posix_error(BC_STATUS_POSIX_NAME_LEN,
                                        lex->file, lex->line, buffer)))
  {
    return status;
  }

  token->string = malloc(i + 1);

  if (!token->string) return BC_STATUS_MALLOC_FAIL;

  strncpy(token->string, buffer, i);
  token->string[i] = '\0';

  // Increment the index. It is minus one
  // because it has already been incremented.
  lex->idx += i - 1;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_token(BcLex *lex, BcLexToken *token) {

  BcStatus status;
  char c;
  char c2;

  status = BC_STATUS_SUCCESS;

  c = lex->buffer[lex->idx];

  ++lex->idx;

  // This is the workhorse of the lexer.
  switch (c) {

    case '\0':
    {
      token->type = BC_LEX_EOF;
      break;
    }

    case '\t':
    {
      status = bc_lex_whitespace(lex, token);
      break;
    }

    case '\n':
    {
      lex->newline = true;
      token->type = BC_LEX_NEWLINE;
      break;
    }

    case '\v':
    case '\f':
    case '\r':
    case ' ':
    {
      status = bc_lex_whitespace(lex, token);
      break;
    }

    case '!':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '=') {
        ++lex->idx;
        token->type = BC_LEX_OP_REL_NOT_EQ;
      }
      else {

        if ((status = bc_posix_error(BC_STATUS_POSIX_BOOL_OPS,
                                     lex->file, lex->line, "!")))
        {
          return status;
        }

        token->type = BC_LEX_OP_BOOL_NOT;
      }

      break;
    }

    case '"':
    {
      status = bc_lex_string(lex, token);
      break;
    }

    case '#':
    {
      if ((status = bc_posix_error(BC_STATUS_POSIX_SCRIPT_COMMENT,
                                  lex->file, lex->line, NULL)))
      {
        return status;
      }

      token->type = BC_LEX_WHITESPACE;

      ++lex->idx;
      while (lex->idx < lex->len && lex->buffer[lex->idx] != '\n') ++lex->idx;

      break;
    }

    case '%':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '=') {
        ++lex->idx;
        token->type = BC_LEX_OP_ASSIGN_MODULUS;
      }
      else token->type = BC_LEX_OP_MODULUS;

      break;
    }

    case '&':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '&') {

        if ((status = bc_posix_error(BC_STATUS_POSIX_BOOL_OPS,
                                     lex->file, lex->line, "&&")))
        {
          return status;
        }

        ++lex->idx;
        token->type = BC_LEX_OP_BOOL_AND;
      }
      else {
        token->type = BC_LEX_INVALID;
        status = BC_STATUS_LEX_INVALID_TOKEN;
      }

      break;
    }

    case '(':
    {
      token->type = BC_LEX_LEFT_PAREN;
      break;
    }

    case ')':
    {
      token->type = BC_LEX_RIGHT_PAREN;
      break;
    }

    case '*':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '=') {
        ++lex->idx;
        token->type = BC_LEX_OP_ASSIGN_MULTIPLY;
      }
      else token->type = BC_LEX_OP_MULTIPLY;

      break;
    }

    case '+':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '=') {
        ++lex->idx;
        token->type = BC_LEX_OP_ASSIGN_PLUS;
      }
      else if (c2 == '+') {
        ++lex->idx;
        token->type = BC_LEX_OP_INC;
      }
      else token->type = BC_LEX_OP_PLUS;

      break;
    }

    case ',':
    {
      token->type = BC_LEX_COMMA;
      break;
    }

    case '-':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '=') {
        ++lex->idx;
        token->type = BC_LEX_OP_ASSIGN_MINUS;
      }
      else if (c2 == '-') {
        ++lex->idx;
        token->type = BC_LEX_OP_DEC;
      }
      else token->type = BC_LEX_OP_MINUS;

      break;
    }

    case '.':
    {
      c2 = lex->buffer[lex->idx];

      if (isdigit(c2)) {
        status = bc_lex_number(lex, token, c);
      }
      else {

        status = bc_posix_error(BC_STATUS_POSIX_DOT_LAST,
                                lex->file, lex->line, NULL);

        token->type = BC_LEX_KEY_LAST;
      }

      break;
    }

    case '/':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '=') {
        ++lex->idx;
        token->type = BC_LEX_OP_ASSIGN_DIVIDE;
      }
      else if (c2 == '*') status = bc_lex_comment(lex, token);
      else token->type = BC_LEX_OP_DIVIDE;

      break;
    }

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    {
      status = bc_lex_number(lex, token, c);
      break;
    }

    case ';':
    {
      token->type = BC_LEX_SEMICOLON;
      break;
    }

    case '<':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '=') {
        ++lex->idx;
        token->type = BC_LEX_OP_REL_LESS_EQ;
      }
      else token->type = BC_LEX_OP_REL_LESS;

      break;
    }

    case '=':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '=') {
        ++lex->idx;
        token->type = BC_LEX_OP_REL_EQUAL;
      }
      else token->type = BC_LEX_OP_ASSIGN;

      break;
    }

    case '>':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '=') {
        ++lex->idx;
        token->type = BC_LEX_OP_REL_GREATER_EQ;
      }
      else token->type = BC_LEX_OP_REL_GREATER;

      break;
    }

    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    {
      status = bc_lex_number(lex, token, c);
      break;
    }

    case '[':
    {
      token->type = BC_LEX_LEFT_BRACKET;
      break;
    }

    case '\\':
    {
      status = bc_lex_whitespace(lex, token);
      break;
    }

    case ']':
    {
      token->type = BC_LEX_RIGHT_BRACKET;
      break;
    }

    case '^':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '=') {
        ++lex->idx;
        token->type = BC_LEX_OP_ASSIGN_POWER;
      }
      else token->type = BC_LEX_OP_POWER;

      break;
    }

    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    {
      status = bc_lex_name(lex, token);
      break;
    }

    case '{':
    {
      token->type = BC_LEX_LEFT_BRACE;
      break;
    }

    case '|':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '|') {

        if ((status = bc_posix_error(BC_STATUS_POSIX_BOOL_OPS,
                                     lex->file, lex->line, "||")))
        {
          return status;
        }

        ++lex->idx;
        token->type = BC_LEX_OP_BOOL_OR;
      }
      else {
        token->type = BC_LEX_INVALID;
        status = BC_STATUS_LEX_INVALID_TOKEN;
      }

      break;
    }

    case '}':
    {
      token->type = BC_LEX_RIGHT_BRACE;
      break;
    }

    default:
    {
      token->type = BC_LEX_INVALID;
      status = BC_STATUS_LEX_INVALID_TOKEN;
      break;
    }
  }

  return status;
}

BcStatus bc_lex_printToken(BcLexToken *token) {

  printf("<%s", bc_lex_token_type_strs[token->type]);

  switch (token->type) {

    case BC_LEX_STRING:
    case BC_LEX_NAME:
    case BC_LEX_NUMBER:
    {
      printf(":%s", token->string);
      break;
    }

    default:
    {
      // Do nothing.
      break;
    }
  }

  putchar('>');
  putchar('\n');

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_init(BcLex *lex, const char *file) {

  if (lex == NULL ) return BC_STATUS_INVALID_PARAM;

  lex->line = 1;
  lex->newline = false;
  lex->file = file;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_text(BcLex *lex, const char *text) {

  if (lex == NULL || text == NULL) return BC_STATUS_INVALID_PARAM;

  lex->buffer = text;
  lex->idx = 0;
  lex->len = strlen(text);

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_next(BcLex *lex, BcLexToken *token) {

  BcStatus status;

  if (lex == NULL || token == NULL) return BC_STATUS_INVALID_PARAM;

  if (lex->idx == lex->len) {
    token->type = BC_LEX_EOF;
    return BC_STATUS_LEX_EOF;
  }

  if (lex->newline) {
    ++lex->line;
    lex->newline = false;
  }

  // Loop until failure or we don't have whitespace. This
  // is so the parser doesn't get inundated with whitespace.
  do {
    token->string = NULL;
    status = bc_lex_token(lex, token);
  } while (!status && token->type == BC_LEX_WHITESPACE);

  return status;
}

BcStatus bc_parse_else(BcParse *parse, BcVec *code);
BcStatus bc_parse_semicolonListEnd(BcParse *parse, BcVec *code);
BcStatus bc_parse_stmt(BcParse *parse, BcVec *code);

BcStatus bc_parse_pushName(BcVec *code, char *name) {

  BcStatus status;
  size_t len, i;

  status = BC_STATUS_SUCCESS;
  len = strlen(name);

  for (i = 0; !status && i < len; ++i)
    status = bc_vec_pushByte(code, (uint8_t) name[i]);

  if (status) return status;

  free(name);

  return bc_vec_pushByte(code, (uint8_t) ':');
}

BcStatus bc_parse_pushIndex(BcVec *code, size_t idx) {

  BcStatus status;
  uint8_t amt;
  uint8_t nums[sizeof(size_t)];
  uint32_t i;

  amt = 0;
  i = 0;

  while (idx) {
    nums[amt] = (uint8_t) idx;
    idx &= ~(0xFF);
    idx >>= sizeof(uint8_t) * CHAR_BIT;
    ++amt;
  }

  status = bc_vec_pushByte(code, amt);

  if (status) return status;

  while (!status && i < amt) {
    status = bc_vec_pushByte(code, nums[i]);
    ++i;
  }

  return status;
}

BcStatus bc_parse_operator(BcParse *parse, BcVec *code, BcVec *ops,
                                  BcLexTokenType t, uint32_t *num_exprs,
                                  bool next)
{
  BcStatus status;
  BcLexTokenType top;
  BcLexTokenType *ptr;
  uint8_t lp;
  uint8_t rp;
  bool rleft;

  rp = bc_parse_ops[t].prec;
  rleft = bc_parse_ops[t].left;

  if (ops->len != 0) {

    ptr = bc_vec_top(ops);
    top = *ptr;

    if (top != BC_LEX_LEFT_PAREN) {

      lp = bc_parse_ops[top].prec;

      while (lp < rp || (lp == rp && rleft)) {

        status = bc_vec_pushByte(code, bc_parse_insts[top - BC_LEX_OP_NEGATE]);

        if (status) return status;

        status = bc_vec_pop(ops);

        if (status) return status;

        *num_exprs -= top != BC_LEX_OP_BOOL_NOT &&
                      top != BC_LEX_OP_NEGATE ? 1 : 0;

        if (ops->len == 0) break;

        ptr = bc_vec_top(ops);
        top = *ptr;

        if (top == BC_LEX_LEFT_PAREN) break;

        lp = bc_parse_ops[top].prec;
      }
    }
  }

  status = bc_vec_push(ops, &t);

  if (status) return status;

  if (next && (status = bc_lex_next(&parse->lex, &parse->token))) goto err;

  return status;

err:

  if (parse->token.string) free(parse->token.string);

  return status;
}

BcStatus bc_parse_rightParen(BcParse *parse, BcVec *code,
                                    BcVec *ops, uint32_t *nexs)
{
  BcStatus status;
  BcLexTokenType top;
  BcLexTokenType *ptr;

  if (ops->len == 0) return BC_STATUS_PARSE_INVALID_EXPR;

  ptr = bc_vec_top(ops);
  top = *ptr;

  while (top != BC_LEX_LEFT_PAREN) {

    status = bc_vec_pushByte(code, bc_parse_insts[top - BC_LEX_OP_NEGATE]);

    if (status) return status;

    status = bc_vec_pop(ops);

    if (status) return status;

    *nexs -= top != BC_LEX_OP_BOOL_NOT &&
             top != BC_LEX_OP_NEGATE ? 1 : 0;

    if (ops->len == 0) return BC_STATUS_PARSE_INVALID_EXPR;

    ptr = bc_vec_top(ops);
    top = *ptr;
  }

  status = bc_vec_pop(ops);

  if (status) return status;

  return bc_lex_next(&parse->lex, &parse->token);
}

BcStatus bc_parse_params(BcParse *parse, BcVec *code, uint8_t flags) {

  BcStatus status;
  bool comma;
  size_t nparams;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type == BC_LEX_RIGHT_PAREN) {

    status = bc_vec_pushByte(code, BC_INST_CALL);

    if (status) return status;

    return bc_vec_pushByte(code, 0);
  }

  nparams = 0;

  do {

    ++nparams;

    status = bc_parse_expr(parse, code, flags & ~(BC_PARSE_EXPR_PRINT));

    if (status) return status;

    if (parse->token.type == BC_LEX_COMMA) {

      comma = true;
      status = bc_lex_next(&parse->lex, &parse->token);

      if (status) return status;
    }
    else comma = false;

  } while (!status && parse->token.type != BC_LEX_RIGHT_PAREN);

  if (status) return status;

  if (comma) return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_vec_pushByte(code, BC_INST_CALL);

  if (status) return status;

  return bc_parse_pushIndex(code, nparams);
}

BcStatus bc_parse_call(BcParse *parse, BcVec *code,
                              char *name, uint8_t flags)
{
  BcStatus status;
  BcEntry entry;
  BcEntry *entry_ptr;
  size_t idx;

  entry.name = name;

  status = bc_parse_params(parse, code, flags);

  if (status) goto err;

  if (parse->token.type != BC_LEX_RIGHT_PAREN) {
    status = BC_STATUS_PARSE_INVALID_TOKEN;
    goto err;
  }

  idx = bc_veco_index(&parse->program->func_map, &entry);

  if (idx == -1) {

    status = bc_program_func_add(parse->program, name, &idx);

    if (status) return status;

    name = NULL;
  }
  else free(name);

  entry_ptr = bc_veco_item(&parse->program->func_map, idx);

  if (!entry_ptr) return BC_STATUS_EXEC_UNDEFINED_FUNC;

  status = bc_parse_pushIndex(code, entry_ptr->idx);

  if (status) return status;

  return bc_lex_next(&parse->lex, &parse->token);

err:

  if (name) free(name);

  return status;
}

BcStatus bc_parse_expr_name(BcParse *parse, BcVec *code,
                                   BcExprType *type, uint8_t flags)
{
  BcStatus status;
  char *name;

  name = parse->token.string;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) goto err;

  if (parse->token.type == BC_LEX_LEFT_BRACKET) {

    *type = BC_EXPR_ARRAY_ELEM;

    status = bc_lex_next(&parse->lex, &parse->token);

    if (status) goto err;

    status = bc_parse_expr(parse, code, flags);

    if (status) goto err;

    if (parse->token.type != BC_LEX_RIGHT_BRACKET) {
      status = BC_STATUS_PARSE_INVALID_TOKEN;
      goto err;
    }

    status = bc_vec_pushByte(code, BC_INST_PUSH_ARRAY);

    if (status) goto err;

    status = bc_parse_pushName(code, name);
  }
  else if (parse->token.type == BC_LEX_LEFT_PAREN) {

    if (flags & BC_PARSE_EXPR_NO_CALL) {
      status = BC_STATUS_PARSE_INVALID_TOKEN;
      goto err;
    }

    *type = BC_EXPR_FUNC_CALL;

    status = bc_parse_call(parse, code, name, flags);
  }
  else {

    *type = BC_EXPR_VAR;

    status = bc_vec_pushByte(code, BC_INST_PUSH_VAR);

    if (status) goto err;

    status = bc_parse_pushName(code, name);
  }

  return status;

err:

  free(name);

  return status;
}

BcStatus bc_parse_read(BcParse *parse, BcVec *code) {

  BcStatus status;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_LEFT_PAREN)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_RIGHT_PAREN)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_vec_pushByte(code, BC_INST_READ);

  if (status) return status;

  return bc_lex_next(&parse->lex, &parse->token);
}

BcStatus bc_parse_builtin(BcParse *parse, BcVec *code,
                                 BcLexTokenType type, uint8_t flags)
{
  BcStatus status;
  uint8_t inst;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_LEFT_PAREN)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  status = bc_parse_expr(parse, code, flags & ~(BC_PARSE_EXPR_PRINT));

  if (status) return status;

  if (parse->token.type != BC_LEX_RIGHT_PAREN)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  inst = type == BC_LEX_KEY_LENGTH ? BC_INST_LENGTH : BC_INST_SQRT;

  status = bc_vec_pushByte(code, inst);

  if (status) return status;

  return bc_lex_next(&parse->lex, &parse->token);
}

BcStatus bc_parse_scale(BcParse *parse, BcVec *code,
                               BcExprType *type, uint8_t flags)
{
  BcStatus status;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_LEFT_PAREN) {

    *type = BC_EXPR_SCALE;

    return bc_vec_pushByte(code, BC_INST_PUSH_SCALE);
  }

  *type = BC_EXPR_SCALE_FUNC;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  status = bc_parse_expr(parse, code, flags);

  if (status) return status;

  if (parse->token.type != BC_LEX_RIGHT_PAREN)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_vec_pushByte(code, BC_INST_SCALE_FUNC);

  if (status) return status;

  return bc_lex_next(&parse->lex, &parse->token);
}

BcStatus bc_parse_incdec(BcParse *parse, BcVec *code, BcExprType *prev,
                                uint32_t *nexprs, uint8_t flags)
{
  BcStatus status;
  BcLexTokenType type;
  BcExprType etype;
  uint8_t inst;

  etype = *prev;

  if (etype == BC_EXPR_VAR || etype == BC_EXPR_ARRAY_ELEM ||
      etype == BC_EXPR_SCALE || etype == BC_EXPR_LAST ||
      etype == BC_EXPR_IBASE || etype == BC_EXPR_OBASE)
  {
    *prev = parse->token.type == BC_LEX_OP_INC ?
              BC_EXPR_INC_POST : BC_EXPR_DEC_POST;

    inst = parse->token.type == BC_LEX_OP_INC ?
             BC_INST_INC_DUP : BC_INST_DEC_DUP;

    status = bc_vec_pushByte(code, inst);

    if (status) return status;

    status = bc_lex_next(&parse->lex, &parse->token);
  }
  else {

    inst = parse->token.type == BC_LEX_OP_INC ? BC_INST_INC : BC_INST_DEC;

    *prev = parse->token.type == BC_LEX_OP_INC ?
              BC_EXPR_INC_PRE : BC_EXPR_DEC_PRE;

    status = bc_lex_next(&parse->lex, &parse->token);

    if (status) return status;

    type = parse->token.type;

    // Because we parse the next part of the expression
    // right here, we need to increment this.
    *nexprs = *nexprs + 1;

    switch (type) {

      case BC_LEX_NAME:
      {
        status = bc_parse_expr_name(parse, code, prev,
                                    flags | BC_PARSE_EXPR_NO_CALL);
        break;
      }

      case BC_LEX_KEY_IBASE:
      {
        status = bc_vec_pushByte(code, BC_INST_PUSH_IBASE);

        if (status) return status;

        status = bc_lex_next(&parse->lex, &parse->token);

        break;
      }

      case BC_LEX_KEY_LAST:
      {
        status = bc_vec_pushByte(code, BC_INST_PUSH_LAST);

        if (status) return status;

        status = bc_lex_next(&parse->lex, &parse->token);

        break;
      }

      case BC_LEX_KEY_OBASE:
      {
        status = bc_vec_pushByte(code, BC_INST_PUSH_OBASE);

        if (status) return status;

        status = bc_lex_next(&parse->lex, &parse->token);

        break;
      }

      case BC_LEX_KEY_SCALE:
      {
        status = bc_lex_next(&parse->lex, &parse->token);

        if (status) return status;

        if (parse->token.type == BC_LEX_LEFT_PAREN)
          return BC_STATUS_PARSE_INVALID_TOKEN;

        status = bc_vec_pushByte(code, BC_INST_PUSH_SCALE);

        break;
      }

      default:
      {
        return BC_STATUS_PARSE_INVALID_TOKEN;
      }
    }

    if (status) return status;

    status = bc_vec_pushByte(code, inst);
  }

  return status;
}

BcStatus bc_parse_minus(BcParse *parse, BcVec *exs, BcVec *ops,
                               BcExprType *prev, bool rparen, uint32_t *nexprs)
{
  BcStatus status;
  BcLexTokenType type;
  BcExprType etype;

  etype = *prev;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  type = parse->token.type;

  if (type != BC_LEX_NAME && type != BC_LEX_NUMBER &&
      type != BC_LEX_KEY_SCALE && type != BC_LEX_KEY_LAST &&
      type != BC_LEX_KEY_IBASE && type != BC_LEX_KEY_OBASE &&
      type != BC_LEX_LEFT_PAREN && type != BC_LEX_OP_MINUS &&
      type != BC_LEX_OP_INC && type != BC_LEX_OP_DEC &&
      type != BC_LEX_OP_BOOL_NOT)
  {
    return BC_STATUS_PARSE_INVALID_TOKEN;
  }

  type = rparen || etype == BC_EXPR_INC_POST || etype == BC_EXPR_DEC_POST ||
         (etype >= BC_EXPR_NUMBER && etype <= BC_EXPR_SQRT) ?
                  BC_LEX_OP_MINUS : BC_LEX_OP_NEGATE;

  *prev = BC_PARSE_TOKEN_TO_EXPR(type);

  if (type == BC_LEX_OP_MINUS)
    status = bc_parse_operator(parse, exs, ops, type, nexprs, false);
  else
    // We can just push onto the op stack because this is the largest
    // precedence operator that gets pushed. Inc/dec does not.
    status = bc_vec_push(ops, &type);

  return status;
}

BcStatus bc_parse_string(BcParse *parse, BcVec *code) {

  BcStatus status;
  size_t len;

  len = parse->program->strings.len;

  status = bc_vec_push(&parse->program->strings, &parse->token.string);

  if (status) return status;

  status = bc_vec_pushByte(code, BC_INST_STR);

  if (status) return status;

  status = bc_parse_pushIndex(code, len);

  if (status) return status;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  return bc_parse_semicolonListEnd(parse, code);
}

BcStatus bc_parse_print(BcParse *parse, BcVec *code) {

  BcStatus status;
  BcLexTokenType type;
  bool comma;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  type = parse->token.type;

  if (type == BC_LEX_SEMICOLON || type == BC_LEX_NEWLINE)
    return BC_STATUS_PARSE_INVALID_PRINT;

  comma = false;

  while (!status && type != BC_LEX_SEMICOLON && type != BC_LEX_NEWLINE) {

    if (type == BC_LEX_STRING) {

      size_t len;

      len = parse->program->strings.len;

      status = bc_vec_push(&parse->program->strings, &parse->token.string);

      if (status) return status;

      status = bc_vec_pushByte(code, BC_INST_PRINT_STR);

      if (status) return status;

      status = bc_parse_pushIndex(code, len);
    }
    else {

      status = bc_parse_expr(parse, code, 0);

      if (status) return status;

      status = bc_vec_pushByte(code, BC_INST_PRINT_EXPR);
    }

    if (status) return status;

    status = bc_lex_next(&parse->lex, &parse->token);

    if (status) return status;

    if (parse->token.type == BC_LEX_COMMA) {
      comma = true;
      status = bc_lex_next(&parse->lex, &parse->token);
    }
    else comma = false;

    type = parse->token.type;
  }

  if (status) return status;

  if (comma) return BC_STATUS_PARSE_INVALID_TOKEN;

  return bc_lex_next(&parse->lex, &parse->token);
}

BcStatus bc_parse_return(BcParse *parse, BcVec *code) {

  BcStatus status;

  if (!BC_PARSE_FUNC(parse)) return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_NEWLINE &&
      parse->token.type != BC_LEX_SEMICOLON &&
      parse->token.type != BC_LEX_LEFT_PAREN &&
      (status = bc_posix_error(BC_STATUS_POSIX_RETURN_PARENS,
                               parse->lex.file, parse->lex.line, NULL)))
  {
     return status;
  }

  if (parse->token.type == BC_LEX_NEWLINE ||
      parse->token.type == BC_LEX_SEMICOLON)
  {
    status = bc_vec_pushByte(code, BC_INST_RETURN_ZERO);
  }
  else {

    status = bc_parse_expr(parse, code, 0);

    if (status) return status;

    status = bc_vec_pushByte(code, BC_INST_RETURN);
  }

  return status;
}

BcStatus bc_parse_endBody(BcParse *parse, BcVec *code, bool brace) {

  BcStatus status;
  uint8_t *flag_ptr;

  if (parse->flags.len <= 1 || parse->num_braces == 0)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  flag_ptr = bc_vec_top(&parse->flags);

  if (!flag_ptr) return BC_STATUS_PARSE_BUG;

  if (brace) {

    if (parse->token.type == BC_LEX_RIGHT_BRACE) {

      if (!parse->num_braces) return BC_STATUS_PARSE_INVALID_TOKEN;

      --parse->num_braces;

      status = bc_lex_next(&parse->lex, &parse->token);

      if (status) return status;
    }
    else return BC_STATUS_PARSE_INVALID_TOKEN;
  }

  if (BC_PARSE_IF(parse)) {

    while (parse->token.type == BC_LEX_NEWLINE) {
      status = bc_lex_next(&parse->lex, &parse->token);
      if (status) return status;
    }

    status = bc_vec_pop(&parse->flags);

    if (status) return status;

    flag_ptr = BC_PARSE_TOP_FLAG_PTR(parse);
    *flag_ptr = (*flag_ptr | BC_PARSE_FLAG_IF_END);

    if (parse->token.type == BC_LEX_KEY_ELSE)
      status = bc_parse_else(parse, code);
  }
  else if (BC_PARSE_FUNC_INNER(parse)) {

    parse->func = 0;

    status = bc_vec_pushByte(code, BC_INST_RETURN_ZERO);

    if (status) return status;

    status = bc_vec_pop(&parse->flags);
  }
  else {

    BcInstPtr *ip;
    BcFunc *func;
    size_t *label;

    ip = bc_vec_top(&parse->exit_labels);

    if (!ip) return BC_STATUS_PARSE_BUG;

    status = bc_vec_pushByte(code, BC_INST_JUMP);

    if (status) return status;

    label = bc_vec_top(&parse->cond_labels);

    if (!label) return BC_STATUS_PARSE_BUG;

    status = bc_parse_pushIndex(code, *label);

    if (status) return status;

    func = bc_vec_item(&parse->program->funcs, parse->func);

    if (!func) return BC_STATUS_PARSE_BUG;

    label = bc_vec_item(&func->labels, ip->idx);

    if (!label) return BC_STATUS_PARSE_BUG;

    *label = code->len;

    status = bc_vec_pop(&parse->flags);
  }

  return status;
}

BcStatus bc_parse_startBody(BcParse *parse, uint8_t flags) {

  uint8_t *flag_ptr;

  flag_ptr = BC_PARSE_TOP_FLAG_PTR(parse);

  if (!flag_ptr) return BC_STATUS_PARSE_BUG;

  flags |= (*flag_ptr & (BC_PARSE_FLAG_FUNC | BC_PARSE_FLAG_LOOP));
  flags |= BC_PARSE_FLAG_BODY;

  return bc_vec_push(&parse->flags, &flags);
}

BcStatus bc_parse_noElse(BcParse *parse, BcVec *code) {

  uint8_t *flag_ptr;
  BcInstPtr *ip;
  BcFunc *func;
  size_t *label;

  flag_ptr = BC_PARSE_TOP_FLAG_PTR(parse);
  *flag_ptr = (*flag_ptr & ~(BC_PARSE_FLAG_IF_END));

  ip = bc_vec_top(&parse->exit_labels);

  if (!ip || ip->func || ip->len) return BC_STATUS_PARSE_BUG;

  func = bc_vec_item(&parse->program->funcs, parse->func);

  if (!func || code != &func->code) return BC_STATUS_PARSE_BUG;

  label = bc_vec_item(&func->labels, ip->idx);

  if (!label) return BC_STATUS_PARSE_BUG;

  *label = code->len;

  return bc_vec_pop(&parse->exit_labels);
}

BcStatus bc_parse_if(BcParse *parse, BcVec *code) {

  BcStatus status;
  BcInstPtr ip;
  BcFunc *func;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_LEFT_PAREN)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  status = bc_parse_expr(parse, code, BC_PARSE_EXPR_POSIX_REL);

  if (status) return status;

  if (parse->token.type != BC_LEX_RIGHT_PAREN)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  status = bc_vec_pushByte(code, BC_INST_JUMP_ZERO);

  if (status) return status;

  func = bc_vec_item(&parse->program->funcs, parse->func);

  if (!func) return BC_STATUS_EXEC_UNDEFINED_FUNC;

  ip.idx = func->labels.len;
  ip.func = 0;
  ip.len = 0;

  status = bc_parse_pushIndex(code, ip.idx);

  if (status) return status;

  status = bc_vec_push(&parse->exit_labels, &ip);

  if (status) return status;

  status = bc_vec_push(&func->labels, &ip.idx);

  if (status) return status;

  return bc_parse_startBody(parse, BC_PARSE_FLAG_IF);
}

BcStatus bc_parse_else(BcParse *parse, BcVec *code) {

  BcStatus status;
  BcInstPtr ip;
  BcFunc *func;

  if (!BC_PARSE_IF_END(parse)) return BC_STATUS_PARSE_INVALID_TOKEN;

  func = bc_vec_item(&parse->program->funcs, parse->func);

  if (!func) return BC_STATUS_PARSE_BUG;

  ip.idx = func->labels.len;
  ip.func = 0;
  ip.len = 0;

  status = bc_vec_pushByte(code, BC_INST_JUMP);

  if (status) return status;

  status = bc_parse_pushIndex(code, ip.idx);

  if (status) return status;

  status = bc_parse_noElse(parse, code);

  if (status) return status;

  status = bc_vec_push(&parse->exit_labels, &ip);

  if (status) return status;

  status = bc_vec_push(&func->labels, &ip.idx);

  if (status) return status;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  return bc_parse_startBody(parse, BC_PARSE_FLAG_ELSE);
}

BcStatus bc_parse_while(BcParse *parse, BcVec *code) {

  BcStatus status;
  uint8_t flags;
  BcFunc *func;
  BcInstPtr ip;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_LEFT_PAREN)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  func = bc_vec_item(&parse->program->funcs, parse->func);

  if (!func) return BC_STATUS_EXEC_UNDEFINED_FUNC;

  ip.idx = func->labels.len;

  status = bc_vec_push(&func->labels, &code->len);

  if (status) return status;

  status = bc_vec_push(&parse->cond_labels, &ip.idx);

  if (status) return status;

  ip.idx = func->labels.len;
  ip.func = 1;
  ip.len = 0;

  status = bc_vec_push(&parse->exit_labels, &ip);

  if (status) return status;

  status = bc_vec_push(&func->labels, &ip.idx);

  if (status) return status;

  status = bc_parse_expr(parse, code, BC_PARSE_EXPR_POSIX_REL);

  if (status) return status;

  if (parse->token.type != BC_LEX_RIGHT_PAREN)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  status = bc_vec_pushByte(code, BC_INST_JUMP_ZERO);

  if (status) return status;

  status = bc_parse_pushIndex(code, ip.idx);

  if (status) return status;

  flags = BC_PARSE_FLAG_LOOP | BC_PARSE_FLAG_LOOP_INNER;

  return bc_parse_startBody(parse, flags);
}

BcStatus bc_parse_for(BcParse *parse, BcVec *code) {

  BcStatus status;
  BcFunc *func;
  BcInstPtr ip;
  size_t cond_idx, exit_idx, body_idx, update_idx;
  uint8_t flags;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_LEFT_PAREN)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_SEMICOLON)
    status = bc_parse_expr(parse, code, 0);
  else
    status = bc_posix_error(BC_STATUS_POSIX_MISSING_FOR_INIT,
                            parse->lex.file, parse->lex.line, NULL);

  if (status) return status;

  if (parse->token.type != BC_LEX_SEMICOLON)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  func = bc_vec_item(&parse->program->funcs, parse->func);

  if (!func) return BC_STATUS_PARSE_BUG;

  cond_idx = func->labels.len;
  update_idx = cond_idx + 1;
  body_idx = update_idx + 1;
  exit_idx = body_idx + 1;

  status = bc_vec_push(&func->labels, &code->len);

  if (status) return status;

  if (parse->token.type != BC_LEX_SEMICOLON)
    status = bc_parse_expr(parse, code, BC_PARSE_EXPR_POSIX_REL);
  else status = bc_posix_error(BC_STATUS_POSIX_MISSING_FOR_COND,
                               parse->lex.file, parse->lex.line, NULL);

  if (status) return status;

  if (parse->token.type != BC_LEX_SEMICOLON)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  status = bc_vec_pushByte(code, BC_INST_JUMP_ZERO);

  if (status) return status;

  status = bc_parse_pushIndex(code, exit_idx);

  if (status) return status;

  status = bc_vec_pushByte(code, BC_INST_JUMP);

  if (status) return status;

  status = bc_parse_pushIndex(code, body_idx);

  if (status) return status;

  ip.idx = func->labels.len;

  status = bc_vec_push(&parse->cond_labels, &update_idx);

  if (status) return status;

  status = bc_vec_push(&func->labels, &code->len);

  if (status) return status;

  if (parse->token.type != BC_LEX_RIGHT_PAREN)
    status = bc_parse_expr(parse, code, 0);
  else
    status = bc_posix_error(BC_STATUS_POSIX_MISSING_FOR_UPDATE,
                            parse->lex.file, parse->lex.line, NULL);

  if (status) return status;

  if (parse->token.type != BC_LEX_RIGHT_PAREN) {
    status = bc_parse_expr(parse, code, BC_PARSE_EXPR_POSIX_REL);
    if (status) return status;
  }

  if (parse->token.type != BC_LEX_RIGHT_PAREN)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  status = bc_vec_pushByte(code, BC_INST_JUMP);

  if (status) return status;

  status = bc_parse_pushIndex(code, cond_idx);

  if (status) return status;

  status = bc_vec_push(&func->labels, &code->len);

  if (status) return status;

  ip.idx = exit_idx;
  ip.func = 1;
  ip.len = 0;

  status = bc_vec_push(&parse->exit_labels, &ip);

  if (status) return status;

  status = bc_vec_push(&func->labels, &ip.idx);

  if (status) return status;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  flags = BC_PARSE_FLAG_LOOP | BC_PARSE_FLAG_LOOP_INNER;

  return bc_parse_startBody(parse, flags);
}

BcStatus bc_parse_loopExit(BcParse *parse, BcVec *code,
                                  BcLexTokenType type)
{
  BcStatus status;
  size_t idx;

  if (!BC_PARSE_LOOP(parse))
    return BC_STATUS_PARSE_INVALID_TOKEN;

  if (type == BC_LEX_KEY_BREAK) {

    size_t top;
    BcInstPtr *ip;

    top = parse->exit_labels.len - 1;
    ip = bc_vec_item(&parse->exit_labels, top);

    while (top < parse->exit_labels.len && ip && !ip->func) {
      ip = bc_vec_item(&parse->exit_labels, top);
      --top;
    }

    if (top >= parse->exit_labels.len || !ip) return BC_STATUS_PARSE_BUG;

    idx = ip->idx;
  }
  else {

    size_t *ptr;

    ptr = bc_vec_top(&parse->cond_labels);

    if (!ptr) return BC_STATUS_PARSE_BUG;

    idx = *ptr;
  }

  status = bc_vec_pushByte(code, BC_INST_JUMP);

  if (status) return status;

  status = bc_parse_pushIndex(code, idx);

  if (status) return status;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_SEMICOLON &&
      parse->token.type != BC_LEX_NEWLINE)
  {
    return BC_STATUS_PARSE_INVALID_TOKEN;
  }

  return bc_lex_next(&parse->lex, &parse->token);
}

BcStatus bc_parse_func(BcParse *parse) {

  BcLexTokenType type;
  BcStatus status;
  BcFunc *fptr;
  bool comma;
  uint8_t flags;
  char *name;
  bool var;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  name = parse->token.string;

  if (parse->token.type != BC_LEX_NAME) {
    status = BC_STATUS_PARSE_INVALID_FUNC;
    goto err;
  }

  if (parse->program->funcs.len != parse->program->func_map.vec.len) {
    status = BC_STATUS_PARSE_MISMATCH_NUM_FUNCS;
    goto err;
  }

  status = bc_program_func_add(parse->program, name, &parse->func);

  if (status) goto err;

  if (!parse->func) return BC_STATUS_PARSE_BUG;

  fptr = bc_vec_item(&parse->program->funcs, parse->func);

  if (!fptr) return BC_STATUS_EXEC_UNDEFINED_FUNC;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_LEFT_PAREN)
    return BC_STATUS_PARSE_INVALID_FUNC;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  comma = false;

  type = parse->token.type;
  name = parse->token.string;

  while (!status && type != BC_LEX_RIGHT_PAREN) {

    if (type != BC_LEX_NAME) {
      status = BC_STATUS_PARSE_INVALID_FUNC;
      goto err;
    }

    status = bc_lex_next(&parse->lex, &parse->token);

    if (status) goto err;

    if (parse->token.type == BC_LEX_LEFT_BRACKET) {

      status = bc_lex_next(&parse->lex, &parse->token);

      if (status) goto err;

      if (parse->token.type != BC_LEX_RIGHT_BRACKET)
        return BC_STATUS_PARSE_INVALID_FUNC;

      status = bc_lex_next(&parse->lex, &parse->token);

      if (status) goto err;

      var = false;
    }
    else {
      var = true;
    }

    if (parse->token.type == BC_LEX_COMMA) {

      comma = true;
      status = bc_lex_next(&parse->lex, &parse->token);

      if (status) goto err;
    }
    else comma = false;

    status = bc_func_insert(fptr, name, var, &fptr->params);

    if (status) goto err;

    type = parse->token.type;
    name = parse->token.string;
  }

  if (comma) return BC_STATUS_PARSE_INVALID_FUNC;

  flags = BC_PARSE_FLAG_FUNC | BC_PARSE_FLAG_FUNC_INNER | BC_PARSE_FLAG_BODY;

  status = bc_parse_startBody(parse, flags);

  if (status) return status;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  if (parse->token.type != BC_LEX_LEFT_BRACE)
    return bc_posix_error(BC_STATUS_POSIX_FUNC_HEADER_LEFT_BRACE,
                          parse->lex.file, parse->lex.line, NULL);

  return status;

err:

  free(name);

  return status;
}

BcStatus bc_parse_auto(BcParse *parse) {

  BcLexTokenType type;
  BcStatus status;
  bool comma;
  char *name;
  bool var;
  bool one;
  BcFunc *func;

  if (!parse->auto_part) return BC_STATUS_PARSE_INVALID_TOKEN;

  parse->auto_part = false;

  status = bc_lex_next(&parse->lex, &parse->token);

  if (status) return status;

  comma = false;
  one = false;

  func = bc_vec_item(&parse->program->funcs, parse->func);

  if (!func) return BC_STATUS_EXEC_UNDEFINED_FUNC;

  type = parse->token.type;

  while (!status && type == BC_LEX_NAME) {

    name = parse->token.string;

    status = bc_lex_next(&parse->lex, &parse->token);

    if (status) return status;

    one = true;

    if (parse->token.type == BC_LEX_LEFT_BRACKET) {

      status = bc_lex_next(&parse->lex, &parse->token);

      if (status) goto err;

      if (parse->token.type != BC_LEX_RIGHT_BRACKET)
        return BC_STATUS_PARSE_INVALID_FUNC;

      status = bc_lex_next(&parse->lex, &parse->token);

      if (status) goto err;

      var = false;
    }
    else var = true;

    if (parse->token.type == BC_LEX_COMMA) {

      comma = true;
      status = bc_lex_next(&parse->lex, &parse->token);

      if (status) goto err;
    }
    else comma = false;

    status = bc_func_insert(func, name, var, &func->autos);

    if (status) goto err;

    type = parse->token.type;
  }

  if (status) return status;

  if (comma) return BC_STATUS_PARSE_INVALID_FUNC;

  if (!one) return BC_STATUS_PARSE_NO_AUTO;

  if (type != BC_LEX_NEWLINE && type != BC_LEX_SEMICOLON)
    return BC_STATUS_PARSE_INVALID_TOKEN;

  return bc_lex_next(&parse->lex, &parse->token);

err:

  free(name);

  return status;
}

BcStatus bc_parse_body(BcParse *parse, BcVec *code, bool brace) {

  BcStatus status;
  uint8_t *flag_ptr;
  uint8_t flags;

  if (parse->flags.len < 2) return BC_STATUS_PARSE_BUG;

  flag_ptr = bc_vec_top(&parse->flags);

  if (!flag_ptr) return BC_STATUS_PARSE_BUG;

  *flag_ptr &= ~(BC_PARSE_FLAG_BODY);

  flags = *flag_ptr;

  if (flags & BC_PARSE_FLAG_FUNC_INNER) {
    if (!brace) return BC_STATUS_PARSE_INVALID_TOKEN;
    parse->auto_part = true;
    status = bc_lex_next(&parse->lex, &parse->token);
  }
  else if (flags) {

    status = bc_parse_stmt(parse, code);

    if (!brace) {

      if (status) return status;

      status = bc_parse_endBody(parse, code, false);
    }
  }
  else status = BC_STATUS_PARSE_BUG;

  return status;
}

BcStatus bc_parse_semicolonList(BcParse *parse, BcVec *code) {

  BcStatus status;

  status = BC_STATUS_SUCCESS;

  switch (parse->token.type) {

    case BC_LEX_OP_INC:
    case BC_LEX_OP_DEC:
    case BC_LEX_OP_MINUS:
    case BC_LEX_OP_BOOL_NOT:
    case BC_LEX_LEFT_PAREN:
    case BC_LEX_STRING:
    case BC_LEX_NAME:
    case BC_LEX_NUMBER:
    case BC_LEX_KEY_BREAK:
    case BC_LEX_KEY_CONTINUE:
    case BC_LEX_KEY_FOR:
    case BC_LEX_KEY_HALT:
    case BC_LEX_KEY_IBASE:
    case BC_LEX_KEY_IF:
    case BC_LEX_KEY_LAST:
    case BC_LEX_KEY_LENGTH:
    case BC_LEX_KEY_LIMITS:
    case BC_LEX_KEY_OBASE:
    case BC_LEX_KEY_PRINT:
    case BC_LEX_KEY_QUIT:
    case BC_LEX_KEY_READ:
    case BC_LEX_KEY_RETURN:
    case BC_LEX_KEY_SCALE:
    case BC_LEX_KEY_SQRT:
    case BC_LEX_KEY_WHILE:
    {
      status = bc_parse_stmt(parse, code);
      break;
    }

    case BC_LEX_NEWLINE:
    {
      status = bc_lex_next(&parse->lex, &parse->token);
      break;
    }

    case BC_LEX_SEMICOLON:
    {
      status = bc_parse_semicolonListEnd(parse, code);
      break;
    }

    case BC_LEX_EOF:
    {
      if (parse->flags.len > 0) status = BC_STATUS_LEX_INVALID_TOKEN;
      break;
    }

    default:
    {
      status = BC_STATUS_PARSE_INVALID_TOKEN;
      break;
    }
  }

  return status;
}

BcStatus bc_parse_semicolonListEnd(BcParse *parse, BcVec *code) {

  BcStatus status;

  if (parse->token.type == BC_LEX_SEMICOLON) {

    status = bc_lex_next(&parse->lex, &parse->token);

    if (status) return status;

    status = bc_parse_semicolonList(parse, code);
  }
  else if (parse->token.type == BC_LEX_NEWLINE)
    status = bc_lex_next(&parse->lex, &parse->token);
  else if (parse->token.type == BC_LEX_EOF)
    status = BC_STATUS_SUCCESS;
  else
    status = BC_STATUS_PARSE_INVALID_TOKEN;

  return status;
}

BcStatus bc_parse_stmt(BcParse *parse, BcVec *code) {

  BcStatus status;

  switch (parse->token.type) {

    case BC_LEX_NEWLINE:
    {
      status = bc_lex_next(&parse->lex, &parse->token);
      return status;
    }

    case BC_LEX_KEY_ELSE:
    {
      parse->auto_part = false;
      break;
    }

    case BC_LEX_LEFT_BRACE:
    {
      ++parse->num_braces;

      if (!BC_PARSE_BODY(parse)) return BC_STATUS_PARSE_INVALID_TOKEN;

      status = bc_lex_next(&parse->lex, &parse->token);

      if (status) return status;

      return bc_parse_body(parse, code, true);
    }

    case BC_LEX_KEY_AUTO:
    {
      return bc_parse_auto(parse);
    }

    default:
    {
      parse->auto_part = false;

      if (BC_PARSE_IF_END(parse)) return bc_parse_noElse(parse, code);
      else if (BC_PARSE_BODY(parse)) return bc_parse_body(parse, code, false);

      break;
    }
  }

  switch (parse->token.type) {

    case BC_LEX_OP_INC:
    case BC_LEX_OP_DEC:
    case BC_LEX_OP_MINUS:
    case BC_LEX_OP_BOOL_NOT:
    case BC_LEX_LEFT_PAREN:
    case BC_LEX_NAME:
    case BC_LEX_NUMBER:
    case BC_LEX_KEY_IBASE:
    case BC_LEX_KEY_LAST:
    case BC_LEX_KEY_LENGTH:
    case BC_LEX_KEY_OBASE:
    case BC_LEX_KEY_READ:
    case BC_LEX_KEY_SCALE:
    case BC_LEX_KEY_SQRT:
    {
      status = bc_parse_expr(parse, code, BC_PARSE_EXPR_PRINT);

      if (status) break;

      status = bc_parse_semicolonListEnd(parse, code);

      break;
    }

    case BC_LEX_KEY_ELSE:
    {
      status = bc_parse_else(parse, code);
      break;
    }

    case BC_LEX_RIGHT_BRACE:
    {
      status = bc_parse_endBody(parse, code, true);
      break;
    }

    case BC_LEX_STRING:
    {
      status = bc_parse_string(parse, code);
      break;
    }

    case BC_LEX_KEY_BREAK:
    case BC_LEX_KEY_CONTINUE:
    {
      status = bc_parse_loopExit(parse, code, parse->token.type);
      break;
    }

    case BC_LEX_KEY_FOR:
    {
      status = bc_parse_for(parse, code);
      break;
    }

    case BC_LEX_KEY_HALT:
    {
      status = bc_vec_pushByte(code, BC_INST_HALT);

      if (status) return status;

      status = bc_lex_next(&parse->lex, &parse->token);

      if (status) return status;

      status = bc_parse_semicolonListEnd(parse, code);

      break;
    }

    case BC_LEX_KEY_IF:
    {
      status = bc_parse_if(parse, code);
      break;
    }

    case BC_LEX_KEY_LIMITS:
    {
      status = bc_lex_next(&parse->lex, &parse->token);

      if (status && status != BC_STATUS_LEX_EOF) return status;

      status = bc_parse_semicolonListEnd(parse, code);

      if (status && status != BC_STATUS_LEX_EOF) return status;

      status = BC_STATUS_PARSE_LIMITS;

      break;
    }

    case BC_LEX_KEY_PRINT:
    {
      status = bc_parse_print(parse, code);
      break;
    }

    case BC_LEX_KEY_QUIT:
    {
      // Quit is a compile-time command,
      // so we send an exit command. We
      // don't exit directly, so the vm
      // can clean up.
      status = BC_STATUS_PARSE_QUIT;
      break;
    }

    case BC_LEX_KEY_RETURN:
    {
      status = bc_parse_return(parse, code);

      if (status) return status;

      status = bc_parse_semicolonListEnd(parse, code);

      break;
    }

    case BC_LEX_KEY_WHILE:
    {
      status = bc_parse_while(parse, code);
      break;
    }

    default:
    {
      status = BC_STATUS_PARSE_INVALID_TOKEN;
      break;
    }
  }

  return status;
}

BcStatus bc_parse_init(BcParse *parse, BcProgram *program) {

  BcStatus status;

  if (parse == NULL || program == NULL) return BC_STATUS_INVALID_PARAM;

  status = bc_vec_init(&parse->flags, sizeof(uint8_t), NULL);

  if (status != BC_STATUS_SUCCESS) return status;

  status = bc_vec_init(&parse->exit_labels, sizeof(BcInstPtr), NULL);

  if (status) goto exit_label_err;

  status = bc_vec_init(&parse->cond_labels, sizeof(size_t), NULL);

  if (status) goto cond_label_err;

  uint8_t flags = 0;

  status = bc_vec_push(&parse->flags, &flags);

  if (status != BC_STATUS_SUCCESS) goto push_err;

  status = bc_vec_init(&parse->ops, sizeof(BcLexTokenType), NULL);

  if (status) goto push_err;

  parse->program = program;
  parse->func = 0;
  parse->num_braces = 0;
  parse->auto_part = false;

  return status;

push_err:

  bc_vec_free(&parse->cond_labels);

cond_label_err:

  bc_vec_free(&parse->exit_labels);

exit_label_err:

  bc_vec_free(&parse->flags);

  return status;
}

BcStatus bc_parse_file(BcParse *parse, const char *file) {

  if (parse == NULL || file == NULL) return BC_STATUS_INVALID_PARAM;

  return bc_lex_init(&parse->lex, file);
}

BcStatus bc_parse_text(BcParse *parse, const char *text) {

  BcStatus status;

  if (parse == NULL || text == NULL) return BC_STATUS_INVALID_PARAM;

  status = bc_lex_text(&parse->lex, text);

  if (status) return status;

  return bc_lex_next(&parse->lex, &parse->token);
}

BcStatus bc_parse_parse(BcParse *parse) {

  BcStatus status;

  if (parse == NULL) return BC_STATUS_INVALID_PARAM;

  switch (parse->token.type) {

    case BC_LEX_NEWLINE:
    {
      status = bc_lex_next(&parse->lex, &parse->token);
      break;
    }

    case BC_LEX_KEY_DEFINE:
    {
      if (!BC_PARSE_CAN_EXEC(parse))
        return BC_STATUS_PARSE_INVALID_TOKEN;

      status = bc_parse_func(parse);

      break;
    }

    case BC_LEX_EOF:
    {
      status = BC_STATUS_LEX_EOF;
      break;
    }

    default:
    {
      BcFunc *func;

      func = bc_vec_item(&parse->program->funcs, parse->func);

      if (!func) return BC_STATUS_EXEC_UNDEFINED_FUNC;

      status = bc_parse_stmt(parse, &func->code);

      break;
    }
  }

  return status;
}

void bc_parse_free(BcParse *parse) {

  if (!parse) {
    return;
  }

  bc_vec_free(&parse->flags);
  bc_vec_free(&parse->exit_labels);
  bc_vec_free(&parse->cond_labels);
  bc_vec_free(&parse->ops);

  switch (parse->token.type) {

    case BC_LEX_STRING:
    case BC_LEX_NAME:
    case BC_LEX_NUMBER:
    {
      if (parse->token.string) {
        free(parse->token.string);
      }

      break;
    }

    default:
    {
      // We don't have have to free anything.
      break;
    }
  }
}

BcStatus bc_parse_expr(BcParse *parse, BcVec *code, uint8_t flags) {

  BcStatus status;
  uint32_t nexprs, nparens, ops_start, nrelops;
  bool paren_first, paren_expr, rparen, done, get_token, assign;
  BcExprType prev;
  BcLexTokenType type, top;
  BcLexTokenType *ptr;

  status = BC_STATUS_SUCCESS;

  ops_start = parse->ops.len;

  prev = BC_EXPR_PRINT;

  paren_first = parse->token.type == BC_LEX_LEFT_PAREN;

  nexprs = 0;
  nparens = 0;
  paren_expr = false;
  rparen = false;
  done = false;
  get_token = false;
  assign = false;
  nrelops = 0;

  type = parse->token.type;

  while (!status && !done && bc_parse_token_exprs[type]) {

    switch (type) {

      case BC_LEX_OP_INC:
      case BC_LEX_OP_DEC:
      {
        status = bc_parse_incdec(parse, code, &prev, &nexprs, flags);
        rparen = false;
        get_token = false;
        break;
      }

      case BC_LEX_OP_MINUS:
      {
        status = bc_parse_minus(parse, code, &parse->ops, &prev,
                                rparen, &nexprs);
        rparen = false;
        get_token = false;
        break;
      }

      case BC_LEX_OP_ASSIGN_POWER:
      case BC_LEX_OP_ASSIGN_MULTIPLY:
      case BC_LEX_OP_ASSIGN_DIVIDE:
      case BC_LEX_OP_ASSIGN_MODULUS:
      case BC_LEX_OP_ASSIGN_PLUS:
      case BC_LEX_OP_ASSIGN_MINUS:
      case BC_LEX_OP_ASSIGN:
        if (prev != BC_EXPR_VAR && prev != BC_EXPR_ARRAY_ELEM &&
            prev != BC_EXPR_SCALE && prev != BC_EXPR_IBASE &&
            prev != BC_EXPR_OBASE && prev != BC_EXPR_LAST)
        {
          status = BC_STATUS_PARSE_INVALID_ASSIGN;
          break;
        }
        // Fallthrough.
      case BC_LEX_OP_POWER:
      case BC_LEX_OP_MULTIPLY:
      case BC_LEX_OP_DIVIDE:
      case BC_LEX_OP_MODULUS:
      case BC_LEX_OP_PLUS:
      case BC_LEX_OP_REL_EQUAL:
      case BC_LEX_OP_REL_LESS_EQ:
      case BC_LEX_OP_REL_GREATER_EQ:
      case BC_LEX_OP_REL_NOT_EQ:
      case BC_LEX_OP_REL_LESS:
      case BC_LEX_OP_REL_GREATER:
      case BC_LEX_OP_BOOL_NOT:
      case BC_LEX_OP_BOOL_OR:
      case BC_LEX_OP_BOOL_AND:
      {
        if (type >= BC_LEX_OP_REL_EQUAL && type <= BC_LEX_OP_REL_GREATER)
          nrelops += 1;

        prev = BC_PARSE_TOKEN_TO_EXPR(type);
        status = bc_parse_operator(parse, code, &parse->ops,
                                   type, &nexprs, true);
        rparen = false;
        get_token = false;

        break;
      }

      case BC_LEX_LEFT_PAREN:
      {
        ++nparens;
        paren_expr = false;
        rparen = false;
        get_token = true;
        status = bc_vec_push(&parse->ops, &type);
        break;
      }

      case BC_LEX_RIGHT_PAREN:
      {
        if (nparens == 0) {
          status = BC_STATUS_SUCCESS;
          done = true;
          get_token = false;
          break;
        }
        else if (!paren_expr) {
          status = BC_STATUS_PARSE_INVALID_EXPR;
          goto err;
        }

        --nparens;
        paren_expr = true;
        rparen = true;
        get_token = false;

        status = bc_parse_rightParen(parse, code, &parse->ops, &nexprs);

        break;
      }

      case BC_LEX_NAME:
      {
        paren_expr = true;
        rparen = false;
        get_token = false;
        status = bc_parse_expr_name(parse, code, &prev,
                                    flags & ~(BC_PARSE_EXPR_NO_CALL));
        ++nexprs;
        break;
      }

      case BC_LEX_NUMBER:
      {
        size_t idx;

        idx = parse->program->constants.len;

        status = bc_vec_push(&parse->program->constants, &parse->token.string);

        if (status) goto err;

        status = bc_vec_pushByte(code, BC_INST_PUSH_NUM);

        if (status) return status;

        status = bc_parse_pushIndex(code, idx);

        if (status) return status;

        paren_expr = true;
        rparen = false;
        get_token = true;
        ++nexprs;
        prev = BC_EXPR_NUMBER;

        break;
      }

      case BC_LEX_KEY_IBASE:
      {
        status = bc_vec_pushByte(code, BC_INST_PUSH_IBASE);

        paren_expr = true;
        rparen = false;
        get_token = true;
        ++nexprs;
        prev = BC_EXPR_IBASE;

        break;
      }

      case BC_LEX_KEY_LENGTH:
      case BC_LEX_KEY_SQRT:
      {
        status = bc_parse_builtin(parse, code, type, flags);
        paren_expr = true;
        rparen = false;
        get_token = false;
        ++nexprs;
        prev = type == BC_LEX_KEY_LENGTH ? BC_EXPR_LENGTH : BC_EXPR_SQRT;
        break;
      }

      case BC_LEX_KEY_OBASE:
      {
        status = bc_vec_pushByte(code, BC_INST_PUSH_OBASE);

        paren_expr = true;
        rparen = false;
        get_token = true;
        ++nexprs;
        prev = BC_EXPR_OBASE;

        break;
      }

      case BC_LEX_KEY_READ:
      {
        if (flags & BC_PARSE_EXPR_NO_READ)
          status = BC_STATUS_EXEC_RECURSIVE_READ;
        else status = bc_parse_read(parse, code);

        paren_expr = true;
        rparen = false;
        get_token = false;
        ++nexprs;
        prev = BC_EXPR_READ;

        break;
      }

      case BC_LEX_KEY_SCALE:
      {
        status = bc_parse_scale(parse, code, &prev, flags);
        paren_expr = true;
        rparen = false;
        get_token = false;
        ++nexprs;
        prev = BC_EXPR_SCALE;
        break;
      }

      default:
      {
        status = BC_STATUS_PARSE_INVALID_TOKEN;
        break;
      }
    }

    if (status && status != BC_STATUS_LEX_EOF) goto err;

    if (get_token) status = bc_lex_next(&parse->lex, &parse->token);

    type = parse->token.type;
  }

  if (status && status != BC_STATUS_LEX_EOF) goto err;

  status = BC_STATUS_SUCCESS;

  while (!status && parse->ops.len > ops_start) {

    ptr = bc_vec_top(&parse->ops);
    top = *ptr;

    assign = top >= BC_LEX_OP_ASSIGN_POWER && top <= BC_LEX_OP_ASSIGN;

    if (top == BC_LEX_LEFT_PAREN || top == BC_LEX_RIGHT_PAREN) {
      status = BC_STATUS_PARSE_INVALID_EXPR;
      goto err;
    }

    status = bc_vec_pushByte(code, bc_parse_insts[top - BC_LEX_OP_NEGATE]);

    if (status) goto err;

    nexprs -= top != BC_LEX_OP_BOOL_NOT && top != BC_LEX_OP_NEGATE ? 1 : 0;

    status = bc_vec_pop(&parse->ops);

    if (status) goto err;
  }

  if (nexprs != 1) {
    status = BC_STATUS_PARSE_INVALID_EXPR;
    goto err;
  }

  if (!(flags & BC_PARSE_EXPR_POSIX_REL) && nrelops &&
      (status = bc_posix_error(BC_STATUS_POSIX_REL_OUTSIDE,
                               parse->lex.file, parse->lex.line, NULL)))
  {
    goto err;
  }
  else if (nrelops > 1 &&
           (status = bc_posix_error(BC_STATUS_POSIX_REL_OUTSIDE,
                                    parse->lex.file, parse->lex.line, NULL)))
  {
    goto err;
  }

  if (flags & BC_PARSE_EXPR_PRINT) {
    if (paren_first || !assign) status = bc_vec_pushByte(code, BC_INST_PRINT);
    else status = bc_vec_pushByte(code, BC_INST_POP);
  }

  return type == BC_LEX_EOF ? BC_STATUS_LEX_EOF : status;

err:

  if (parse->token.string) free(parse->token.string);

  return status;
}

BcStatus bc_program_searchVec(const BcVec *vec, const BcResult *result,
                                     BcNum **ret, uint8_t flags)
{
  BcStatus status;
  BcAuto *a;
  size_t i;

  for (i = 0; i < vec->len; ++i) {

    a = bc_vec_item(vec, i);

    if (!a) return BC_STATUS_EXEC_UNDEFINED_VAR;

    if (!strcmp(a->name, result->data.id.name)) {

      uint8_t cond;

      cond = flags & BC_PROGRAM_SEARCH_VAR;

      if (!a->var != !cond)
        return BC_STATUS_EXEC_INVALID_TYPE;

      if (cond) *ret = &a->data.num;
      else if (flags & BC_PROGRAM_SEARCH_ARRAY_ONLY)
        *ret = (BcNum*) &a->data.array;
      else {

        status = bc_array_expand(&a->data.array, result->data.id.idx + 1);

        if (status) return status;

        *ret = bc_vec_item(&a->data.array, result->data.id.idx);
      }

      return BC_STATUS_SUCCESS;
    }
  }

  return BC_STATUS_EXEC_UNDEFINED_VAR;
}

BcStatus bc_program_search(BcProgram *p, BcResult *result,
                                  BcNum **ret, uint8_t flags)
{
  BcStatus status;
  BcFunc *func;
  BcInstPtr *ip;
  BcEntry entry;
  BcVec *vec;
  BcVecO *veco;
  size_t idx;
  BcEntry *entry_ptr;

  // We use this as either a number or an array, since
  // a BcAuto has a union inside that has both.
  BcAuto a;

  ip = bc_vec_top(&p->stack);

  if (!ip) return BC_STATUS_EXEC_INVALID_STACK;

  if (ip->func == BC_PROGRAM_READ_FUNC) {
    ip = bc_vec_item_rev(&p->stack, 1);
    if (!ip) return BC_STATUS_EXEC_INVALID_STACK;
  }

  if (ip->func != BC_PROGRAM_MAIN_FUNC) {

    func = bc_vec_item(&p->funcs, ip->func);

    if (!func) return BC_STATUS_EXEC_INVALID_STACK;

    status = bc_program_searchVec(&func->params, result, ret, flags);

    if (status != BC_STATUS_EXEC_UNDEFINED_VAR) return status;

    status = bc_program_searchVec(&func->autos, result, ret, flags);

    if (status != BC_STATUS_EXEC_UNDEFINED_VAR) return status;
  }

  vec = (flags & BC_PROGRAM_SEARCH_VAR) ? &p->vars : &p->arrays;
  veco = (flags & BC_PROGRAM_SEARCH_VAR) ? &p->var_map : &p->array_map;

  entry.name = result->data.id.name;
  entry.idx = vec->len;

  status = bc_veco_insert(veco, &entry, &idx);

  if (status != BC_STATUS_VECO_ITEM_EXISTS) {

    size_t len;

    if (status) return status;

    len = strlen(entry.name) + 1;

    result->data.id.name = malloc(len);

    if (!result->data.id.name) return BC_STATUS_MALLOC_FAIL;

    strcpy(result->data.id.name, entry.name);

    status = bc_auto_init(&a, NULL, flags & BC_PROGRAM_SEARCH_VAR);

    if (status) return status;

    status = bc_vec_push(vec, &a.data);

    if (status) return status;
  }

  entry_ptr = bc_veco_item(veco, idx);

  if (!entry_ptr) return BC_STATUS_VECO_OUT_OF_BOUNDS;

  if (flags & BC_PROGRAM_SEARCH_VAR) {
    *ret = bc_vec_item(vec, entry_ptr->idx);
    if (!(*ret)) return BC_STATUS_EXEC_UNDEFINED_VAR;
  }
  else {

    BcArray *aptr;

    aptr = bc_vec_item(vec, entry_ptr->idx);

    if (!aptr) return BC_STATUS_EXEC_UNDEFINED_ARRAY;

    if (flags & BC_PROGRAM_SEARCH_ARRAY_ONLY) {
      *ret = (BcNum*) aptr;
      return BC_STATUS_SUCCESS;
    }

    status = bc_array_expand(aptr, result->data.id.idx + 1);

    if (status) return status;

    *ret = bc_vec_item(aptr, result->data.id.idx);
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_program_num(BcProgram *p, BcResult *result,
                               BcNum** num, bool ibase)
{

  BcStatus status;

  status = BC_STATUS_SUCCESS;

  switch (result->type) {

    case BC_RESULT_INTERMEDIATE:
    case BC_RESULT_SCALE:
    {
      *num = &result->data.num;
      break;
    }

    case BC_RESULT_CONSTANT:
    {
      char** s;
      size_t idx;
      size_t len;
      size_t base;

      idx = result->data.id.idx;

      s = bc_vec_item(&p->constants, idx);

      if (!s) return BC_STATUS_EXEC_INVALID_CONSTANT;

      len = strlen(*s);

      status = bc_num_init(&result->data.num, len);

      if (status) return status;

      base = ibase && len == 1 ? 16 : p->ibase_t;

      status = bc_num_parse(&result->data.num, *s, &p->ibase, base);

      if (status) return status;

      *num = &result->data.num;

      result->type = BC_RESULT_INTERMEDIATE;

      break;
    }

    case BC_RESULT_VAR:
    case BC_RESULT_ARRAY:
    {
      uint8_t flags;

      flags = result->type == BC_RESULT_VAR ? BC_PROGRAM_SEARCH_VAR : 0;

      status = bc_program_search(p, result, num, flags);

      break;
    }

    case BC_RESULT_LAST:
    {
      *num = &p->last;
      break;
    }

    case BC_RESULT_IBASE:
    {
      *num = &p->ibase;
      break;
    }

    case BC_RESULT_OBASE:
    {
      *num = &p->obase;
      break;
    }

    case BC_RESULT_ONE:
    {
      *num = &p->one;
      break;
    }

    default:
    {
      status = BC_STATUS_EXEC_INVALID_EXPR;
      break;
    }
  }

  return status;
}

BcStatus bc_program_binaryOpPrep(BcProgram *p, BcResult **left,
                                        BcNum **lval, BcResult **right,
                                        BcNum **rval)
{
  BcStatus status;
  BcResult *l;
  BcResult *r;

  if (!BC_PROGRAM_CHECK_EXPR_STACK(p, 2)) return BC_STATUS_EXEC_INVALID_EXPR;

  r = bc_vec_item_rev(&p->expr_stack, 0);
  l = bc_vec_item_rev(&p->expr_stack, 1);

  if (!r || !l) return BC_STATUS_EXEC_INVALID_EXPR;

  status = bc_program_num(p, l, lval, false);

  if (status) return status;

  status = bc_program_num(p, r, rval, l->type == BC_RESULT_IBASE);

  if (status) return status;

  *left = l;
  *right = r;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_program_binaryOpRetire(BcProgram *p, BcResult *result,
                                          BcResultType type)
{
  BcStatus status;

  result->type = type;

  status = bc_vec_pop(&p->expr_stack);

  if (status) return status;

  status = bc_vec_pop(&p->expr_stack);

  if (status) return status;

  return bc_vec_push(&p->expr_stack, result);
}

BcStatus bc_program_unaryOpPrep(BcProgram *p, BcResult **result,
                                       BcNum **val)
{
  BcStatus status;
  BcResult *r;

  if (!BC_PROGRAM_CHECK_EXPR_STACK(p, 1)) return BC_STATUS_EXEC_INVALID_EXPR;

  r = bc_vec_item_rev(&p->expr_stack, 0);

  if (!r) return BC_STATUS_EXEC_INVALID_EXPR;

  status = bc_program_num(p, r, val, false);

  if (status) return status;

  *result = r;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_program_unaryOpRetire(BcProgram *p, BcResult *result,
                                         BcResultType type)
{
  BcStatus status;

  result->type = type;

  status = bc_vec_pop(&p->expr_stack);

  if (status) return status;

  return bc_vec_push(&p->expr_stack, result);
}

BcStatus bc_program_op(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult *operand1;
  BcResult *operand2;
  BcResult result;
  BcNum *num1;
  BcNum *num2;

  status = bc_program_binaryOpPrep(p, &operand1, &num1, &operand2, &num2);

  if (status) return status;

  status = bc_num_init(&result.data.num, BC_NUM_DEF_SIZE);

  if (status) return status;

  if (inst != BC_INST_OP_POWER) {

    BcNumBinaryFunc op;

    op = bc_program_math_ops[inst - BC_INST_OP_MODULUS];

    status = op(num1, num2, &result.data.num, p->scale);
  }
  else status = bc_num_pow(num1, num2, &result.data.num, p->scale);

  if (status) goto err;

  status = bc_program_binaryOpRetire(p, &result, BC_RESULT_INTERMEDIATE);

  if (status) goto err;

  return status;

err:

  bc_num_free(&result.data.num);

  return status;
}

BcStatus bc_program_read(BcProgram *p) {

  BcStatus status;
  BcParse parse;
  char *buffer;
  size_t size;
  BcFunc *func;
  BcInstPtr ip;

  func = bc_vec_item(&p->funcs, BC_PROGRAM_READ_FUNC);

  if (!func) return BC_STATUS_EXEC_UNDEFINED_FUNC;

  func->code.len = 0;

  buffer = malloc(BC_PROGRAM_BUF_SIZE + 1);

  if (!buffer) return BC_STATUS_MALLOC_FAIL;

  size = BC_PROGRAM_BUF_SIZE;

  status = bc_io_getline(&buffer, &size);

  if (status) goto io_err;

  status = bc_parse_init(&parse, p);

  if (status) goto io_err;

  status = bc_parse_file(&parse, "<stdin>");

  if (status) goto exec_err;

  status = bc_parse_text(&parse, buffer);

  if (status) goto exec_err;

  status = bc_parse_expr(&parse, &func->code, BC_PARSE_EXPR_NO_READ);

  if (status != BC_STATUS_LEX_EOF && parse.token.type != BC_LEX_NEWLINE) {
    status = status ? status : BC_STATUS_EXEC_INVALID_READ_EXPR;
    goto exec_err;
  }

  ip.func = BC_PROGRAM_READ_FUNC;
  ip.idx = 0;
  ip.len = p->expr_stack.len;

  status = bc_vec_push(&p->stack, &ip);

  if (status) goto exec_err;

  status = bc_program_exec(p);

  if (status) goto exec_err;

  status = bc_vec_pop(&p->stack);

exec_err:

  bc_parse_free(&parse);

io_err:

  free(buffer);

  return status;
}

size_t bc_program_index(uint8_t *code, size_t *start) {

  uint8_t bytes, byte, i;
  size_t result;

  bytes = code[(*start)++];
  byte = 1;

  result = 0;

  for (i = 0; byte && i < bytes; ++i) {
    byte = code[(*start)++];
    result |= (((size_t) byte) << (i * 8));
  }

  return result;
}

char* bc_program_name(uint8_t *code, size_t *start) {

  char byte;
  char *s;
  char *string;
  char *ptr;
  size_t len;
  size_t i;

  string = (char*) (code + *start);

  ptr = strchr((char*) string, ':');

  if (ptr) len = ((unsigned long) ptr) - ((unsigned long) string);
  else len = strlen(string);

  s = malloc(len + 1);

  if (!s) return NULL;

  byte = code[(*start)++];
  i = 0;

  while (byte && byte != ':') {
    s[i++] = byte;
    byte = code[(*start)++];
  }

  s[i] = '\0';

  return s;
}

BcStatus bc_program_printIndex(uint8_t *code, size_t *start) {

  uint8_t bytes, byte, i;

  bytes = code[(*start)++];
  byte = 1;

  if (printf(bc_program_byte_fmt, bytes) < 0) return BC_STATUS_IO_ERR;

  for (i = 0; byte && i < bytes; ++i) {
    byte = code[(*start)++];
    if (printf(bc_program_byte_fmt, byte) < 0) return BC_STATUS_IO_ERR;
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_program_printName(uint8_t *code, size_t *start) {

  BcStatus status;
  char byte;

  status = BC_STATUS_SUCCESS;
  byte = code[(*start)++];

  while (byte && byte != ':') {
    if (putchar(byte) == EOF) return BC_STATUS_IO_ERR;
    byte = code[(*start)++];
  }

  if (byte) {
    if (putchar(byte) == EOF) status = BC_STATUS_IO_ERR;
  }
  else status = BC_STATUS_PARSE_BUG;

  return status;
}

BcStatus bc_program_printString(const char *str) {

  char c;
  char c2;
  size_t len, i;
  int err;

  err = 0;

  len = strlen(str);

  for (i = 0; i < len; ++i) {

    c = str[i];

    if (c != '\\') err = fputc(c, stdout);
    else {

      ++i;

      if (i >= len) return BC_STATUS_EXEC_INVALID_STRING;

      c2 = str[i];

      switch (c2) {

        case 'a':
        {
          err = fputc('\a', stdout);
          break;
        }

        case 'b':
        {
          err = fputc('\b', stdout);
          break;
        }

        case 'e':
        {
          err = fputc('\\', stdout);
          break;
        }

        case 'f':
        {
          err = fputc('\f', stdout);
          break;
        }

        case 'n':
        {
          err = fputc('\n', stdout);
          break;
        }

        case 'r':
        {
          err = fputc('\r', stdout);
          break;
        }

        case 'q':
        {
          fputc('"', stdout);
          break;
        }

        case 't':
        {
          err = fputc('\t', stdout);
          break;
        }

        default:
        {
          // Do nothing.
          err = 0;
          break;
        }
      }
    }

    if (err == EOF) return BC_STATUS_EXEC_PRINT_ERR;
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_program_push(BcProgram *p, uint8_t *code,
                                size_t *start, bool var)
{
  BcStatus status;
  BcResult result;
  char *s;

  s = bc_program_name(code, start);

  if (!s) return BC_STATUS_EXEC_UNDEFINED_VAR;

  result.data.id.name = s;

  if (var) {
    result.type = BC_RESULT_VAR;
    status = bc_vec_push(&p->expr_stack, &result);
  }
  else {

    BcResult *operand;
    BcNum *num;
    unsigned long temp;

    status = bc_program_unaryOpPrep(p, &operand, &num);

    if (status) goto err;

    status = bc_num_ulong(num, &temp);

    if (status) goto err;

    result.data.id.idx = (size_t) temp;

    status = bc_program_unaryOpRetire(p, &result, BC_RESULT_ARRAY);
  }

  if (status) goto err;

  return status;

err:

  free(s);

  return status;
}

BcStatus bc_program_negate(BcProgram *p) {

  BcStatus status;
  BcResult result;
  BcResult *ptr;
  BcNum *num;

  status = bc_program_unaryOpPrep(p, &ptr, &num);

  if (status) return status;

  status = bc_num_init(&result.data.num, num->len);

  if (status) return status;

  status = bc_num_copy(&result.data.num, num);

  if (status) goto err;

  result.data.num.neg = !result.data.num.neg;

  status = bc_program_unaryOpRetire(p, &result, BC_RESULT_INTERMEDIATE);

  if (status) goto err;

  return status;

err:

  bc_num_free(&result.data.num);

  return status;
}

BcStatus bc_program_logical(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult *operand1;
  BcResult *operand2;
  BcNum *num1;
  BcNum *num2;
  BcResult result;
  BcNumInitFunc init;
  bool cond;
  int cmp;

  status = bc_program_binaryOpPrep(p, &operand1, &num1, &operand2, &num2);

  if (status) return status;

  status = bc_num_init(&result.data.num, BC_NUM_DEF_SIZE);

  if (status) return status;

  if (inst == BC_INST_OP_BOOL_AND)
    cond = bc_num_compare(num1, &p->zero) && bc_num_compare(num2, &p->zero);
  else if (inst == BC_INST_OP_BOOL_OR)
    cond = bc_num_compare(num1, &p->zero) || bc_num_compare(num2, &p->zero);
  else {

    cmp = bc_num_compare(num1, num2);

    switch (inst) {
      case BC_INST_OP_REL_EQUAL:
      {
        cond = cmp == 0;
        break;
      }

      case BC_INST_OP_REL_LESS_EQ:
      {
        cond = cmp <= 0;
        break;
      }

      case BC_INST_OP_REL_GREATER_EQ:
      {
        cond = cmp >= 0;
        break;
      }

      case BC_INST_OP_REL_NOT_EQ:
      {
        cond = cmp != 0;
        break;
      }

      case BC_INST_OP_REL_LESS:
      {
        cond = cmp < 0;
        break;
      }

      case BC_INST_OP_REL_GREATER:
      {
        cond = cmp > 0;
        break;
      }

      default:
      {
        return BC_STATUS_EXEC_INVALID_EXPR;
      }
    }
  }

  init = cond ? bc_num_one : bc_num_zero;
  init(&result.data.num);

  status = bc_program_binaryOpRetire(p, &result, BC_RESULT_INTERMEDIATE);

  if (status) goto err;

  return status;

err:

  bc_num_free(&result.data.num);

  return status;
}

BcNumBinaryFunc bc_program_assignOp(uint8_t inst) {

  switch (inst) {

    case BC_INST_OP_ASSIGN_POWER:
    {
      return bc_num_pow;
    }

    case BC_INST_OP_ASSIGN_MULTIPLY:
    {
      return bc_num_mul;
    }

    case BC_INST_OP_ASSIGN_DIVIDE:
    {
      return bc_num_div;
    }

    case BC_INST_OP_ASSIGN_MODULUS:
    {
      return bc_num_mod;
    }

    case BC_INST_OP_ASSIGN_PLUS:
    {
      return bc_num_add;
    }

    case BC_INST_OP_ASSIGN_MINUS:
    {
      return bc_num_sub;
    }

    default:
    {
      return NULL;
    }
  }
}

BcStatus bc_program_assignScale(BcProgram *p, BcNum *scale,
                                       BcNum *rval, uint8_t inst)
{
  BcStatus status;
  unsigned long result;

  switch (inst) {

    case BC_INST_OP_ASSIGN_POWER:
    case BC_INST_OP_ASSIGN_MULTIPLY:
    case BC_INST_OP_ASSIGN_DIVIDE:
    case BC_INST_OP_ASSIGN_MODULUS:
    case BC_INST_OP_ASSIGN_PLUS:
    case BC_INST_OP_ASSIGN_MINUS:
    {
      BcNumBinaryFunc op;

      op = bc_program_assignOp(inst);

      status = op(scale, rval, scale, p->scale);

      if (status) return status;

      break;
    }

    case BC_INST_OP_ASSIGN:
    {
      status = bc_num_copy(scale, rval);
      if (status) return status;
      break;
    }

    default:
    {
      return BC_STATUS_EXEC_INVALID_EXPR;
    }
  }

  status = bc_num_ulong(scale, &result);

  if (status) return status;

  p->scale = (size_t) result;

  return status;
}

BcStatus bc_program_assign(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult *left;
  BcResult *right;
  BcResult result;
  BcNum *lval;
  BcNum *rval;

  status = bc_program_binaryOpPrep(p, &left, &lval, &right, &rval);

  if (status) return status;

  if (left->type == BC_RESULT_CONSTANT || left->type == BC_RESULT_INTERMEDIATE)
    return BC_STATUS_EXEC_INVALID_LVALUE;

  if (inst == BC_EXPR_ASSIGN_DIVIDE && !bc_num_compare(rval, &p->zero))
    return BC_STATUS_MATH_DIVIDE_BY_ZERO;

  if (left->type != BC_RESULT_SCALE) {

    if (status) return status;

    switch (inst) {

      case BC_INST_OP_ASSIGN_POWER:
      case BC_INST_OP_ASSIGN_MULTIPLY:
      case BC_INST_OP_ASSIGN_DIVIDE:
      case BC_INST_OP_ASSIGN_MODULUS:
      case BC_INST_OP_ASSIGN_PLUS:
      case BC_INST_OP_ASSIGN_MINUS:
      {
        BcNumBinaryFunc op;

        op = bc_program_assignOp(inst);
        status = op(lval, rval, lval, p->scale);

        break;
      }

      case BC_INST_OP_ASSIGN:
      {
        status = bc_num_copy(lval, rval);
        break;
      }

      default:
      {
        status = BC_STATUS_EXEC_INVALID_EXPR;
        break;
      }
    }

    if (status) return status;

    if (left->type == BC_RESULT_IBASE || left->type == BC_RESULT_OBASE) {

      unsigned long base;
      size_t *ptr;

      ptr = left->type == BC_RESULT_IBASE ? &p->ibase_t : &p->obase_t;

      status = bc_num_ulong(lval, &base);

      if (status) return status;

      *ptr = (size_t) base;
    }
  }
  else {
    status = bc_program_assignScale(p, lval, rval, inst);
    if (status) return status;
  }

  status = bc_num_init(&result.data.num, lval->len);

  if (status) return status;

  status = bc_num_copy(&result.data.num, lval);

  if (status) goto err;

  status = bc_program_binaryOpRetire(p, &result, BC_RESULT_INTERMEDIATE);

  if (status) goto err;

  return status;

err:

  bc_num_free(&result.data.num);

  return status;
}

BcStatus bc_program_call(BcProgram *p, uint8_t *code, size_t *idx) {

  BcStatus status;
  BcInstPtr ip;
  size_t nparams;
  BcFunc *func;
  BcAuto *auto_ptr;
  BcResult *param;
  size_t i;

  nparams = bc_program_index(code, idx);

  ip.idx = 0;
  ip.len = p->expr_stack.len;

  ip.func = bc_program_index(code, idx);

  func = bc_vec_item(&p->funcs, ip.func);

  if (!func) return BC_STATUS_EXEC_UNDEFINED_FUNC;

  if (func->params.len != nparams) return BC_STATUS_EXEC_MISMATCHED_PARAMS;

  for (i = 0; i < func->autos.len; ++i) {

    auto_ptr = bc_vec_item(&func->autos, i);

    if (!auto_ptr) return BC_STATUS_EXEC_UNDEFINED_VAR;

    if (auto_ptr->var) bc_num_zero(&auto_ptr->data.num);
    else {
      status = bc_array_zero(&auto_ptr->data.array);
      if (status) return status;
    }
  }

  for (i = 0; i < func->params.len; ++i) {

    auto_ptr = bc_vec_item_rev(&func->params, i);
    param = bc_vec_item_rev(&p->expr_stack, i);

    if (!auto_ptr || !param) return BC_STATUS_EXEC_UNDEFINED_VAR;

    if (auto_ptr->var) {

      BcNum *num;

      status = bc_program_num(p, param, &num, false);

      if (status) return status;

      status = bc_num_copy(&auto_ptr->data.num, num);
    }
    else {

      BcArray *array;

      if (param->type != BC_RESULT_VAR || param->type != BC_RESULT_ARRAY)
        return BC_STATUS_EXEC_INVALID_TYPE;

      status = bc_program_search(p, param, (BcNum**) &array,
                                 BC_PROGRAM_SEARCH_ARRAY_ONLY);

      if (status) return status;

      status = bc_array_copy(&auto_ptr->data.array, array);
    }

    if (status) return status;
  }

  return bc_vec_push(&p->stack, &ip);
}

BcStatus bc_program_return(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult result;
  BcResult *operand;
  size_t req;
  BcInstPtr *ip;
  BcFunc *func;
  size_t len;

  if (!BC_PROGRAM_CHECK_STACK(p)) return BC_STATUS_EXEC_INVALID_RETURN;

  ip = bc_vec_top(&p->stack);

  if (!ip) return BC_STATUS_EXEC_INVALID_STACK;

  req = ip->len + (inst == BC_INST_RETURN ? 1 : 0);

  if (!BC_PROGRAM_CHECK_EXPR_STACK(p, req))
    return BC_STATUS_EXEC_INVALID_EXPR;

  func = bc_vec_item(&p->funcs, ip->func);

  if (!func) return BC_STATUS_EXEC_INVALID_STMT;

  result.type = BC_RESULT_INTERMEDIATE;

  if (inst == BC_INST_RETURN) {

    BcNum *num;

    operand = bc_vec_top(&p->expr_stack);

    if (!operand) return BC_STATUS_EXEC_INVALID_EXPR;

    status = bc_program_num(p, operand, &num, false);

    if (status) return status;

    status = bc_num_init(&result.data.num, num->len);

    if (status) return status;

    status = bc_num_copy(&result.data.num, num);

    if (status) goto err;
  }
  else {

    status = bc_num_init(&result.data.num, BC_NUM_DEF_SIZE);

    if (status) return status;

    bc_num_zero(&result.data.num);
  }

  // We need to pop arguments as well, so this takes that into account.
  len = ip->len - func->params.len;
  while (p->expr_stack.len > len) {
    status = bc_vec_pop(&p->expr_stack);
    if (status) goto err;
  }

  status = bc_vec_push(&p->expr_stack, &result);

  if (status) goto err;

  return bc_vec_pop(&p->stack);

err:

  bc_num_free(&result.data.num);

  return status;
}

unsigned long bc_program_scale(BcNum *n) {
  return (unsigned long) n->rdx;
}

unsigned long bc_program_length(BcNum *n) {
  return (unsigned long) n->len;
}

BcStatus bc_program_builtin(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult *operand;
  BcNum *num1;
  BcResult result;

  status = bc_program_unaryOpPrep(p, &operand, &num1);

  if (status) return status;

  status = bc_num_init(&result.data.num, BC_NUM_DEF_SIZE);

  if (status) return status;

  if (inst == BC_INST_SQRT) {
    status = bc_num_sqrt(num1, &result.data.num, p->scale);
  }
  else {

    BcProgramBuiltInFunc func;
    unsigned long ans;

    func = inst == BC_INST_LENGTH ? bc_program_length : bc_program_scale;

    ans = func(num1);

    status = bc_num_ulong2num(&result.data.num, ans);
  }

  if (status) goto err;

  status = bc_program_unaryOpRetire(p, &result, BC_RESULT_INTERMEDIATE);

  if (status) goto err;

  return status;

err:

  bc_num_free(&result.data.num);

  return status;
}

BcStatus bc_program_pushScale(BcProgram *p) {

  BcStatus status;
  BcResult result;

  result.type = BC_RESULT_SCALE;
  status = bc_num_init(&result.data.num, BC_NUM_DEF_SIZE);

  if (status) return status;

  status = bc_num_ulong2num(&result.data.num, (unsigned long) p->scale);

  if (status) goto err;

  status = bc_vec_push(&p->expr_stack, &result);

  if (status) goto err;

  return status;

err:

  bc_num_free(&result.data.num);

  return status;
}

BcStatus bc_program_incdec(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult *ptr;
  BcNum *num;
  BcResult copy;
  uint8_t inst2;
  BcResult result;

  status = bc_program_unaryOpPrep(p, &ptr, &num);

  if (status) return status;

  inst2 = inst == BC_INST_INC || inst == BC_INST_INC_DUP ?
            BC_INST_OP_ASSIGN_PLUS : BC_INST_OP_ASSIGN_MINUS;

  if (inst == BC_INST_INC_DUP || inst == BC_INST_DEC_DUP) {
    copy.type = BC_RESULT_INTERMEDIATE;
    status = bc_num_init(&copy.data.num, num->len);
    if (status) return status;
  }

  result.type = BC_RESULT_ONE;

  status = bc_vec_push(&p->expr_stack, &result);

  if (status) goto err;

  status = bc_program_assign(p, inst2);

  if (status) goto err;

  if (inst == BC_INST_INC_DUP || inst == BC_INST_DEC_DUP) {

    status = bc_vec_pop(&p->expr_stack);

    if (status) goto err;

    status = bc_vec_push(&p->expr_stack, &copy);

    if (status) goto err;
  }

  return status;

err:

  if (inst == BC_INST_INC_DUP || inst == BC_INST_DEC_DUP)
    bc_num_free(&copy.data.num);

  return status;
}

BcStatus bc_program_init(BcProgram *p) {

  BcStatus s;
  size_t idx;
  char *name;
  char *read_name;
  BcInstPtr ip;

  if (p == NULL) return BC_STATUS_INVALID_PARAM;

  name = NULL;

#ifdef _POSIX_BC_BASE_MAX
  p->base_max = _POSIX_BC_BASE_MAX;
#elif defined(_BC_BASE_MAX)
  p->base_max = _BC_BASE_MAX;
#else
  errno = 0;
  p->base_max = sysconf(_SC_BC_BASE_MAX);

  if (p->base_max == -1) {
    if (errno) return BC_STATUS_NO_LIMIT;
    p->base_max = BC_BASE_MAX_DEF;
  }
  else if (p->base_max > BC_BASE_MAX_DEF) return BC_STATUS_INVALID_LIMIT;
#endif

#ifdef _POSIX_BC_DIM_MAX
  p->dim_max = _POSIX_BC_DIM_MAX;
#elif defined(_BC_DIM_MAX)
  p->dim_max = _BC_DIM_MAX;
#else
  errno = 0;
  p->dim_max = sysconf(_SC_BC_DIM_MAX);

  if (p->dim_max == -1) {
    if (errno) return BC_STATUS_NO_LIMIT;
    p->dim_max = BC_DIM_MAX_DEF;
  }
  else if (p->dim_max > BC_DIM_MAX_DEF) return BC_STATUS_INVALID_LIMIT;
#endif

#ifdef _POSIX_BC_SCALE_MAX
  p->scale_max = _POSIX_BC_SCALE_MAX;
#elif defined(_BC_SCALE_MAX)
  p->scale_max = _BC_SCALE_MAX;
#else
  errno = 0;
  p->scale_max = sysconf(_SC_BC_SCALE_MAX);

  if (p->scale_max == -1) {
    if (errno) return BC_STATUS_NO_LIMIT;
    p->scale_max = BC_SCALE_MAX_DEF;
  }
  else if (p->scale_max > BC_SCALE_MAX_DEF) return BC_STATUS_INVALID_LIMIT;
#endif

#ifdef _POSIX_BC_STRING_MAX
  p->string_max = _POSIX_BC_STRING_MAX;
#elif defined(_BC_STRING_MAX)
  p->string_max = _BC_STRING_MAX;
#else
  errno = 0;
  p->string_max = sysconf(_SC_BC_STRING_MAX);

  if (p->string_max == -1) {
    if (errno) return BC_STATUS_NO_LIMIT;
    p->string_max = BC_STRING_MAX_DEF;
  }
  else if (p->string_max > BC_STRING_MAX_DEF) return BC_STATUS_INVALID_LIMIT;
#endif

  p->scale = 0;

  s = bc_num_init(&p->ibase, BC_NUM_DEF_SIZE);

  if (s) return s;

  bc_num_ten(&p->ibase);
  p->ibase_t = 10;

  s = bc_num_init(&p->obase, BC_NUM_DEF_SIZE);

  if (s) goto obase_err;

  bc_num_ten(&p->obase);
  p->obase_t = 10;

  s = bc_num_init(&p->last, BC_NUM_DEF_SIZE);

  if (s) goto last_err;

  bc_num_zero(&p->last);

  s = bc_num_init(&p->zero, BC_NUM_DEF_SIZE);

  if (s) goto zero_err;

  bc_num_zero(&p->zero);

  s = bc_num_init(&p->one, BC_NUM_DEF_SIZE);

  if (s) goto one_err;

  bc_num_one(&p->one);

  p->num_buf = malloc(BC_PROGRAM_BUF_SIZE + 1);

  if (!p->num_buf) {
    s = BC_STATUS_MALLOC_FAIL;
    goto num_buf_err;
  }

  p->buf_size = BC_PROGRAM_BUF_SIZE;

  s = bc_vec_init(&p->funcs, sizeof(BcFunc), bc_func_free);

  if (s) goto func_err;

  s = bc_veco_init(&p->func_map, sizeof(BcEntry), bc_entry_free, bc_entry_cmp);

  if (s) goto func_map_err;

  name = malloc(strlen(bc_lang_func_main) + 1);

  if (!name) {
    s = BC_STATUS_MALLOC_FAIL;
    goto name_err;
  }

  strcpy(name, bc_lang_func_main);

  s = bc_program_func_add(p, name, &idx);

  name = NULL;

  if (s || idx != BC_PROGRAM_MAIN_FUNC) goto read_err;

  read_name = malloc(strlen(bc_lang_func_read) + 1);

  if (!read_name) {
    s = BC_STATUS_MALLOC_FAIL;
    goto read_err;
  }

  strcpy(read_name, bc_lang_func_read);

  s = bc_program_func_add(p, read_name, &idx);

  read_name = NULL;

  if (s || idx != BC_PROGRAM_READ_FUNC) goto var_err;

  s = bc_vec_init(&p->vars, sizeof(BcVar), bc_var_free);

  if (s) goto var_err;

  s = bc_veco_init(&p->var_map, sizeof(BcEntry), bc_entry_free, bc_entry_cmp);

  if (s) goto var_map_err;

  s = bc_vec_init(&p->arrays, sizeof(BcArray), bc_array_free);

  if (s) goto array_err;

  s = bc_veco_init(&p->array_map, sizeof(BcEntry), bc_entry_free, bc_entry_cmp);

  if (s) goto array_map_err;

  s = bc_vec_init(&p->strings, sizeof(char*), bc_string_free);

  if (s) goto string_err;

  s = bc_vec_init(&p->constants, sizeof(char*), bc_constant_free);

  if (s) goto const_err;

  s = bc_vec_init(&p->expr_stack, sizeof(BcResult), bc_result_free);

  if (s) goto expr_err;

  s = bc_vec_init(&p->stack, sizeof(BcInstPtr), NULL);

  if (s) goto stack_err;

  ip.idx = 0;
  ip.func = 0;
  ip.len = 0;

  s = bc_vec_push(&p->stack, &ip);

  if (s) goto push_err;

  return s;

push_err:

  bc_vec_free(&p->stack);

stack_err:

  bc_vec_free(&p->expr_stack);

expr_err:

  bc_vec_free(&p->constants);

const_err:

  bc_vec_free(&p->strings);

string_err:

  bc_veco_free(&p->array_map);

array_map_err:

  bc_vec_free(&p->arrays);

array_err:

  bc_veco_free(&p->var_map);

var_map_err:

  bc_vec_free(&p->vars);

var_err:

  if (read_name) free(read_name);

read_err:

  if (name) free(name);

name_err:

  bc_veco_free(&p->func_map);

func_map_err:

  bc_vec_free(&p->funcs);

func_err:

  free(p->num_buf);

num_buf_err:

  bc_num_free(&p->one);

one_err:

  bc_num_free(&p->zero);

zero_err:

  bc_num_free(&p->last);

last_err:

  bc_num_free(&p->obase);

obase_err:

  bc_num_free(&p->ibase);

  return s;
}

void bc_program_limits(BcProgram *p) {

  putchar('\n');

  printf("BC_BASE_MAX     = %ld\n", p->base_max);
  printf("BC_DIM_MAX      = %ld\n", p->dim_max);
  printf("BC_SCALE_MAX    = %ld\n", p->scale_max);
  printf("BC_STRING_MAX   = %ld\n", p->string_max);
  printf("Max Exponent    = %ld\n", LONG_MAX);
  printf("Number of Vars  = %u\n", UINT32_MAX);

  putchar('\n');
}

BcStatus bc_program_func_add(BcProgram *p, char *name, size_t *idx) {

  BcStatus status;
  BcEntry entry;
  BcEntry *entry_ptr;
  BcFunc f;

  if (!p || !name || !idx) return BC_STATUS_INVALID_PARAM;

  entry.name = name;
  entry.idx = p->funcs.len;

  status = bc_veco_insert(&p->func_map, &entry, idx);

  if (status) {
    free(name);
    if (status != BC_STATUS_VECO_ITEM_EXISTS) return status;
  }

  entry_ptr = bc_veco_item(&p->func_map, *idx);

  if (!entry_ptr) return BC_STATUS_EXEC_UNDEFINED_FUNC;

  *idx = entry_ptr->idx;

  if (status == BC_STATUS_VECO_ITEM_EXISTS) {

    BcFunc *func;

    func = bc_vec_item(&p->funcs, entry_ptr->idx);

    if (!func) return BC_STATUS_EXEC_UNDEFINED_FUNC;

    status = BC_STATUS_SUCCESS;

    // We need to reset these so the function can be repopulated.
    while (!status && func->autos.len) status = bc_vec_pop(&func->autos);
    while (!status && func->params.len) status = bc_vec_pop(&func->params);
  }
  else {

    status = bc_func_init(&f);

    if (status) return status;

    status = bc_vec_push(&p->funcs, &f);
  }

  return status;
}

BcStatus bc_program_var_add(BcProgram *p, char *name, size_t *idx) {

  BcStatus status;
  BcEntry entry;
  BcVar v;

  if (!p || !name || !idx) return BC_STATUS_INVALID_PARAM;

  entry.name = name;
  entry.idx = p->vars.len;

  status = bc_veco_insert(&p->var_map, &entry, idx);

  if (status) return status == BC_STATUS_VECO_ITEM_EXISTS ?
                               BC_STATUS_SUCCESS : status;

  status = bc_var_init(&v);

  if (status) return status;

  return bc_vec_push(&p->vars, &v);
}

BcStatus bc_program_array_add(BcProgram *p, char *name, size_t *idx) {

  BcStatus status;
  BcEntry entry;
  BcArray a;

  if (!p || !name || !idx) return BC_STATUS_INVALID_PARAM;

  entry.name = name;
  entry.idx = p->arrays.len;

  status = bc_veco_insert(&p->array_map, &entry, idx);

  if (status) return status == BC_STATUS_VECO_ITEM_EXISTS ?
                               BC_STATUS_SUCCESS : status;

  status = bc_array_init(&a);

  if (status) return status;

  return bc_vec_push(&p->arrays, &a);
}

BcStatus bc_program_exec(BcProgram *p) {

  BcStatus status;
  uint8_t *code;
  size_t idx;
  int pchars;
  BcResult result;
  BcFunc *func;
  BcInstPtr *ip;
  bool cond;

  ip = bc_vec_top(&p->stack);

  if (!ip) return BC_STATUS_EXEC_INVALID_STACK;

  func = bc_vec_item(&p->funcs, ip->func);

  if (!func) return BC_STATUS_EXEC_INVALID_STACK;

  status = BC_STATUS_SUCCESS;

  code = func->code.array;
  cond = false;

  while (ip->idx < func->code.len) {

    uint8_t inst;

    inst = code[(ip->idx)++];

    switch (inst) {

      case BC_INST_CALL:
      {
        status = bc_program_call(p, code, &ip->idx);
        break;
      }

      case BC_INST_RETURN:
      case BC_INST_RETURN_ZERO:
      {
        status = bc_program_return(p, inst);
        break;
      }

      case BC_INST_READ:
      {
        status = bc_program_read(p);
        break;
      }

      case BC_INST_JUMP_NOT_ZERO:
      case BC_INST_JUMP_ZERO:
      {
        BcResult *operand;
        BcNum *num;

        status = bc_program_unaryOpPrep(p, &operand, &num);

        if (status) return status;

        cond = bc_num_compare(num, &p->zero) == 0;

        status = bc_vec_pop(&p->expr_stack);

        if (status) return status;

        // Fallthrough.
      }
      case BC_INST_JUMP:
      {
        size_t idx;
        size_t *addr;

        idx = bc_program_index(code, &ip->idx);
        addr = bc_vec_item(&func->labels, idx);

        if (!addr) return BC_STATUS_EXEC_INVALID_LABEL;

        if (inst == BC_INST_JUMP ||
            (inst == BC_INST_JUMP_ZERO && cond) ||
            (inst == BC_INST_JUMP_NOT_ZERO && !cond))
        {
          ip->idx = *addr;
        }

        break;
      }

      case BC_INST_PUSH_VAR:
      case BC_INST_PUSH_ARRAY:
      {
        status = bc_program_push(p, code, &ip->idx, inst == BC_INST_PUSH_VAR);
        break;
      }

      case BC_INST_PUSH_LAST:
      {
        result.type = BC_RESULT_LAST;
        status = bc_vec_push(&p->expr_stack, &result);
        break;
      }

      case BC_INST_PUSH_SCALE:
      {
        status = bc_program_pushScale(p);
        break;
      }

      case BC_INST_PUSH_IBASE:
      {
        result.type = BC_RESULT_IBASE;
        status = bc_vec_push(&p->expr_stack, &result);
        break;
      }

      case BC_INST_PUSH_OBASE:
      {
        result.type = BC_RESULT_OBASE;
        status = bc_vec_push(&p->expr_stack, &result);
        break;
      }

      case BC_INST_SCALE_FUNC:
      case BC_INST_LENGTH:
      case BC_INST_SQRT:
      {
        status = bc_program_builtin(p, inst);
        break;
      }

      case BC_INST_PUSH_NUM:
      {
        BcResult result;

        result.type = BC_RESULT_CONSTANT;
        result.data.id.idx = bc_program_index(code, &ip->idx);

        status = bc_vec_push(&p->expr_stack, &result);

        break;
      }

      case BC_INST_POP:
      {
        status = bc_vec_pop(&p->expr_stack);
        break;
      }

      case BC_INST_INC_DUP:
      case BC_INST_DEC_DUP:
      case BC_INST_INC:
      case BC_INST_DEC:
      {
        status = bc_program_incdec(p, inst);
        break;
      }

      case BC_INST_HALT:
      {
        status = BC_STATUS_EXEC_HALT;
        break;
      }

      case BC_INST_PRINT:
      case BC_INST_PRINT_EXPR:
      {
        BcResult *operand;
        BcNum *num;

        status = bc_program_unaryOpPrep(p, &operand, &num);

        if (status) return status;

        status = bc_num_print(num, &p->obase, p->obase_t,
                              inst == BC_INST_PRINT);

        fflush(stdout);

        if (status) return status;

        status = bc_num_copy(&p->last, num);

        if (status) return status;

        status = bc_vec_pop(&p->expr_stack);

        break;
      }

      case BC_INST_STR:
      {
        const char *string;

        idx = bc_program_index(code, &ip->idx);

        if (idx >= p->strings.len) return BC_STATUS_EXEC_INVALID_STRING;

        string = bc_vec_item(&p->strings, idx);

        pchars = fprintf(stdout, "%s", string);
        status = pchars > 0 ? BC_STATUS_SUCCESS :
                              BC_STATUS_EXEC_PRINT_ERR;

        break;
      }

      case BC_INST_PRINT_STR:
      {
        const char **string;

        idx = bc_program_index(code, &ip->idx);

        if (idx >= p->strings.len) return BC_STATUS_EXEC_INVALID_STRING;

        string = bc_vec_item(&p->strings, idx);

        if (!string) return BC_STATUS_EXEC_INVALID_STRING;

        status = bc_program_printString(*string);

        break;
      }

      case BC_INST_OP_POWER:
      case BC_INST_OP_MULTIPLY:
      case BC_INST_OP_DIVIDE:
      case BC_INST_OP_MODULUS:
      case BC_INST_OP_PLUS:
      case BC_INST_OP_MINUS:
      {
        status = bc_program_op(p, inst);
        break;
      }

      case BC_INST_OP_REL_EQUAL:
      case BC_INST_OP_REL_LESS_EQ:
      case BC_INST_OP_REL_GREATER_EQ:
      case BC_INST_OP_REL_NOT_EQ:
      case BC_INST_OP_REL_LESS:
      case BC_INST_OP_REL_GREATER:
      {
        status = bc_program_logical(p, inst);
        break;
      }

      case BC_INST_OP_BOOL_NOT:
      {
        BcResult *ptr;
        BcNum *num;

        status = bc_program_unaryOpPrep(p, &ptr, &num);

        if (status) return status;

        status = bc_num_init(&result.data.num, BC_NUM_DEF_SIZE);

        if (status) return status;

        if (bc_num_compare(num, &p->zero)) bc_num_one(&result.data.num);
        else bc_num_zero(&result.data.num);

        status = bc_program_unaryOpRetire(p, &result, BC_RESULT_INTERMEDIATE);

        if (status) bc_num_free(&result.data.num);

        break;
      }

      case BC_INST_OP_BOOL_OR:
      case BC_INST_OP_BOOL_AND:
      {
        status = bc_program_logical(p, inst);
        break;
      }

      case BC_INST_OP_NEGATE:
      {
        status = bc_program_negate(p);
        break;
      }

      case BC_INST_OP_ASSIGN_POWER:
      case BC_INST_OP_ASSIGN_MULTIPLY:
      case BC_INST_OP_ASSIGN_DIVIDE:
      case BC_INST_OP_ASSIGN_MODULUS:
      case BC_INST_OP_ASSIGN_PLUS:
      case BC_INST_OP_ASSIGN_MINUS:
      case BC_INST_OP_ASSIGN:
      {
        status = bc_program_assign(p, inst);
        break;
      }

      default:
      {
        status = BC_STATUS_EXEC_INVALID_STMT;
        break;
      }
    }

    if (status) return status;

    // We keep getting these because if the size of the
    // stack changes, pointers may end up being invalid.
    ip = bc_vec_top(&p->stack);

    if (!ip) return BC_STATUS_EXEC_INVALID_STACK;

    func = bc_vec_item(&p->funcs, ip->func);

    if (!func) return BC_STATUS_EXEC_INVALID_STACK;

    code = func->code.array;
  }

  return status;
}

BcStatus bc_program_print(BcProgram *p) {

  BcStatus status;
  BcFunc *func;
  uint8_t *code;
  BcInstPtr ip;
  size_t i;

  status = BC_STATUS_SUCCESS;

  for (i = 0; !status && i < p->funcs.len; ++i) {

    ip.idx = 0;
    ip.func = i;
    ip.len = 0;

    func = bc_vec_item(&p->funcs, ip.func);

    if (!func) return BC_STATUS_EXEC_INVALID_STACK;

    code = func->code.array;

    if (printf("func[%zu]: ", ip.func) < 0) return BC_STATUS_IO_ERR;

    while (ip.idx < func->code.len) {

      uint8_t inst;

      inst = code[ip.idx++];

      switch (inst) {

        case BC_INST_PUSH_VAR:
        case BC_INST_PUSH_ARRAY:
        {
          if (putchar(inst) == EOF) return BC_STATUS_IO_ERR;
          status = bc_program_printName(code, &ip.idx);
          break;
        }

        case BC_INST_CALL:
        {
          if (putchar(inst) == EOF) return BC_STATUS_IO_ERR;

          status = bc_program_printIndex(code, &ip.idx);

          if (status) return status;

          status = bc_program_printIndex(code, &ip.idx);

          break;
        }

        case BC_INST_JUMP:
        case BC_INST_JUMP_NOT_ZERO:
        case BC_INST_JUMP_ZERO:
        case BC_INST_PUSH_NUM:
        case BC_INST_STR:
        case BC_INST_PRINT_STR:
        {
          if (putchar(inst) == EOF) return BC_STATUS_IO_ERR;
          bc_program_printIndex(code, &ip.idx);
          break;
        }

        default:
        {
          if (putchar(inst) == EOF) return BC_STATUS_IO_ERR;
          break;
        }
      }
    }

    if (status) return status;

    if (putchar('\n') == EOF) status = BC_STATUS_IO_ERR;
  }

  return status;
}

void bc_program_free(BcProgram *p) {

  if (p == NULL) return;

  free(p->num_buf);

  bc_num_free(&p->ibase);
  bc_num_free(&p->obase);

  bc_vec_free(&p->funcs);
  bc_veco_free(&p->func_map);

  bc_vec_free(&p->vars);
  bc_veco_free(&p->var_map);

  bc_vec_free(&p->arrays);
  bc_veco_free(&p->array_map);

  bc_vec_free(&p->strings);
  bc_vec_free(&p->constants);

  bc_vec_free(&p->expr_stack);
  bc_vec_free(&p->stack);

  bc_num_free(&p->last);
  bc_num_free(&p->zero);
  bc_num_free(&p->one);

  memset(p, 0, sizeof(BcProgram));
}

void bc_vm_sigint(int sig) {

  struct sigaction act;
  ssize_t err;

  sigemptyset(&act.sa_mask);
  act.sa_handler = bc_vm_sigint;

  sigaction(SIGINT, &act, NULL);

  if (sig == SIGINT) {
    err = write(STDERR_FILENO, bc_program_sigint_msg,
                strlen(bc_program_sigint_msg));
    if (err >= 0) TT.bc_signal = 1;
  }
}

BcStatus bc_vm_signal(BcVm *vm) {

  BcStatus status;
  BcFunc *func;
  BcInstPtr *ip;

  TT.bc_signal = 0;

  while (vm->program.stack.len > 1) {

    status = bc_vec_pop(&vm->program.stack);

    if (status) return status;
  }

  func = bc_vec_item(&vm->program.funcs, 0);

  if (!func) return BC_STATUS_EXEC_UNDEFINED_FUNC;

  ip = bc_vec_top(&vm->program.stack);

  if (!ip) return BC_STATUS_EXEC_INVALID_STMT;

  ip->idx = func->code.len;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_vm_execFile(BcVm *vm, int idx) {

  BcStatus status;
  const char *file;
  char *data;
  BcProgramExecFunc exec;

  exec = TT.bc_code ? bc_program_print : bc_program_exec;

  file = vm->filev[idx];
  vm->program.file = file;

  status = bc_io_fread(file, &data);

  if (status) return status;

  status = bc_parse_file(&vm->parse, file);

  if (status) goto read_err;

  status = bc_parse_text(&vm->parse, data);

  if (status) goto read_err;

  do {

    status = bc_parse_parse(&vm->parse);

    if (status && status != BC_STATUS_LEX_EOF) {
      bc_error_file(status, vm->parse.lex.file, vm->parse.lex.line);
      goto err;
    }

    if (TT.bc_signal) {
      if (!TT.bc_interactive) goto read_err;
      else {
        status = bc_vm_signal(vm);
        if (status) goto read_err;
      }
    }

    if (status) {

      if (status != BC_STATUS_LEX_EOF &&
          status != BC_STATUS_PARSE_QUIT &&
          status != BC_STATUS_PARSE_LIMITS)
      {
        bc_error_file(status, vm->program.file, vm->parse.lex.line);
        goto err;
      }
      else if (status == BC_STATUS_PARSE_QUIT) {
        break;
      }
      else if (status == BC_STATUS_PARSE_LIMITS) {
        bc_program_limits(&vm->program);
        status = BC_STATUS_SUCCESS;
        continue;
      }

      while (!status && vm->parse.token.type != BC_LEX_NEWLINE &&
             vm->parse.token.type != BC_LEX_SEMICOLON)
      {
        status = bc_lex_next(&vm->parse.lex, &vm->parse.token);
      }
    }

  } while (!status);

  if (status != BC_STATUS_LEX_EOF && status != BC_STATUS_PARSE_QUIT)
    goto read_err;

  if (BC_PARSE_CAN_EXEC(&vm->parse)) {

    status = exec(&vm->program);

    if (status) goto read_err;

    if (TT.bc_interactive) {

      fflush(stdout);

      if (TT.bc_signal) {

        status = bc_vm_signal(vm);

        fprintf(stderr, "%s", bc_program_ready_prompt);
        fflush(stderr);
      }
    }
    else if (TT.bc_signal) {
      status = bc_vm_signal(vm);
      goto read_err;
    }
  }
  else status = BC_STATUS_EXEC_FILE_NOT_EXECUTABLE;

read_err:

  bc_error(status);

err:

  free(data);

  return status;
}

BcStatus bc_vm_execStdin(BcVm *vm) {

  BcStatus status;
  char *buf;
  char *buffer;
  char *temp;
  size_t n;
  size_t bufn;
  size_t slen;
  size_t total_len;
  bool string;
  bool comment;

  vm->program.file = bc_program_stdin_name;

  status = bc_parse_file(&vm->parse, bc_program_stdin_name);

  if (status) return status;

  n = BC_VM_BUF_SIZE;
  bufn = BC_VM_BUF_SIZE;
  buffer = malloc(BC_VM_BUF_SIZE + 1);

  if (!buffer) return BC_STATUS_MALLOC_FAIL;

  buffer[0] = '\0';

  buf = malloc(BC_VM_BUF_SIZE + 1);

  if (!buf) {
    status = BC_STATUS_MALLOC_FAIL;
    goto buf_err;
  }

  string = false;
  comment = false;

  // The following loop is complicated because the vm tries
  // not to send any lines that end with a backslash to the
  // parser. The reason for that is because the parser treats
  // a backslash newline combo as whitespace, per the bc spec.
  // Thus, the parser will expect more stuff. That is also
  // the case with strings and comments.
  while ((!status || status != BC_STATUS_PARSE_QUIT) &&
         !(status = bc_io_getline(&buf, &bufn)))
  {
    size_t len, i;

    len = strlen(buf);
    slen = strlen(buffer);
    total_len = slen + len;

    if (len == 1 && buf[0] == '"') string = !string;
    else if (len > 1 || comment) {

      for (i = 0; i < len; ++i) {

        char c;
        bool notend;

        notend = len > i + 1;

        c = buf[i];

        if (c == '"') string = !string;
        else if (c == '/' && notend && !comment && buf[i + 1] == '*') {
          comment = true;
          break;
        }
        else if (c == '*' && notend && comment && buf[i + 1] == '/') {
          comment = false;
        }
      }

      if (string || comment || buf[len - 2] == '\\') {

        if (total_len > n) {

          temp = realloc(buffer, total_len + 1);

          if (!temp) {
            status = BC_STATUS_MALLOC_FAIL;
            goto exit_err;
          }

          buffer = temp;
          n = slen + len;
        }

        strcat(buffer, buf);

        continue;
      }
    }

    if (total_len > n) {

      temp = realloc(buffer, total_len + 1);

      if (!temp) {
        status = BC_STATUS_MALLOC_FAIL;
        goto exit_err;
      }

      buffer = temp;
      n = slen + len;
    }

    strcat(buffer, buf);

    status = bc_parse_text(&vm->parse, buffer);

    if (!TT.bc_signal) {

      if (status) {

        if (status == BC_STATUS_PARSE_QUIT || status == BC_STATUS_LEX_EOF) {
          break;
        }
        else if (status == BC_STATUS_PARSE_LIMITS) {
          bc_program_limits(&vm->program);
          status = BC_STATUS_SUCCESS;
        }
        else {
          bc_error(status);
          goto exit_err;
        }
      }
    }
    else if (status == BC_STATUS_PARSE_QUIT) {
      break;
    }
    else if (status == BC_STATUS_PARSE_LIMITS) {
      bc_program_limits(&vm->program);
      status = BC_STATUS_SUCCESS;
    }

    while (!status) status = bc_parse_parse(&vm->parse);

    if (status == BC_STATUS_PARSE_QUIT) break;
    else if (status == BC_STATUS_PARSE_LIMITS) {
      bc_program_limits(&vm->program);
      status = BC_STATUS_SUCCESS;
    }
    else if (status != BC_STATUS_LEX_EOF) {

      BcFunc *func;
      BcInstPtr *ip;

      bc_error_file(status, vm->program.file, vm->parse.lex.line);

      ip = bc_vec_item(&vm->program.stack, 0);
      func = bc_vec_item(&vm->program.funcs, 0);

      if (ip && func) ip->idx = func->code.len;

      while (vm->parse.token.type != BC_LEX_NEWLINE &&
             vm->parse.token.type != BC_LEX_SEMICOLON)
      {
        status = bc_lex_next(&vm->parse.lex, &vm->parse.token);

        if (status && status != BC_STATUS_LEX_EOF) {

          bc_error_file(status, vm->program.file, vm->parse.lex.line);

          ip = bc_vec_item(&vm->program.stack, 0);
          func = bc_vec_item(&vm->program.funcs, 0);

          if (ip && func) ip->idx = func->code.len;

          break;
        }
        else if (status == BC_STATUS_LEX_EOF) {
          status = BC_STATUS_SUCCESS;
          break;
        }
      }
    }

    if (BC_PARSE_CAN_EXEC(&vm->parse)) {

      if (!TT.bc_code) {

        status = bc_program_exec(&vm->program);

        if (status) {
          bc_error(status);
          goto exit_err;
        }

        if (TT.bc_interactive) {

          fflush(stdout);

          if (TT.bc_signal) {
            status = bc_vm_signal(vm);
            fprintf(stderr, "%s", bc_program_ready_prompt);
          }
        }
        else if (TT.bc_signal) {
          status = bc_vm_signal(vm);
          goto exit_err;
        }
      }
      else {

        bc_program_print(&vm->program);

        if (TT.bc_interactive) {

          fflush(stdout);

          if (TT.bc_signal) {
            status = bc_vm_signal(vm);
            fprintf(stderr, "%s", bc_program_ready_prompt);
          }
        }
        else if (TT.bc_signal) {
          status = bc_vm_signal(vm);
          goto exit_err;
        }
      }
    }

    buffer[0] = '\0';
  }

  status = !status || status == BC_STATUS_PARSE_QUIT ||
           status == BC_STATUS_EXEC_HALT ||
           status == BC_STATUS_LEX_EOF ?
               BC_STATUS_SUCCESS : status;

exit_err:

  free(buf);

buf_err:

  free(buffer);

  return status;
}

BcStatus bc_vm_init(BcVm *vm, int filec, char *filev[]) {

  BcStatus status;
  struct sigaction act;

  sigemptyset(&act.sa_mask);
  act.sa_handler = bc_vm_sigint;

  if (sigaction(SIGINT, &act, NULL) < 0) return BC_STATUS_EXEC_SIGACTION_FAIL;

  status = bc_program_init(&vm->program);

  if (status) return status;

  status = bc_parse_init(&vm->parse, &vm->program);

  if (status) {
    bc_program_free(&vm->program);
    return status;
  }

  vm->filec = filec;
  vm->filev = filev;

  return BC_STATUS_SUCCESS;
}

void bc_vm_free(BcVm *vm) {
  bc_parse_free(&vm->parse);
  bc_program_free(&vm->program);
}

BcStatus bc_vm_exec(BcVm *vm) {

  BcStatus status;
  int num_files, i;

  status = BC_STATUS_SUCCESS;

  num_files = vm->filec;

  for (i = 0; !status && i < num_files; ++i) status = bc_vm_execFile(vm, i);

  if (status != BC_STATUS_SUCCESS &&
      status != BC_STATUS_PARSE_QUIT &&
      status != BC_STATUS_EXEC_HALT)
  {
    return status;
  }

  status = bc_vm_execStdin(vm);

  status = status == BC_STATUS_PARSE_QUIT ||
           status == BC_STATUS_EXEC_HALT ?
               BC_STATUS_SUCCESS : status;

  return status;
}

BcStatus bc_print_version() {

  int err;

  err = printf(bc_version_fmt, bc_version, bc_copyright, bc_warranty_short);

  return err < 0 ? BC_STATUS_IO_ERR : BC_STATUS_SUCCESS;
}

void bc_error(BcStatus status) {

  if (!status || status == BC_STATUS_PARSE_QUIT ||
      status == BC_STATUS_EXEC_HALT ||
      status >= BC_STATUS_POSIX_NAME_LEN)
  {
    return;
  }

  fprintf(stderr, "\n%s error: %s\n\n",
          bc_err_types[status], bc_err_descs[status]);
}

void bc_error_file(BcStatus status, const char *file, uint32_t line) {

  if (!status || status == BC_STATUS_PARSE_QUIT ||
      !file || status >= BC_STATUS_POSIX_NAME_LEN)
  {
    return;
  }

  fprintf(stderr, "\n%s error: %s\n", bc_err_types[status],
          bc_err_descs[status]);

  fprintf(stderr, "    %s", file);
  fprintf(stderr, &":%d\n\n"[3 * !line], line);
}

BcStatus bc_posix_error(BcStatus status, const char *file,
                        uint32_t line, const char *msg)
{
  if (!(TT.bc_std || TT.bc_warn) ||
      status < BC_STATUS_POSIX_NAME_LEN ||
      !file)
  {
    return BC_STATUS_SUCCESS;
  }

  fprintf(stderr, "\n%s %s: %s\n", bc_err_types[status],
          TT.bc_std ? "error" : "warning", bc_err_descs[status]);

  if (msg) fprintf(stderr, "    %s\n", msg);

  fprintf(stderr, "    %s", file);
  fprintf(stderr, &":%d\n\n"[3 * !line], line);

  return TT.bc_std ? status : BC_STATUS_SUCCESS;
}

BcStatus bc_exec(unsigned int flags, unsigned int filec, char *filev[]) {

  BcStatus status;
  BcVm vm;

  if ((flags & FLAG_i) || (isatty(0) && isatty(1))) {
    TT.bc_interactive = 1;
  } else TT.bc_interactive = 0;

  TT.bc_code = flags & FLAG_c;
  TT.bc_std = flags & FLAG_s;
  TT.bc_warn = flags & FLAG_w;

  if (!(flags & FLAG_q)) {

    status = bc_print_version();

    if (status) return status;
  }

  status = bc_vm_init(&vm, filec, filev);

  if (status) return status;

  if (flags & FLAG_l) {

    status = bc_parse_file(&vm.parse, bc_lib_name);

    if (status) goto err;

    status = bc_parse_text(&vm.parse, (const char*) bc_lib);

    if (status) goto err;

    while (!status) status = bc_parse_parse(&vm.parse);

    if (status != BC_STATUS_LEX_EOF) goto err;

    // Make sure to execute the math library.
    status = bc_program_exec(&vm.program);

    if (status) goto err;
  }

  status = bc_vm_exec(&vm);

err:

  bc_vm_free(&vm);

  return status;
}

void bc_main(void) {

  unsigned int flags;

  flags = (unsigned int) toys.optflags;

  toys.exitval = (char) bc_exec(flags, toys.optc, (const char**) toys.optargs);
}
