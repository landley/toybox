/* bc.c - An implementation of POSIX bc.
 *
 * Copyright 2018 Gavin D. Howard <yzena.tech@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/bc.html

USE_BC(NEWTOY(bc, "i(interactive)l(mathlib)q(quiet)s(standard)w(warn)", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_LOCALE))

config BC
  bool "bc"
  default n
  help
    usage: bc [-ilqsw] [file ...]

    bc is a command-line calculator with a Turing-complete language.

    options:

      -i  --interactive  force interactive mode
      -l  --mathlib      use predefined math routines:

                         s(expr)  =  sine of expr in radians
                         c(expr)  =  cosine of expr in radians
                         a(expr)  =  arctangent of expr, returning radians
                         l(expr)  =  natural log of expr
                         e(expr)  =  raises e to the power of expr
                         j(n, x)  =  Bessel function of integer order n of x

      -q  --quiet        don't print version and copyright
      -s  --standard     error if any non-POSIX extensions are used
      -w  --warn         warn if any non-POSIX extensions are used

*/

#define FOR_bc
#include "toys.h"

GLOBALS(
  long tty;

  unsigned long sig;
  unsigned long sigc;
  unsigned long signe;
  long sig_other;
)

typedef enum BcStatus {

  BC_STATUS_SUCCESS,

  BC_STATUS_MALLOC_FAIL,
  BC_STATUS_IO_ERR,
  BC_STATUS_BINARY_FILE,

  BC_STATUS_LEX_BAD_CHARACTER,
  BC_STATUS_LEX_NO_STRING_END,
  BC_STATUS_LEX_NO_COMMENT_END,
  BC_STATUS_LEX_EOF,

  BC_STATUS_PARSE_BAD_TOKEN,
  BC_STATUS_PARSE_BAD_EXPR,
  BC_STATUS_PARSE_BAD_PRINT,
  BC_STATUS_PARSE_BAD_FUNC,
  BC_STATUS_PARSE_BAD_ASSIGN,
  BC_STATUS_PARSE_NO_AUTO,
  BC_STATUS_PARSE_DUPLICATE_LOCAL,

  BC_STATUS_MATH_NEGATIVE,
  BC_STATUS_MATH_NON_INTEGER,
  BC_STATUS_MATH_OVERFLOW,
  BC_STATUS_MATH_DIVIDE_BY_ZERO,
  BC_STATUS_MATH_NEG_SQRT,
  BC_STATUS_MATH_BAD_STRING,

  BC_STATUS_EXEC_FILE_ERR,
  BC_STATUS_EXEC_MISMATCHED_PARAMS,
  BC_STATUS_EXEC_UNDEFINED_FUNC,
  BC_STATUS_EXEC_FILE_NOT_EXECUTABLE,
  BC_STATUS_EXEC_SIGACTION_FAIL,
  BC_STATUS_EXEC_BAD_SCALE,
  BC_STATUS_EXEC_BAD_IBASE,
  BC_STATUS_EXEC_BAD_OBASE,
  BC_STATUS_EXEC_STRING_LEN,
  BC_STATUS_EXEC_ARRAY_LEN,
  BC_STATUS_EXEC_BAD_READ_EXPR,
  BC_STATUS_EXEC_NESTED_READ,
  BC_STATUS_EXEC_BAD_TYPE,
  BC_STATUS_EXEC_SIGNAL,

  BC_STATUS_POSIX_NAME_LEN,
  BC_STATUS_POSIX_SCRIPT_COMMENT,
  BC_STATUS_POSIX_BAD_KEYWORD,
  BC_STATUS_POSIX_DOT_LAST,
  BC_STATUS_POSIX_RETURN_PARENS,
  BC_STATUS_POSIX_BOOL_OPS,
  BC_STATUS_POSIX_REL_OUTSIDE,
  BC_STATUS_POSIX_MULTIPLE_REL,
  BC_STATUS_POSIX_NO_FOR_INIT,
  BC_STATUS_POSIX_NO_FOR_COND,
  BC_STATUS_POSIX_NO_FOR_UPDATE,
  BC_STATUS_POSIX_HEADER_BRACE,

  BC_STATUS_VEC_OUT_OF_BOUNDS,
  BC_STATUS_VEC_ITEM_EXISTS,

  BC_STATUS_QUIT,
  BC_STATUS_LIMITS,

} BcStatus;

#define BC_ERR_IDX_BC (0)
#define BC_ERR_IDX_LEX (1)
#define BC_ERR_IDX_PARSE (2)
#define BC_ERR_IDX_MATH (3)
#define BC_ERR_IDX_EXEC (4)
#define BC_ERR_IDX_POSIX (5)

#define BC_VEC_INITIAL_CAP (32)

typedef void (*BcVecFreeFunc)(void*);
typedef int (*BcVecCmpFunc)(void*, void*);

typedef struct BcVec {

  uint8_t *array;
  size_t len;
  size_t cap;
  size_t size;

  BcVecFreeFunc dtor;

} BcVec;

typedef struct BcVecO {

  BcVec vec;
  BcVecCmpFunc cmp;

} BcVecO;

#define bc_veco_item(v, idx) (bc_vec_item(&(v)->vec, (idx)))
#define bc_veco_free(v) (bc_vec_free(&(v)->vec))

typedef signed char BcDigit;

typedef struct BcNum {

  BcDigit *num;
  size_t rdx;
  size_t len;
  size_t cap;
  int neg;

} BcNum;

#define BC_NUM_MIN_BASE (2)
#define BC_NUM_MAX_INPUT_BASE (16)
#define BC_NUM_DEF_SIZE (16)

#define BC_NUM_PRINT_WIDTH (69)

#define BC_NUM_ONE(n) ((n)->len == 1 && (n)->rdx == 0 && (n)->num[0] == 1)

typedef BcStatus (*BcNumUnaryFunc)(BcNum*, BcNum*, size_t);
typedef BcStatus (*BcNumBinaryFunc)(BcNum*, BcNum*, BcNum*, size_t);
typedef BcStatus (*BcNumDigitFunc)(size_t, size_t, int, size_t*, size_t);

BcStatus bc_num_init(BcNum *n, size_t request);
BcStatus bc_num_expand(BcNum *n, size_t request);
BcStatus bc_num_copy(BcNum *d, BcNum *s);
void bc_num_free(void *num);

BcStatus bc_num_ulong(BcNum *n, unsigned long *result);
BcStatus bc_num_ulong2num(BcNum *n, unsigned long val);

BcStatus bc_num_add(BcNum *a, BcNum *b, BcNum *result, size_t scale);
BcStatus bc_num_sub(BcNum *a, BcNum *b, BcNum *result, size_t scale);
BcStatus bc_num_mul(BcNum *a, BcNum *b, BcNum *result, size_t scale);
BcStatus bc_num_div(BcNum *a, BcNum *b, BcNum *result, size_t scale);
BcStatus bc_num_mod(BcNum *a, BcNum *b, BcNum *result, size_t scale);
BcStatus bc_num_pow(BcNum *a, BcNum *b, BcNum *result, size_t scale);

ssize_t bc_num_cmp(BcNum *a, BcNum *b);

void bc_num_zero(BcNum *n);
void bc_num_one(BcNum *n);
void bc_num_ten(BcNum *n);

typedef enum BcInst {

  BC_INST_INC_PRE,
  BC_INST_DEC_PRE,
  BC_INST_INC_POST,
  BC_INST_DEC_POST,

  BC_INST_NEG,

  BC_INST_POWER,
  BC_INST_MULTIPLY,
  BC_INST_DIVIDE,
  BC_INST_MODULUS,
  BC_INST_PLUS,
  BC_INST_MINUS,

  BC_INST_REL_EQ,
  BC_INST_REL_LE,
  BC_INST_REL_GE,
  BC_INST_REL_NE,
  BC_INST_REL_LT,
  BC_INST_REL_GT,

  BC_INST_BOOL_NOT,
  BC_INST_BOOL_OR,
  BC_INST_BOOL_AND,

  BC_INST_ASSIGN_POWER,
  BC_INST_ASSIGN_MULTIPLY,
  BC_INST_ASSIGN_DIVIDE,
  BC_INST_ASSIGN_MODULUS,
  BC_INST_ASSIGN_PLUS,
  BC_INST_ASSIGN_MINUS,
  BC_INST_ASSIGN,

  BC_INST_PUSH_NUM,
  BC_INST_PUSH_VAR,
  BC_INST_PUSH_ARRAY_ELEM,

  BC_INST_CALL,

  BC_INST_SCALE_FUNC,
  BC_INST_PUSH_IBASE,
  BC_INST_PUSH_SCALE,
  BC_INST_PUSH_LAST,
  BC_INST_LENGTH,
  BC_INST_READ,
  BC_INST_PUSH_OBASE,
  BC_INST_SQRT,

  BC_INST_PRINT,
  BC_INST_PRINT_EXPR,
  BC_INST_STR,
  BC_INST_PRINT_STR,

  BC_INST_JUMP,
  BC_INST_JUMP_ZERO,

  BC_INST_POP,

  BC_INST_RETURN,
  BC_INST_RETURN_ZERO,

  BC_INST_HALT,

} BcInst;

typedef struct BcEntry {

  char *name;
  size_t idx;

} BcEntry;

typedef struct BcAuto {

  char *name;
  int var;

} BcAuto;

typedef struct BcFunc {

  BcVec code;
  BcVec labels;
  size_t nparams;
  BcVec autos;

} BcFunc;

typedef enum BcResultType {

  BC_RESULT_TEMP,

  BC_RESULT_CONSTANT,

  BC_RESULT_ARRAY_AUTO,
  BC_RESULT_VAR_AUTO,

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
    BcVec array;
    BcEntry id;

  } data;

} BcResult;

typedef struct BcInstPtr {

  size_t func;
  size_t idx;
  size_t len;

} BcInstPtr;

void bc_auto_free(void *auto1);

// BC_LEX_OP_NEGATE is not used in lexing; it is only for parsing.
typedef enum BcLexToken {

  BC_LEX_OP_INC,
  BC_LEX_OP_DEC,

  BC_LEX_OP_NEG,

  BC_LEX_OP_POWER,
  BC_LEX_OP_MULTIPLY,
  BC_LEX_OP_DIVIDE,
  BC_LEX_OP_MODULUS,
  BC_LEX_OP_PLUS,
  BC_LEX_OP_MINUS,

  BC_LEX_OP_REL_EQUAL,
  BC_LEX_OP_REL_LESS_EQ,
  BC_LEX_OP_REL_GREATER_EQ,
  BC_LEX_OP_REL_NOT_EQ,
  BC_LEX_OP_REL_LESS,
  BC_LEX_OP_REL_GREATER,

  BC_LEX_OP_BOOL_NOT,
  BC_LEX_OP_BOOL_OR,
  BC_LEX_OP_BOOL_AND,

  BC_LEX_OP_ASSIGN_POWER,
  BC_LEX_OP_ASSIGN_MULTIPLY,
  BC_LEX_OP_ASSIGN_DIVIDE,
  BC_LEX_OP_ASSIGN_MODULUS,
  BC_LEX_OP_ASSIGN_PLUS,
  BC_LEX_OP_ASSIGN_MINUS,
  BC_LEX_OP_ASSIGN,

  BC_LEX_NEWLINE,

  BC_LEX_WHITESPACE,

  BC_LEX_LEFT_PAREN,
  BC_LEX_RIGHT_PAREN,

  BC_LEX_LEFT_BRACKET,
  BC_LEX_COMMA,
  BC_LEX_RIGHT_BRACKET,

  BC_LEX_LEFT_BRACE,
  BC_LEX_SEMICOLON,
  BC_LEX_RIGHT_BRACE,

  BC_LEX_STRING,
  BC_LEX_NAME,
  BC_LEX_NUMBER,

  BC_LEX_KEY_AUTO,
  BC_LEX_KEY_BREAK,
  BC_LEX_KEY_CONTINUE,
  BC_LEX_KEY_DEFINE,
  BC_LEX_KEY_ELSE,
  BC_LEX_KEY_FOR,
  BC_LEX_KEY_HALT,
  BC_LEX_KEY_IBASE,
  BC_LEX_KEY_IF,
  BC_LEX_KEY_LAST,
  BC_LEX_KEY_LENGTH,
  BC_LEX_KEY_LIMITS,
  BC_LEX_KEY_OBASE,
  BC_LEX_KEY_PRINT,
  BC_LEX_KEY_QUIT,
  BC_LEX_KEY_READ,
  BC_LEX_KEY_RETURN,
  BC_LEX_KEY_SCALE,
  BC_LEX_KEY_SQRT,
  BC_LEX_KEY_WHILE,

  BC_LEX_EOF,
  BC_LEX_INVALID,

} BcLexToken;

typedef struct BcLex {

  const char *buffer;
  size_t idx;
  size_t line;
  int newline;
  const char *file;
  size_t len;

  struct {
    BcLexToken type;
    char *string;
  } token;

} BcLex;

typedef struct BcLexKeyword {

  const char name[9];
  const char len;
  const int posix;

} BcLexKeyword;

#define BC_LEX_KW_ENTRY(a, b, c) { .name = a, .len = (b), .posix = (c) }

#define BC_PROGRAM_BUF_SIZE (1024)

typedef struct BcProgram {

  BcVec ip_stack;

  size_t line_len;

  size_t scale;

  BcNum ibase;
  size_t ibase_t;
  BcNum obase;
  size_t obase_t;

  BcVec results;
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

  size_t nchars;

} BcProgram;

#define BC_PROGRAM_MAIN (0)
#define BC_PROGRAM_READ (1)

#define BC_PROGRAM_SEARCH_VAR (1<<0)
#define BC_PROGRAM_SEARCH_ARRAY (1<<1)

typedef unsigned long (*BcProgramBuiltInFunc)(BcNum*);
typedef void (*BcNumInitFunc)(BcNum*);

BcStatus bc_program_addFunc(BcProgram *p, char *name, size_t *idx);
BcStatus bc_program_reset(BcProgram *p, BcStatus status);

BcStatus bc_program_exec(BcProgram *p);

#define BC_PARSE_TOP_FLAG_PTR(parse)  ((uint8_t*) bc_vec_top(&(parse)->flags))

#define BC_PARSE_TOP_FLAG(parse)  (*(BC_PARSE_TOP_FLAG_PTR(parse)))

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
#define BC_PARSE_TOKEN_TO_INST(type) ((type) - BC_LEX_OP_NEG + BC_INST_NEG)

typedef struct BcOp {

  uint8_t prec;
  int left;

} BcOp;

typedef struct BcParse {

  BcLex lex;

  BcVec flags;

  BcVec exits;
  BcVec conds;

  BcVec ops;

  BcProgram *prog;
  size_t func;

  size_t num_braces;

  int auto_part;

} BcParse;

#define BC_PARSE_EXPR_POSIX_REL (1<<0)
#define BC_PARSE_EXPR_PRINT (1<<1)
#define BC_PARSE_EXPR_NOCALL (1<<2)
#define BC_PARSE_EXPR_NOREAD (1<<3)

BcStatus bc_parse_expr(BcParse *p, BcVec *code, uint8_t flags);

#define maxof_BASE (999)
#define maxof_DIM (INT_MAX)
#define maxof_SCALE (LONG_MAX)
#define maxof_STRING (INT_MAX)

#define BC_BUF_SIZE (1024)

typedef struct Bc {

  BcParse parse;
  BcProgram prog;

} Bc;

BcStatus bc_error(BcStatus st);
BcStatus bc_error_file(BcStatus st, const char *file, size_t line);

BcStatus bc_posix_error(BcStatus s, const char *file,
                        size_t line, const char *msg);

BcStatus bc_io_fread(const char *path, char **buf);

const char *bc_header =
  "bc 1.0\n"
  "bc copyright (c) 2018 Gavin D. Howard and contributors\n"
  "Report bugs at: https://github.com/gavinhoward/bc\n\n"
  "This is free software with ABSOLUTELY NO WARRANTY.\n\n";

const char bc_err_fmt[] = "\n%s error: %s\n\n";

const char *bc_errs[] = {
  "bc",
  "Lex",
  "Parse",
  "Math",
  "Runtime",
  "POSIX",
};

const uint8_t bc_err_indices[] = {
  BC_ERR_IDX_BC, BC_ERR_IDX_BC, BC_ERR_IDX_BC, BC_ERR_IDX_BC,
  BC_ERR_IDX_LEX, BC_ERR_IDX_LEX, BC_ERR_IDX_LEX, BC_ERR_IDX_LEX,
  BC_ERR_IDX_PARSE, BC_ERR_IDX_PARSE, BC_ERR_IDX_PARSE, BC_ERR_IDX_PARSE,
  BC_ERR_IDX_PARSE, BC_ERR_IDX_PARSE, BC_ERR_IDX_PARSE,
  BC_ERR_IDX_MATH, BC_ERR_IDX_MATH, BC_ERR_IDX_MATH, BC_ERR_IDX_MATH,
  BC_ERR_IDX_MATH, BC_ERR_IDX_MATH,
  BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC,
  BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC,
  BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC,
  BC_ERR_IDX_EXEC,
  BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX,
  BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX,
  BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX,
};

const char *bc_err_descs[] = {
  NULL,
  "memory allocation error",
  "I/O error",
  "file is not text",

  "bad character",
  "string end could not be found",
  "comment end could not be found",
  "end of file",

  "bad token",
  "bad expression",
  "bad print statement",
  "bad function definition",
  "bad assignment: left must be scale, ibase, "
    "obase, last, var, or array element",
  "no auto variable found",
  "function parameter or auto var has the same name as another",

  "negative number",
  "non integer number",
  "overflow",
  "divide by zero",
  "negative square root",
  "bad number string",

  "could not open file",
  "mismatched parameters",
  "undefined function",
  "file is not executable",
  "could not install signal handler",
  "bad scale; must be [0, BC_SCALE_MAX]",
  "bad ibase; must be [2, 16]",
  "bad obase; must be [2, BC_BASE_MAX]",
  "string too long: must be [1, BC_STRING_MAX]",
  "array too long; must be [1, BC_DIM_MAX]",
  "bad read() expression",
  "read() call inside of a read() call",
  "variable is wrong type",

  "POSIX only allows one character names; the following is bad:",
  "POSIX does not allow '#' script comments",
  "POSIX does not allow the following keyword:",
  "POSIX does not allow a period ('.') as a shortcut for the last result",
  "POSIX requires parentheses around return expressions",
  "POSIX does not allow intean operators; the following is bad:",
  "POSIX does not allow comparison operators outside if or loops",
  "POSIX requires exactly one comparison operator per condition",
  "POSIX does not allow an empty init expression in a for loop",
  "POSIX does not allow an empty condition expression in a for loop",
  "POSIX does not allow an empty update expression in a for loop",
  "POSIX requires the left brace be on the same line as the function header",
};

const char bc_sig_msg[34] = "\ninterrupt (type \"quit\" to exit)\n";

const char bc_lang_func_main[7] = "(main)";
const char bc_lang_func_read[7] = "(read)";

const BcLexKeyword bc_lex_keywords[20] = {
  BC_LEX_KW_ENTRY("auto", 4, 1),
  BC_LEX_KW_ENTRY("break", 5, 1),
  BC_LEX_KW_ENTRY("continue", 8, 0),
  BC_LEX_KW_ENTRY("define", 6, 1),
  BC_LEX_KW_ENTRY("else", 4, 0),
  BC_LEX_KW_ENTRY("for", 3, 1),
  BC_LEX_KW_ENTRY("halt", 4, 0),
  BC_LEX_KW_ENTRY("ibase", 5, 1),
  BC_LEX_KW_ENTRY("if", 2, 1),
  BC_LEX_KW_ENTRY("last", 4, 0),
  BC_LEX_KW_ENTRY("length", 6, 1),
  BC_LEX_KW_ENTRY("limits", 6, 0),
  BC_LEX_KW_ENTRY("obase", 5, 1),
  BC_LEX_KW_ENTRY("print", 5, 0),
  BC_LEX_KW_ENTRY("quit", 4, 1),
  BC_LEX_KW_ENTRY("read", 4, 0),
  BC_LEX_KW_ENTRY("return", 6, 1),
  BC_LEX_KW_ENTRY("scale", 5, 1),
  BC_LEX_KW_ENTRY("sqrt", 4, 1),
  BC_LEX_KW_ENTRY("while", 5, 1),
};

const char bc_num_hex_digits[] = "0123456789ABCDEF";

// This is an array that corresponds to token types. An entry is
// 1 if the token is valid in an expression, 0 otherwise.
const int bc_parse_token_exprs[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
  0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1,
  1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1,
  1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 0,
};

// This is an array of data for operators that correspond to token types.
const BcOp bc_parse_ops[] = {
  { 0, 0 }, { 0, 0 },
  { 1, 0 },
  { 2, 0 },
  { 3, 1 }, { 3, 1 }, { 3, 1 },
  { 4, 1 }, { 4, 1 },
  { 6, 1 }, { 6, 1 }, { 6, 1 }, { 6, 1 }, { 6, 1 }, { 6, 1 },
  { 7, 0 },
  { 8, 1 }, { 8, 1 },
  { 5, 0 }, { 5, 0 }, { 5, 0 }, { 5, 0 }, { 5, 0 },  { 5, 0 }, { 5, 0 },
};

const char bc_program_byte_fmt[] = "%02x";

const BcNumBinaryFunc bc_program_math_ops[] = {
  bc_num_pow, bc_num_mul, bc_num_div, bc_num_mod, bc_num_add, bc_num_sub,
};

const char bc_program_stdin_name[] = "<stdin>";
const char bc_program_ready_prompt[] = "ready for more input\n";
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
  9,112,61,120,10,9,102,61,118,61,49,10,9,102,111,114,40,105,61,50,59,118,33,
  61,48,59,43,43,105,41,123,10,9,9,112,42,61,120,59,10,9,9,102,42,61,105,10,9,
  9,118,61,112,47,102,10,9,9,114,43,61,118,10,9,125,10,9,119,104,105,108,101,
  40,40,102,45,45,41,33,61,48,41,114,42,61,114,10,9,115,99,97,108,101,61,115,
  10,9,105,98,97,115,101,61,98,10,9,105,102,40,109,33,61,48,41,114,101,116,117,
  114,110,40,49,47,114,41,10,9,114,101,116,117,114,110,40,114,47,49,41,10,125,
  10,100,101,102,105,110,101,32,108,40,120,41,123,10,9,97,117,116,111,32,98,44,
  115,44,114,44,112,44,97,44,113,44,105,44,118,10,9,98,61,105,98,97,115,101,10,
  9,105,98,97,115,101,61,65,10,9,105,102,40,120,60,61,48,41,123,10,9,9,114,61,
  40,49,45,49,48,94,115,99,97,108,101,41,47,49,10,9,9,105,98,97,115,101,61,98,
  10,9,9,114,101,116,117,114,110,40,114,41,10,9,125,10,9,115,61,115,99,97,108,
  101,10,9,115,99,97,108,101,43,61,55,10,9,112,61,50,10,9,119,104,105,108,101,
  40,120,62,61,50,41,123,10,9,9,112,42,61,50,10,9,9,120,61,115,113,114,116,40,
  120,41,10,9,125,10,9,119,104,105,108,101,40,120,60,61,48,46,53,41,123,10,9,
  9,112,42,61,50,10,9,9,120,61,115,113,114,116,40,120,41,10,9,125,10,9,114,61,
  97,61,40,120,45,49,41,47,40,120,43,49,41,10,9,113,61,97,42,97,10,9,118,61,49,
  10,9,102,111,114,40,105,61,51,59,118,33,61,48,59,105,43,61,50,41,123,10,9,9,
  110,42,61,109,10,9,9,118,61,110,47,105,10,9,9,114,43,61,118,10,9,125,10,9,114,
  42,61,112,10,9,115,99,97,108,101,61,115,10,9,105,98,97,115,101,61,98,10,9,114,
  101,116,117,114,110,40,114,47,49,41,10,125,10,100,101,102,105,110,101,32,115,
  40,120,41,123,10,9,97,117,116,111,32,98,44,115,44,114,44,110,44,97,44,113,44,
  105,10,9,98,61,105,98,97,115,101,10,9,105,98,97,115,101,61,65,10,9,115,61,115,
  99,97,108,101,10,9,115,99,97,108,101,61,49,46,51,42,115,43,50,10,9,97,61,97,
  40,49,41,10,9,105,102,40,120,60,48,41,123,10,9,9,110,61,49,10,9,9,120,61,45,
  120,10,9,125,10,9,115,99,97,108,101,61,48,10,9,113,61,40,120,47,97,43,50,41,
  47,52,10,9,120,45,61,52,42,113,42,97,10,9,105,102,40,113,37,50,33,61,48,41,
  120,61,45,120,10,9,115,99,97,108,101,61,115,43,50,10,9,114,61,97,61,120,10,
  9,113,61,45,120,42,120,10,9,102,111,114,40,105,61,51,59,97,33,61,48,59,105,
  43,61,50,41,123,10,9,9,97,42,61,113,47,40,105,42,40,105,45,49,41,41,10,9,9,
  114,43,61,97,10,9,125,10,9,115,99,97,108,101,61,115,10,9,105,98,97,115,101,
  61,98,10,9,105,102,40,110,33,61,48,41,114,101,116,117,114,110,40,45,114,47,
  49,41,10,9,114,101,116,117,114,110,40,114,47,49,41,10,125,10,100,101,102,105,
  110,101,32,99,40,120,41,123,10,9,97,117,116,111,32,98,44,115,10,9,98,61,105,
  98,97,115,101,10,9,105,98,97,115,101,61,65,10,9,115,61,115,99,97,108,101,10,
  9,115,99,97,108,101,43,61,49,10,9,120,61,115,40,50,42,97,40,49,41,43,120,41,
  10,9,115,99,97,108,101,61,115,10,9,105,98,97,115,101,61,98,10,9,114,101,116,
  117,114,110,40,120,47,49,41,10,125,10,100,101,102,105,110,101,32,97,40,120,
  41,123,10,9,97,117,116,111,32,98,44,115,44,114,44,110,44,97,44,109,44,116,44,
  102,44,105,44,117,10,9,98,61,105,98,97,115,101,10,9,105,98,97,115,101,61,65,
  10,9,110,61,49,10,9,105,102,40,120,60,48,41,123,10,9,9,110,61,45,49,10,9,9,
  120,61,45,120,10,9,125,10,9,105,102,40,120,61,61,49,41,123,10,9,9,105,102,40,
  115,99,97,108,101,60,61,54,52,41,123,10,9,9,9,114,101,116,117,114,110,40,46,
  55,56,53,51,57,56,49,54,51,51,57,55,52,52,56,51,48,57,54,49,53,54,54,48,56,
  52,53,56,49,57,56,55,53,55,50,49,48,52,57,50,57,50,51,52,57,56,52,51,55,55,
  54,52,53,53,50,52,51,55,51,54,49,52,56,48,47,110,41,10,9,9,125,10,9,125,10,
  9,105,102,40,120,61,61,46,50,54,55,41,123,10,9,9,105,102,40,115,99,97,108,101,
  60,61,54,52,41,123,10,9,9,9,114,101,116,117,114,110,40,46,50,54,48,57,49,51,
  53,54,57,50,51,50,57,52,48,53,55,57,53,57,54,55,56,53,50,54,55,55,55,55,57,
  56,54,53,54,51,57,55,55,52,55,52,48,50,51,57,56,56,50,52,52,53,56,50,50,51,
  50,57,56,56,50,57,49,55,47,110,41,10,9,9,125,10,9,125,10,9,115,61,115,99,97,
  108,101,10,9,105,102,40,120,62,46,50,54,55,41,123,10,9,9,115,99,97,108,101,
  43,61,53,10,9,9,97,61,97,40,46,50,54,55,41,10,9,125,10,9,115,99,97,108,101,
  61,115,43,51,10,9,119,104,105,108,101,40,120,62,46,50,54,55,41,123,10,9,9,109,
  43,61,49,10,9,9,120,61,40,120,45,46,50,54,55,41,47,40,49,43,46,50,54,55,42,
  120,41,10,9,125,10,9,114,61,117,61,120,10,9,102,61,45,120,42,120,10,9,116,61,
  49,10,9,102,111,114,40,105,61,51,59,116,33,61,48,59,105,43,61,50,41,123,10,
  9,9,117,42,61,102,10,9,9,116,61,117,47,105,10,9,9,114,43,61,116,10,9,125,10,
  9,115,99,97,108,101,61,115,10,9,105,98,97,115,101,61,98,10,9,114,101,116,117,
  114,110,40,40,109,42,97,43,114,41,47,110,41,10,125,10,100,101,102,105,110,101,
  32,106,40,110,44,120,41,123,10,9,97,117,116,111,32,98,44,115,44,111,44,97,44,
  105,44,118,44,102,10,9,98,61,105,98,97,115,101,10,9,105,98,97,115,101,61,65,
  10,9,115,61,115,99,97,108,101,10,9,115,99,97,108,101,61,48,10,9,110,47,61,49,
  10,9,105,102,40,110,60,48,41,123,10,9,9,110,61,45,110,10,9,9,105,102,40,110,
  37,50,61,61,49,41,111,61,49,10,9,125,10,9,97,61,49,10,9,102,111,114,40,105,
  61,50,59,105,60,61,110,59,43,43,105,41,102,42,61,105,10,9,115,99,97,108,101,
  61,49,46,53,42,115,10,9,97,61,40,120,94,110,41,47,40,50,94,110,42,97,41,10,
  9,114,61,118,61,49,10,9,102,61,45,120,42,120,47,52,10,9,115,99,97,108,101,43,
  61,108,101,110,103,116,104,40,97,41,45,115,99,97,108,101,40,97,41,10,9,102,
  111,114,40,105,61,49,59,118,33,61,48,59,43,43,105,41,123,10,9,9,118,61,118,
  42,115,47,105,47,40,110,43,105,41,10,9,9,114,43,61,118,10,9,125,10,9,115,99,
  97,108,101,61,115,10,9,105,98,97,115,101,61,98,10,9,105,102,40,111,33,61,48,
  41,114,101,116,117,114,110,40,45,97,42,114,47,49,41,10,9,114,101,116,117,114,
  110,40,97,42,114,47,49,41,10,125,10,0
};

BcStatus bc_vec_double(BcVec *vec) {

  uint8_t *ptr = realloc(vec->array, vec->size * (vec->cap * 2));
  if (!ptr) return BC_STATUS_MALLOC_FAIL;

  vec->array = ptr;
  vec->cap *= 2;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_vec_init(BcVec *vec, size_t esize, BcVecFreeFunc dtor) {

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

  if (vec->len == vec->cap && (status = bc_vec_double(vec))) return status;

  size = vec->size;
  memmove(vec->array + (size * vec->len++), data, size);

  return BC_STATUS_SUCCESS;
}

BcStatus bc_vec_pushByte(BcVec *vec, uint8_t data) {

  BcStatus status;

  if (vec->len == vec->cap && (status = bc_vec_double(vec))) return status;

  vec->array[vec->len++] = data;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_vec_pushAt(BcVec *vec, void *data, size_t idx) {

  BcStatus status;
  uint8_t *ptr;

  if (idx == vec->len) return bc_vec_push(vec, data);
  if (vec->len == vec->cap && (status = bc_vec_double(vec))) return status;

  ptr = vec->array + vec->size * idx;

  memmove(ptr + vec->size, ptr, vec->size * (vec->len++ - idx));
  memmove(ptr, data, vec->size);

  return BC_STATUS_SUCCESS;
}

void* bc_vec_top(const BcVec *vec) {
  return vec->array + vec->size * (vec->len - 1);
}

void* bc_vec_item(const BcVec *vec, size_t idx) {
  return vec->array + vec->size * idx;
}

void* bc_vec_item_rev(const BcVec *vec, size_t idx) {
  return vec->array + vec->size * (vec->len - idx - 1);
}

void bc_vec_pop(BcVec *vec) {
  vec->len -= 1;
  if (vec->dtor) vec->dtor(vec->array + (vec->size * vec->len));
}

void bc_vec_npop(BcVec *vec, size_t n) {
  if (!vec->dtor) vec->len -= n;
  else {
    size_t len = vec->len - n;
    while (vec->len > len) bc_vec_pop(vec);
  }
}

void bc_vec_free(void *vec) {

  size_t i;
  BcVec *s = (BcVec*) vec;

  if (!s) return;

  if (s->dtor) {
    for (i = 0; i < s->len; ++i) s->dtor(s->array + (i * s->size));
  }

  free(s->array);
  memset(s, 0, sizeof(BcVec));
}

size_t bc_veco_find(const BcVecO* vec, void *data) {

  size_t low, high;

  low = 0;
  high = vec->vec.len;

  while (low < high) {

    size_t mid = (low + high) / 2;
    uint8_t *ptr = bc_vec_item(&vec->vec, mid);
    int result = vec->cmp(data, ptr);

    if (!result) return mid;

    if (result < 0) high = mid;
    else low = mid + 1;
  }

  return low;
}

BcStatus bc_veco_init(BcVecO* vec, size_t esize,
                      BcVecFreeFunc dtor, BcVecCmpFunc cmp)
{
  vec->cmp = cmp;
  return bc_vec_init(&vec->vec, esize, dtor);
}

BcStatus bc_veco_insert(BcVecO* vec, void *data, size_t *idx) {

  BcStatus status;

  *idx = bc_veco_find(vec, data);

  if (*idx > vec->vec.len) return BC_STATUS_VEC_OUT_OF_BOUNDS;
  if (*idx != vec->vec.len && !vec->cmp(data, bc_vec_item(&vec->vec, *idx)))
    return BC_STATUS_VEC_ITEM_EXISTS;

  if (*idx >= vec->vec.len) {
    *idx = vec->vec.len;
    status = bc_vec_push(&vec->vec, data);
  }
  else status = bc_vec_pushAt(&vec->vec, data, *idx);

  return status;
}

size_t bc_veco_index(const BcVecO* v, void *data) {
  size_t i;
  i = bc_veco_find(v, data);
  if (i >= v->vec.len || v->cmp(data, bc_vec_item(&v->vec, i))) return -1;
  return i;
}

BcStatus bc_io_getline(char **buf, size_t *n) {

  char *temp;
  int c;
  size_t size, i;

  if (TT.tty && fputs(">>> ", stdout) == EOF) return BC_STATUS_IO_ERR;

  for (i = 0, c = 0; c != '\n'; ++i) {

    if (i == *n) {

      size = *n * 2;

      if (size > (1 << 20) || !(temp = realloc(*buf, size + 1)))
        return BC_STATUS_MALLOC_FAIL;

      *buf = temp;
      *n = size;
    }

    if ((c = fgetc(stdin)) == EOF) {

      if (errno == EINTR) {

        TT.sigc = TT.sig;
        TT.signe = 0;
        --i;

        fprintf(stderr, "%s", bc_program_ready_prompt);
        fflush(stderr);

        if (TT.tty && fputs(">>> ", stdout) == EOF) return BC_STATUS_IO_ERR;

        continue;
      }
      else return BC_STATUS_IO_ERR;
    }
    else if (!c || (iscntrl(c) && !isspace(c)) || c > SCHAR_MAX)
      return BC_STATUS_BINARY_FILE;

    (*buf)[i] = c;
  }

  (*buf)[i] = '\0';

  return BC_STATUS_SUCCESS;
}

BcStatus bc_io_fread(const char *path, char **buf) {

  BcStatus st;
  FILE *f;
  size_t size, read;

  if (!(f = fopen(path, "r"))) return BC_STATUS_EXEC_FILE_ERR;

  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (!(*buf = malloc(size + 1))) {
    st = BC_STATUS_MALLOC_FAIL;
    goto malloc_err;
  }

  if ((read = fread(*buf, 1, size, f)) != size) {
    st = BC_STATUS_IO_ERR;
    goto read_err;
  }

  (*buf)[size] = '\0';
  fclose(f);

  return BC_STATUS_SUCCESS;

read_err:
  free(*buf);
malloc_err:
  fclose(f);
  return st;
}

BcStatus bc_num_subArrays(BcDigit *n1, BcDigit *n2, size_t len) {
  size_t i, j;
  for (i = 0; !TT.signe && i < len; ++i) {
    for (n1[i] -= n2[i], j = 0; !TT.signe && n1[i + j] < 0;) {
      n1[i + j++] += 10;
      n1[i + j] -= 1;
    }
  }
  return TT.signe ? BC_STATUS_EXEC_SIGNAL : BC_STATUS_SUCCESS;
}

ssize_t bc_num_compare(BcDigit *n1, BcDigit *n2, size_t len) {
  size_t i;
  BcDigit c;
  for (c = 0, i = len - 1; !TT.signe && !(c = n1[i] - n2[i]) && i < len; --i);
  return (c < 0 ? -1 : 1) * (ssize_t) (i + 1);
}

ssize_t bc_num_cmp(BcNum *a, BcNum *b) {

  size_t i, min, a_int, b_int, diff;
  BcDigit *max_num, *min_num;
  int a_max;
  int cmp, neg;

  if (!a) return !b ? 0 : !b->neg * -2 + 1;
  else if (!b) return a->neg * -2 + 1;

  neg = 1;

  if (a->neg) {
    if (b->neg) neg = -1;
    else return -1;
  }
  else if (b->neg) return 1;

  if (!a->len) return (!b->neg * -2 + 1) * !!b->len;
  else if (!b->len) return a->neg * -2 + 1;

  a_int = a->len - a->rdx;
  b_int = b->len - b->rdx;
  a_int -= b_int;

  if (a_int) return a_int;

  a_max = a->rdx > b->rdx;

  if (a_max) {
    min = b->rdx;
    diff = a->rdx - b->rdx;
    max_num = a->num + diff;
    min_num = b->num;
  }
  else {
    min = a->rdx;
    diff = b->rdx - a->rdx;
    max_num = b->num + diff;
    min_num = a->num;
  }

  cmp = bc_num_compare(max_num, min_num, b_int + min);
  if (cmp) return cmp * (!a_max * -2 + 1) * neg;

  for (max_num -= diff, i = diff - 1; !TT.signe && i < diff; --i) {
    if (max_num[i]) return neg * (!a_max * -2 + 1);
  }

  return 0;
}

void bc_num_truncate(BcNum *n, size_t places) {

  BcDigit *ptr;

  if (!places) return;

  ptr = n->num + places;
  n->len -= places;
  n->rdx -= places;

  memmove(n->num, ptr, n->len * sizeof(BcDigit));
  memset(n->num + n->len, 0, sizeof(BcDigit) * (n->cap - n->len));
}

BcStatus bc_num_extend(BcNum *n, size_t places) {

  BcStatus status;
  BcDigit *ptr;
  size_t len;

  if (!places) return BC_STATUS_SUCCESS;

  len = n->len + places;
  if (n->cap < len && (status = bc_num_expand(n, len))) return status;

  ptr = n->num + places;
  memmove(ptr, n->num, sizeof(BcDigit) * n->len);
  memset(n->num, 0, sizeof(BcDigit) * places);

  n->len += places;
  n->rdx += places;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_inv(BcNum *a, BcNum *b, size_t scale) {

  BcStatus status;
  BcNum one;

  if ((status = bc_num_init(&one, BC_NUM_DEF_SIZE))) return status;

  bc_num_one(&one);
  status = bc_num_div(&one, a, b, scale);
  bc_num_free(&one);

  return status;
}

BcStatus bc_num_alg_a(BcNum *a, BcNum *b, BcNum *c, size_t scale) {

  BcDigit *ptr, *ptr_a, *ptr_b, *ptr_c;
  size_t i, max, min_rdx, min_int, diff, a_int, b_int;
  BcDigit carry;

  (void) scale;

  if (!a->len) return bc_num_copy(c, b);
  else if (!b->len) return bc_num_copy(c, a);

  c->neg = a->neg;
  memset(c->num, 0, c->cap * sizeof(BcDigit));

  c->rdx = maxof(a->rdx, b->rdx);
  min_rdx = minof(a->rdx, b->rdx);
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

  for (ptr_c = c->num, i = 0; i < diff; ++i, ++c->len) ptr_c[i] = ptr[i];

  ptr_c += diff;
  a_int = a->len - a->rdx;
  b_int = b->len - b->rdx;

  if (a_int > b_int) {
    min_int = b_int;
    max = a_int;
    ptr = ptr_a;
  }
  else {
    min_int = a_int;
    max = b_int;
    ptr = ptr_b;
  }

  for (carry = 0, i = 0; !TT.signe && i < min_rdx + min_int; ++i, ++c->len) {
    ptr_c[i] = ptr_a[i] + ptr_b[i] + carry;
    carry = ptr_c[i] / 10;
    ptr_c[i] %= 10;
  }

  for (; !TT.signe && i < max + min_rdx; ++i, ++c->len) {
    ptr_c[i] += ptr[i] + carry;
    carry = ptr_c[i] / 10;
    ptr_c[i] %= 10;
  }

  if (TT.signe) return BC_STATUS_EXEC_SIGNAL;

  if (carry) c->num[c->len++] = carry;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_alg_s(BcNum *a, BcNum *b, BcNum *c, size_t sub) {

  BcStatus status;
  int cmp;
  BcNum *minuend, *subtrahend;
  size_t start;
  int aneg, bneg, neg;

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
  a->neg = b->neg = 0;

  cmp = bc_num_cmp(a, b);

  a->neg = aneg;
  b->neg = bneg;

  if (!cmp) {
    bc_num_zero(c);
    return BC_STATUS_SUCCESS;
  }
  else if (cmp > 0) {
    neg = sub && a->neg;
    minuend = a;
    subtrahend = b;
  }
  else {
    neg = sub && !b->neg;
    minuend = b;
    subtrahend = a;
  }

  if ((status = bc_num_copy(c, minuend))) return status;
  c->neg = neg;

  if (c->rdx < subtrahend->rdx) {
    if ((status = bc_num_extend(c, subtrahend->rdx - c->rdx))) return status;
    start = 0;
  }
  else start = c->rdx - subtrahend->rdx;

  status = bc_num_subArrays(c->num + start, subtrahend->num, subtrahend->len);

  while (c->len > c->rdx && !c->num[c->len - 1]) --c->len;

  return status;
}

BcStatus bc_num_alg_m(BcNum *a, BcNum *b, BcNum *c, size_t scale) {

  BcStatus status;
  BcDigit carry;
  size_t i, j, len;

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
  c->len = carry = len = 0;

  for (i = 0; !TT.signe && i < b->len; ++i) {

    for (j = 0; !TT.signe && j < a->len; ++j) {
      c->num[i + j] += a->num[j] * b->num[i] + carry;
      carry = c->num[i + j] / 10;
      c->num[i + j] %= 10;
    }

    if (TT.signe) return BC_STATUS_EXEC_SIGNAL;

    if (carry) {
      c->num[i + j] += carry;
      carry = 0;
      len = maxof(len, i + j + 1);
    }
    else len = maxof(len, i + j);
  }

  if (TT.signe) return BC_STATUS_EXEC_SIGNAL;

  c->len = maxof(len, c->rdx);
  c->neg = !a->neg != !b->neg;

  if (scale < c->rdx) bc_num_truncate(c, c->rdx - scale);
  while (c->len > c->rdx && !c->num[c->len - 1]) --c->len;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_alg_d(BcNum *a, BcNum *b, BcNum *c, size_t scale) {

  BcStatus status;
  BcDigit *ptr, *bptr, q;
  size_t len, end, i;
  BcNum copy;
  int zero;

  if (!b->len) return BC_STATUS_MATH_DIVIDE_BY_ZERO;
  else if (!a->len) {
    bc_num_zero(c);
    return BC_STATUS_SUCCESS;
  }
  else if (BC_NUM_ONE(b)) {

    if ((status = bc_num_copy(c, a))) return status;
    if (b->neg) c->neg = !c->neg;

    if (c->rdx < scale) status = bc_num_extend(c, scale - c->rdx);
    else bc_num_truncate(c, c->rdx - scale);

    return status;
  }

  if ((status = bc_num_init(&copy, a->len + b->rdx + scale + 1))) return status;
  if ((status = bc_num_copy(&copy, a))) goto err;

  if ((len = b->len) > copy.len) {
    if ((status = bc_num_expand(&copy, len + 2))) goto err;
    if ((status = bc_num_extend(&copy, len - copy.len))) goto err;
  }

  if (b->rdx > copy.rdx && (status = bc_num_extend(&copy, b->rdx - copy.rdx)))
    goto err;

  copy.rdx -= b->rdx;

  if (scale > copy.rdx && (status = bc_num_extend(&copy, scale - copy.rdx)))
    goto err;

  if (b->rdx == b->len) {
    int zero;
    for (zero = 1, i = 0; zero && i < len; ++i) zero = !b->num[len - i - 1];
    if (i == len) return BC_STATUS_MATH_DIVIDE_BY_ZERO;
    len -= i - 1;
  }

  if (copy.cap == copy.len && (status = bc_num_expand(&copy, copy.len + 1)))
    goto err;

  // We want an extra zero in front to make things simpler.
  copy.num[copy.len++] = 0;
  end = copy.len - len;

  if ((status = bc_num_expand(c, copy.len))) goto err;

  bc_num_zero(c);
  c->rdx = copy.rdx;
  c->len = copy.len;
  bptr = b->num;

  for (i = end - 1; !TT.signe && i < end; --i) {

    ptr = copy.num + i;

    q = 0;
    for (; (!status && ptr[len]) || bc_num_compare(ptr, bptr, len) >= 0; ++q)
      status = bc_num_subArrays(ptr, bptr, len);

    c->num[i] = q;
  }

  if (status) goto err;

  c->neg = !a->neg != !b->neg;
  while (c->len > c->rdx && !c->num[c->len - 1]) --c->len;
  if (c->rdx > scale) bc_num_truncate(c, c->rdx - scale);

  for (i = 0, zero = 1; zero && i < c->len; ++i) zero = !c->num[i];
  if (zero) bc_num_zero(c);

err:
  bc_num_free(&copy);
  return status;
}

BcStatus bc_num_alg_mod(BcNum *a, BcNum *b, BcNum *c, size_t scale) {

  BcStatus status;
  BcNum c1, c2;
  size_t len;

  if (!b->len) return BC_STATUS_MATH_DIVIDE_BY_ZERO;

  if (!a->len) {
    bc_num_zero(c);
    return BC_STATUS_SUCCESS;
  }

  len = a->len + b->len + scale;

  if ((status = bc_num_init(&c1, len))) return status;
  if ((status = bc_num_init(&c2, len))) goto c2_err;
  if ((status = bc_num_div(a, b, &c1, scale))) goto err;

  c->rdx = maxof(scale + b->rdx, a->rdx);
  if ((status = bc_num_mul(&c1, b, &c2, scale))) goto err;
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
  unsigned long pow;
  size_t i, powrdx, resrdx;
  int neg, zero;

  if (b->rdx) return BC_STATUS_MATH_NON_INTEGER;

  if (!b->len) {
    bc_num_one(c);
    return BC_STATUS_SUCCESS;
  }
  else if (!a->len) {
    bc_num_zero(c);
    return BC_STATUS_SUCCESS;
  }
  else if (BC_NUM_ONE(b)) {

    if (!b->neg) status = bc_num_copy(c, a);
    else status = bc_num_inv(a, c, scale);

    return status;
  }

  neg = b->neg;
  b->neg = 0;

  if ((status = bc_num_ulong(b, &pow))) return status;
  if ((status = bc_num_init(&copy, a->len))) return status;
  if ((status = bc_num_copy(&copy, a))) goto err;

  if (!neg) scale = minof(a->rdx * pow, maxof(scale, a->rdx));

  b->neg = neg;

  for (powrdx = a->rdx; !TT.signe && !(pow & 1); pow >>= 1) {
    powrdx <<= 1;
    if ((status = bc_num_mul(&copy, &copy, &copy, powrdx))) goto err;
  }

  if ((status = bc_num_copy(c, &copy))) goto err;
  if (TT.signe) {
    status = BC_STATUS_EXEC_SIGNAL;
    goto err;
  }

  resrdx = powrdx;

  for (pow >>= 1; !TT.signe && pow != 0; pow >>= 1) {

    powrdx <<= 1;

    if ((status = bc_num_mul(&copy, &copy, &copy, powrdx))) goto err;

    if (pow & 1) {
      resrdx += powrdx;
      if ((status = bc_num_mul(c, &copy, c, resrdx))) goto err;
    }
  }

  if (neg && (status = bc_num_inv(c, c, scale))) goto err;
  if (TT.signe) {
    status = BC_STATUS_EXEC_SIGNAL;
    goto err;
  }

  if (c->rdx > scale) bc_num_truncate(c, c->rdx - scale);

  for (zero = 1, i = 0; zero && i < c->len; ++i) zero = !c->num[i];
  if (zero) bc_num_zero(c);

err:
  bc_num_free(&copy);
  return status;
}

BcStatus bc_num_binary(BcNum *a, BcNum *b, BcNum *c,  size_t scale,
                       BcNumBinaryFunc op, size_t req)
{
  BcStatus status;
  BcNum num2, *ptr_a, *ptr_b;
  int init = 0;

  if (c == a) {
    memcpy(&num2, c, sizeof(BcNum));
    ptr_a = &num2;
    init = 1;
  }
  else ptr_a = a;

  if (c == b) {

    if (c == a) {
      ptr_b = ptr_a;
    }
    else {
      memcpy(&num2, c, sizeof(BcNum));
      ptr_b = &num2;
      init = 1;
    }
  }
  else ptr_b = b;

  if (init) status = bc_num_init(c, req);
  else status = bc_num_expand(c, req);

  if (status) goto err;
  status = op(ptr_a, ptr_b, c, scale);

err:
  if (c == a || c == b) bc_num_free(&num2);
  return status;
}

int bc_num_strValid(const char *val, size_t base) {

  size_t len, i;
  BcDigit c, b;
  int small, radix;

  radix = 0;
  len = strlen(val);

  if (!len) return 1;

  small = base <= 10;
  b = small ? base + '0' : base - 9 + 'A';

  for (i = 0; i < len; ++i) {

    if ((c = val[i]) == '.') {

      if (radix) return 0;

      radix = 1;
      continue;
    }

    if (c < '0' || (small && c >= b) || (c > '9' && (c < 'A' || c >= b)))
      return 0;
  }

  return 1;
}

BcStatus bc_num_parseDecimal(BcNum *n, const char *val) {

  BcStatus status;
  size_t len, i;
  const char *ptr;
  int zero = 1;

  for (i = 0; val[i] == '0'; ++i);

  val += i;
  len = strlen(val);
  bc_num_zero(n);

  if (len) {
    for (i = 0; zero && i < len; ++i) zero = val[i] == '0' || val[i] == '.';
    if ((status = bc_num_expand(n, len))) return status;
  }

  if (zero) {
    memset(n->num, 0, sizeof(BcDigit) * n->cap);
    n->neg = 0;
    return BC_STATUS_SUCCESS;
  }

  ptr = strchr(val, '.');

  // Explicitly test for NULL here to produce either a 0 or 1.
  n->rdx = (ptr != NULL) * ((val + len) - (ptr + 1));

  for (i = len - 1; i < len; ++n->len, i -= 1 + (i && val[i - 1] == '.'))
    n->num[n->len] = val[i] - '0';

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_parseBase(BcNum *n, const char *val, BcNum *base) {

  BcStatus status;
  BcNum temp, mult, result;
  size_t i, len, digits;
  BcDigit c;
  int zero;
  unsigned long v;

  len = strlen(val);
  bc_num_zero(n);

  for (zero = 1, i = 0; zero && i < len; ++i)
    zero = (val[i] == '.' || val[i] == '0');
  if (zero) return BC_STATUS_SUCCESS;

  if ((status = bc_num_init(&temp, BC_NUM_DEF_SIZE))) return status;
  if ((status = bc_num_init(&mult, BC_NUM_DEF_SIZE))) goto mult_err;

  for (i = 0; i < len && (c = val[i]) != '.'; ++i) {

    v = c <= '9' ? c - '0' : c - 'A' + 10;

    if ((status = bc_num_mul(n, base, &mult, 0))) goto int_err;
    if ((status = bc_num_ulong2num(&temp, v))) goto int_err;
    if ((status = bc_num_add(&mult, &temp, n, 0))) goto int_err;
  }

  if (i == len && !(c = val[i])) goto int_err;
  if ((status = bc_num_init(&result, base->len))) goto int_err;

  bc_num_zero(&result);
  bc_num_one(&mult);

  for (i += 1, digits = 0; i < len && (c = val[i]); ++i, ++digits) {

    v = c <= '9' ? c - '0' : c - 'A' + 10;

    if ((status = bc_num_mul(&result, base, &result, 0))) goto err;
    if ((status = bc_num_ulong2num(&temp, v))) goto err;
    if ((status = bc_num_add(&result, &temp, &result, 0))) goto err;
    if ((status = bc_num_mul(&mult, base, &mult, 0))) goto err;
  }

  if ((status = bc_num_div(&result, &mult, &result, digits))) goto err;
  if ((status = bc_num_add(n, &result, n, digits))) goto err;

  if (n->len) {
    if (n->rdx < digits && n->len) status = bc_num_extend(n, digits - n->rdx);
  }
  else bc_num_zero(n);

err:
  bc_num_free(&result);
int_err:
  bc_num_free(&mult);
mult_err:
  bc_num_free(&temp);
  return status;
}

BcStatus bc_num_printDigits(size_t num, size_t width, int radix,
                            size_t *nchars, size_t line_len)
{
  size_t exp, pow, div;

  if (*nchars == line_len - 1) {
    if (putchar('\\') == EOF) return BC_STATUS_IO_ERR;
    if (putchar('\n') == EOF) return BC_STATUS_IO_ERR;
    *nchars = 0;
  }

  if (*nchars || radix) {
    if (putchar(radix ? '.' : ' ') == EOF) return BC_STATUS_IO_ERR;
    ++(*nchars);
  }

  for (exp = 0, pow = 1; exp < width - 1; ++exp, pow *= 10);

  for (exp = 0; exp < width; pow /= 10, ++(*nchars), ++exp) {

    if (*nchars == line_len - 1) {
      if (putchar('\\') == EOF) return BC_STATUS_IO_ERR;
      if (putchar('\n') == EOF) return BC_STATUS_IO_ERR;
      *nchars = 0;
    }

    div = num / pow;
    num -= div * pow;

    if (putchar(((char) div) + '0') == EOF) return BC_STATUS_IO_ERR;
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_printHex(size_t num, size_t width, int radix,
                         size_t *nchars, size_t line_len)
{
  width += !!radix;
  if (*nchars + width  >= line_len) {
    if (putchar('\\') == EOF) return BC_STATUS_IO_ERR;
    if (putchar('\n') == EOF) return BC_STATUS_IO_ERR;
    *nchars = 0;
  }

  if (radix && putchar('.') == EOF) return BC_STATUS_IO_ERR;
  if (putchar(bc_num_hex_digits[num]) == EOF) return BC_STATUS_IO_ERR;

  *nchars = *nchars + width;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_printDecimal(BcNum *n, size_t *nchars, size_t line_len) {

  BcStatus status;
  size_t i;

  if (n->neg) {
    if (putchar('-') == EOF) return BC_STATUS_IO_ERR;
    ++(*nchars);
  }

  status = BC_STATUS_SUCCESS;

  for (i = n->len - 1; !status && i < n->len; --i)
    status = bc_num_printHex(n->num[i], 1, i == n->rdx - 1, nchars, line_len);

  return status;
}

BcStatus bc_num_printBase(BcNum *n, BcNum *base, size_t base_t,
                          size_t *nchars, size_t line_len)
{
  BcStatus status;
  BcVec stack;
  BcNum intp, fracp, digit, frac_len;
  size_t width, i;
  BcNumDigitFunc print;
  unsigned long dig, *ptr;
  int neg, radix;

  neg = n->neg;
  n->neg = 0;

  if (neg && putchar('-') == EOF) return BC_STATUS_IO_ERR;
  nchars += neg;

  if (base_t <= BC_NUM_MAX_INPUT_BASE) {
    width = 1;
    print = bc_num_printHex;
  }
  else {
    width = (size_t) floor(log10((double) (base_t - 1)) + 1.0);
    print = bc_num_printDigits;
  }

  if ((status = bc_vec_init(&stack, sizeof(long), NULL))) return status;
  if ((status = bc_num_init(&intp, n->len))) goto int_err;
  if ((status = bc_num_init(&fracp, n->rdx))) goto frac_err;
  if ((status = bc_num_init(&digit, width))) goto digit_err;
  if ((status = bc_num_copy(&intp, n))) goto frac_len_err;

  bc_num_truncate(&intp, intp.rdx);
  if ((status = bc_num_sub(n, &intp, &fracp, 0))) goto frac_len_err;

  while (intp.len) {
    if ((status = bc_num_mod(&intp, base, &digit, 0))) goto frac_len_err;
    if ((status = bc_num_ulong(&digit, &dig))) goto frac_len_err;
    if ((status = bc_vec_push(&stack, &dig))) goto frac_len_err;
    if ((status = bc_num_div(&intp, base, &intp, 0))) goto frac_len_err;
  }

  for (i = 0; i < stack.len; ++i) {
    ptr = bc_vec_item_rev(&stack, i);
    status = print(*ptr, width, 0, nchars, line_len);
    if (status) goto frac_len_err;
  }

  if (!n->rdx || (status = bc_num_init(&frac_len, n->len - n->rdx)))
    goto frac_len_err;

  bc_num_one(&frac_len);

  for (radix = 1; frac_len.len <= n->rdx; radix = 0) {
    if ((status = bc_num_mul(&fracp, base, &fracp, n->rdx))) goto err;
    if ((status = bc_num_ulong(&fracp, &dig))) goto err;
    if ((status = bc_num_ulong2num(&intp, dig))) goto err;
    if ((status = bc_num_sub(&fracp, &intp, &fracp, 0))) goto err;
    if ((status = print(dig, width, radix, nchars, line_len))) goto err;
    if ((status = bc_num_mul(&frac_len, base, &frac_len, 0))) goto err;
  }

err:
  n->neg = neg;
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

  memset(n, 0, sizeof(BcNum));

  request = request >= BC_NUM_DEF_SIZE ? request : BC_NUM_DEF_SIZE;
  if (!(n->num = malloc(request))) return BC_STATUS_MALLOC_FAIL;

  n->cap = request;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_expand(BcNum *n, size_t request) {

  BcDigit *temp;

  if (request <= n->cap) return BC_STATUS_SUCCESS;
  if (!(temp = realloc(n->num, request))) return BC_STATUS_MALLOC_FAIL;

  memset(temp + n->cap, 0, sizeof(char) * (request - n->cap));
  n->num = temp;
  n->cap = request;

  return BC_STATUS_SUCCESS;
}

void bc_num_free(void *num) {
  BcNum *n = (BcNum*) num;
  if (n && n->num) free(n->num);
}

BcStatus bc_num_copy(BcNum *d, BcNum *s) {

  BcStatus status;

  if (d == s) return BC_STATUS_SUCCESS;
  if ((status = bc_num_expand(d, s->cap))) return status;

  d->len = s->len;
  d->neg = s->neg;
  d->rdx = s->rdx;

  memcpy(d->num, s->num, sizeof(BcDigit) * d->len);
  memset(d->num + d->len, 0, sizeof(BcDigit) * (d->cap - d->len));

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_parse(BcNum *n, const char *val, BcNum *base, size_t base_t) {

  BcStatus status;

  if (!bc_num_strValid(val, base_t)) return BC_STATUS_MATH_BAD_STRING;

  if (base_t == 10) status = bc_num_parseDecimal(n, val);
  else status = bc_num_parseBase(n, val, base);

  return status;
}

BcStatus bc_num_print(BcNum *n, BcNum *base, size_t base_t, int newline,
                      size_t *nchars, size_t line_len)
{
  BcStatus status;

  if (*nchars  >= line_len) {
    if (putchar('\\') == EOF) return BC_STATUS_IO_ERR;
    if (putchar('\n') == EOF) return BC_STATUS_IO_ERR;
    *nchars = 0;
  }

  if (!n->len) {
    if (putchar('0') == EOF) return BC_STATUS_IO_ERR;
    ++(*nchars);
    status = BC_STATUS_SUCCESS;
  }
  else if (base_t == 10) status = bc_num_printDecimal(n, nchars, line_len);
  else status = bc_num_printBase(n, base, base_t, nchars, line_len);

  if (status) return status;

  if (newline) {
    if (putchar('\n') == EOF) return BC_STATUS_IO_ERR;
    *nchars = 0;
  }

  return status;
}

BcStatus bc_num_ulong(BcNum *n, unsigned long *result) {

  size_t i;
  unsigned long prev, pow;

  if (n->neg) return BC_STATUS_MATH_NEGATIVE;

  for (*result = 0, pow = 1, i = n->rdx; i < n->len; ++i) {

    prev = *result;
    *result += n->num[i] * pow;
    pow *= 10;

    if (*result < prev) return BC_STATUS_MATH_OVERFLOW;
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_ulong2num(BcNum *n, unsigned long val) {

  BcStatus status;
  size_t len, i;
  BcDigit *ptr;

  bc_num_zero(n);

  if (!val) {
    memset(n->num, 0, sizeof(char) * n->cap);
    return BC_STATUS_SUCCESS;
  }

  len = (size_t) ceil(log10(((double) ULONG_MAX) + 1.0f));

  if ((status = bc_num_expand(n, len))) return status;

  for (ptr = n->num, i = 0; val; ++i, ++n->len) {
    ptr[i] = (char) (val % 10);
    val /= 10;
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_add(BcNum *a, BcNum *b, BcNum *result, size_t scale) {
  (void) scale;
  BcNumBinaryFunc op = (!a->neg == !b->neg) ? bc_num_alg_a : bc_num_alg_s;
  return bc_num_binary(a, b, result, 0, op, a->len + b->len + 1);
}

BcStatus bc_num_sub(BcNum *a, BcNum *b, BcNum *result, size_t scale) {
  (void) scale;
  BcNumBinaryFunc op = (!a->neg == !b->neg) ? bc_num_alg_s : bc_num_alg_a;
  return bc_num_binary(a, b, result, 1, op, a->len + b->len + 1);
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

  BcStatus status;
  BcNum a2, *ptr_a, num1, num2, two, f, fprime, *x0, *x1, *temp;
  size_t pow, len, digits, resrdx, req;
  int cmp;

  req = a->rdx + (a->len - a->rdx) * 2 + 1;

  if (result == a) {
    memcpy(&a2, result, sizeof(BcNum));
    ptr_a = &a2;
    status = bc_num_init(result, req);
  }
  else {
    ptr_a = a;
    status = bc_num_expand(result, req);
  }

  if (status) goto init_err;

  if (!ptr_a->len) {
    bc_num_zero(result);
    return BC_STATUS_SUCCESS;
  }
  else if (ptr_a->neg) return BC_STATUS_MATH_NEG_SQRT;
  else if (BC_NUM_ONE(a)) {
    bc_num_one(result);
    return bc_num_extend(result, scale);
  }

  memset(result->num, 0, result->cap * sizeof(BcDigit));
  len = ptr_a->len;

  scale = maxof(scale, ptr_a->rdx) + 1;

  if ((status = bc_num_init(&num1, len))) return status;
  if ((status = bc_num_init(&num2, num1.len))) goto num2_err;
  if ((status = bc_num_init(&two, BC_NUM_DEF_SIZE))) goto two_err;

  bc_num_one(&two);
  two.num[0] = 2;

  len += scale;

  if ((status = bc_num_init(&f, len))) goto f_err;
  if ((status = bc_num_init(&fprime, len + scale))) goto fprime_err;

  x0 = &num1;
  x1 = &num2;

  bc_num_one(x0);

  pow = ptr_a->len - ptr_a->rdx;

  if (pow) {

    if (pow & 1) {
      x0->num[0] = 2;
      pow -= 1;
    }
    else {
      x0->num[0] = 6;
      pow -= 2;
    }

    if ((status = bc_num_extend(x0, pow))) goto err;
  }

  cmp = 1;
  x0->rdx = digits = 0;
  resrdx = scale + 1;
  len = (x0->len - x0->rdx) + resrdx;

  while (!TT.signe && cmp && digits <= len) {

    if ((status = bc_num_mul(x0, x0, &f, resrdx))) goto err;
    if ((status = bc_num_sub(&f, a, &f, resrdx))) goto err;
    if ((status = bc_num_mul(x0, &two, &fprime, resrdx))) goto err;
    if ((status = bc_num_div(&f, &fprime, &f, resrdx))) goto err;
    if ((status = bc_num_sub(x0, &f, x1, resrdx))) goto err;

    cmp = bc_num_cmp(x1, x0);
    digits = x1->len - llabs(cmp);

    temp = x0;
    x0 = x1;
    x1 = temp;
  }

  if (TT.signe) {
    status = BC_STATUS_EXEC_SIGNAL;
    goto err;
  }

  if ((status = bc_num_copy(result, x0))) goto err;

  if (result->rdx > --scale) bc_num_truncate(result, result->rdx - scale);
  else if (result->rdx < scale)
    status = bc_num_extend(result, scale - result->rdx);

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
init_err:
  if (result == a) bc_num_free(&a2);
  return status;
}

void bc_num_zero(BcNum *n) {
  if (!n) return;
  memset(n->num, 0, n->cap * sizeof(char));
  n->neg = 0;
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

BcStatus bc_func_insert(BcFunc *f, char *name, int var) {

  BcAuto a;
  size_t i;

  for (i = 0; i < f->autos.len; ++i) {
    if (!strcmp(name, ((BcAuto*) bc_vec_item(&f->autos, i))->name))
      return BC_STATUS_PARSE_DUPLICATE_LOCAL;
  }

  a.var = var;
  a.name = name;

  return bc_vec_push(&f->autos, &a);
}

BcStatus bc_func_init(BcFunc *f) {

  BcStatus status;

  if ((status = bc_vec_init(&f->code, sizeof(uint8_t), NULL))) return status;
  if ((status = bc_vec_init(&f->autos, sizeof(BcAuto), bc_auto_free))) goto err;
  if ((status = bc_vec_init(&f->labels, sizeof(size_t), NULL))) goto label_err;

  f->nparams = 0;

  return BC_STATUS_SUCCESS;

label_err:
  bc_vec_free(&f->autos);
err:
  bc_vec_free(&f->code);
  return status;
}

void bc_func_free(void *func) {

  BcFunc *f = (BcFunc*) func;

  if (!f) return;

  bc_vec_free(&f->code);
  bc_vec_free(&f->autos);
  bc_vec_free(&f->labels);
}

BcStatus bc_array_copy(BcVec *d, BcVec *s) {

  BcStatus status;
  size_t i;
  BcNum *dnum, *snum;

  bc_vec_npop(d, d->len);

  if ((status = bc_vec_expand(d, s->cap))) return status;

  d->len = s->len;

  for (i = 0; !status && i < s->len; ++i) {

    dnum = bc_vec_item(d, i);
    snum = bc_vec_item(s, i);

    if ((status = bc_num_init(dnum, snum->len))) return status;
    if ((status = bc_num_copy(dnum, snum))) bc_num_free(dnum);
  }

  return status;
}

BcStatus bc_array_expand(BcVec *a, size_t len) {

  BcStatus status = BC_STATUS_SUCCESS;
  BcNum num;

  while (!status && len > a->len) {
    if ((status = bc_num_init(&num, BC_NUM_DEF_SIZE))) return status;
    bc_num_zero(&num);
    if ((status = bc_vec_push(a, &num))) bc_num_free(&num);
  }

  return status;
}

void bc_string_free(void *string) {
  char **s = string;
  if (s) free(*s);
}

int bc_entry_cmp(void *entry1, void *entry2) {
  return strcmp(((BcEntry*) entry1)->name, ((BcEntry*) entry2)->name);
}

void bc_entry_free(void *entry) {
  BcEntry *e = entry;
  if (e) free(e->name);
}

void bc_auto_free(void *auto1) {
  BcAuto *a = (BcAuto*) auto1;
  if (a && a->name) free(a->name);
}

void bc_result_free(void *result) {

  BcResult *r = (BcResult*) result;

  if (!r) return;

  switch (r->type) {

    case BC_RESULT_TEMP:
    case BC_RESULT_SCALE:
    case BC_RESULT_VAR_AUTO:
    {
      bc_num_free(&r->data.num);
      break;
    }

    case BC_RESULT_ARRAY_AUTO:
    {
      bc_vec_free(&r->data.array);
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

BcStatus bc_lex_string(BcLex *lex) {

  const char *start;
  size_t newlines, len, i, j;
  char c;

  newlines = 0;
  lex->token.type = BC_LEX_STRING;
  i = lex->idx;

  for (c = lex->buffer[i]; c != '"' && c != '\0'; c = lex->buffer[++i]) {
    if (c == '\n') ++newlines;
  }

  if (c == '\0') {
    lex->idx = i;
    return BC_STATUS_LEX_NO_STRING_END;
  }

  len = i - lex->idx;
  if (!(lex->token.string = malloc(len + 1))) return BC_STATUS_MALLOC_FAIL;

  start = lex->buffer + lex->idx;

  for (j = 0; j < len; ++j) lex->token.string[j] = start[j];

  lex->token.string[len] = '\0';
  lex->idx = i + 1;
  lex->line += newlines;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_comment(BcLex *lex) {

  size_t newlines, i;
  const char *buffer;
  char c;
  int end;

  newlines = 0;
  lex->token.type = BC_LEX_WHITESPACE;
  end = 0;

  buffer = lex->buffer;

  for (i = ++lex->idx; !end; i += !end) {

    while ((c = buffer[i]) != '*' && c != '\0') {
      if (c == '\n') ++newlines;
      c = buffer[++i];
    }

    if (c == '\0' || buffer[i + 1] == '\0') {
      lex->idx = i;
      return BC_STATUS_LEX_NO_COMMENT_END;
    }

    end = buffer[i + 1] == '/';
  }

  lex->idx = i + 2;
  lex->line += newlines;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_number(BcLex *lex, char start) {

  const char *buffer, *buf;
  size_t backslashes, len, hits, i, j;
  char c;
  int point;

  lex->token.type = BC_LEX_NUMBER;
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

  len = i + 1 * (*(buffer + i - 1) != '.');

  lex->token.string = malloc(len - backslashes * 2 + 1);
  if (!lex->token.string) return BC_STATUS_MALLOC_FAIL;

  lex->token.string[0] = start;
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

    lex->token.string[j - (hits * 2)] = c;
  }

  lex->token.string[j - (hits * 2)] = '\0';
  lex->idx += i;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_name(BcLex *lex) {

  BcStatus status;
  const char *buffer;
  size_t i;
  char c;

  buffer = lex->buffer + lex->idx - 1;

  for (i = 0; i < sizeof(bc_lex_keywords) / sizeof(bc_lex_keywords[0]); ++i) {

    if (!strncmp(buffer, bc_lex_keywords[i].name, bc_lex_keywords[i].len)) {

      lex->token.type = BC_LEX_KEY_AUTO + i;

      if (!bc_lex_keywords[i].posix &&
          (status = bc_posix_error(BC_STATUS_POSIX_BAD_KEYWORD, lex->file,
                                   lex->line, bc_lex_keywords[i].name)))
      {
        return status;
      }

      // We need to minus one because the
      // index has already been incremented.
      lex->idx += bc_lex_keywords[i].len - 1;

      return BC_STATUS_SUCCESS;
    }
  }

  lex->token.type = BC_LEX_NAME;

  i = 0;
  c = buffer[i];

  while ((c >= 'a' && c<= 'z') || (c >= '0' && c <= '9') || c == '_')
    c = buffer[++i];

  if (i > 1 && (status = bc_posix_error(BC_STATUS_POSIX_NAME_LEN,
                                        lex->file, lex->line, buffer)))
  {
    return status;
  }

  if (!(lex->token.string = malloc(i + 1))) return BC_STATUS_MALLOC_FAIL;

  strncpy(lex->token.string, buffer, i);
  lex->token.string[i] = '\0';

  // Increment the index. It is minus one
  // because it has already been incremented.
  lex->idx += i - 1;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_token(BcLex *lex) {

  BcStatus status = BC_STATUS_SUCCESS;
  char c, c2;

  // This is the workhorse of the lexer.
  switch ((c = lex->buffer[lex->idx++])) {

    case '\0':
    case '\n':
    {
      lex->newline = 1;
      lex->token.type = BC_LEX_NEWLINE  + (!c) * (BC_LEX_EOF - BC_LEX_NEWLINE);
      break;
    }

    case '\t':
    case '\v':
    case '\f':
    case '\r':
    case ' ':
    case '\\':
    {
      lex->token.type = BC_LEX_WHITESPACE;
      c = lex->buffer[lex->idx];

      while ((isspace(c) && c != '\n') || c == '\\')
        c = lex->buffer[++lex->idx];

      break;
    }

    case '!':
    {
      c2 = lex->buffer[lex->idx];

      if (c2 == '=') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_REL_NOT_EQ;
      }
      else {

        if ((status = bc_posix_error(BC_STATUS_POSIX_BOOL_OPS,
                                     lex->file, lex->line, "!")))
        {
          return status;
        }

        lex->token.type = BC_LEX_OP_BOOL_NOT;
      }

      break;
    }

    case '"':
    {
      status = bc_lex_string(lex);
      break;
    }

    case '#':
    {
      if ((status = bc_posix_error(BC_STATUS_POSIX_SCRIPT_COMMENT,
                                  lex->file, lex->line, NULL)))
      {
        return status;
      }

      lex->token.type = BC_LEX_WHITESPACE;
      while (++lex->idx < lex->len && lex->buffer[lex->idx] != '\n');

      break;
    }

    case '%':
    {
      if ((c2 = lex->buffer[lex->idx]) == '=') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_ASSIGN_MODULUS;
      }
      else lex->token.type = BC_LEX_OP_MODULUS;
      break;
    }

    case '&':
    {
      if ((c2 = lex->buffer[lex->idx]) == '&') {

        if ((status = bc_posix_error(BC_STATUS_POSIX_BOOL_OPS,
                                     lex->file, lex->line, "&&")))
        {
          return status;
        }

        ++lex->idx;
        lex->token.type = BC_LEX_OP_BOOL_AND;
      }
      else {
        lex->token.type = BC_LEX_INVALID;
        status = BC_STATUS_LEX_BAD_CHARACTER;
      }

      break;
    }

    case '(':
    case ')':
    {
      lex->token.type = c - '(' + BC_LEX_LEFT_PAREN;
      break;
    }

    case '*':
    {
      if ((c2 = lex->buffer[lex->idx]) == '=') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_ASSIGN_MULTIPLY;
      }
      else lex->token.type = BC_LEX_OP_MULTIPLY;
      break;
    }

    case '+':
    {
      if ((c2 = lex->buffer[lex->idx]) == '=') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_ASSIGN_PLUS;
      }
      else if (c2 == '+') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_INC;
      }
      else lex->token.type = BC_LEX_OP_PLUS;
      break;
    }

    case ',':
    {
      lex->token.type = BC_LEX_COMMA;
      break;
    }

    case '-':
    {
      if ((c2 = lex->buffer[lex->idx]) == '=') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_ASSIGN_MINUS;
      }
      else if (c2 == '-') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_DEC;
      }
      else lex->token.type = BC_LEX_OP_MINUS;
      break;
    }

    case '.':
    {
      c2 = lex->buffer[lex->idx];
      if (isdigit(c2)) status = bc_lex_number(lex, c);
      else {
        status = bc_posix_error(BC_STATUS_POSIX_DOT_LAST,
                                lex->file, lex->line, NULL);
        lex->token.type = BC_LEX_KEY_LAST;
      }
      break;
    }

    case '/':
    {
      if ((c2 = lex->buffer[lex->idx]) == '=') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_ASSIGN_DIVIDE;
      }
      else if (c2 == '*') status = bc_lex_comment(lex);
      else lex->token.type = BC_LEX_OP_DIVIDE;
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
      status = bc_lex_number(lex, c);
      break;
    }

    case ';':
    {
      lex->token.type = BC_LEX_SEMICOLON;
      break;
    }

    case '<':
    {
      if ((c2 = lex->buffer[lex->idx]) == '=') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_REL_LESS_EQ;
      }
      else lex->token.type = BC_LEX_OP_REL_LESS;
      break;
    }

    case '=':
    {
      if ((c2 = lex->buffer[lex->idx]) == '=') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_REL_EQUAL;
      }
      else lex->token.type = BC_LEX_OP_ASSIGN;
      break;
    }

    case '>':
    {
      if ((c2 = lex->buffer[lex->idx]) == '=') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_REL_GREATER_EQ;
      }
      else lex->token.type = BC_LEX_OP_REL_GREATER;
      break;
    }

    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    {
      status = bc_lex_number(lex, c);
      break;
    }

    case '[':
    case ']':
    {
      lex->token.type = c - '[' + BC_LEX_LEFT_BRACKET;
      break;
    }

    case '^':
    {
      if ((c2 = lex->buffer[lex->idx]) == '=') {
        ++lex->idx;
        lex->token.type = BC_LEX_OP_ASSIGN_POWER;
      }
      else lex->token.type = BC_LEX_OP_POWER;
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
      status = bc_lex_name(lex);
      break;
    }

    case '{':
    case '}':
    {
      lex->token.type = c - '{' + BC_LEX_LEFT_BRACE;
      break;
    }

    case '|':
    {
      if ((c2 = lex->buffer[lex->idx]) == '|') {

        if ((status = bc_posix_error(BC_STATUS_POSIX_BOOL_OPS,
                                     lex->file, lex->line, "||")))
        {
          return status;
        }

        ++lex->idx;
        lex->token.type = BC_LEX_OP_BOOL_OR;
      }
      else {
        lex->token.type = BC_LEX_INVALID;
        status = BC_STATUS_LEX_BAD_CHARACTER;
      }

      break;
    }

    default:
    {
      lex->token.type = BC_LEX_INVALID;
      status = BC_STATUS_LEX_BAD_CHARACTER;
      break;
    }
  }

  return status;
}

void bc_lex_init(BcLex *lex, const char *file) {
  lex->line = 1;
  lex->newline = 0;
  lex->file = file;
}

BcStatus bc_lex_next(BcLex *lex) {

  BcStatus status;

  if (lex->token.type == BC_LEX_EOF) return BC_STATUS_LEX_EOF;

  if (lex->idx == lex->len) {
    lex->newline = 1;
    lex->token.type = BC_LEX_EOF;
    return BC_STATUS_SUCCESS;
  }

  if (lex->newline) {
    ++lex->line;
    lex->newline = 0;
  }

  // Loop until failure or we don't have whitespace. This
  // is so the parser doesn't get inundated with whitespace.
  do {
    lex->token.string = NULL;
    status = bc_lex_token(lex);
  } while (!status && lex->token.type == BC_LEX_WHITESPACE);

  return status;
}

BcStatus bc_lex_text(BcLex *lex, const char *text) {
  lex->buffer = text;
  lex->idx = 0;
  lex->len = strlen(text);
  lex->token.type = BC_LEX_INVALID;
  return bc_lex_next(lex);
}

BcStatus bc_parse_else(BcParse *p, BcVec *code);
BcStatus bc_parse_stmt(BcParse *p, BcVec *code);

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
  uint8_t amt, i, nums[sizeof(size_t)];

  for (amt = 0; idx; ++amt) {
    nums[amt] = (uint8_t) idx;
    idx = (idx & ~(UINT8_MAX)) >> sizeof(uint8_t) * CHAR_BIT;
  }

  if ((status = bc_vec_pushByte(code, amt))) return status;
  for (i = 0; !status && i < amt; ++i) status = bc_vec_pushByte(code, nums[i]);

  return status;
}

BcStatus bc_parse_operator(BcParse *p, BcVec *code, BcVec *ops, BcLexToken t,
                           uint32_t *num_exprs, int next)
{
  BcStatus status;
  BcLexToken top;
  uint8_t lp, rp;
  int rleft;

  rp = bc_parse_ops[t].prec;
  rleft = bc_parse_ops[t].left;

  while (ops->len &&
         (top = *((BcLexToken*) bc_vec_top(ops))) != BC_LEX_LEFT_PAREN &&
         ((lp = bc_parse_ops[top].prec) < rp || (lp == rp && rleft)))
  {
    status = bc_vec_pushByte(code, BC_PARSE_TOKEN_TO_INST(top));
    if (status) return status;

    bc_vec_pop(ops);

    *num_exprs -= top != BC_LEX_OP_BOOL_NOT && top != BC_LEX_OP_NEG;
  }

  if ((status = bc_vec_push(ops, &t))) return status;
  if (next && (status = bc_lex_next(&p->lex)) && p->lex.token.string) {
    free(p->lex.token.string);
    p->lex.token.string = NULL;
  }

  return status;
}

BcStatus bc_parse_rightParen(BcParse *p, BcVec *code,
                             BcVec *ops, uint32_t *nexs)
{
  BcStatus status;
  BcLexToken top;

  if (!ops->len) return BC_STATUS_PARSE_BAD_EXPR;

  while ((top = *((BcLexToken*) bc_vec_top(ops))) != BC_LEX_LEFT_PAREN) {

    status = bc_vec_pushByte(code, BC_PARSE_TOKEN_TO_INST(top));
    if (status) return status;

    bc_vec_pop(ops);
    *nexs -= top != BC_LEX_OP_BOOL_NOT && top != BC_LEX_OP_NEG;

    if (!ops->len) return BC_STATUS_PARSE_BAD_EXPR;
  }

  bc_vec_pop(ops);

  return bc_lex_next(&p->lex);
}

BcStatus bc_parse_params(BcParse *p, BcVec *code, uint8_t flags) {

  BcStatus status;
  int comma = 0;
  size_t nparams;

  if ((status = bc_lex_next(&p->lex))) return status;

  for (nparams = 0; p->lex.token.type != BC_LEX_RIGHT_PAREN; ++nparams) {

    status = bc_parse_expr(p, code, flags & ~(BC_PARSE_EXPR_PRINT));
    if (status) return status;

    if (p->lex.token.type == BC_LEX_COMMA) {
      comma = 1;
      if ((status = bc_lex_next(&p->lex))) return status;
    }
    else comma = 0;
  }

  if (comma) return BC_STATUS_PARSE_BAD_TOKEN;
  if ((status = bc_vec_pushByte(code, BC_INST_CALL))) return status;

  return bc_parse_pushIndex(code, nparams);
}

BcStatus bc_parse_call(BcParse *p, BcVec *code, char *name, uint8_t flags) {

  BcStatus status;
  BcEntry entry, *entry_ptr;
  size_t idx;

  entry.name = name;

  if ((status = bc_parse_params(p, code, flags))) goto err;

  if (p->lex.token.type != BC_LEX_RIGHT_PAREN) {
    status = BC_STATUS_PARSE_BAD_TOKEN;
    goto err;
  }

  idx = bc_veco_index(&p->prog->func_map, &entry);

  if (idx == -1) {
    if ((status = bc_program_addFunc(p->prog, name, &idx))) return status;
    name = NULL;
  }
  else free(name);

  entry_ptr = bc_veco_item(&p->prog->func_map, idx);
  if ((status = bc_parse_pushIndex(code, entry_ptr->idx))) return status;

  return bc_lex_next(&p->lex);

err:
  if (name) free(name);
  return status;
}

BcStatus bc_parse_name(BcParse *p, BcVec *code, BcInst *type, uint8_t flags)
{
  BcStatus status;
  char *name;

  name = p->lex.token.string;

  if ((status = bc_lex_next(&p->lex))) goto err;

  if (p->lex.token.type == BC_LEX_LEFT_BRACKET) {

    *type = BC_INST_PUSH_ARRAY_ELEM;

    if ((status = bc_lex_next(&p->lex))) goto err;
    if ((status = bc_parse_expr(p, code, flags))) goto err;

    if (p->lex.token.type != BC_LEX_RIGHT_BRACKET) {
      status = BC_STATUS_PARSE_BAD_TOKEN;
      goto err;
    }

    if ((status = bc_vec_pushByte(code, BC_INST_PUSH_ARRAY_ELEM))) goto err;

    status = bc_parse_pushName(code, name);
  }
  else if (p->lex.token.type == BC_LEX_LEFT_PAREN) {

    if (flags & BC_PARSE_EXPR_NOCALL) {
      status = BC_STATUS_PARSE_BAD_TOKEN;
      goto err;
    }

    *type = BC_INST_CALL;
    status = bc_parse_call(p, code, name, flags);
  }
  else {
    *type = BC_INST_PUSH_VAR;
    if ((status = bc_vec_pushByte(code, BC_INST_PUSH_VAR))) goto err;
    status = bc_parse_pushName(code, name);
  }

  return status;

err:
  free(name);
  return status;
}

BcStatus bc_parse_read(BcParse *p, BcVec *code) {

  BcStatus status;

  if ((status = bc_lex_next(&p->lex))) return status;
  if (p->lex.token.type != BC_LEX_LEFT_PAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  if ((status = bc_lex_next(&p->lex))) return status;
  if (p->lex.token.type != BC_LEX_RIGHT_PAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  if ((status = bc_vec_pushByte(code, BC_INST_READ))) return status;

  return bc_lex_next(&p->lex);
}

BcStatus bc_parse_builtin(BcParse *p, BcVec *code,
                          BcLexToken type, uint8_t flags)
{
  BcStatus status;
  uint8_t inst;

  if ((status = bc_lex_next(&p->lex))) return status;
  if (p->lex.token.type != BC_LEX_LEFT_PAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  if ((status = bc_lex_next(&p->lex))) return status;

  status = bc_parse_expr(p, code, flags & ~(BC_PARSE_EXPR_PRINT));
  if (status) return status;
  if (p->lex.token.type != BC_LEX_RIGHT_PAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  inst = type == BC_LEX_KEY_LENGTH ? BC_INST_LENGTH : BC_INST_SQRT;
  if ((status = bc_vec_pushByte(code, inst))) return status;

  return bc_lex_next(&p->lex);
}

BcStatus bc_parse_scale(BcParse *p, BcVec *code, BcInst *type, uint8_t flags) {

  BcStatus status;

  if ((status = bc_lex_next(&p->lex))) return status;

  if (p->lex.token.type != BC_LEX_LEFT_PAREN) {
    *type = BC_INST_PUSH_SCALE;
    return bc_vec_pushByte(code, BC_INST_PUSH_SCALE);
  }

  *type = BC_INST_SCALE_FUNC;

  if ((status = bc_lex_next(&p->lex))) return status;
  if ((status = bc_parse_expr(p, code, flags))) return status;
  if (p->lex.token.type != BC_LEX_RIGHT_PAREN) return BC_STATUS_PARSE_BAD_TOKEN;
  if ((status = bc_vec_pushByte(code, BC_INST_SCALE_FUNC))) return status;

  return bc_lex_next(&p->lex);
}

BcStatus bc_parse_incdec(BcParse *p, BcVec *code, BcInst *prev,
                         uint32_t *nexprs, uint8_t flags)
{
  BcStatus status;
  BcLexToken type;
  BcInst etype;
  uint8_t inst;

  etype = *prev;

  if (etype == BC_INST_PUSH_VAR || etype == BC_INST_PUSH_ARRAY_ELEM ||
      etype == BC_INST_PUSH_SCALE || etype == BC_INST_PUSH_LAST ||
      etype == BC_INST_PUSH_IBASE || etype == BC_INST_PUSH_OBASE)
  {
    *prev = inst = BC_INST_INC_POST + (p->lex.token.type != BC_LEX_OP_INC);
    if ((status = bc_vec_pushByte(code, inst))) return status;
    status = bc_lex_next(&p->lex);
  }
  else {

    *prev = inst = BC_INST_INC_PRE + (p->lex.token.type != BC_LEX_OP_INC);

    if ((status = bc_lex_next(&p->lex))) return status;
    type = p->lex.token.type;

    // Because we parse the next part of the expression
    // right here, we need to increment this.
    *nexprs = *nexprs + 1;

    switch (type) {

      case BC_LEX_NAME:
      {
        status = bc_parse_name(p, code, prev, flags | BC_PARSE_EXPR_NOCALL);
        break;
      }

      case BC_LEX_KEY_IBASE:
      {
        if ((status = bc_vec_pushByte(code, BC_INST_PUSH_IBASE))) return status;
        status = bc_lex_next(&p->lex);
        break;
      }

      case BC_LEX_KEY_LAST:
      {
        if ((status = bc_vec_pushByte(code, BC_INST_PUSH_LAST))) return status;
        status = bc_lex_next(&p->lex);
        break;
      }

      case BC_LEX_KEY_OBASE:
      {
        if ((status = bc_vec_pushByte(code, BC_INST_PUSH_OBASE))) return status;
        status = bc_lex_next(&p->lex);
        break;
      }

      case BC_LEX_KEY_SCALE:
      {
        if ((status = bc_lex_next(&p->lex))) return status;
        if (p->lex.token.type == BC_LEX_LEFT_PAREN)
          return BC_STATUS_PARSE_BAD_TOKEN;

        status = bc_vec_pushByte(code, BC_INST_PUSH_SCALE);

        break;
      }

      default:
      {
        return BC_STATUS_PARSE_BAD_TOKEN;
      }
    }

    if (status) return status;
    status = bc_vec_pushByte(code, inst);
  }

  return status;
}

BcStatus bc_parse_minus(BcParse *p, BcVec *exs, BcVec *ops, BcInst *prev,
                        int rparen, uint32_t *nexprs)
{
  BcStatus status;
  BcLexToken type;
  BcInst etype;

  if ((status = bc_lex_next(&p->lex))) return status;

  etype = *prev;
  type = p->lex.token.type;

  if (type != BC_LEX_NAME && type != BC_LEX_NUMBER &&
      type != BC_LEX_KEY_SCALE && type != BC_LEX_KEY_LAST &&
      type != BC_LEX_KEY_IBASE && type != BC_LEX_KEY_OBASE &&
      type != BC_LEX_LEFT_PAREN && type != BC_LEX_OP_MINUS &&
      type != BC_LEX_OP_INC && type != BC_LEX_OP_DEC &&
      type != BC_LEX_OP_BOOL_NOT)
  {
    return BC_STATUS_PARSE_BAD_TOKEN;
  }

  type = rparen || etype == BC_INST_INC_POST || etype == BC_INST_DEC_POST ||
         (etype >= BC_INST_PUSH_NUM && etype <= BC_INST_SQRT) ?
                  BC_LEX_OP_MINUS : BC_LEX_OP_NEG;
  *prev = BC_PARSE_TOKEN_TO_INST(type);

  if (type == BC_LEX_OP_MINUS)
    status = bc_parse_operator(p, exs, ops, type, nexprs, 0);
  else
    // We can just push onto the op stack because this is the largest
    // precedence operator that gets pushed. Inc/dec does not.
    status = bc_vec_push(ops, &type);

  return status;
}

BcStatus bc_parse_string(BcParse *p, BcVec *code) {

  BcStatus status;
  size_t len;

  if (strlen(p->lex.token.string) > (unsigned long) maxof_STRING) {
    status = BC_STATUS_EXEC_STRING_LEN;
    goto err;
  }

  len = p->prog->strings.len;

  if ((status = bc_vec_push(&p->prog->strings, &p->lex.token.string))) goto err;
  if ((status = bc_vec_pushByte(code, BC_INST_STR))) return status;
  if ((status = bc_parse_pushIndex(code, len))) return status;

  return bc_lex_next(&p->lex);

err:
  free(p->lex.token.string);
  return status;
}

BcStatus bc_parse_print(BcParse *p, BcVec *code) {

  BcStatus status;
  BcLexToken type;
  int comma;

  if ((status = bc_lex_next(&p->lex))) return status;

  type = p->lex.token.type;

  if (type == BC_LEX_SEMICOLON || type == BC_LEX_NEWLINE)
    return BC_STATUS_PARSE_BAD_PRINT;

  comma = 0;

  while (!status && type != BC_LEX_SEMICOLON && type != BC_LEX_NEWLINE) {

    if (type == BC_LEX_STRING) {

      size_t len = p->prog->strings.len;

      status = bc_vec_push(&p->prog->strings, &p->lex.token.string);
      if (status) {
        free(p->lex.token.string);
        return status;
      }

      if ((status = bc_vec_pushByte(code, BC_INST_PRINT_STR))) return status;
      status = bc_parse_pushIndex(code, len);
    }
    else {
      if ((status = bc_parse_expr(p, code, 0))) return status;
      status = bc_vec_pushByte(code, BC_INST_PRINT_EXPR);
    }

    if (status) return status;
    if ((status = bc_lex_next(&p->lex))) return status;

    if (p->lex.token.type == BC_LEX_COMMA) {
      comma = 1;
      status = bc_lex_next(&p->lex);
    }
    else comma = 0;

    type = p->lex.token.type;
  }

  if (status) return status;
  if (comma) return BC_STATUS_PARSE_BAD_TOKEN;

  return bc_lex_next(&p->lex);
}

BcStatus bc_parse_return(BcParse *p, BcVec *code) {

  BcStatus status;

  if (!BC_PARSE_FUNC(p)) return BC_STATUS_PARSE_BAD_TOKEN;

  if ((status = bc_lex_next(&p->lex))) return status;

  if (p->lex.token.type != BC_LEX_NEWLINE &&
      p->lex.token.type != BC_LEX_SEMICOLON &&
      p->lex.token.type != BC_LEX_LEFT_PAREN &&
      (status = bc_posix_error(BC_STATUS_POSIX_RETURN_PARENS,
                               p->lex.file, p->lex.line, NULL)))
  {
     return status;
  }

  if (p->lex.token.type == BC_LEX_NEWLINE ||
      p->lex.token.type == BC_LEX_SEMICOLON)
  {
    status = bc_vec_pushByte(code, BC_INST_RETURN_ZERO);
  }
  else {
    if ((status = bc_parse_expr(p, code, 0))) return status;
    status = bc_vec_pushByte(code, BC_INST_RETURN);
  }

  return status;
}

BcStatus bc_parse_endBody(BcParse *p, BcVec *code, int brace) {

  BcStatus status = BC_STATUS_SUCCESS;
  uint8_t *flag_ptr;

  if (p->flags.len <= 1 || p->num_braces == 0) return BC_STATUS_PARSE_BAD_TOKEN;

  if (brace) {

    if (p->lex.token.type == BC_LEX_RIGHT_BRACE) {

      if (!p->num_braces) return BC_STATUS_PARSE_BAD_TOKEN;

      --p->num_braces;

      if ((status = bc_lex_next(&p->lex))) return status;
    }
    else return BC_STATUS_PARSE_BAD_TOKEN;
  }

  if (BC_PARSE_IF(p)) {

    while (p->lex.token.type == BC_LEX_NEWLINE) {
      if ((status = bc_lex_next(&p->lex))) return status;
    }

    bc_vec_pop(&p->flags);

    flag_ptr = BC_PARSE_TOP_FLAG_PTR(p);
    *flag_ptr = (*flag_ptr | BC_PARSE_FLAG_IF_END);

    if (p->lex.token.type == BC_LEX_KEY_ELSE) status = bc_parse_else(p, code);
  }
  else if (BC_PARSE_ELSE(p)) {

    BcInstPtr *ip;
    BcFunc *func;
    size_t *label;

    bc_vec_pop(&p->flags);

    ip = bc_vec_top(&p->exits);
    func = bc_vec_item(&p->prog->funcs, p->func);
    label = bc_vec_item(&func->labels, ip->idx);
    *label = code->len;

    bc_vec_pop(&p->exits);
  }
  else if (BC_PARSE_FUNC_INNER(p)) {
    p->func = 0;
    if ((status = bc_vec_pushByte(code, BC_INST_RETURN_ZERO))) return status;
    bc_vec_pop(&p->flags);
  }
  else {

    BcInstPtr *ip;
    BcFunc *func;
    size_t *label;

    if ((status = bc_vec_pushByte(code, BC_INST_JUMP))) return status;

    ip = bc_vec_top(&p->exits);
    label = bc_vec_top(&p->conds);

    if ((status = bc_parse_pushIndex(code, *label))) return status;

    func = bc_vec_item(&p->prog->funcs, p->func);
    label = bc_vec_item(&func->labels, ip->idx);
    *label = code->len;

    bc_vec_pop(&p->flags);
    bc_vec_pop(&p->exits);
    bc_vec_pop(&p->conds);
  }

  return status;
}

BcStatus bc_parse_startBody(BcParse *p, uint8_t flags) {
  uint8_t *flag_ptr = BC_PARSE_TOP_FLAG_PTR(p);
  flags |= (*flag_ptr & (BC_PARSE_FLAG_FUNC | BC_PARSE_FLAG_LOOP));
  flags |= BC_PARSE_FLAG_BODY;
  return bc_vec_push(&p->flags, &flags);
}

void bc_parse_noElse(BcParse *p, BcVec *code) {

  uint8_t *flag_ptr;
  BcInstPtr *ip;
  BcFunc *func;
  size_t *label;

  flag_ptr = BC_PARSE_TOP_FLAG_PTR(p);
  *flag_ptr = (*flag_ptr & ~(BC_PARSE_FLAG_IF_END));

  ip = bc_vec_top(&p->exits);
  func = bc_vec_item(&p->prog->funcs, p->func);
  label = bc_vec_item(&func->labels, ip->idx);
  *label = code->len;

  bc_vec_pop(&p->exits);
}

BcStatus bc_parse_if(BcParse *p, BcVec *code) {

  BcStatus status;
  BcInstPtr ip;
  BcFunc *func;

  if ((status = bc_lex_next(&p->lex))) return status;
  if (p->lex.token.type != BC_LEX_LEFT_PAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  if ((status = bc_lex_next(&p->lex))) return status;
  status = bc_parse_expr(p, code, BC_PARSE_EXPR_POSIX_REL);
  if (status) return status;
  if (p->lex.token.type != BC_LEX_RIGHT_PAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  if ((status = bc_lex_next(&p->lex))) return status;
  if ((status = bc_vec_pushByte(code, BC_INST_JUMP_ZERO))) return status;

  func = bc_vec_item(&p->prog->funcs, p->func);

  ip.idx = func->labels.len;
  ip.func = 0;
  ip.len = 0;

  if ((status = bc_parse_pushIndex(code, ip.idx))) return status;
  if ((status = bc_vec_push(&p->exits, &ip))) return status;
  if ((status = bc_vec_push(&func->labels, &ip.idx))) return status;

  return bc_parse_startBody(p, BC_PARSE_FLAG_IF);
}

BcStatus bc_parse_else(BcParse *p, BcVec *code) {

  BcStatus status;
  BcInstPtr ip;
  BcFunc *func;

  if (!BC_PARSE_IF_END(p)) return BC_STATUS_PARSE_BAD_TOKEN;

  func = bc_vec_item(&p->prog->funcs, p->func);

  ip.idx = func->labels.len;
  ip.func = 0;
  ip.len = 0;

  if ((status = bc_vec_pushByte(code, BC_INST_JUMP))) return status;
  if ((status = bc_parse_pushIndex(code, ip.idx))) return status;

  bc_parse_noElse(p, code);

  if ((status = bc_vec_push(&p->exits, &ip))) return status;
  if ((status = bc_vec_push(&func->labels, &ip.idx))) return status;
  if ((status = bc_lex_next(&p->lex))) return status;

  return bc_parse_startBody(p, BC_PARSE_FLAG_ELSE);
}

BcStatus bc_parse_while(BcParse *p, BcVec *code) {

  BcStatus status;
  BcFunc *func;
  BcInstPtr ip;

  if ((status = bc_lex_next(&p->lex))) return status;
  if (p->lex.token.type != BC_LEX_LEFT_PAREN) return BC_STATUS_PARSE_BAD_TOKEN;
  if ((status = bc_lex_next(&p->lex))) return status;

  func = bc_vec_item(&p->prog->funcs, p->func);
  ip.idx = func->labels.len;

  if ((status = bc_vec_push(&func->labels, &code->len))) return status;
  if ((status = bc_vec_push(&p->conds, &ip.idx))) return status;

  ip.idx = func->labels.len;
  ip.func = 1;
  ip.len = 0;

  if ((status = bc_vec_push(&p->exits, &ip))) return status;
  if ((status = bc_vec_push(&func->labels, &ip.idx))) return status;

  if ((status = bc_parse_expr(p, code, BC_PARSE_EXPR_POSIX_REL))) return status;
  if (p->lex.token.type != BC_LEX_RIGHT_PAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  if ((status = bc_lex_next(&p->lex))) return status;
  if ((status = bc_vec_pushByte(code, BC_INST_JUMP_ZERO))) return status;
  if ((status = bc_parse_pushIndex(code, ip.idx))) return status;

  return bc_parse_startBody(p, BC_PARSE_FLAG_LOOP | BC_PARSE_FLAG_LOOP_INNER);
}

BcStatus bc_parse_for(BcParse *p, BcVec *code) {

  BcStatus status;
  BcFunc *func;
  BcInstPtr ip;
  size_t cond_idx, exit_idx, body_idx, update_idx;

  if ((status = bc_lex_next(&p->lex))) return status;
  if (p->lex.token.type != BC_LEX_LEFT_PAREN) return BC_STATUS_PARSE_BAD_TOKEN;
  if ((status = bc_lex_next(&p->lex))) return status;

  if (p->lex.token.type != BC_LEX_SEMICOLON)
    status = bc_parse_expr(p, code, 0);
  else
    status = bc_posix_error(BC_STATUS_POSIX_NO_FOR_INIT,
                            p->lex.file, p->lex.line, NULL);

  if (status) return status;
  if (p->lex.token.type != BC_LEX_SEMICOLON) return BC_STATUS_PARSE_BAD_TOKEN;
  if ((status = bc_lex_next(&p->lex))) return status;

  func = bc_vec_item(&p->prog->funcs, p->func);

  cond_idx = func->labels.len;
  update_idx = cond_idx + 1;
  body_idx = update_idx + 1;
  exit_idx = body_idx + 1;

  if ((status = bc_vec_push(&func->labels, &code->len))) return status;

  if (p->lex.token.type != BC_LEX_SEMICOLON)
    status = bc_parse_expr(p, code, BC_PARSE_EXPR_POSIX_REL);
  else status = bc_posix_error(BC_STATUS_POSIX_NO_FOR_COND,
                               p->lex.file, p->lex.line, NULL);

  if (status) return status;
  if (p->lex.token.type != BC_LEX_SEMICOLON) return BC_STATUS_PARSE_BAD_TOKEN;

  if ((status = bc_lex_next(&p->lex))) return status;
  if ((status = bc_vec_pushByte(code, BC_INST_JUMP_ZERO))) return status;
  if ((status = bc_parse_pushIndex(code, exit_idx))) return status;
  if ((status = bc_vec_pushByte(code, BC_INST_JUMP))) return status;
  if ((status = bc_parse_pushIndex(code, body_idx))) return status;

  ip.idx = func->labels.len;

  if ((status = bc_vec_push(&p->conds, &update_idx))) return status;
  if ((status = bc_vec_push(&func->labels, &code->len))) return status;

  if (p->lex.token.type != BC_LEX_RIGHT_PAREN)
    status = bc_parse_expr(p, code, 0);
  else
    status = bc_posix_error(BC_STATUS_POSIX_NO_FOR_UPDATE,
                            p->lex.file, p->lex.line, NULL);

  if (status) return status;

  if (p->lex.token.type != BC_LEX_RIGHT_PAREN) {
    status = bc_parse_expr(p, code, BC_PARSE_EXPR_POSIX_REL);
    if (status) return status;
  }

  if (p->lex.token.type != BC_LEX_RIGHT_PAREN) return BC_STATUS_PARSE_BAD_TOKEN;
  if ((status = bc_vec_pushByte(code, BC_INST_JUMP))) return status;
  if ((status = bc_parse_pushIndex(code, cond_idx))) return status;
  if ((status = bc_vec_push(&func->labels, &code->len))) return status;

  ip.idx = exit_idx;
  ip.func = 1;
  ip.len = 0;

  if ((status = bc_vec_push(&p->exits, &ip))) return status;
  if ((status = bc_vec_push(&func->labels, &ip.idx))) return status;
  if ((status = bc_lex_next(&p->lex))) return status;

  return bc_parse_startBody(p, BC_PARSE_FLAG_LOOP | BC_PARSE_FLAG_LOOP_INNER);
}

BcStatus bc_parse_loopExit(BcParse *p, BcVec *code, BcLexToken type) {

  BcStatus status;
  size_t idx, top;
  BcInstPtr *ip;

  if (!BC_PARSE_LOOP(p)) return BC_STATUS_PARSE_BAD_TOKEN;

  if (type == BC_LEX_KEY_BREAK) {

    if (!p->exits.len) return BC_STATUS_PARSE_BAD_TOKEN;

    top = p->exits.len - 1;
    ip = bc_vec_item(&p->exits, top);

    while (top < p->exits.len && ip && !ip->func)
      ip = bc_vec_item(&p->exits, top--);

    if (top >= p->exits.len || !ip) return BC_STATUS_PARSE_BAD_TOKEN;

    idx = ip->idx;
  }
  else idx = *((size_t*) bc_vec_top(&p->conds));

  if ((status = bc_vec_pushByte(code, BC_INST_JUMP))) return status;
  if ((status = bc_parse_pushIndex(code, idx))) return status;
  if ((status = bc_lex_next(&p->lex))) return status;

  if (p->lex.token.type != BC_LEX_SEMICOLON &&
      p->lex.token.type != BC_LEX_NEWLINE)
  {
    return BC_STATUS_PARSE_BAD_TOKEN;
  }

  return bc_lex_next(&p->lex);
}

BcStatus bc_parse_func(BcParse *p) {

  BcStatus status;
  BcFunc *fptr;
  int var, comma = 0;
  uint8_t flags;
  char *name;

  if ((status = bc_lex_next(&p->lex))) return status;

  name = p->lex.token.string;

  if (p->lex.token.type != BC_LEX_NAME) {
    status = BC_STATUS_PARSE_BAD_FUNC;
    goto err;
  }

  status = bc_program_addFunc(p->prog, name, &p->func);
  if (status) goto err;

  fptr = bc_vec_item(&p->prog->funcs, p->func);

  if ((status = bc_lex_next(&p->lex))) return status;
  if (p->lex.token.type != BC_LEX_LEFT_PAREN) return BC_STATUS_PARSE_BAD_FUNC;
  if ((status = bc_lex_next(&p->lex))) return status;

  while (!status && p->lex.token.type != BC_LEX_RIGHT_PAREN) {

    if (p->lex.token.type != BC_LEX_NAME) {
      status = BC_STATUS_PARSE_BAD_FUNC;
      goto err;
    }

    ++fptr->nparams;
    name = p->lex.token.string;

    if ((status = bc_lex_next(&p->lex))) goto err;

    var = p->lex.token.type != BC_LEX_LEFT_BRACKET;

    if (!var) {
      if ((status = bc_lex_next(&p->lex))) goto err;
      if (p->lex.token.type != BC_LEX_RIGHT_BRACKET)
        return BC_STATUS_PARSE_BAD_FUNC;
      if ((status = bc_lex_next(&p->lex))) goto err;
    }

    comma = p->lex.token.type == BC_LEX_COMMA;
    if (comma && (status = bc_lex_next(&p->lex))) goto err;

    if ((status = bc_func_insert(fptr, name, var))) goto err;
  }

  if (comma) return BC_STATUS_PARSE_BAD_FUNC;

  flags = BC_PARSE_FLAG_FUNC | BC_PARSE_FLAG_FUNC_INNER | BC_PARSE_FLAG_BODY;

  if ((status = bc_parse_startBody(p, flags))) return status;
  if ((status = bc_lex_next(&p->lex))) return status;

  if (p->lex.token.type != BC_LEX_LEFT_BRACE)
    return bc_posix_error(BC_STATUS_POSIX_HEADER_BRACE,
                          p->lex.file, p->lex.line, NULL);

  return status;

err:
  free(name);
  return status;
}

BcStatus bc_parse_auto(BcParse *p) {

  BcStatus status;
  int comma, var, one;
  char *name;
  BcFunc *func;

  if (!p->auto_part) return BC_STATUS_PARSE_BAD_TOKEN;
  if ((status = bc_lex_next(&p->lex))) return status;

  p->auto_part = comma = one = 0;
  func = bc_vec_item(&p->prog->funcs, p->func);

  while (!status && p->lex.token.type == BC_LEX_NAME) {

    name = p->lex.token.string;

    if ((status = bc_lex_next(&p->lex))) return status;

    one = 1;

    var = p->lex.token.type != BC_LEX_LEFT_BRACKET;

    if (!var) {

      if ((status = bc_lex_next(&p->lex))) goto err;
      if (p->lex.token.type != BC_LEX_RIGHT_BRACKET)
        return BC_STATUS_PARSE_BAD_FUNC;

      if ((status = bc_lex_next(&p->lex))) goto err;
    }

    comma = p->lex.token.type == BC_LEX_COMMA;
    if (comma && (status = bc_lex_next(&p->lex))) goto err;

    if ((status = bc_func_insert(func, name, var))) goto err;
  }

  if (status) return status;
  if (comma) return BC_STATUS_PARSE_BAD_FUNC;
  if (!one) return BC_STATUS_PARSE_NO_AUTO;

  if (p->lex.token.type != BC_LEX_NEWLINE &&
      p->lex.token.type != BC_LEX_SEMICOLON)
  {
    return BC_STATUS_PARSE_BAD_TOKEN;
  }

  return bc_lex_next(&p->lex);

err:
  free(name);
  return status;
}

BcStatus bc_parse_body(BcParse *p, BcVec *code, int brace) {

  BcStatus status;
  uint8_t *flag_ptr;

  flag_ptr = bc_vec_top(&p->flags);
  *flag_ptr &= ~(BC_PARSE_FLAG_BODY);

  if (*flag_ptr & BC_PARSE_FLAG_FUNC_INNER) {
    if (!brace) return BC_STATUS_PARSE_BAD_TOKEN;
    p->auto_part = 1;
    status = bc_lex_next(&p->lex);
  }
  else {
    if ((status = bc_parse_stmt(p, code))) return status;
    if (!brace) status = bc_parse_endBody(p, code, 0);
  }

  return status;
}

BcStatus bc_parse_stmt(BcParse *p, BcVec *code) {

  BcStatus status;

  switch (p->lex.token.type) {

    case BC_LEX_NEWLINE:
    {
      return bc_lex_next(&p->lex);
    }

    case BC_LEX_KEY_ELSE:
    {
      p->auto_part = 0;
      break;
    }

    case BC_LEX_LEFT_BRACE:
    {
      if (!BC_PARSE_BODY(p)) return BC_STATUS_PARSE_BAD_TOKEN;

      ++p->num_braces;
      if ((status = bc_lex_next(&p->lex))) return status;

      return bc_parse_body(p, code, 1);
    }

    case BC_LEX_KEY_AUTO:
    {
      return bc_parse_auto(p);
    }

    default:
    {
      p->auto_part = 0;

      if (BC_PARSE_IF_END(p)) {
        bc_parse_noElse(p, code);
        return BC_STATUS_SUCCESS;
      }
      else if (BC_PARSE_BODY(p)) return bc_parse_body(p, code, 0);

      break;
    }
  }

  switch (p->lex.token.type) {

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
      status = bc_parse_expr(p, code, BC_PARSE_EXPR_PRINT);
      break;
    }

    case BC_LEX_KEY_ELSE:
    {
      status = bc_parse_else(p, code);
      break;
    }

    case BC_LEX_SEMICOLON:
    {
      status = BC_STATUS_SUCCESS;

      while (!status && p->lex.token.type == BC_LEX_SEMICOLON)
        status = bc_lex_next(&p->lex);

      break;
    }

    case BC_LEX_RIGHT_BRACE:
    {
      status = bc_parse_endBody(p, code, 1);
      break;
    }

    case BC_LEX_STRING:
    {
      status = bc_parse_string(p, code);
      break;
    }

    case BC_LEX_KEY_BREAK:
    case BC_LEX_KEY_CONTINUE:
    {
      status = bc_parse_loopExit(p, code, p->lex.token.type);
      break;
    }

    case BC_LEX_KEY_FOR:
    {
      status = bc_parse_for(p, code);
      break;
    }

    case BC_LEX_KEY_HALT:
    {
      if ((status = bc_vec_pushByte(code, BC_INST_HALT))) return status;
      status = bc_lex_next(&p->lex);
      break;
    }

    case BC_LEX_KEY_IF:
    {
      status = bc_parse_if(p, code);
      break;
    }

    case BC_LEX_KEY_LIMITS:
    {
      if ((status = bc_lex_next(&p->lex))) return status;
      status = BC_STATUS_LIMITS;
      break;
    }

    case BC_LEX_KEY_PRINT:
    {
      status = bc_parse_print(p, code);
      break;
    }

    case BC_LEX_KEY_QUIT:
    {
      // Quit is a compile-time command, so we send an exit command. We don't
      // exit directly, so the vm can clean up. Limits do the same thing.
      status = BC_STATUS_QUIT;
      break;
    }

    case BC_LEX_KEY_RETURN:
    {
      if ((status = bc_parse_return(p, code))) return status;
      break;
    }

    case BC_LEX_KEY_WHILE:
    {
      status = bc_parse_while(p, code);
      break;
    }

    case BC_LEX_EOF:
    {
      status = (p->flags.len > 0) * BC_STATUS_LEX_BAD_CHARACTER;
      break;
    }

    default:
    {
      status = BC_STATUS_PARSE_BAD_TOKEN;
      break;
    }
  }

  return status;
}

BcStatus bc_parse_init(BcParse *p, BcProgram *program) {

  BcStatus status;
  uint8_t flags = 0;

  if ((status = bc_vec_init(&p->flags, sizeof(uint8_t), NULL))) return status;
  if ((status = bc_vec_init(&p->exits, sizeof(BcInstPtr), NULL))) goto exit_err;
  if ((status = bc_vec_init(&p->conds, sizeof(size_t), NULL))) goto cond_err;
  if ((status = bc_vec_push(&p->flags, &flags))) goto push_err;
  if ((status = bc_vec_init(&p->ops, sizeof(BcLexToken), NULL))) goto push_err;

  p->prog = program;
  p->func = p->num_braces = 0;
  p->auto_part = 0;

  return status;

push_err:
  bc_vec_free(&p->conds);
cond_err:
  bc_vec_free(&p->exits);
exit_err:
  bc_vec_free(&p->flags);
  return status;
}

BcStatus bc_parse_parse(BcParse *p) {

  BcStatus status;

  if (p->lex.token.type == BC_LEX_EOF) status = BC_STATUS_LEX_EOF;
  else if (p->lex.token.type == BC_LEX_KEY_DEFINE) {
    if (!BC_PARSE_CAN_EXEC(p)) return BC_STATUS_PARSE_BAD_TOKEN;
    status = bc_parse_func(p);
  }
  else {
    BcFunc *func = bc_vec_item(&p->prog->funcs, p->func);
    status = bc_parse_stmt(p, &func->code);
  }

  if (status || TT.signe) {

    if (p->func) {

      BcFunc *func = bc_vec_item(&p->prog->funcs, p->func);

      func->nparams = 0;
      bc_vec_npop(&func->code, func->code.len);
      bc_vec_npop(&func->autos, func->autos.len);
      bc_vec_npop(&func->labels, func->labels.len);

      p->func = 0;
    }

    p->lex.idx = p->lex.len;
    p->lex.token.type = BC_LEX_EOF;
    p->auto_part = 0;
    p->num_braces = 0;

    bc_vec_npop(&p->flags, p->flags.len - 1);
    bc_vec_npop(&p->exits, p->exits.len);
    bc_vec_npop(&p->conds, p->conds.len);
    bc_vec_npop(&p->ops, p->ops.len);

    status = bc_program_reset(p->prog, status);
  }

  return status;
}

void bc_parse_free(BcParse *p) {

  if (!p) return;

  bc_vec_free(&p->flags);
  bc_vec_free(&p->exits);
  bc_vec_free(&p->conds);
  bc_vec_free(&p->ops);

  if ((p->lex.token.type == BC_LEX_STRING || p->lex.token.type == BC_LEX_NAME ||
       p->lex.token.type == BC_LEX_NUMBER) && p->lex.token.string)
  {
    free(p->lex.token.string);
  }
}

BcStatus bc_parse_expr(BcParse *p, BcVec *code, uint8_t flags) {

  BcStatus status;
  uint32_t nexprs, nparens, ops_start, nrelops;
  int paren_first, paren_expr, rparen, done, get_token, assign;
  BcInst prev;
  BcLexToken type, top;

  status = BC_STATUS_SUCCESS;
  prev = BC_INST_PRINT;

  ops_start = p->ops.len;
  paren_first = p->lex.token.type == BC_LEX_LEFT_PAREN;
  nexprs = nparens = nrelops = 0;
  paren_expr = rparen = done = get_token = assign = 0;

  type = p->lex.token.type;

  while (!TT.signe && !status && !done && bc_parse_token_exprs[type]) {

    switch (type) {

      case BC_LEX_OP_INC:
      case BC_LEX_OP_DEC:
      {
        status = bc_parse_incdec(p, code, &prev, &nexprs, flags);
        rparen = get_token = 0;
        break;
      }

      case BC_LEX_OP_MINUS:
      {
        status = bc_parse_minus(p, code, &p->ops, &prev,
                                rparen, &nexprs);
        rparen = get_token = 0;
        break;
      }

      case BC_LEX_OP_ASSIGN_POWER:
      case BC_LEX_OP_ASSIGN_MULTIPLY:
      case BC_LEX_OP_ASSIGN_DIVIDE:
      case BC_LEX_OP_ASSIGN_MODULUS:
      case BC_LEX_OP_ASSIGN_PLUS:
      case BC_LEX_OP_ASSIGN_MINUS:
      case BC_LEX_OP_ASSIGN:
        if (prev != BC_INST_PUSH_VAR && prev != BC_INST_PUSH_ARRAY_ELEM &&
            prev != BC_INST_PUSH_SCALE && prev != BC_INST_PUSH_IBASE &&
            prev != BC_INST_PUSH_OBASE && prev != BC_INST_PUSH_LAST)
        {
          status = BC_STATUS_PARSE_BAD_ASSIGN;
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

        prev = BC_PARSE_TOKEN_TO_INST(type);
        status = bc_parse_operator(p, code, &p->ops, type, &nexprs, 1);
        rparen = get_token = 0;

        break;
      }

      case BC_LEX_LEFT_PAREN:
      {
        ++nparens;
        paren_expr = rparen = 0;
        get_token = 1;
        status = bc_vec_push(&p->ops, &type);
        break;
      }

      case BC_LEX_RIGHT_PAREN:
      {
        if (nparens == 0) {
          status = BC_STATUS_SUCCESS;
          done = 1;
          get_token = 0;
          break;
        }
        else if (!paren_expr) {
          status = BC_STATUS_PARSE_BAD_EXPR;
          goto err;
        }

        --nparens;
        paren_expr = rparen = 1;
        get_token = 0;

        status = bc_parse_rightParen(p, code, &p->ops, &nexprs);

        break;
      }

      case BC_LEX_NAME:
      {
        paren_expr = 1;
        rparen = get_token = 0;
        status = bc_parse_name(p, code, &prev, flags & ~(BC_PARSE_EXPR_NOCALL));
        ++nexprs;
        break;
      }

      case BC_LEX_NUMBER:
      {
        size_t idx = p->prog->constants.len;

        status = bc_vec_push(&p->prog->constants, &p->lex.token.string);
        if (status) goto err;

        if ((status = bc_vec_pushByte(code, BC_INST_PUSH_NUM))) return status;
        if ((status = bc_parse_pushIndex(code, idx))) return status;

        paren_expr = get_token = 1;
        rparen = 0;
        ++nexprs;
        prev = BC_INST_PUSH_NUM;

        break;
      }

      case BC_LEX_KEY_IBASE:
      case BC_LEX_KEY_LAST:
      case BC_LEX_KEY_OBASE:
      {
        uint8_t inst = type - BC_LEX_KEY_IBASE + BC_INST_PUSH_IBASE;
        status = bc_vec_pushByte(code, inst);

        paren_expr = get_token = 1;
        rparen = 0;
        ++nexprs;
        prev = BC_INST_PUSH_OBASE;

        break;
      }

      case BC_LEX_KEY_LENGTH:
      case BC_LEX_KEY_SQRT:
      {
        status = bc_parse_builtin(p, code, type, flags);
        paren_expr = 1;
        rparen = get_token = 0;
        ++nexprs;
        prev = type == BC_LEX_KEY_LENGTH ? BC_INST_LENGTH : BC_INST_SQRT;
        break;
      }

      case BC_LEX_KEY_READ:
      {
        if (flags & BC_PARSE_EXPR_NOREAD) status = BC_STATUS_EXEC_NESTED_READ;
        else status = bc_parse_read(p, code);

        paren_expr = 1;
        rparen = get_token = 0;
        ++nexprs;
        prev = BC_INST_READ;

        break;
      }

      case BC_LEX_KEY_SCALE:
      {
        status = bc_parse_scale(p, code, &prev, flags);
        paren_expr = 1;
        rparen = get_token = 0;
        ++nexprs;
        prev = BC_INST_PUSH_SCALE;
        break;
      }

      default:
      {
        status = BC_STATUS_PARSE_BAD_TOKEN;
        break;
      }
    }

    if (status) goto err;
    if (get_token) status = bc_lex_next(&p->lex);

    type = p->lex.token.type;
  }

  if (status) goto err;
  if (TT.signe) {
    status = BC_STATUS_EXEC_SIGNAL;
    goto err;
  }

  status = BC_STATUS_SUCCESS;

  while (!status && p->ops.len > ops_start) {

    top = *((BcLexToken*) bc_vec_top(&p->ops));
    assign = top >= BC_LEX_OP_ASSIGN_POWER && top <= BC_LEX_OP_ASSIGN;

    if (top == BC_LEX_LEFT_PAREN || top == BC_LEX_RIGHT_PAREN) {
      status = BC_STATUS_PARSE_BAD_EXPR;
      goto err;
    }

    if ((status = bc_vec_pushByte(code, BC_PARSE_TOKEN_TO_INST(top)))) goto err;

    nexprs -= top != BC_LEX_OP_BOOL_NOT && top != BC_LEX_OP_NEG;
    bc_vec_pop(&p->ops);
  }

  if (nexprs != 1) {
    status = BC_STATUS_PARSE_BAD_EXPR;
    goto err;
  }

  if (!(flags & BC_PARSE_EXPR_POSIX_REL) && nrelops &&
      (status = bc_posix_error(BC_STATUS_POSIX_REL_OUTSIDE,
                               p->lex.file, p->lex.line, NULL)))
  {
    goto err;
  }
  else if ((flags & BC_PARSE_EXPR_POSIX_REL) && nrelops != 1 &&
           (status = bc_posix_error(BC_STATUS_POSIX_MULTIPLE_REL,
                                    p->lex.file, p->lex.line, NULL)))
  {
    goto err;
  }

  if (flags & BC_PARSE_EXPR_PRINT) {
    if (paren_first || !assign) status = bc_vec_pushByte(code, BC_INST_PRINT);
    else status = bc_vec_pushByte(code, BC_INST_POP);
  }

  return status;

err:

  if (p->lex.token.string) {
    free(p->lex.token.string);
    p->lex.token.string = NULL;
  }

  return status;
}

BcStatus bc_program_search(BcProgram *p, BcResult *result,
                           BcNum **ret, uint8_t flags)
{
  BcStatus status;
  BcEntry entry, *entry_ptr;
  BcVec *vec;
  BcVecO *veco;
  size_t idx, ip_idx;
  BcAuto *a;
  int var;

  for (ip_idx = 0; ip_idx < p->stack.len - 1; ++ip_idx) {

    BcFunc *func;
    BcInstPtr *ip;

    ip = bc_vec_item_rev(&p->stack, ip_idx);
    if (ip->func == BC_PROGRAM_READ || ip->func == BC_PROGRAM_MAIN) continue;

    func = bc_vec_item(&p->funcs, ip->func);

    for (idx = 0; idx < func->autos.len; ++idx) {

      a = bc_vec_item(&func->autos, idx);

      if (!strcmp(a->name, result->data.id.name)) {

        BcResult *r;
        uint8_t cond;

        cond = flags & BC_PROGRAM_SEARCH_VAR;

        if (!a->var != !cond) return BC_STATUS_EXEC_BAD_TYPE;

        r = bc_vec_item(&p->results, ip->len + idx);

        if (cond || flags & BC_PROGRAM_SEARCH_ARRAY) *ret = &r->data.num;
        else {
          status = bc_array_expand(&r->data.array, result->data.id.idx + 1);
          if (status) return status;
          *ret = bc_vec_item(&r->data.array, result->data.id.idx);
        }

        return BC_STATUS_SUCCESS;
      }
    }
  }

  var = flags & BC_PROGRAM_SEARCH_VAR;
  vec = var ? &p->vars : &p->arrays;
  veco = var ? &p->var_map : &p->array_map;

  entry.name = result->data.id.name;
  entry.idx = vec->len;

  status = bc_veco_insert(veco, &entry, &idx);

  if (status != BC_STATUS_VEC_ITEM_EXISTS) {

    // We use this because it has a union of BcNum and BcVec.
    BcResult data;
    size_t len;

    if (status) return status;

    len = strlen(entry.name) + 1;

    if (!(result->data.id.name = malloc(len))) return BC_STATUS_MALLOC_FAIL;

    strcpy(result->data.id.name, entry.name);

    if (flags & BC_PROGRAM_SEARCH_VAR)
      status = bc_num_init(&data.data.num, BC_NUM_DEF_SIZE);
    else status = bc_vec_init(&data.data.array, sizeof(BcNum), bc_num_free);

    if (status) return status;
    if ((status = bc_vec_push(vec, &data.data))) return status;
  }

  entry_ptr = bc_veco_item(veco, idx);

  if (var) *ret = bc_vec_item(vec, entry_ptr->idx);
  else {

    BcVec *ptr = bc_vec_item(vec, entry_ptr->idx);

    if (flags & BC_PROGRAM_SEARCH_ARRAY) {
      *ret = (BcNum*) ptr;
      return BC_STATUS_SUCCESS;
    }

    if ((status = bc_array_expand(ptr, result->data.id.idx + 1))) return status;

    *ret = bc_vec_item(ptr, result->data.id.idx);
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_program_num(BcProgram *p, BcResult *result, BcNum** num, int hex) {

  BcStatus status = BC_STATUS_SUCCESS;

  switch (result->type) {

    case BC_RESULT_TEMP:
    case BC_RESULT_SCALE:
    {
      *num = &result->data.num;
      break;
    }

    case BC_RESULT_CONSTANT:
    {
      char** s;
      size_t len, base;

      s = bc_vec_item(&p->constants, result->data.id.idx);
      len = strlen(*s);

      if ((status = bc_num_init(&result->data.num, len))) return status;

      base = hex && len == 1 ? BC_NUM_MAX_INPUT_BASE : p->ibase_t;

      if ((status = bc_num_parse(&result->data.num, *s, &p->ibase, base))) {
        bc_num_free(&result->data.num);
        return status;
      }

      *num = &result->data.num;
      result->type = BC_RESULT_TEMP;

      break;
    }

    case BC_RESULT_VAR:
    case BC_RESULT_ARRAY:
    {
      uint8_t flags = result->type == BC_RESULT_VAR ? BC_PROGRAM_SEARCH_VAR : 0;
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
      // This is here to prevent compiler warnings in release mode.
      *num = &result->data.num;
      break;
    }
  }

  return status;
}

BcStatus bc_program_binaryOpPrep(BcProgram *p, BcResult **left, BcNum **lval,
                                 BcResult **right, BcNum **rval)
{
  BcStatus status;
  BcResult *l, *r;
  int hex;

  r = bc_vec_item_rev(&p->results, 0);
  l = bc_vec_item_rev(&p->results, 1);

  *left = l;
  *right = r;

  hex = l->type == BC_RESULT_IBASE || l->type == BC_RESULT_OBASE;

  if ((status = bc_program_num(p, l, lval, 0))) return status;
  if ((status = bc_program_num(p, r, rval, hex))) return status;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_program_binaryOpRetire(BcProgram *p, BcResult *result,
                                   BcResultType type)
{
  result->type = type;
  bc_vec_pop(&p->results);
  bc_vec_pop(&p->results);
  return bc_vec_push(&p->results, result);
}

BcStatus bc_program_unaryOpPrep(BcProgram *p, BcResult **result, BcNum **val) {

  BcStatus status;
  BcResult *r;

  r = bc_vec_item_rev(&p->results, 0);

  if ((status = bc_program_num(p, r, val, 0))) return status;

  *result = r;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_program_unaryOpRetire(BcProgram *p, BcResult *result,
                                  BcResultType type)
{
  result->type = type;
  bc_vec_pop(&p->results);
  return bc_vec_push(&p->results, result);
}

BcStatus bc_program_op(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult *operand1, *operand2, res;
  BcNum *num1, *num2;
  BcNumBinaryFunc op;

  status = bc_program_binaryOpPrep(p, &operand1, &num1, &operand2, &num2);
  if (status) return status;

  if ((status = bc_num_init(&res.data.num, BC_NUM_DEF_SIZE))) return status;

  op = bc_program_math_ops[inst - BC_INST_POWER];
  if ((status = op(num1, num2, &res.data.num, p->scale))) goto err;
  if ((status = bc_program_binaryOpRetire(p, &res, BC_RESULT_TEMP))) goto err;

  return status;

err:
  bc_num_free(&res.data.num);
  return status;
}

BcStatus bc_program_read(BcProgram *p) {

  BcStatus status;
  BcParse parse;
  char *buffer;
  size_t size;
  BcFunc *func;
  BcInstPtr ip;

  func = bc_vec_item(&p->funcs, BC_PROGRAM_READ);
  func->code.len = 0;

  if (!(buffer = malloc(BC_PROGRAM_BUF_SIZE + 1))) return BC_STATUS_MALLOC_FAIL;

  size = BC_PROGRAM_BUF_SIZE;

  if ((status = bc_io_getline(&buffer, &size)))goto io_err;

  if ((status = bc_parse_init(&parse, p))) goto io_err;
  bc_lex_init(&parse.lex, "<stdin>");
  if ((status = bc_lex_text(&parse.lex, buffer))) goto exec_err;

  if ((status = bc_parse_expr(&parse, &func->code, BC_PARSE_EXPR_NOREAD))) return status;

  if (parse.lex.token.type != BC_LEX_NEWLINE &&
      parse.lex.token.type != BC_LEX_EOF)
  {
    status = BC_STATUS_EXEC_BAD_READ_EXPR;
    goto exec_err;
  }

  ip.func = BC_PROGRAM_READ;
  ip.idx = 0;
  ip.len = p->results.len;

  if ((status = bc_vec_push(&p->stack, &ip))) goto exec_err;
  if ((status = bc_program_exec(p))) goto exec_err;

  bc_vec_pop(&p->stack);

exec_err:
  bc_parse_free(&parse);
io_err:
  free(buffer);
  return status;
}

size_t bc_program_index(uint8_t *code, size_t *start) {

  uint8_t bytes, i;
  size_t result;

  for (bytes = code[(*start)++], result = 0, i = 0; i < bytes; ++i)
    result |= (((size_t) code[(*start)++]) << (i * CHAR_BIT));

  return result;
}

char* bc_program_name(uint8_t *code, size_t *start) {

  char byte, *s, *string, *ptr;
  size_t len, i;

  string = (char*) (code + *start);
  ptr = strchr((char*) string, ':');

  if (ptr) len = ((unsigned long) ptr) - ((unsigned long) string);
  else len = strlen(string);

  if (!(s = malloc(len + 1))) return NULL;

  for (byte = code[(*start)++], i = 0; byte && byte != ':'; ++i) {
    s[i] = byte;
    byte = code[(*start)++];
  }

  s[i] = '\0';

  return s;
}

BcStatus bc_program_printString(const char *str, size_t *nchars) {

  char c, c2;
  size_t len, i;
  int err;

  len = strlen(str);

  for (i = 0; i < len; ++i,  ++(*nchars)) {

    if ((c = str[i]) != '\\') err = putchar(c);
    else {

      ++i;
      c2 = str[i];

      switch (c2) {

        case 'a':
        {
          err = putchar('\a');
          break;
        }

        case 'b':
        {
          err = putchar('\b');
          break;
        }

        case 'e':
        {
          err = putchar('\\');
          break;
        }

        case 'f':
        {
          err = putchar('\f');
          break;
        }

        case 'n':
        {
          err = putchar('\n');
          *nchars = SIZE_MAX;
          break;
        }

        case 'r':
        {
          err = putchar('\r');
          break;
        }

        case 'q':
        {
          err = putchar('"');
          break;
        }

        case 't':
        {
          err = putchar('\t');
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

    if (err == EOF) return BC_STATUS_IO_ERR;
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_program_push(BcProgram *p, uint8_t *code, size_t *start, int var) {

  BcStatus status;
  BcResult result;

  result.data.id.name = bc_program_name(code, start);

  if (var) {
    result.type = BC_RESULT_VAR;
    status = bc_vec_push(&p->results, &result);
  }
  else {

    BcResult *operand;
    BcNum *num;
    unsigned long temp;

    if ((status = bc_program_unaryOpPrep(p, &operand, &num))) goto err;
    if ((status = bc_num_ulong(num, &temp))) goto err;

    if (temp > (unsigned long) maxof_DIM) {
      status = BC_STATUS_EXEC_ARRAY_LEN;
      goto err;
    }

    result.data.id.idx = (size_t) temp;

    status = bc_program_unaryOpRetire(p, &result, BC_RESULT_ARRAY);
  }

  if (status) goto err;

  return status;

err:
  free(result.data.id.name);
  return status;
}

BcStatus bc_program_negate(BcProgram *p) {

  BcStatus status;
  BcResult result, *ptr;
  BcNum *num;

  if ((status = bc_program_unaryOpPrep(p, &ptr, &num))) return status;
  if ((status = bc_num_init(&result.data.num, num->len))) return status;
  if ((status = bc_num_copy(&result.data.num, num))) goto err;

  result.data.num.neg = !result.data.num.neg;

  if ((status = bc_program_unaryOpRetire(p, &result, BC_RESULT_TEMP))) goto err;

  return status;

err:
  bc_num_free(&result.data.num);
  return status;
}

BcStatus bc_program_logical(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult *operand1, *operand2, res;
  BcNum *num1, *num2;
  int cond;
  int cmp;

  status = bc_program_binaryOpPrep(p, &operand1, &num1, &operand2, &num2);
  if (status) return status;

  if ((status = bc_num_init(&res.data.num, BC_NUM_DEF_SIZE))) return status;

  if (inst == BC_INST_BOOL_AND)
    cond = bc_num_cmp(num1, &p->zero) && bc_num_cmp(num2, &p->zero);
  else if (inst == BC_INST_BOOL_OR)
    cond = bc_num_cmp(num1, &p->zero) || bc_num_cmp(num2, &p->zero);
  else {

    cmp = bc_num_cmp(num1, num2);

    switch (inst) {
      case BC_INST_REL_EQ:
      {
        cond = cmp == 0;
        break;
      }

      case BC_INST_REL_LE:
      {
        cond = cmp <= 0;
        break;
      }

      case BC_INST_REL_GE:
      {
        cond = cmp >= 0;
        break;
      }

      case BC_INST_REL_NE:
      {
        cond = cmp != 0;
        break;
      }

      case BC_INST_REL_LT:
      {
        cond = cmp < 0;
        break;
      }

      case BC_INST_REL_GT:
      {
        cond = cmp > 0;
        break;
      }

      default:
      {
        // This is here to silence a compiler warning in release mode.
        cond = 0;
        break;
      }
    }
  }

  (cond ? bc_num_one : bc_num_zero)(&res.data.num);

  if ((status = bc_program_binaryOpRetire(p, &res, BC_RESULT_TEMP))) goto err;

  return status;

err:
  bc_num_free(&res.data.num);
  return status;
}

BcStatus bc_program_assign(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult *left, *right, res;
  BcNum *l, *r;
  BcNumBinaryFunc op;
  unsigned long val, max;
  size_t *ptr;

  status = bc_program_binaryOpPrep(p, &left, &l, &right, &r);
  if (status) return status;

  if (left->type == BC_RESULT_CONSTANT || left->type == BC_RESULT_TEMP)
    return BC_STATUS_PARSE_BAD_ASSIGN;

  if (inst == BC_INST_ASSIGN_DIVIDE && !bc_num_cmp(r, &p->zero))
    return BC_STATUS_MATH_DIVIDE_BY_ZERO;

  if (inst == BC_INST_ASSIGN)  status = bc_num_copy(l, r);
  else {
    op = bc_program_math_ops[inst - BC_INST_ASSIGN_POWER];
    status = op(l, r, l, p->scale);
  }

  if (status) return status;

  if (left->type == BC_RESULT_IBASE || left->type == BC_RESULT_OBASE) {

    ptr = left->type == BC_RESULT_IBASE ? &p->ibase_t : &p->obase_t;
    max = left->type == BC_RESULT_IBASE ? BC_NUM_MAX_INPUT_BASE : maxof_BASE;

    if ((status = bc_num_ulong(l, &val))) return status;

    if (val < BC_NUM_MIN_BASE || val > max)
      return left->type - BC_RESULT_IBASE + BC_STATUS_EXEC_BAD_IBASE;

    *ptr = (size_t) val;
  }
  else if (left->type == BC_RESULT_SCALE) {

    if ((status = bc_num_ulong(l, &val))) return status;
    if (val > (unsigned long) maxof_SCALE) return BC_STATUS_EXEC_BAD_SCALE;

    p->scale = (size_t) val;
  }

  if ((status = bc_num_init(&res.data.num, l->len))) return status;
  if ((status = bc_num_copy(&res.data.num, l))) goto err;

  if ((status = bc_program_binaryOpRetire(p, &res, BC_RESULT_TEMP))) goto err;

  return status;

err:
  bc_num_free(&res.data.num);
  return status;
}

BcStatus bc_program_call(BcProgram *p, uint8_t *code, size_t *idx) {

  BcStatus status;
  BcInstPtr ip;
  size_t nparams, i;
  BcFunc *func;
  BcAuto *auto_ptr;
  BcResult param, *arg;

  status = BC_STATUS_SUCCESS;
  nparams = bc_program_index(code, idx);

  ip.idx = 0;
  ip.len = p->results.len;
  ip.func = bc_program_index(code, idx);

  func = bc_vec_item(&p->funcs, ip.func);

  if (!func->code.len) return BC_STATUS_EXEC_UNDEFINED_FUNC;
  if (nparams != func->nparams) return BC_STATUS_EXEC_MISMATCHED_PARAMS;

  for (i = 0; i < nparams; ++i) {

    auto_ptr = bc_vec_item(&func->autos, i);
    arg = bc_vec_item_rev(&p->results, nparams - 1);
    param.type = auto_ptr->var + BC_RESULT_ARRAY_AUTO;

    if (auto_ptr->var) {

      BcNum *n;

      if ((status = bc_program_num(p, arg, &n, 0))) return status;
      if ((status = bc_num_init(&param.data.num, n->len))) return status;

      status = bc_num_copy(&param.data.num, n);
    }
    else {

      BcVec *a;

      if (arg->type != BC_RESULT_VAR || arg->type != BC_RESULT_ARRAY)
        return BC_STATUS_EXEC_BAD_TYPE;

      status = bc_program_search(p, arg, (BcNum**) &a, BC_PROGRAM_SEARCH_ARRAY);
      if (status) return status;

      status = bc_vec_init(&param.data.array, sizeof(BcNum), bc_num_free);
      if (status) return status;

      status = bc_array_copy(&param.data.array, a);
    }

    if (status || (status = bc_vec_push(&p->results, &param))) goto err;
  }

  for (; !status && i < func->autos.len; ++i) {

    auto_ptr = bc_vec_item_rev(&func->autos, i);
    param.type = auto_ptr->var + BC_RESULT_ARRAY_AUTO;

    if (auto_ptr->var) status = bc_num_init(&param.data.num, BC_NUM_DEF_SIZE);
    else status = bc_vec_init(&param.data.array, sizeof(BcNum), bc_num_free);

    if (status) return status;

    status = bc_vec_push(&p->results, &param);
  }

  if (status) goto err;

  return bc_vec_push(&p->stack, &ip);

err:
  bc_result_free(&param);
  return status;
}

BcStatus bc_program_return(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult result, *operand;
  BcInstPtr *ip;
  BcFunc *func;

  ip = bc_vec_top(&p->stack);
  func = bc_vec_item(&p->funcs, ip->func);

  result.type = BC_RESULT_TEMP;

  if (inst == BC_INST_RETURN) {

    BcNum *num;

    operand = bc_vec_top(&p->results);

    if ((status = bc_program_num(p, operand, &num, 0))) return status;
    if ((status = bc_num_init(&result.data.num, num->len))) return status;
    if ((status = bc_num_copy(&result.data.num, num))) goto err;
  }
  else {
    status = bc_num_init(&result.data.num, BC_NUM_DEF_SIZE);
    if (status) return status;
    bc_num_zero(&result.data.num);
  }

  // We need to pop arguments as well, so this takes that into account.
  bc_vec_npop(&p->results, p->results.len - (ip->len - func->nparams));

  if ((status = bc_vec_push(&p->results, &result))) goto err;
  bc_vec_pop(&p->stack);

  return status;

err:
  bc_num_free(&result.data.num);
  return status;
}

unsigned long bc_program_scale(BcNum *n) {
  return (unsigned long) n->rdx;
}

unsigned long bc_program_length(BcNum *n) {

  size_t i;
  unsigned long len = n->len;

  if (n->rdx == n->len) {
    for (i = n->len - 1; i < n->len && !n->num[i]; --len, --i);
  }

  return len;
}

BcStatus bc_program_builtin(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult *operand;
  BcNum *num1;
  BcResult result;

  if ((status = bc_program_unaryOpPrep(p, &operand, &num1))) return status;
  if ((status = bc_num_init(&result.data.num, BC_NUM_DEF_SIZE))) return status;

  if (inst == BC_INST_SQRT)
    status = bc_num_sqrt(num1, &result.data.num, p->scale);
  else {

    BcProgramBuiltInFunc func;
    unsigned long ans;

    func = inst == BC_INST_LENGTH ? bc_program_length : bc_program_scale;
    ans = func(num1);

    status = bc_num_ulong2num(&result.data.num, ans);
  }

  if (status || (status = bc_program_unaryOpRetire(p, &result, BC_RESULT_TEMP)))
    goto err;

  return status;

err:
  bc_num_free(&result.data.num);
  return status;
}

BcStatus bc_program_pushScale(BcProgram *p) {

  BcStatus status;
  BcResult result;

  result.type = BC_RESULT_SCALE;

  if ((status = bc_num_init(&result.data.num, BC_NUM_DEF_SIZE))) return status;
  status = bc_num_ulong2num(&result.data.num, (unsigned long) p->scale);
  if (status || (status = bc_vec_push(&p->results, &result))) goto err;

  return status;

err:
  bc_num_free(&result.data.num);
  return status;
}

BcStatus bc_program_incdec(BcProgram *p, uint8_t inst) {

  BcStatus status;
  BcResult *ptr, result, copy;
  BcNum *num;

  if ((status = bc_program_unaryOpPrep(p, &ptr, &num))) return status;

  if (inst == BC_INST_INC_POST || inst == BC_INST_DEC_POST) {
    copy.type = BC_RESULT_TEMP;
    if ((status = bc_num_init(&copy.data.num, num->len))) return status;
  }

  result.type = BC_RESULT_ONE;
  inst = inst == BC_INST_INC_PRE || inst == BC_INST_INC_POST ?
            BC_INST_ASSIGN_PLUS : BC_INST_ASSIGN_MINUS;

  if ((status = bc_vec_push(&p->results, &result))) goto err;
  if ((status = bc_program_assign(p, inst))) goto err;

  if (inst == BC_INST_INC_POST || inst == BC_INST_DEC_POST) {
    bc_vec_pop(&p->results);
    if ((status = bc_vec_push(&p->results, &copy))) goto err;
  }

  return status;

err:

  if (inst == BC_INST_INC_POST || inst == BC_INST_DEC_POST)
    bc_num_free(&copy.data.num);

  return status;
}

BcStatus bc_program_init(BcProgram *p, size_t line_len) {

  BcStatus s;
  size_t idx;
  char *main_name, *read_name;
  BcInstPtr ip;

  main_name = read_name = NULL;
  p->nchars = p->scale = 0;
  p->line_len = line_len;

  if ((s = bc_num_init(&p->ibase, BC_NUM_DEF_SIZE))) return s;
  bc_num_ten(&p->ibase);
  p->ibase_t = 10;

  if ((s = bc_num_init(&p->obase, BC_NUM_DEF_SIZE))) goto obase_err;
  bc_num_ten(&p->obase);
  p->obase_t = 10;

  if ((s = bc_num_init(&p->last, BC_NUM_DEF_SIZE))) goto last_err;
  bc_num_zero(&p->last);

  if ((s = bc_num_init(&p->zero, BC_NUM_DEF_SIZE))) goto zero_err;
  bc_num_zero(&p->zero);

  if ((s = bc_num_init(&p->one, BC_NUM_DEF_SIZE))) goto one_err;
  bc_num_one(&p->one);

  if ((s = bc_vec_init(&p->funcs, sizeof(BcFunc), bc_func_free))) goto func_err;

  s = bc_veco_init(&p->func_map, sizeof(BcEntry), bc_entry_free, bc_entry_cmp);
  if (s) goto func_map_err;

  if (!(main_name = malloc(sizeof(bc_lang_func_main)))) {
    s = BC_STATUS_MALLOC_FAIL;
    goto name_err;
  }

  strcpy(main_name, bc_lang_func_main);
  s = bc_program_addFunc(p, main_name, &idx);
  main_name = NULL;
  if (s || idx != BC_PROGRAM_MAIN) goto read_err;

  if (!(read_name = malloc(sizeof(bc_lang_func_read)))) {
    s = BC_STATUS_MALLOC_FAIL;
    goto read_err;
  }

  strcpy(read_name, bc_lang_func_read);
  s = bc_program_addFunc(p, read_name, &idx);
  read_name = NULL;
  if (s || idx != BC_PROGRAM_READ) goto var_err;

  if ((s = bc_vec_init(&p->vars, sizeof(BcNum), bc_num_free))) goto var_err;
  s = bc_veco_init(&p->var_map, sizeof(BcEntry), bc_entry_free, bc_entry_cmp);
  if (s) goto var_map_err;

  if ((s = bc_vec_init(&p->arrays, sizeof(BcVec), bc_vec_free))) goto array_err;
  s = bc_veco_init(&p->array_map, sizeof(BcEntry), bc_entry_free, bc_entry_cmp);
  if (s) goto array_map_err;

  s = bc_vec_init(&p->strings, sizeof(char*), bc_string_free);
  if (s) goto string_err;

  s = bc_vec_init(&p->constants, sizeof(char*), bc_string_free);
  if (s) goto const_err;

  s = bc_vec_init(&p->results, sizeof(BcResult), bc_result_free);
  if (s) goto expr_err;

  if ((s = bc_vec_init(&p->stack, sizeof(BcInstPtr), NULL))) goto stack_err;

  memset(&ip, 0, sizeof(BcInstPtr));

  if ((s = bc_vec_push(&p->stack, &ip))) goto push_err;

  return s;

push_err:
  bc_vec_free(&p->stack);
stack_err:
  bc_vec_free(&p->results);
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
  if (main_name) free(main_name);
name_err:
  bc_veco_free(&p->func_map);
func_map_err:
  bc_vec_free(&p->funcs);
func_err:
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

BcStatus bc_program_addFunc(BcProgram *p, char *name, size_t *idx) {

  BcStatus status;
  BcEntry entry, *entry_ptr;
  BcFunc f;

  entry.name = name;
  entry.idx = p->funcs.len;

  if ((status = bc_veco_insert(&p->func_map, &entry, idx))) {
    free(name);
    if (status != BC_STATUS_VEC_ITEM_EXISTS) return status;
  }

  entry_ptr = bc_veco_item(&p->func_map, *idx);
  *idx = entry_ptr->idx;

  if (status == BC_STATUS_VEC_ITEM_EXISTS) {

    BcFunc *func = bc_vec_item(&p->funcs, entry_ptr->idx);

    status = BC_STATUS_SUCCESS;

    // We need to reset these, so the function can be repopulated.
    func->nparams = 0;
    bc_vec_npop(&func->autos, func->autos.len);
    bc_vec_npop(&func->code, func->code.len);
    bc_vec_npop(&func->labels, func->labels.len);
  }
  else {
    if ((status = bc_func_init(&f))) return status;
    if ((status = bc_vec_push(&p->funcs, &f))) bc_func_free(&f);
  }

  return status;
}

BcStatus bc_program_reset(BcProgram *p, BcStatus status) {

  BcFunc *func;
  BcInstPtr *ip;

  bc_vec_npop(&p->stack, p->stack.len - 1);
  bc_vec_npop(&p->results, p->results.len);

  func = bc_vec_item(&p->funcs, 0);
  ip = bc_vec_top(&p->stack);
  ip->idx = func->code.len;

  if (!status && TT.signe && !TT.tty) return BC_STATUS_QUIT;

  TT.sigc += TT.signe;
  TT.signe = TT.sig != TT.sigc;

  if ((!status || status == BC_STATUS_EXEC_SIGNAL) && TT.tty) {
    status = BC_STATUS_SUCCESS;
    fprintf(stderr, "%s", bc_program_ready_prompt);
    fflush(stderr);
  }

  return status;
}

BcStatus bc_program_exec(BcProgram *p) {

  BcStatus status;
  uint8_t *code;
  size_t idx, len, *addr;
  BcResult result;
  BcResult *ptr;
  BcNum *num;
  BcFunc *func;
  BcInstPtr *ip;
  int cond;
  const char **string, *s;

  status = BC_STATUS_SUCCESS;
  cond = 0;

  ip = bc_vec_top(&p->stack);
  func = bc_vec_item(&p->funcs, ip->func);
  code = func->code.array;

  while (!status && !TT.sig_other && ip->idx < func->code.len) {

    uint8_t inst = code[(ip->idx)++];

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

      case BC_INST_JUMP_ZERO:
      {
        if ((status = bc_program_unaryOpPrep(p, &ptr, &num))) return status;
        cond = !bc_num_cmp(num, &p->zero);
        bc_vec_pop(&p->results);
      }
      // Fallthrough.
      case BC_INST_JUMP:
      {
        idx = bc_program_index(code, &ip->idx);
        addr = bc_vec_item(&func->labels, idx);
        if (inst == BC_INST_JUMP || cond) ip->idx = *addr;
        break;
      }

      case BC_INST_PUSH_VAR:
      case BC_INST_PUSH_ARRAY_ELEM:
      {
        status = bc_program_push(p, code, &ip->idx, inst == BC_INST_PUSH_VAR);
        break;
      }

      case BC_INST_PUSH_LAST:
      {
        result.type = BC_RESULT_LAST;
        status = bc_vec_push(&p->results, &result);
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
        status = bc_vec_push(&p->results, &result);
        break;
      }

      case BC_INST_PUSH_OBASE:
      {
        result.type = BC_RESULT_OBASE;
        status = bc_vec_push(&p->results, &result);
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
        result.type = BC_RESULT_CONSTANT;
        result.data.id.idx = bc_program_index(code, &ip->idx);
        status = bc_vec_push(&p->results, &result);
        break;
      }

      case BC_INST_POP:
      {
        bc_vec_pop(&p->results);
        break;
      }

      case BC_INST_INC_POST:
      case BC_INST_DEC_POST:
      case BC_INST_INC_PRE:
      case BC_INST_DEC_PRE:
      {
        status = bc_program_incdec(p, inst);
        break;
      }

      case BC_INST_HALT:
      {
        status = BC_STATUS_QUIT;
        break;
      }

      case BC_INST_PRINT:
      case BC_INST_PRINT_EXPR:
      {
        if ((status = bc_program_unaryOpPrep(p, &ptr, &num))) return status;

        status = bc_num_print(num, &p->obase, p->obase_t, inst == BC_INST_PRINT,
                              &p->nchars, p->line_len);
        if (status) return status;
        if ((status = bc_num_copy(&p->last, num))) return status;

        bc_vec_pop(&p->results);

        break;
      }

      case BC_INST_STR:
      {
        idx = bc_program_index(code, &ip->idx);
        string = bc_vec_item(&p->strings, idx);

        s = *string;
        len = strlen(s);

        for (idx = 0; idx < len; ++idx) {
          char c = s[idx];
          if (putchar(c) == EOF) return BC_STATUS_IO_ERR;
          if (c == '\n') p->nchars = SIZE_MAX;
          ++p->nchars;
        }

        break;
      }

      case BC_INST_PRINT_STR:
      {
        idx = bc_program_index(code, &ip->idx);
        string = bc_vec_item(&p->strings, idx);
        status = bc_program_printString(*string, &p->nchars);
        break;
      }

      case BC_INST_POWER:
      case BC_INST_MULTIPLY:
      case BC_INST_DIVIDE:
      case BC_INST_MODULUS:
      case BC_INST_PLUS:
      case BC_INST_MINUS:
      {
        status = bc_program_op(p, inst);
        break;
      }

      case BC_INST_REL_EQ:
      case BC_INST_REL_LE:
      case BC_INST_REL_GE:
      case BC_INST_REL_NE:
      case BC_INST_REL_LT:
      case BC_INST_REL_GT:
      {
        status = bc_program_logical(p, inst);
        break;
      }

      case BC_INST_BOOL_NOT:
      {
        if ((status = bc_program_unaryOpPrep(p, &ptr, &num))) return status;
        status = bc_num_init(&result.data.num, BC_NUM_DEF_SIZE);
        if (status) return status;

        if (bc_num_cmp(num, &p->zero)) bc_num_one(&result.data.num);
        else bc_num_zero(&result.data.num);

        status = bc_program_unaryOpRetire(p, &result, BC_RESULT_TEMP);
        if (status) bc_num_free(&result.data.num);

        break;
      }

      case BC_INST_BOOL_OR:
      case BC_INST_BOOL_AND:
      {
        status = bc_program_logical(p, inst);
        break;
      }

      case BC_INST_NEG:
      {
        status = bc_program_negate(p);
        break;
      }

      case BC_INST_ASSIGN_POWER:
      case BC_INST_ASSIGN_MULTIPLY:
      case BC_INST_ASSIGN_DIVIDE:
      case BC_INST_ASSIGN_MODULUS:
      case BC_INST_ASSIGN_PLUS:
      case BC_INST_ASSIGN_MINUS:
      case BC_INST_ASSIGN:
      {
        status = bc_program_assign(p, inst);
        break;
      }

      default:
      {
        break;
      }
    }

    if ((status && status != BC_STATUS_QUIT) || TT.signe)
      status = bc_program_reset(p, status);

    // We need to update because if the stack changes, pointers may be invalid.
    ip = bc_vec_top(&p->stack);
    func = bc_vec_item(&p->funcs, ip->func);
    code = func->code.array;
  }

  return status;
}

void bc_program_free(BcProgram *p) {

  if (!p) return;

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

  bc_vec_free(&p->results);
  bc_vec_free(&p->stack);

  bc_num_free(&p->last);
  bc_num_free(&p->zero);
  bc_num_free(&p->one);

  memset(p, 0, sizeof(BcProgram));
}

void bc_sig(int sig) {
  if (sig == SIGINT) {
    if (write(2, bc_sig_msg, sizeof(bc_sig_msg) - 1) >= 0)
      TT.sig += (TT.signe = TT.sig == TT.sigc);
  }
  else TT.sig_other = 1;
}

BcStatus bc_error(BcStatus s) {
  if (!s || s >= BC_STATUS_POSIX_NAME_LEN) return BC_STATUS_SUCCESS;
  fprintf(stderr, bc_err_fmt, bc_errs[bc_err_indices[s]], bc_err_descs[s]);
  return s * !TT.tty;
}

BcStatus bc_error_file(BcStatus s, const char *file, size_t line) {
  if (!s || !file || s >= BC_STATUS_POSIX_NAME_LEN) return BC_STATUS_SUCCESS;
  fprintf(stderr, bc_err_fmt, bc_errs[bc_err_indices[s]], bc_err_descs[s]);
  fprintf(stderr, "    %s", file);
  fprintf(stderr, &":%d\n\n"[3 * !line], line);
  return s * !TT.tty;
}

BcStatus bc_posix_error(BcStatus s, const char *file,
                        size_t line, const char *msg)
{
  int p = (int) (toys.optflags & FLAG_s), w = (int) (toys.optflags & FLAG_w);

  if (!(p || w) || s < BC_STATUS_POSIX_NAME_LEN || !file)
    return BC_STATUS_SUCCESS;

  fprintf(stderr, "\n%s %s: %s\n", bc_errs[bc_err_indices[s]],
          p ? "error" : "warning", bc_err_descs[s]);

  if (msg) fprintf(stderr, "    %s\n", msg);
  fprintf(stderr, "    %s", file);
  fprintf(stderr, &":%d\n\n"[3 * !line], line);

  return s * !!p;
}

BcStatus bc_process(Bc *bc, const char *text) {

  BcStatus s = bc_lex_text(&bc->parse.lex, text);

  if (s && (s = bc_error_file(s, bc->parse.lex.file, bc->parse.lex.line)))
    return s;

  while (bc->parse.lex.token.type != BC_LEX_EOF) {

    if ((s = bc_parse_parse(&bc->parse)) == BC_STATUS_LIMITS) {

      s = BC_STATUS_IO_ERR;

      if (putchar('\n') == EOF) return s;

      if (printf("BC_BASE_MAX     = %zu\n", (size_t) maxof_BASE) < 0 ||
          printf("BC_DIM_MAX      = %zu\n", (size_t) maxof_DIM) < 0 ||
          printf("BC_SCALE_MAX    = %zu\n", (size_t) maxof_SCALE) < 0 ||
          printf("BC_STRING_MAX   = %zu\n", (size_t) maxof_STRING) < 0 ||
          printf("Max Exponent    = %ld\n", (long) LONG_MAX) < 0 ||
          printf("Number of Vars  = %zu\n", (size_t) SIZE_MAX) < 0)
      {
        return s;
      }

      if (putchar('\n') == EOF) return s;
    }
    else if (s == BC_STATUS_QUIT || TT.sig_other ||
        (s && (s = bc_error_file(s, bc->parse.lex.file, bc->parse.lex.line))))
    {
      return s;
    }
  }

  if (BC_PARSE_CAN_EXEC(&bc->parse)) {
    s = bc_program_exec(&bc->prog);
    if (TT.tty) fflush(stdout);
    if (s && s != BC_STATUS_QUIT) s = bc_error(s);
  }

  return s;
}

BcStatus bc_file(Bc *bc, const char *file) {

  BcStatus s;
  char *data;
  BcFunc *main_func;
  BcInstPtr *ip;

  bc->prog.file = file;
  if ((s = bc_io_fread(file, &data))) return s;

  bc_lex_init(&bc->parse.lex, file);
  if ((s = bc_process(bc, data))) goto err;

  main_func = bc_vec_item(&bc->prog.funcs, BC_PROGRAM_MAIN);
  ip = bc_vec_item(&bc->prog.stack, 0);

  if (main_func->code.len > ip->idx) s = BC_STATUS_EXEC_FILE_NOT_EXECUTABLE;

err:
  free(data);
  return s;
}

BcStatus bc_concat(char **buffer, size_t *n, char *buf, size_t total_len) {

  if (total_len > *n) {

    char *temp = realloc(*buffer, total_len + 1);
    if (!temp) return BC_STATUS_MALLOC_FAIL;

    *buffer = temp;
    *n = total_len;
  }

  strcat(*buffer, buf);

  return BC_STATUS_SUCCESS;
}

BcStatus bc_stdin(Bc *bc) {

  BcStatus s;
  char *buf, *buffer, c;
  size_t n, bufn, slen, total_len, len, i;
  int string, comment, notend;

  bc->prog.file = bc_program_stdin_name;
  bc_lex_init(&bc->parse.lex, bc_program_stdin_name);

  n = bufn = BC_BUF_SIZE;

  if (!(buffer = malloc(BC_BUF_SIZE + 1))) return BC_STATUS_MALLOC_FAIL;

  if (!(buf = malloc(BC_BUF_SIZE + 1))) {
    s = BC_STATUS_MALLOC_FAIL;
    goto buf_err;
  }

  buffer[0] = '\0';
  string = comment = 0;
  s = BC_STATUS_SUCCESS;

  // The following loop is complex because the vm tries not to send any lines
  // that end with a backslash to the parser. The reason for that is because the
  // parser treats a backslash+newline combo as whitespace, per the bc spec. In
  // that case, and for strings and comments, the parser will expect more stuff.
  while ((!s || s != BC_STATUS_QUIT) &&
         !((s = bc_io_getline(&buf, &bufn)) && s != BC_STATUS_BINARY_FILE))
  {
    if (s == BC_STATUS_BINARY_FILE) {
      putchar('\a');
      s = BC_STATUS_SUCCESS;
      continue;
    }

    len = strlen(buf);
    slen = strlen(buffer);
    total_len = slen + len;

    if (len == 1 && buf[0] == '"') string = !string;
    else if (len > 1 || comment) {

      for (i = 0; i < len; ++i) {

        notend = len > i + 1;

        if ((c = buf[i]) == '"') string = !string;
        else if (c == '/' && notend && !comment && buf[i + 1] == '*') {
          comment = 1;
          break;
        }
        else if (c == '*' && notend && comment && buf[i + 1] == '/')
          comment = 0;
      }

      if (string || comment || buf[len - 2] == '\\') {
        if ((s = bc_concat(&buffer, &n, buf, total_len))) goto exit_err;
        continue;
      }
    }

    if ((s = bc_concat(&buffer, &n, buf, total_len))) goto exit_err;

    s = bc_process(bc, buffer);
    buffer[0] = '\0';
  }

  // I/O error will always happen when stdin is
  // closed. It's not a problem in that case.
  s = s == BC_STATUS_IO_ERR ? BC_STATUS_SUCCESS : s;

exit_err:
  free(buf);
buf_err:
  free(buffer);
  return s;
}

void bc_main(void) {

  BcStatus status;
  Bc bc;
  struct sigaction sa;
  size_t i, len;
  char *lenv;
  int num;

  TT.tty = (toys.optflags & FLAG_i) || (isatty(0) && isatty(1));

  if ((lenv = getenv("BC_LINE_LENGTH"))) {
    len = strlen(lenv);
    for (num = 1, i = 0; num && i < len; ++i) num = isdigit(lenv[i]);
    if (!num || (len = (size_t) atoi(lenv) - 1) < 2) len = BC_NUM_PRINT_WIDTH;
  }
  else len = BC_NUM_PRINT_WIDTH;

  if ((toys.exitval = bc_program_init(&bc.prog, len))) return;
  if ((status = bc_parse_init(&bc.parse, &bc.prog))) goto parse_err;

  sigemptyset(&sa.sa_mask);
  sa.sa_handler = bc_sig;
  sa.sa_flags = 0;

  if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGPIPE, &sa, NULL) < 0 ||
      sigaction(SIGHUP, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0)
  {
    status = BC_STATUS_EXEC_SIGACTION_FAIL;
    goto err;
  }

  if (TT.tty && !(toys.optflags & FLAG_q) && printf("%s", bc_header) < 0) {
    status = BC_STATUS_IO_ERR;
    goto err;
  }

  if (toys.optflags & FLAG_l) {

    bc_lex_init(&bc.parse.lex, bc_lib_name);
    if ((status = bc_lex_text(&bc.parse.lex, bc_lib))) goto err;

    while (!status && bc.parse.lex.token.type != BC_LEX_EOF)
      status = bc_parse_parse(&bc.parse);

    if (status || (status = bc_program_exec(&bc.prog))) goto err;
  }

  for (i = 0; !TT.sig_other && !status && i < toys.optc; ++i)
    status = bc_file(&bc, toys.optargs[i]);
  if (status || TT.sig_other) goto err;

  status = bc_stdin(&bc);

err:
  if (CFG_TOYBOX_FREE) bc_parse_free(&bc.parse);
parse_err:
  bc_program_free(&bc.prog);
  toys.exitval = status == BC_STATUS_QUIT ? BC_STATUS_SUCCESS : status;
}
