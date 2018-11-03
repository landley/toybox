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
  long ttyin;

  unsigned long sig;
  unsigned long sigc;
  unsigned long signe;
  long sig_other;
)

typedef enum BcStatus {

  BC_STATUS_SUCCESS,

  BC_STATUS_ALLOC_ERR,
  BC_STATUS_IO_ERR,
  BC_STATUS_BIN_FILE,
  BC_STATUS_PATH_IS_DIR,

  BC_STATUS_LEX_BAD_CHAR,
  BC_STATUS_LEX_NO_STRING_END,
  BC_STATUS_LEX_NO_COMMENT_END,
  BC_STATUS_LEX_EOF,

  BC_STATUS_PARSE_BAD_TOKEN,
  BC_STATUS_PARSE_BAD_EXP,
  BC_STATUS_PARSE_EMPTY_EXP,
  BC_STATUS_PARSE_BAD_PRINT,
  BC_STATUS_PARSE_BAD_FUNC,
  BC_STATUS_PARSE_BAD_ASSIGN,
  BC_STATUS_PARSE_NO_AUTO,
  BC_STATUS_PARSE_DUPLICATE_LOCAL,
  BC_STATUS_PARSE_NO_BLOCK_END,

  BC_STATUS_MATH_NEGATIVE,
  BC_STATUS_MATH_NON_INTEGER,
  BC_STATUS_MATH_OVERFLOW,
  BC_STATUS_MATH_DIVIDE_BY_ZERO,
  BC_STATUS_MATH_BAD_STRING,

  BC_STATUS_EXEC_FILE_ERR,
  BC_STATUS_EXEC_MISMATCHED_PARAMS,
  BC_STATUS_EXEC_UNDEFINED_FUNC,
  BC_STATUS_EXEC_FILE_NOT_EXECUTABLE,
  BC_STATUS_EXEC_NUM_LEN,
  BC_STATUS_EXEC_NAME_LEN,
  BC_STATUS_EXEC_STRING_LEN,
  BC_STATUS_EXEC_ARRAY_LEN,
  BC_STATUS_EXEC_BAD_IBASE,
  BC_STATUS_EXEC_BAD_SCALE,
  BC_STATUS_EXEC_BAD_READ_EXPR,
  BC_STATUS_EXEC_REC_READ,
  BC_STATUS_EXEC_BAD_TYPE,
  BC_STATUS_EXEC_BAD_OBASE,
  BC_STATUS_EXEC_SIGNAL,
  BC_STATUS_EXEC_STACK,

  BC_STATUS_VEC_OUT_OF_BOUNDS,
  BC_STATUS_VEC_ITEM_EXISTS,

  BC_STATUS_POSIX_NAME_LEN,
  BC_STATUS_POSIX_COMMENT,
  BC_STATUS_POSIX_BAD_KW,
  BC_STATUS_POSIX_DOT,
  BC_STATUS_POSIX_RET,
  BC_STATUS_POSIX_BOOL,
  BC_STATUS_POSIX_REL_POS,
  BC_STATUS_POSIX_MULTIREL,
  BC_STATUS_POSIX_FOR1,
  BC_STATUS_POSIX_FOR2,
  BC_STATUS_POSIX_FOR3,
  BC_STATUS_POSIX_BRACE,

  BC_STATUS_QUIT,
  BC_STATUS_LIMITS,

} BcStatus;

#define BC_ERR_IDX_VM (0)
#define BC_ERR_IDX_LEX (1)
#define BC_ERR_IDX_PARSE (2)
#define BC_ERR_IDX_MATH (3)
#define BC_ERR_IDX_EXEC (4)
#define BC_ERR_IDX_VEC (5)
#define BC_ERR_IDX_POSIX (6)

#define BC_VEC_START_CAP (1<<5)

typedef void (*BcVecFree)(void*);
typedef int (*BcVecCmp)(const void*, const void*);

typedef struct BcVec {
  char *v;
  size_t len;
  size_t cap;
  size_t size;
  BcVecFree dtor;
} BcVec;

#define bc_vec_pop(v) (bc_vec_npop((v), 1))
#define bc_vec_top(v) (bc_vec_item_rev((v), 0))

#define bc_map_init(v) (bc_vec_init((v), sizeof(BcId), bc_id_free))

#define BC_READ_BIN_CHAR(c) ((((c) < ' ' && !isspace((c))) || (c) > '~'))

typedef signed char BcDig;

typedef struct BcNum {
  BcDig *restrict num;
  size_t rdx;
  size_t len;
  size_t cap;
  int neg;
} BcNum;

#define BC_NUM_MIN_BASE ((unsigned long) 2)
#define BC_NUM_MAX_IBASE ((unsigned long) 16)
#define BC_NUM_DEF_SIZE (16)
#define BC_NUM_PRINT_WIDTH (69)

#define BC_NUM_KARATSUBA_LEN (32)

#define BC_NUM_NEG(n, neg) ((((ssize_t) (n)) ^ -((ssize_t) (neg))) + (neg))
#define BC_NUM_ONE(n) ((n)->len == 1 && (n)->rdx == 0 && (n)->num[0] == 1)
#define BC_NUM_INT(n) ((n)->len - (n)->rdx)
#define BC_NUM_AREQ(a, b) \
  (maxof((a)->rdx, (b)->rdx) + maxof(BC_NUM_INT(a), BC_NUM_INT(b)) + 1)
#define BC_NUM_MREQ(a, b, scale) \
  (BC_NUM_INT(a) + BC_NUM_INT(b) + maxof((scale), (a)->rdx + (b)->rdx) + 1)

typedef BcStatus (*BcNumBinaryOp)(BcNum*, BcNum*, BcNum*, size_t);
typedef void (*BcNumDigitOp)(size_t, size_t, int, size_t*, size_t);

void bc_num_init(BcNum *n, size_t req);
void bc_num_expand(BcNum *n, size_t req);
void bc_num_copy(BcNum *d, BcNum *s);
void bc_num_free(void *num);

BcStatus bc_num_ulong(BcNum *n, unsigned long *result);
BcStatus bc_num_ulong2num(BcNum *n, unsigned long val);

BcStatus bc_num_add(BcNum *a, BcNum *b, BcNum *c, size_t scale);
BcStatus bc_num_sub(BcNum *a, BcNum *b, BcNum *c, size_t scale);
BcStatus bc_num_mul(BcNum *a, BcNum *b, BcNum *c, size_t scale);
BcStatus bc_num_div(BcNum *a, BcNum *b, BcNum *c, size_t scale);
BcStatus bc_num_mod(BcNum *a, BcNum *b, BcNum *c, size_t scale);
BcStatus bc_num_pow(BcNum *a, BcNum *b, BcNum *c, size_t scale);
BcStatus bc_num_sqrt(BcNum *a, BcNum *b, size_t scale);
BcStatus bc_num_divmod(BcNum *a, BcNum *b, BcNum *c, BcNum *d, size_t scale);

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

  BC_INST_NUM,
  BC_INST_VAR,
  BC_INST_ARRAY_ELEM,
  BC_INST_ARRAY,

  BC_INST_SCALE_FUNC,
  BC_INST_IBASE,
  BC_INST_SCALE,
  BC_INST_LAST,
  BC_INST_LENGTH,
  BC_INST_READ,
  BC_INST_OBASE,
  BC_INST_SQRT,

  BC_INST_PRINT,
  BC_INST_PRINT_POP,
  BC_INST_STR,
  BC_INST_PRINT_STR,

  BC_INST_JUMP,
  BC_INST_JUMP_ZERO,

  BC_INST_CALL,

  BC_INST_RET,
  BC_INST_RET0,

  BC_INST_HALT,

  BC_INST_POP,
  BC_INST_POP_EXEC,

} BcInst;

typedef struct BcId {
  char *name;
  size_t idx;
} BcId;

typedef struct BcFunc {
  BcVec code;
  BcVec labels;
  size_t nparams;
  BcVec autos;
} BcFunc;

typedef enum BcResultType {

  BC_RESULT_TEMP,

  BC_RESULT_VAR,
  BC_RESULT_ARRAY_ELEM,
  BC_RESULT_ARRAY,

  BC_RESULT_STR,

  BC_RESULT_IBASE,
  BC_RESULT_SCALE,
  BC_RESULT_LAST,

  // These are between to calculate ibase, obase, and last from instructions.
  BC_RESULT_CONSTANT,
  BC_RESULT_ONE,

  BC_RESULT_OBASE,

} BcResultType;

typedef union BcResultData {
  BcNum n;
  BcVec v;
  BcId id;
} BcResultData;

typedef struct BcResult {
  BcResultType t;
  BcResultData d;
} BcResult;

typedef struct BcInstPtr {
  size_t func;
  size_t idx;
  size_t len;
} BcInstPtr;

void bc_array_expand(BcVec *a, size_t len);
int bc_id_cmp(const void *e1, const void *e2);

// BC_LEX_NEG is not used in lexing; it is only for parsing.
typedef enum BcLexType {

  BC_LEX_EOF,
  BC_LEX_INVALID,

  BC_LEX_OP_INC,
  BC_LEX_OP_DEC,

  BC_LEX_NEG,

  BC_LEX_OP_POWER,
  BC_LEX_OP_MULTIPLY,
  BC_LEX_OP_DIVIDE,
  BC_LEX_OP_MODULUS,
  BC_LEX_OP_PLUS,
  BC_LEX_OP_MINUS,

  BC_LEX_OP_REL_EQ,
  BC_LEX_OP_REL_LE,
  BC_LEX_OP_REL_GE,
  BC_LEX_OP_REL_NE,
  BC_LEX_OP_REL_LT,
  BC_LEX_OP_REL_GT,

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

  BC_LEX_NLINE,
  BC_LEX_WHITESPACE,

  BC_LEX_LPAREN,
  BC_LEX_RPAREN,

  BC_LEX_LBRACKET,
  BC_LEX_COMMA,
  BC_LEX_RBRACKET,

  BC_LEX_LBRACE,
  BC_LEX_SCOLON,
  BC_LEX_RBRACE,

  BC_LEX_STR,
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

} BcLexType;

typedef struct BcLex {

  const char *buf;
  size_t i;
  size_t line;
  const char *f;
  size_t len;
  int newline;

  struct {
    BcLexType t;
    BcLexType last;
    BcVec v;
  } t;

} BcLex;

#define BC_PARSE_STREND ((char) UCHAR_MAX)

#define bc_parse_push(p, i) (bc_vec_pushByte(&(p)->func->code, (char) (i)))
#define bc_parse_updateFunc(p, f) \
  ((p)->func = bc_vec_item(&(p)->prog->fns, ((p)->fidx = (f))))

#define BC_PARSE_REL (1<<0)
#define BC_PARSE_PRINT (1<<1)
#define BC_PARSE_NOCALL (1<<2)
#define BC_PARSE_NOREAD (1<<3)
#define BC_PARSE_ARRAY (1<<4)

#define BC_PARSE_TOP_FLAG_PTR(parse) ((uint8_t*) bc_vec_top(&(parse)->flags))
#define BC_PARSE_TOP_FLAG(parse) (*(BC_PARSE_TOP_FLAG_PTR(parse)))

#define BC_PARSE_FLAG_FUNC_INNER (1<<0)
#define BC_PARSE_FUNC_INNER(parse) \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_FUNC_INNER)

#define BC_PARSE_FLAG_FUNC (1<<1)
#define BC_PARSE_FUNC(parse) \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_FUNC)

#define BC_PARSE_FLAG_BODY (1<<2)
#define BC_PARSE_BODY(parse) \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_BODY)

#define BC_PARSE_FLAG_LOOP (1<<3)
#define BC_PARSE_LOOP(parse) \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_LOOP)

#define BC_PARSE_FLAG_LOOP_INNER (1<<4)
#define BC_PARSE_LOOP_INNER(parse) \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_LOOP_INNER)

#define BC_PARSE_FLAG_IF (1<<5)
#define BC_PARSE_IF(parse) \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_IF)

#define BC_PARSE_FLAG_ELSE (1<<6)
#define BC_PARSE_ELSE(parse) \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_ELSE)

#define BC_PARSE_FLAG_IF_END (1<<7)
#define BC_PARSE_IF_END(parse) \
  (BC_PARSE_TOP_FLAG(parse) & BC_PARSE_FLAG_IF_END)

#define BC_PARSE_CAN_EXEC(parse) \
  (!(BC_PARSE_TOP_FLAG(parse) & (BC_PARSE_FLAG_FUNC_INNER |  \
                                 BC_PARSE_FLAG_FUNC |        \
                                 BC_PARSE_FLAG_BODY |        \
                                 BC_PARSE_FLAG_LOOP |        \
                                 BC_PARSE_FLAG_LOOP_INNER |  \
                                 BC_PARSE_FLAG_IF |          \
                                 BC_PARSE_FLAG_ELSE |        \
                                 BC_PARSE_FLAG_IF_END)))

typedef struct BcOp {
  char prec;
  int left;
} BcOp;

typedef struct BcParseNext {
  uint32_t len;
  BcLexType tokens[4];
} BcParseNext;

#define BC_PARSE_NEXT_TOKENS(...) .tokens = { __VA_ARGS__ }
#define BC_PARSE_NEXT(a, ...) { .len = (a), BC_PARSE_NEXT_TOKENS(__VA_ARGS__) }

struct BcProgram;

typedef struct BcParse {

  BcLex l;

  BcVec flags;

  BcVec exits;
  BcVec conds;

  BcVec ops;

  struct BcProgram *prog;
  BcFunc *func;
  size_t fidx;

  size_t nbraces;
  int auto_part;

} BcParse;

typedef struct BcLexKeyword {
  const char name[9];
  const char len;
  const int posix;
} BcLexKeyword;

#define BC_LEX_KW_ENTRY(a, b, c) { .name = a, .len = (b), .posix = (c) }

BcStatus bc_lex_token(BcLex *l);

#define BC_PARSE_TOP_OP(p) (*((BcLexType*) bc_vec_top(&(p)->ops)))
#define BC_PARSE_LEAF(p, rparen) \
  (((p) >= BC_INST_NUM && (p) <= BC_INST_SQRT) || (rparen) || \
  (p) == BC_INST_INC_POST || (p) == BC_INST_DEC_POST)

// We can calculate the conversion between tokens and exprs by subtracting the
// position of the first operator in the lex enum and adding the position of the
// first in the expr enum. Note: This only works for binary operators.
#define BC_PARSE_TOKEN_INST(t) ((char) ((t) - BC_LEX_NEG + BC_INST_NEG))

BcStatus bc_parse_parse(BcParse *p);
BcStatus bc_parse_expr(BcParse *p, uint8_t flags, BcParseNext next);

typedef struct BcProgram {

  size_t len;
  size_t scale;

  BcNum ib;
  size_t ib_t;
  BcNum ob;
  size_t ob_t;

  BcNum hexb;

  BcVec results;
  BcVec stack;

  BcVec fns;
  BcVec fn_map;

  BcVec vars;
  BcVec var_map;

  BcVec arrs;
  BcVec arr_map;

  BcVec strs;
  BcVec consts;

  const char *file;

  BcNum last;
  BcNum zero;
  BcNum one;

  size_t nchars;

} BcProgram;

#define BC_PROG_STACK(s, n) ((s)->len >= ((size_t) n))

#define BC_PROG_MAIN (0)
#define BC_PROG_READ (1)

#define BC_PROG_STR(n) (!(n)->num && !(n)->cap)
#define BC_PROG_NUM(r, n) \
  ((r)->t != BC_RESULT_ARRAY && (r)->t != BC_RESULT_STR && !BC_PROG_STR(n))

typedef unsigned long (*BcProgramBuiltIn)(BcNum*);

void bc_program_addFunc(BcProgram *p, char *name, size_t *idx);
BcStatus bc_program_reset(BcProgram *p, BcStatus s);
BcStatus bc_program_exec(BcProgram *p);

#define BC_MAX_OBASE ((unsigned long) 999)
#define BC_MAX_DIM ((unsigned long) INT_MAX)
#define BC_MAX_SCALE ((unsigned long) UINT_MAX)
#define BC_MAX_STRING ((unsigned long) UINT_MAX - 1)
#define BC_MAX_NAME BC_MAX_STRING
#define BC_MAX_NUM BC_MAX_STRING
#define BC_MAX_EXP ((unsigned long) LONG_MAX)
#define BC_MAX_VARS ((unsigned long) SIZE_MAX - 1)

typedef struct BcVm {

  BcParse prs;
  BcProgram prog;

} BcVm;

BcStatus bc_vm_posixError(BcStatus s, const char *file,
                          size_t line, const char *msg);

void bc_vm_exit(BcStatus s);
void bc_vm_printf(FILE *restrict f, const char *fmt, ...);
void bc_vm_puts(const char *str, FILE *restrict f);
void bc_vm_putchar(int c);
void bc_vm_fflush(FILE *restrict f);

// clang-format off

const char bc_sig_msg[] = "\ninterrupt (type \"quit\" to exit)\n";

const char bc_copyright[] =
  "Copyright (c) 2018 Gavin D. Howard and contributors\n"
  "Report bugs at: https://github.com/gavinhoward/bc\n\n"
  "This is free software with ABSOLUTELY NO WARRANTY.\n";

const char bc_err_fmt[] = "\n%s error: %s\n";
const char bc_warn_fmt[] = "\n%s warning: %s\n";
const char bc_err_line[] = ":%zu\n\n";

const char *bc_errs[] = {
  "VM",
  "Lex",
  "Parse",
  "Math",
  "Runtime",
  "Vector",
  "POSIX",
};

const uint8_t bc_err_ids[] = {
  BC_ERR_IDX_VM, BC_ERR_IDX_VM, BC_ERR_IDX_VM, BC_ERR_IDX_VM, BC_ERR_IDX_VM,
  BC_ERR_IDX_LEX, BC_ERR_IDX_LEX, BC_ERR_IDX_LEX, BC_ERR_IDX_LEX,
  BC_ERR_IDX_PARSE, BC_ERR_IDX_PARSE, BC_ERR_IDX_PARSE, BC_ERR_IDX_PARSE,
  BC_ERR_IDX_PARSE, BC_ERR_IDX_PARSE, BC_ERR_IDX_PARSE, BC_ERR_IDX_PARSE,
  BC_ERR_IDX_MATH, BC_ERR_IDX_MATH, BC_ERR_IDX_MATH, BC_ERR_IDX_MATH,
  BC_ERR_IDX_MATH,
  BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC,
  BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC,
  BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC,
  BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC, BC_ERR_IDX_EXEC,
  BC_ERR_IDX_EXEC,
  BC_ERR_IDX_VEC, BC_ERR_IDX_VEC,
  BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX,
  BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX,
  BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX, BC_ERR_IDX_POSIX,
  BC_ERR_IDX_VM, BC_ERR_IDX_VM, BC_ERR_IDX_VM,
};

const char *bc_err_msgs[] = {

  NULL,
  "memory allocation error",
  "I/O error",
  "file is not text:",
  "path is a directory:",

  "bad character",
  "string end could not be found",
  "comment end could not be found",
  "end of file",

  "bad token",
  "bad expression",
  "empty expression",
  "bad print statement",
  "bad function definition",
  "bad assignment: left side must be scale, ibase, "
    "obase, last, var, or array element",
  "no auto variable found",
  "function parameter or auto var has the same name as another",
  "block end could not be found",

  "negative number",
  "non integer number",
  "overflow",
  "divide by zero",
  "bad number string",

  "could not open file:",
  "mismatched parameters",
  "undefined function",
  "file is not executable:",
  "number too long: must be [1, BC_NUM_MAX]",
  "name too long: must be [1, BC_NAME_MAX]",
  "string too long: must be [1, BC_STRING_MAX]",
  "array too long; must be [1, BC_DIM_MAX]",
  "bad ibase; must be [2, 16]",
  "bad scale; must be [0, BC_SCALE_MAX]",
  "bad read() expression",
  "read() call inside of a read() call",
  "variable is wrong type",
  "bad obase; must be [2, BC_BASE_MAX]",
  "signal caught and not handled",
  "stack has too few elements",

  "index is out of bounds",
  "item already exists",

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

const char bc_func_main[] = "(main)";
const char bc_func_read[] = "(read)";

const BcLexKeyword bc_lex_kws[20] = {
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

// This is an array that corresponds to token types. An entry is
// 1 if the token is valid in an expression, 0 otherwise.
const int bc_parse_exprs[] = {
  0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0,
  0, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1,
  1, 0,
};

// This is an array of data for operators that correspond to token types.
const BcOp bc_parse_ops[] = {
  { 0, 0 }, { 0, 0 },
  { 1, 0 },
  { 2, 0 },
  { 3, 1 }, { 3, 1 }, { 3, 1 },
  { 4, 1 }, { 4, 1 },
  { 6, 1 }, { 6, 1 }, { 6, 1 }, { 6, 1 }, { 6, 1 }, { 6, 1 },
  { 1, 0 },
  { 7, 1 }, { 7, 1 },
  { 5, 0 }, { 5, 0 }, { 5, 0 }, { 5, 0 }, { 5, 0 },
  { 5, 0 }, { 5, 0 },
};

// These identify what tokens can come after expressions in certain cases.
const BcParseNext bc_parse_next_expr =
  BC_PARSE_NEXT(4, BC_LEX_NLINE, BC_LEX_SCOLON, BC_LEX_RBRACE, BC_LEX_EOF);
const BcParseNext bc_parse_next_param =
  BC_PARSE_NEXT(2, BC_LEX_RPAREN, BC_LEX_COMMA);
const BcParseNext bc_parse_next_print =
  BC_PARSE_NEXT(4, BC_LEX_COMMA, BC_LEX_NLINE, BC_LEX_SCOLON, BC_LEX_EOF);
const BcParseNext bc_parse_next_rel = BC_PARSE_NEXT(1, BC_LEX_RPAREN);
const BcParseNext bc_parse_next_elem = BC_PARSE_NEXT(1, BC_LEX_RBRACKET);
const BcParseNext bc_parse_next_for = BC_PARSE_NEXT(1, BC_LEX_SCOLON);
const BcParseNext bc_parse_next_read =
  BC_PARSE_NEXT(2, BC_LEX_NLINE, BC_LEX_EOF);

const char bc_num_hex_digits[] = "0123456789ABCDEF";

const BcNumBinaryOp bc_program_ops[] = {
  bc_num_pow, bc_num_mul, bc_num_div, bc_num_mod, bc_num_add, bc_num_sub,
};

const char bc_program_stdin_name[] = "<stdin>";
const char bc_program_ready_msg[] = "ready for more input\n";

// clang-format on
const char *bc_lib_name = "gen/lib.bc";

const char bc_lib[] = {
  115,99,97,108,101,61,50,48,10,100,101,102,105,110,101,32,101,40,120,41,123,
  10,9,97,117,116,111,32,98,44,115,44,110,44,114,44,100,44,105,44,112,44,102,
  44,118,10,9,98,61,105,98,97,115,101,10,9,105,98,97,115,101,61,65,10,9,105,102,
  40,120,60,48,41,123,10,9,9,110,61,49,10,9,9,120,61,45,120,10,9,125,10,9,115,
  61,115,99,97,108,101,10,9,114,61,54,43,115,43,48,46,52,52,42,120,10,9,115,99,
  97,108,101,61,115,99,97,108,101,40,120,41,43,49,10,9,119,104,105,108,101,40,
  120,62,49,41,123,10,9,9,100,43,61,49,10,9,9,120,47,61,50,10,9,9,115,99,97,108,
  101,43,61,49,10,9,125,10,9,115,99,97,108,101,61,114,10,9,114,61,120,43,49,10,
  9,112,61,120,10,9,102,61,118,61,49,10,9,102,111,114,40,105,61,50,59,118,33,
  61,48,59,43,43,105,41,123,10,9,9,112,42,61,120,10,9,9,102,42,61,105,10,9,9,
  118,61,112,47,102,10,9,9,114,43,61,118,10,9,125,10,9,119,104,105,108,101,40,
  40,100,45,45,41,33,61,48,41,114,42,61,114,10,9,115,99,97,108,101,61,115,10,
  9,105,98,97,115,101,61,98,10,9,105,102,40,110,33,61,48,41,114,101,116,117,114,
  110,40,49,47,114,41,10,9,114,101,116,117,114,110,40,114,47,49,41,10,125,10,
  100,101,102,105,110,101,32,108,40,120,41,123,10,9,97,117,116,111,32,98,44,115,
  44,114,44,112,44,97,44,113,44,105,44,118,10,9,98,61,105,98,97,115,101,10,9,
  105,98,97,115,101,61,65,10,9,105,102,40,120,60,61,48,41,123,10,9,9,114,61,40,
  49,45,49,48,94,115,99,97,108,101,41,47,49,10,9,9,105,98,97,115,101,61,98,10,
  9,9,114,101,116,117,114,110,40,114,41,10,9,125,10,9,115,61,115,99,97,108,101,
  10,9,115,99,97,108,101,43,61,54,10,9,112,61,50,10,9,119,104,105,108,101,40,
  120,62,61,50,41,123,10,9,9,112,42,61,50,10,9,9,120,61,115,113,114,116,40,120,
  41,10,9,125,10,9,119,104,105,108,101,40,120,60,61,48,46,53,41,123,10,9,9,112,
  42,61,50,10,9,9,120,61,115,113,114,116,40,120,41,10,9,125,10,9,114,61,97,61,
  40,120,45,49,41,47,40,120,43,49,41,10,9,113,61,97,42,97,10,9,118,61,49,10,9,
  102,111,114,40,105,61,51,59,118,33,61,48,59,105,43,61,50,41,123,10,9,9,97,42,
  61,113,10,9,9,118,61,97,47,105,10,9,9,114,43,61,118,10,9,125,10,9,114,42,61,
  112,10,9,115,99,97,108,101,61,115,10,9,105,98,97,115,101,61,98,10,9,114,101,
  116,117,114,110,40,114,47,49,41,10,125,10,100,101,102,105,110,101,32,115,40,
  120,41,123,10,9,97,117,116,111,32,98,44,115,44,114,44,110,44,97,44,113,44,105,
  10,9,98,61,105,98,97,115,101,10,9,105,98,97,115,101,61,65,10,9,115,61,115,99,
  97,108,101,10,9,115,99,97,108,101,61,49,46,49,42,115,43,50,10,9,97,61,97,40,
  49,41,10,9,105,102,40,120,60,48,41,123,10,9,9,110,61,49,10,9,9,120,61,45,120,
  10,9,125,10,9,115,99,97,108,101,61,48,10,9,113,61,40,120,47,97,43,50,41,47,
  52,10,9,120,61,120,45,52,42,113,42,97,10,9,105,102,40,113,37,50,33,61,48,41,
  120,61,45,120,10,9,115,99,97,108,101,61,115,43,50,10,9,114,61,97,61,120,10,
  9,113,61,45,120,42,120,10,9,102,111,114,40,105,61,51,59,97,33,61,48,59,105,
  43,61,50,41,123,10,9,9,97,42,61,113,47,40,105,42,40,105,45,49,41,41,10,9,9,
  114,43,61,97,10,9,125,10,9,115,99,97,108,101,61,115,10,9,105,98,97,115,101,
  61,98,10,9,105,102,40,110,33,61,48,41,114,101,116,117,114,110,40,45,114,47,
  49,41,10,9,114,101,116,117,114,110,40,114,47,49,41,10,125,10,100,101,102,105,
  110,101,32,99,40,120,41,123,10,9,97,117,116,111,32,98,44,115,10,9,98,61,105,
  98,97,115,101,10,9,105,98,97,115,101,61,65,10,9,115,61,115,99,97,108,101,10,
  9,115,99,97,108,101,42,61,49,46,50,10,9,120,61,115,40,50,42,97,40,49,41,43,
  120,41,10,9,115,99,97,108,101,61,115,10,9,105,98,97,115,101,61,98,10,9,114,
  101,116,117,114,110,40,120,47,49,41,10,125,10,100,101,102,105,110,101,32,97,
  40,120,41,123,10,9,97,117,116,111,32,98,44,115,44,114,44,110,44,97,44,109,44,
  116,44,102,44,105,44,117,10,9,98,61,105,98,97,115,101,10,9,105,98,97,115,101,
  61,65,10,9,110,61,49,10,9,105,102,40,120,60,48,41,123,10,9,9,110,61,45,49,10,
  9,9,120,61,45,120,10,9,125,10,9,105,102,40,120,61,61,49,41,123,10,9,9,105,102,
  40,115,99,97,108,101,60,54,53,41,123,10,9,9,9,114,101,116,117,114,110,40,46,
  55,56,53,51,57,56,49,54,51,51,57,55,52,52,56,51,48,57,54,49,53,54,54,48,56,
  52,53,56,49,57,56,55,53,55,50,49,48,52,57,50,57,50,51,52,57,56,52,51,55,55,
  54,52,53,53,50,52,51,55,51,54,49,52,56,48,47,110,41,10,9,9,125,10,9,125,10,
  9,105,102,40,120,61,61,46,50,41,123,10,9,9,105,102,40,115,99,97,108,101,60,
  54,53,41,123,10,9,9,9,114,101,116,117,114,110,40,46,49,57,55,51,57,53,53,53,
  57,56,52,57,56,56,48,55,53,56,51,55,48,48,52,57,55,54,53,49,57,52,55,57,48,
  50,57,51,52,52,55,53,56,53,49,48,51,55,56,55,56,53,50,49,48,49,53,49,55,54,
  56,56,57,52,48,50,47,110,41,10,9,9,125,10,9,125,10,9,115,61,115,99,97,108,101,
  10,9,105,102,40,120,62,46,50,41,123,10,9,9,115,99,97,108,101,43,61,53,10,9,
  9,97,61,97,40,46,50,41,10,9,125,10,9,115,99,97,108,101,61,115,43,51,10,9,119,
  104,105,108,101,40,120,62,46,50,41,123,10,9,9,109,43,61,49,10,9,9,120,61,40,
  120,45,46,50,41,47,40,49,43,46,50,42,120,41,10,9,125,10,9,114,61,117,61,120,
  10,9,102,61,45,120,42,120,10,9,116,61,49,10,9,102,111,114,40,105,61,51,59,116,
  33,61,48,59,105,43,61,50,41,123,10,9,9,117,42,61,102,10,9,9,116,61,117,47,105,
  10,9,9,114,43,61,116,10,9,125,10,9,115,99,97,108,101,61,115,10,9,105,98,97,
  115,101,61,98,10,9,114,101,116,117,114,110,40,40,109,42,97,43,114,41,47,110,
  41,10,125,10,100,101,102,105,110,101,32,106,40,110,44,120,41,123,10,9,97,117,
  116,111,32,98,44,115,44,111,44,97,44,105,44,118,44,102,10,9,98,61,105,98,97,
  115,101,10,9,105,98,97,115,101,61,65,10,9,115,61,115,99,97,108,101,10,9,115,
  99,97,108,101,61,48,10,9,110,47,61,49,10,9,105,102,40,110,60,48,41,123,10,9,
  9,110,61,45,110,10,9,9,105,102,40,110,37,50,61,61,49,41,111,61,49,10,9,125,
  10,9,97,61,49,10,9,102,111,114,40,105,61,50,59,105,60,61,110,59,43,43,105,41,
  97,42,61,105,10,9,115,99,97,108,101,61,49,46,53,42,115,10,9,97,61,40,120,94,
  110,41,47,50,94,110,47,97,10,9,114,61,118,61,49,10,9,102,61,45,120,42,120,47,
  52,10,9,115,99,97,108,101,61,115,99,97,108,101,43,108,101,110,103,116,104,40,
  97,41,45,115,99,97,108,101,40,97,41,10,9,102,111,114,40,105,61,49,59,118,33,
  61,48,59,43,43,105,41,123,10,9,9,118,61,118,42,102,47,105,47,40,110,43,105,
  41,10,9,9,114,43,61,118,10,9,125,10,9,115,99,97,108,101,61,115,10,9,105,98,
  97,115,101,61,98,10,9,105,102,40,111,33,61,48,41,97,61,45,97,10,9,114,101,116,
  117,114,110,40,97,42,114,47,49,41,10,125,10,0
};

void bc_vec_grow(BcVec *v, size_t n) {
  size_t cap = v->cap * 2;
  while (cap < v->len + n) cap *= 2;
  v->v = xrealloc(v->v, v->size * cap);
  v->cap = cap;
}

void bc_vec_init(BcVec *v, size_t esize, BcVecFree dtor) {
  v->size = esize;
  v->cap = BC_VEC_START_CAP;
  v->len = 0;
  v->dtor = dtor;
  v->v = xmalloc(esize * BC_VEC_START_CAP);
}

void bc_vec_expand(BcVec *v, size_t req) {
  if (v->cap < req) {
    v->v = xrealloc(v->v, v->size * req);
    v->cap = req;
  }
}

void bc_vec_npop(BcVec *v, size_t n) {
  if (!v->dtor) v->len -= n;
  else {
    size_t len = v->len - n;
    while (v->len > len) v->dtor(v->v + (v->size * --v->len));
  }
}

void bc_vec_push(BcVec *v, const void *data) {
  if (v->len + 1 > v->cap) bc_vec_grow(v, 1);
  memmove(v->v + (v->size * v->len), data, v->size);
  v->len += 1;
}

void bc_vec_pushByte(BcVec *v, char data) {
  bc_vec_push(v, &data);
}

void bc_vec_pushAt(BcVec *v, const void *data, size_t idx) {

  if (idx == v->len) bc_vec_push(v, data);
  else {

    char *ptr;

    if (v->len == v->cap) bc_vec_grow(v, 1);

    ptr = v->v + v->size * idx;

    memmove(ptr + v->size, ptr, v->size * (v->len++ - idx));
    memmove(ptr, data, v->size);
  }
}

void bc_vec_string(BcVec *v, size_t len, const char *str) {

  bc_vec_npop(v, v->len);
  bc_vec_expand(v, len + 1);
  memcpy(v->v, str, len);
  v->len = len;

  bc_vec_pushByte(v, '\0');
}

void bc_vec_concat(BcVec *v, const char *str) {

  size_t len;

  if (v->len == 0) bc_vec_pushByte(v, '\0');

  len = v->len + strlen(str);

  if (v->cap < len) bc_vec_grow(v, len - v->len);
  strcat(v->v, str);

  v->len = len;
}

void* bc_vec_item(const BcVec *v, size_t idx) {
  return v->v + v->size * idx;
}

void* bc_vec_item_rev(const BcVec *v, size_t idx) {
  return v->v + v->size * (v->len - idx - 1);
}

void bc_vec_free(void *vec) {
  BcVec *v = (BcVec*) vec;
  bc_vec_npop(v, v->len);
  free(v->v);
}

size_t bc_map_find(const BcVec *v, const void *ptr) {

  size_t low = 0, high = v->len;

  while (low < high) {

    size_t mid = (low + high) / 2;
    BcId *id = bc_vec_item(v, mid);
    int result = bc_id_cmp(ptr, id);

    if (result == 0) return mid;
    else if (result < 0) high = mid;
    else low = mid + 1;
  }

  return low;
}

BcStatus bc_map_insert(BcVec *v, const void *ptr, size_t *i) {

  BcStatus s = BC_STATUS_SUCCESS;

  *i = bc_map_find(v, ptr);

  if (*i == v->len) bc_vec_push(v, ptr);
  else if (!bc_id_cmp(ptr, bc_vec_item(v, *i))) s = BC_STATUS_VEC_ITEM_EXISTS;
  else bc_vec_pushAt(v, ptr, *i);

  return s;
}

size_t bc_map_index(const BcVec* v, const void *ptr) {
  size_t i = bc_map_find(v, ptr);
  if (i >= v->len) return ((size_t) -1);
  return bc_id_cmp(ptr, bc_vec_item(v, i)) ? ((size_t) -1) : i;
}

BcStatus bc_read_line(BcVec *vec, const char* prompt) {

  int i;
  signed char c = 0;

  if (TT.ttyin && !(toys.optflags & FLAG_s)) {
    bc_vm_puts(prompt, stderr);
    bc_vm_fflush(stderr);
  }

  bc_vec_npop(vec, vec->len);

  while (c != '\n') {

    i = fgetc(stdin);

    if (i == EOF) {

      if (errno == EINTR) {

        TT.sigc = TT.sig;
        TT.signe = 0;

        if (TT.ttyin) {
          bc_vm_puts(bc_program_ready_msg, stderr);
          if (!(toys.optflags & FLAG_s)) bc_vm_puts(prompt, stderr);
          bc_vm_fflush(stderr);
        }

        continue;
      }

      return BC_STATUS_IO_ERR;
    }

    c = (signed char) i;
    if (i > UCHAR_MAX || BC_READ_BIN_CHAR(c)) return BC_STATUS_BIN_FILE;
    bc_vec_push(vec, &c);
  }

  bc_vec_pushByte(vec, '\0');

  return BC_STATUS_SUCCESS;
}

BcStatus bc_read_file(const char *path, char **buf) {

  BcStatus s = BC_STATUS_IO_ERR;
  FILE *f;
  size_t size, read;
  long res;
  struct stat pstat;

  f = fopen(path, "r");
  if (!f) bc_vm_exit(BC_STATUS_EXEC_FILE_ERR);
  if (fstat(fileno(f), &pstat) == -1) goto malloc_err;

  if (S_ISDIR(pstat.st_mode)) {
    s = BC_STATUS_PATH_IS_DIR;
    goto malloc_err;
  }

  if (fseek(f, 0, SEEK_END) == -1) goto malloc_err;
  res = ftell(f);
  if (res < 0) goto malloc_err;
  if (fseek(f, 0, SEEK_SET) == -1) goto malloc_err;

  size = (size_t) res;
  *buf = xmalloc(size + 1);

  read = fread(*buf, 1, size, f);
  if (read != size) goto read_err;

  (*buf)[size] = '\0';
  s = BC_STATUS_BIN_FILE;

  for (read = 0; read < size; ++read) {
    if (BC_READ_BIN_CHAR((*buf)[read])) goto read_err;
  }

  fclose(f);

  return BC_STATUS_SUCCESS;

read_err:
  free(*buf);
malloc_err:
  fclose(f);
  return s;
}

void bc_num_setToZero(BcNum *n, size_t scale) {
  n->len = 0;
  n->neg = 0;
  n->rdx = scale;
}

void bc_num_zero(BcNum *n) {
  bc_num_setToZero(n, 0);
}

void bc_num_one(BcNum *n) {
  bc_num_setToZero(n, 0);
  n->len = 1;
  n->num[0] = 1;
}

void bc_num_ten(BcNum *n) {
  bc_num_setToZero(n, 0);
  n->len = 2;
  n->num[0] = 0;
  n->num[1] = 1;
}

BcStatus bc_num_subArrays(BcDig *restrict a, BcDig *restrict b,size_t len) {
  size_t i, j;
  for (i = 0; !TT.signe && i < len; ++i) {
    for (a[i] -= b[i], j = 0; !TT.signe && a[i + j] < 0;) {
      a[i + j++] += 10;
      a[i + j] -= 1;
    }
  }
  return TT.signe ? BC_STATUS_EXEC_SIGNAL : BC_STATUS_SUCCESS;
}

ssize_t bc_num_compare(BcDig *restrict a, BcDig *restrict b, size_t len) {
  size_t i;
  int c = 0;
  for (i = len - 1; !TT.signe && i < len && !(c = a[i] - b[i]); --i);
  return BC_NUM_NEG(i + 1, c < 0);
}

ssize_t bc_num_cmp(BcNum *a, BcNum *b) {

  size_t i, min, a_int, b_int, diff;
  BcDig *max_num, *min_num;
  int a_max, neg = 0;
  ssize_t cmp;

  if (a == b) return 0;
  if (a->len == 0) return BC_NUM_NEG(!!b->len, !b->neg);
  if (b->len == 0) return BC_NUM_NEG(1, a->neg);
  if (a->neg) {
    if (b->neg) neg = 1;
    else return -1;
  }
  else if (b->neg) return 1;

  a_int = BC_NUM_INT(a);
  b_int = BC_NUM_INT(b);
  a_int -= b_int;
  a_max = (a->rdx > b->rdx);

  if (a_int) return (ssize_t) a_int;

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
  if (cmp) return BC_NUM_NEG(cmp, (!a_max) != neg);

  for (max_num -= diff, i = diff - 1; !TT.signe && i < diff; --i) {
    if (max_num[i]) return BC_NUM_NEG(1, (!a_max) != neg);
  }

  return 0;
}

void bc_num_truncate(BcNum *n, size_t places) {

  if (places == 0) return;

  n->rdx -= places;

  if (n->len) {
    n->len -= places;
    memmove(n->num, n->num + places, n->len * sizeof(BcDig));
  }
}

void bc_num_extend(BcNum *n, size_t places) {

  size_t len = n->len + places;

  if (places) {

    if (n->cap < len) bc_num_expand(n, len);

    memmove(n->num + places, n->num, sizeof(BcDig) * n->len);
    memset(n->num, 0, sizeof(BcDig) * places);

    n->len += places;
    n->rdx += places;
  }
}

void bc_num_clean(BcNum *n) {
  while (n->len > 0 && n->num[n->len - 1] == 0) --n->len;
  if (n->len == 0) n->neg = 0;
  else if (n->len < n->rdx) n->len = n->rdx;
}

void bc_num_retireMul(BcNum *n, size_t scale, int neg1, int neg2) {

  if (n->rdx < scale) bc_num_extend(n, scale - n->rdx);
  else bc_num_truncate(n, n->rdx - scale);

  bc_num_clean(n);
  if (n->len) n->neg = !neg1 != !neg2;
}

void bc_num_split(BcNum *restrict n, size_t idx, BcNum *restrict a,
                  BcNum *restrict b)
{
  if (idx < n->len) {

    b->len = n->len - idx;
    a->len = idx;
    a->rdx = b->rdx = 0;

    memcpy(b->num, n->num + idx, b->len * sizeof(BcDig));
    memcpy(a->num, n->num, idx * sizeof(BcDig));
  }
  else {
    bc_num_zero(b);
    bc_num_copy(a, n);
  }

  bc_num_clean(a);
  bc_num_clean(b);
}

BcStatus bc_num_shift(BcNum *n, size_t places) {

  if (places == 0 || n->len == 0) return BC_STATUS_SUCCESS;
  if (places + n->len > BC_MAX_NUM) return BC_STATUS_EXEC_NUM_LEN;

  if (n->rdx >= places) n->rdx -= places;
  else {
    bc_num_extend(n, places - n->rdx);
    n->rdx = 0;
  }

  bc_num_clean(n);

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_inv(BcNum *a, BcNum *b, size_t scale) {

  BcNum one;
  BcDig num[2];

  one.cap = 2;
  one.num = num;
  bc_num_one(&one);

  return bc_num_div(&one, a, b, scale);
}

BcStatus bc_num_a(BcNum *a, BcNum *b, BcNum *restrict c, size_t sub) {

  BcDig *ptr, *ptr_a, *ptr_b, *ptr_c;
  size_t i, max, min_rdx, min_int, diff, a_int, b_int;
  int carry, in;

  // Because this function doesn't need to use scale (per the bc spec),
  // I am hijacking it to say whether it's doing an add or a subtract.

  if (a->len == 0) {
    bc_num_copy(c, b);
    if (sub && c->len) c->neg = !c->neg;
    return BC_STATUS_SUCCESS;
  }
  else if (b->len == 0) {
    bc_num_copy(c, a);
    return BC_STATUS_SUCCESS;
  }

  c->neg = a->neg;
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
  a_int = BC_NUM_INT(a);
  b_int = BC_NUM_INT(b);

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
    in = ((int) ptr_a[i]) + ((int) ptr_b[i]) + carry;
    carry = in / 10;
    ptr_c[i] = (BcDig) (in % 10);
  }

  for (; !TT.signe && i < max + min_rdx; ++i, ++c->len) {
    in = ((int) ptr[i]) + carry;
    carry = in / 10;
    ptr_c[i] = (BcDig) (in % 10);
  }

  if (carry) c->num[c->len++] = (BcDig) carry;

  return TT.signe ? BC_STATUS_EXEC_SIGNAL : BC_STATUS_SUCCESS;
}

BcStatus bc_num_s(BcNum *a, BcNum *b, BcNum *restrict c, size_t sub) {

  BcStatus s;
  ssize_t cmp;
  BcNum *minuend, *subtrahend;
  size_t start;
  int aneg, bneg, neg;

  // Because this function doesn't need to use scale (per the bc spec),
  // I am hijacking it to say whether it's doing an add or a subtract.

  if (a->len == 0) {
    bc_num_copy(c, b);
    if (sub && c->len) c->neg = !c->neg;
    return BC_STATUS_SUCCESS;
  }
  else if (b->len == 0) {
    bc_num_copy(c, a);
    return BC_STATUS_SUCCESS;
  }

  aneg = a->neg;
  bneg = b->neg;
  a->neg = b->neg = 0;

  cmp = bc_num_cmp(a, b);

  a->neg = aneg;
  b->neg = bneg;

  if (cmp == 0) {
    bc_num_setToZero(c, maxof(a->rdx, b->rdx));
    return BC_STATUS_SUCCESS;
  }
  else if (cmp > 0) {
    neg = a->neg;
    minuend = a;
    subtrahend = b;
  }
  else {
    neg = b->neg;
    if (sub) neg = !neg;
    minuend = b;
    subtrahend = a;
  }

  bc_num_copy(c, minuend);
  c->neg = neg;

  if (c->rdx < subtrahend->rdx) {
    bc_num_extend(c, subtrahend->rdx - c->rdx);
    start = 0;
  }
  else start = c->rdx - subtrahend->rdx;

  s = bc_num_subArrays(c->num + start, subtrahend->num, subtrahend->len);

  bc_num_clean(c);

  return s;
}

BcStatus bc_num_k(BcNum *restrict a, BcNum *restrict b, BcNum *restrict c) {

  BcStatus s;
  int carry;
  size_t i, j, len, max = maxof(a->len, b->len), max2 = (max + 1) / 2;
  BcNum l1, h1, l2, h2, m2, m1, z0, z1, z2, temp;
  int aone = BC_NUM_ONE(a);

  if (TT.signe) return BC_STATUS_EXEC_SIGNAL;
  if (a->len == 0 || b->len == 0) {
    bc_num_zero(c);
    return BC_STATUS_SUCCESS;
  }
  else if (aone || BC_NUM_ONE(b)) {
    bc_num_copy(c, aone ? b : a);
    return BC_STATUS_SUCCESS;
  }

  if (a->len + b->len < BC_NUM_KARATSUBA_LEN ||
      a->len < BC_NUM_KARATSUBA_LEN || b->len < BC_NUM_KARATSUBA_LEN)
  {
    bc_num_expand(c, a->len + b->len + 1);

    memset(c->num, 0, sizeof(BcDig) * c->cap);
    c->len = carry = len = 0;

    for (i = 0; !TT.signe && i < b->len; ++i) {

      for (j = 0; !TT.signe && j < a->len; ++j) {
        int in = (int) c->num[i + j];
        in += ((int) a->num[j]) * ((int) b->num[i]) + carry;
        carry = in / 10;
        c->num[i + j] = (BcDig) (in % 10);
      }

      c->num[i + j] += (BcDig) carry;
      len = maxof(len, i + j + !!carry);
      carry = 0;
    }

    c->len = len;

    return TT.signe ? BC_STATUS_EXEC_SIGNAL : BC_STATUS_SUCCESS;
  }

  bc_num_init(&l1, max);
  bc_num_init(&h1, max);
  bc_num_init(&l2, max);
  bc_num_init(&h2, max);
  bc_num_init(&m1, max);
  bc_num_init(&m2, max);
  bc_num_init(&z0, max);
  bc_num_init(&z1, max);
  bc_num_init(&z2, max);
  bc_num_init(&temp, max + max);

  bc_num_split(a, max2, &l1, &h1);
  bc_num_split(b, max2, &l2, &h2);

  s = bc_num_add(&h1, &l1, &m1, 0);
  if (s) goto err;
  s = bc_num_add(&h2, &l2, &m2, 0);
  if (s) goto err;

  s = bc_num_k(&h1, &h2, &z0);
  if (s) goto err;
  s = bc_num_k(&m1, &m2, &z1);
  if (s) goto err;
  s = bc_num_k(&l1, &l2, &z2);
  if (s) goto err;

  s = bc_num_sub(&z1, &z0, &temp, 0);
  if (s) goto err;
  s = bc_num_sub(&temp, &z2, &z1, 0);
  if (s) goto err;

  s = bc_num_shift(&z0, max2 * 2);
  if (s) goto err;
  s = bc_num_shift(&z1, max2);
  if (s) goto err;
  s = bc_num_add(&z0, &z1, &temp, 0);
  if (s) goto err;
  s = bc_num_add(&temp, &z2, c, 0);

err:
  bc_num_free(&temp);
  bc_num_free(&z2);
  bc_num_free(&z1);
  bc_num_free(&z0);
  bc_num_free(&m2);
  bc_num_free(&m1);
  bc_num_free(&h2);
  bc_num_free(&l2);
  bc_num_free(&h1);
  bc_num_free(&l1);
  return s;
}

BcStatus bc_num_m(BcNum *a, BcNum *b, BcNum *restrict c, size_t scale) {

  BcStatus s;
  BcNum cpa, cpb;
  size_t maxrdx = maxof(a->rdx, b->rdx);

  scale = maxof(scale, a->rdx);
  scale = maxof(scale, b->rdx);
  scale = minof(a->rdx + b->rdx, scale);
  maxrdx = maxof(maxrdx, scale);

  bc_num_init(&cpa, a->len);
  bc_num_init(&cpb, b->len);

  bc_num_copy(&cpa, a);
  bc_num_copy(&cpb, b);
  cpa.neg = cpb.neg = 0;

  s = bc_num_shift(&cpa, maxrdx);
  if (s) goto err;
  s = bc_num_shift(&cpb, maxrdx);
  if (s) goto err;
  s = bc_num_k(&cpa, &cpb, c);
  if (s) goto err;

  maxrdx += scale;
  bc_num_expand(c, c->len + maxrdx);

  if (c->len < maxrdx) {
    memset(c->num + c->len, 0, (c->cap - c->len) * sizeof(BcDig));
    c->len += maxrdx;
  }

  c->rdx = maxrdx;
  bc_num_retireMul(c, scale, a->neg, b->neg);

err:
  bc_num_free(&cpb);
  bc_num_free(&cpa);
  return s;
}

BcStatus bc_num_d(BcNum *a, BcNum *b, BcNum *restrict c, size_t scale) {

  BcStatus s = BC_STATUS_SUCCESS;
  BcDig *n, *p, q;
  size_t len, end, i;
  BcNum cp;
  int zero = 1;

  if (b->len == 0) return BC_STATUS_MATH_DIVIDE_BY_ZERO;
  else if (a->len == 0) {
    bc_num_setToZero(c, scale);
    return BC_STATUS_SUCCESS;
  }
  else if (BC_NUM_ONE(b)) {
    bc_num_copy(c, a);
    bc_num_retireMul(c, scale, a->neg, b->neg);
    return BC_STATUS_SUCCESS;
  }

  bc_num_init(&cp, BC_NUM_MREQ(a, b, scale));
  bc_num_copy(&cp, a);
  len = b->len;

  if (len > cp.len) {
    bc_num_expand(&cp, len + 2);
    bc_num_extend(&cp, len - cp.len);
  }

  if (b->rdx > cp.rdx) bc_num_extend(&cp, b->rdx - cp.rdx);
  cp.rdx -= b->rdx;
  if (scale > cp.rdx) bc_num_extend(&cp, scale - cp.rdx);

  if (b->rdx == b->len) {
    for (i = 0; zero && i < len; ++i) zero = !b->num[len - i - 1];
    len -= i - 1;
  }

  if (cp.cap == cp.len) bc_num_expand(&cp, cp.len + 1);

  // We want an extra zero in front to make things simpler.
  cp.num[cp.len++] = 0;
  end = cp.len - len;

  bc_num_expand(c, cp.len);

  bc_num_zero(c);
  memset(c->num + end, 0, (c->cap - end) * sizeof(BcDig));
  c->rdx = cp.rdx;
  c->len = cp.len;
  p = b->num;

  for (i = end - 1; !TT.signe && !s && i < end; --i) {
    n = cp.num + i;
    for (q = 0; (!s && n[len]) || bc_num_compare(n, p, len) >= 0; ++q)
      s = bc_num_subArrays(n, p, len);
    c->num[i] = q;
  }

  if (!s) bc_num_retireMul(c, scale, a->neg, b->neg);
  bc_num_free(&cp);

  return s;
}

BcStatus bc_num_r(BcNum *a, BcNum *b, BcNum *restrict c,
                  BcNum *restrict d, size_t scale, size_t ts)
{
  BcStatus s;
  BcNum temp;
  int neg;

  if (b->len == 0) return BC_STATUS_MATH_DIVIDE_BY_ZERO;

  if (a->len == 0) {
    bc_num_setToZero(d, ts);
    return BC_STATUS_SUCCESS;
  }

  bc_num_init(&temp, d->cap);
  bc_num_d(a, b, c, scale);

  if (scale) scale = ts;

  s = bc_num_m(c, b, &temp, scale);
  if (s) goto err;
  s = bc_num_sub(a, &temp, d, scale);
  if (s) goto err;

  if (ts > d->rdx && d->len) bc_num_extend(d, ts - d->rdx);

  neg = d->neg;
  bc_num_retireMul(d, ts, a->neg, b->neg);
  d->neg = neg;

err:
  bc_num_free(&temp);
  return s;
}

BcStatus bc_num_rem(BcNum *a, BcNum *b, BcNum *restrict c, size_t scale) {

  BcStatus s;
  BcNum c1;
  size_t ts = maxof(scale + b->rdx, a->rdx), len = BC_NUM_MREQ(a, b, ts);

  bc_num_init(&c1, len);
  s = bc_num_r(a, b, &c1, c, scale, ts);
  bc_num_free(&c1);

  return s;
}

BcStatus bc_num_p(BcNum *a, BcNum *b, BcNum *restrict c, size_t scale) {

  BcStatus s = BC_STATUS_SUCCESS;
  BcNum copy;
  unsigned long pow;
  size_t i, powrdx, resrdx;
  int neg, zero;

  if (b->rdx) return BC_STATUS_MATH_NON_INTEGER;

  if (b->len == 0) {
    bc_num_one(c);
    return BC_STATUS_SUCCESS;
  }
  else if (a->len == 0) {
    bc_num_setToZero(c, scale);
    return BC_STATUS_SUCCESS;
  }
  else if (BC_NUM_ONE(b)) {
    if (!b->neg) bc_num_copy(c, a);
    else s = bc_num_inv(a, c, scale);
    return s;
  }

  neg = b->neg;
  b->neg = 0;

  s = bc_num_ulong(b, &pow);
  if (s) return s;

  bc_num_init(&copy, a->len);
  bc_num_copy(&copy, a);

  if (!neg) scale = minof(a->rdx * pow, maxof(scale, a->rdx));

  b->neg = neg;

  for (powrdx = a->rdx; !TT.signe && !(pow & 1); pow >>= 1) {
    powrdx <<= 1;
    s = bc_num_mul(&copy, &copy, &copy, powrdx);
    if (s) goto err;
  }

  if (TT.signe) {
    s = BC_STATUS_EXEC_SIGNAL;
    goto err;
  }

  bc_num_copy(c, &copy);

  for (resrdx = powrdx, pow >>= 1; !TT.signe && pow; pow >>= 1) {

    powrdx <<= 1;
    s = bc_num_mul(&copy, &copy, &copy, powrdx);
    if (s) goto err;

    if (pow & 1) {
      resrdx += powrdx;
      s = bc_num_mul(c, &copy, c, resrdx);
      if (s) goto err;
    }
  }

  if (neg) {
    s = bc_num_inv(c, c, scale);
    if (s) goto err;
  }

  if (TT.signe) {
    s = BC_STATUS_EXEC_SIGNAL;
    goto err;
  }

  if (c->rdx > scale) bc_num_truncate(c, c->rdx - scale);

  // We can't use bc_num_clean() here.
  for (zero = 1, i = 0; zero && i < c->len; ++i) zero = !c->num[i];
  if (zero) bc_num_setToZero(c, scale);

err:
  bc_num_free(&copy);
  return s;
}

BcStatus bc_num_binary(BcNum *a, BcNum *b, BcNum *c, size_t scale,
                       BcNumBinaryOp op, size_t req)
{
  BcStatus s;
  BcNum num2, *ptr_a, *ptr_b;
  int init = 0;

  if (c == a) {
    ptr_a = &num2;
    memcpy(ptr_a, c, sizeof(BcNum));
    init = 1;
  }
  else ptr_a = a;

  if (c == b) {
    ptr_b = &num2;
    if (c != a) {
      memcpy(ptr_b, c, sizeof(BcNum));
      init = 1;
    }
  }
  else ptr_b = b;

  if (init) bc_num_init(c, req);
  else bc_num_expand(c, req);

  s = op(ptr_a, ptr_b, c, scale);

  if (init) bc_num_free(&num2);

  return s;
}

int bc_num_strValid(const char *val, size_t base) {

  BcDig b;
  int small, radix = 0;
  size_t i, len = strlen(val);

  if (!len) return 1;

  small = base <= 10;
  b = (BcDig) (small ? base + '0' : base - 10 + 'A');

  for (i = 0; i < len; ++i) {

    BcDig c = val[i];

    if (c == '.') {

      if (radix) return 0;

      radix = 1;
      continue;
    }

    if (c < '0' || (small && c >= b) || (c > '9' && (c < 'A' || c >= b)))
      return 0;
  }

  return 1;
}

void bc_num_parseDecimal(BcNum *n, const char *val) {

  size_t len, i;
  const char *ptr;
  int zero = 1;

  for (i = 0; val[i] == '0'; ++i);

  val += i;
  len = strlen(val);
  bc_num_zero(n);

  if (len) {
    for (i = 0; zero && i < len; ++i) zero = val[i] == '0' || val[i] == '.';
    bc_num_expand(n, len);
  }

  ptr = strchr(val, '.');

  // Explicitly test for NULL here to produce either a 0 or 1.
  n->rdx = (size_t) ((ptr != NULL) * ((val + len) - (ptr + 1)));

  if (!zero) {
    for (i = len - 1; i < len; ++n->len, i -= 1 + (i && val[i - 1] == '.'))
      n->num[n->len] = val[i] - '0';
  }
}

void bc_num_parseBase(BcNum *n, const char *val, BcNum *base) {

  BcStatus s;
  BcNum temp, mult, result;
  BcDig c = '\0';
  int zero = 1;
  unsigned long v;
  size_t i, digits, len = strlen(val);

  bc_num_zero(n);

  for (i = 0; zero && i < len; ++i) zero = (val[i] == '.' || val[i] == '0');
  if (zero) return;

  bc_num_init(&temp, BC_NUM_DEF_SIZE);
  bc_num_init(&mult, BC_NUM_DEF_SIZE);

  for (i = 0; i < len; ++i) {

    c = val[i];
    if (c == '.') break;

    v = (unsigned long) (c <= '9' ? c - '0' : c - 'A' + 10);

    s = bc_num_mul(n, base, &mult, 0);
    if (s) goto int_err;
    s = bc_num_ulong2num(&temp, v);
    if (s) goto int_err;
    s = bc_num_add(&mult, &temp, n, 0);
    if (s) goto int_err;
  }

  if (i == len) {
    c = val[i];
    if (c == 0) goto int_err;
  }

  bc_num_init(&result, base->len);
  bc_num_zero(&result);
  bc_num_one(&mult);

  for (i += 1, digits = 0; i < len; ++i, ++digits) {

    c = val[i];
    if (c == 0) break;

    v = (unsigned long) (c <= '9' ? c - '0' : c - 'A' + 10);

    s = bc_num_mul(&result, base, &result, 0);
    if (s) goto err;
    s = bc_num_ulong2num(&temp, v);
    if (s) goto err;
    s = bc_num_add(&result, &temp, &result, 0);
    if (s) goto err;
    s = bc_num_mul(&mult, base, &mult, 0);
    if (s) goto err;
  }

  s = bc_num_div(&result, &mult, &result, digits);
  if (s) goto err;
  s = bc_num_add(n, &result, n, digits);
  if (s) goto err;

  if (n->len) {
    if (n->rdx < digits) bc_num_extend(n, digits - n->rdx);
  }
  else bc_num_zero(n);

err:
  bc_num_free(&result);
int_err:
  bc_num_free(&mult);
  bc_num_free(&temp);
}

void bc_num_printNewline(size_t *nchars, size_t line_len) {
  if (*nchars == line_len - 1) {
    bc_vm_putchar('\\');
    bc_vm_putchar('\n');
    *nchars = 0;
  }
}

void bc_num_printDigits(size_t num, size_t width, int radix,
                        size_t *nchars, size_t line_len)
{
  size_t exp, pow, div;

  bc_num_printNewline(nchars, line_len);
  bc_vm_putchar(radix ? '.' : ' ');
  ++(*nchars);

  bc_num_printNewline(nchars, line_len);
  for (exp = 0, pow = 1; exp < width - 1; ++exp, pow *= 10);

  for (exp = 0; exp < width; pow /= 10, ++(*nchars), ++exp) {
    bc_num_printNewline(nchars, line_len);
    div = num / pow;
    num -= div * pow;
    bc_vm_putchar(((char) div) + '0');
  }
}

void bc_num_printHex(size_t num, size_t width, int radix,
                     size_t *nchars, size_t line_len)
{

  if (radix) {
    bc_num_printNewline(nchars, line_len);
    bc_vm_putchar('.');
    *nchars += 1;
  }

  bc_num_printNewline(nchars, line_len);
  bc_vm_putchar(bc_num_hex_digits[num]);
  *nchars = *nchars + width;
}

void bc_num_printDecimal(BcNum *n, size_t *nchars, size_t len) {

  size_t i, rdx = n->rdx - 1;

  if (n->neg) bc_vm_putchar('-');
  (*nchars) += n->neg;

  for (i = n->len - 1; i < n->len; --i)
    bc_num_printHex((size_t) n->num[i], 1, i == rdx, nchars, len);
}

BcStatus bc_num_printNum(BcNum *n, BcNum *base, size_t width, size_t *nchars,
                         size_t len, BcNumDigitOp print)
{
  BcStatus s;
  BcVec stack;
  BcNum intp, fracp, digit, frac_len;
  unsigned long dig, *ptr;
  size_t i;
  int radix;

  if (n->len == 0) {
    print(0, width, 0, nchars, len);
    return BC_STATUS_SUCCESS;
  }

  bc_vec_init(&stack, sizeof(long), NULL);
  bc_num_init(&intp, n->len);
  bc_num_init(&fracp, n->rdx);
  bc_num_init(&digit, width);
  bc_num_init(&frac_len, BC_NUM_INT(n));
  bc_num_copy(&intp, n);
  bc_num_one(&frac_len);

  bc_num_truncate(&intp, intp.rdx);
  s = bc_num_sub(n, &intp, &fracp, 0);
  if (s) goto err;

  while (intp.len) {
    s = bc_num_divmod(&intp, base, &intp, &digit, 0);
    if (s) goto err;
    s = bc_num_ulong(&digit, &dig);
    if (s) goto err;
    bc_vec_push(&stack, &dig);
  }

  for (i = 0; i < stack.len; ++i) {
    ptr = bc_vec_item_rev(&stack, i);
    print(*ptr, width, 0, nchars, len);
  }

  if (!n->rdx) goto err;

  for (radix = 1; frac_len.len <= n->rdx; radix = 0) {
    s = bc_num_mul(&fracp, base, &fracp, n->rdx);
    if (s) goto err;
    s = bc_num_ulong(&fracp, &dig);
    if (s) goto err;
    s = bc_num_ulong2num(&intp, dig);
    if (s) goto err;
    s = bc_num_sub(&fracp, &intp, &fracp, 0);
    if (s) goto err;
    print(dig, width, radix, nchars, len);
    s = bc_num_mul(&frac_len, base, &frac_len, 0);
    if (s) goto err;
  }

err:
  bc_num_free(&frac_len);
  bc_num_free(&digit);
  bc_num_free(&fracp);
  bc_num_free(&intp);
  bc_vec_free(&stack);
  return s;
}

BcStatus bc_num_printBase(BcNum *n, BcNum *base, size_t base_t,
                          size_t *nchars, size_t line_len)
{
  BcStatus s;
  size_t width, i;
  BcNumDigitOp print;
  int neg = n->neg;

  if (neg) bc_vm_putchar('-');
  (*nchars) += neg;

  n->neg = 0;

  if (base_t <= BC_NUM_MAX_IBASE) {
    width = 1;
    print = bc_num_printHex;
  }
  else {
    for (i = base_t - 1, width = 0; i; i /= 10, ++width);
    print = bc_num_printDigits;
  }

  s = bc_num_printNum(n, base, width, nchars, line_len, print);
  n->neg = neg;

  return s;
}

void bc_num_init(BcNum *n, size_t req) {
  req = req >= BC_NUM_DEF_SIZE ? req : BC_NUM_DEF_SIZE;
  memset(n, 0, sizeof(BcNum));
  n->num = xmalloc(req);
  n->cap = req;
}

void bc_num_expand(BcNum *n, size_t req) {
  req = req >= BC_NUM_DEF_SIZE ? req : BC_NUM_DEF_SIZE;
  if (req > n->cap) {
    n->num = xrealloc(n->num, req);
    n->cap = req;
  }
}

void bc_num_free(void *num) {
  free(((BcNum*) num)->num);
}

void bc_num_copy(BcNum *d, BcNum *s) {

  if (d != s) {
    bc_num_expand(d, s->cap);
    d->len = s->len;
    d->neg = s->neg;
    d->rdx = s->rdx;
    memcpy(d->num, s->num, sizeof(BcDig) * d->len);
  }
}

BcStatus bc_num_parse(BcNum *n, const char *val, BcNum *base, size_t base_t) {

  if (!bc_num_strValid(val, base_t)) return BC_STATUS_MATH_BAD_STRING;

  if (base_t == 10) bc_num_parseDecimal(n, val);
  else bc_num_parseBase(n, val, base);

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_print(BcNum *n, BcNum *base, size_t base_t, int newline,
                      size_t *nchars, size_t line_len)
{
  BcStatus s = BC_STATUS_SUCCESS;

  bc_num_printNewline(nchars, line_len);

  if (n->len == 0) {
    bc_vm_putchar('0');
    ++(*nchars);
  }
  else if (base_t == 10) bc_num_printDecimal(n, nchars, line_len);
  else s = bc_num_printBase(n, base, base_t, nchars, line_len);

  if (newline) {
    bc_vm_putchar('\n');
    *nchars = 0;
  }

  return s;
}

BcStatus bc_num_ulong(BcNum *n, unsigned long *result) {

  size_t i;
  unsigned long pow;

  if (n->neg) return BC_STATUS_MATH_NEGATIVE;

  for (*result = 0, pow = 1, i = n->rdx; i < n->len; ++i) {

    unsigned long prev = *result, powprev = pow;

    *result += ((unsigned long) n->num[i]) * pow;
    pow *= 10;

    if (*result < prev || pow < powprev) return BC_STATUS_MATH_OVERFLOW;
  }

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_ulong2num(BcNum *n, unsigned long val) {

  size_t len;
  BcDig *ptr;
  unsigned long i;

  bc_num_zero(n);

  if (val == 0) return BC_STATUS_SUCCESS;

  for (len = 1, i = ULONG_MAX; i; i /= 10, ++len)
  bc_num_expand(n, len);
  for (ptr = n->num, i = 0; val; ++i, ++n->len, val /= 10) ptr[i] = val % 10;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_num_add(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
  BcNumBinaryOp op = (!a->neg == !b->neg) ? bc_num_a : bc_num_s;
  (void) scale;
  return bc_num_binary(a, b, c, 0, op, BC_NUM_AREQ(a, b));
}

BcStatus bc_num_sub(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
  BcNumBinaryOp op = (!a->neg == !b->neg) ? bc_num_s : bc_num_a;
  (void) scale;
  return bc_num_binary(a, b, c, 1, op, BC_NUM_AREQ(a, b));
}

BcStatus bc_num_mul(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
  size_t req = BC_NUM_MREQ(a, b, scale);
  return bc_num_binary(a, b, c, scale, bc_num_m, req);
}

BcStatus bc_num_div(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
  size_t req = BC_NUM_MREQ(a, b, scale);
  return bc_num_binary(a, b, c, scale, bc_num_d, req);
}

BcStatus bc_num_mod(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
  size_t req = BC_NUM_MREQ(a, b, scale);
  return bc_num_binary(a, b, c, scale, bc_num_rem, req);
}

BcStatus bc_num_pow(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
  return bc_num_binary(a, b, c, scale, bc_num_p, a->len * b->len + 1);
}

BcStatus bc_num_sqrt(BcNum *a, BcNum *restrict b, size_t scale) {

  BcStatus s;
  BcNum num1, num2, half, f, fprime, *x0, *x1, *temp;
  size_t pow, len, digs, digs1, resrdx, req, times = 0;
  ssize_t cmp = 1, cmp1 = SSIZE_MAX, cmp2 = SSIZE_MAX;

  req = maxof(scale, a->rdx) + ((BC_NUM_INT(a) + 1) >> 1) + 1;
  bc_num_expand(b, req);

  if (a->len == 0) {
    bc_num_setToZero(b, scale);
    return BC_STATUS_SUCCESS;
  }
  else if (a->neg) return BC_STATUS_MATH_NEGATIVE;
  else if (BC_NUM_ONE(a)) {
    bc_num_one(b);
    bc_num_extend(b, scale);
    return BC_STATUS_SUCCESS;
  }

  scale = maxof(scale, a->rdx) + 1;
  len = a->len + scale;

  bc_num_init(&num1, len);
  bc_num_init(&num2, len);
  bc_num_init(&half, BC_NUM_DEF_SIZE);

  bc_num_one(&half);
  half.num[0] = 5;
  half.rdx = 1;

  bc_num_init(&f, len);
  bc_num_init(&fprime, len);

  x0 = &num1;
  x1 = &num2;

  bc_num_one(x0);
  pow = BC_NUM_INT(a);

  if (pow) {

    if (pow & 1) x0->num[0] = 2;
    else x0->num[0] = 6;

    pow -= 2 - (pow & 1);

    bc_num_extend(x0, pow);

    // Make sure to move the radix back.
    x0->rdx -= pow;
  }

  x0->rdx = digs = digs1 = 0;
  resrdx = scale + 2;
  len = BC_NUM_INT(x0) + resrdx - 1;

  while (!TT.signe && (cmp || digs < len)) {

    s = bc_num_div(a, x0, &f, resrdx);
    if (s) goto err;
    s = bc_num_add(x0, &f, &fprime, resrdx);
    if (s) goto err;
    s = bc_num_mul(&fprime, &half, x1, resrdx);
    if (s) goto err;

    cmp = bc_num_cmp(x1, x0);
    digs = x1->len - (unsigned long long) llabs(cmp);

    if (cmp == cmp2 && digs == digs1) times += 1;
    else times = 0;

    resrdx += times > 4;

    cmp2 = cmp1;
    cmp1 = cmp;
    digs1 = digs;

    temp = x0;
    x0 = x1;
    x1 = temp;
  }

  if (TT.signe) {
    s = BC_STATUS_EXEC_SIGNAL;
    goto err;
  }

  bc_num_copy(b, x0);
  scale -= 1;
  if (b->rdx > scale) bc_num_truncate(b, b->rdx - scale);

err:
  bc_num_free(&fprime);
  bc_num_free(&f);
  bc_num_free(&half);
  bc_num_free(&num2);
  bc_num_free(&num1);
  return s;
}

BcStatus bc_num_divmod(BcNum *a, BcNum *b, BcNum *c, BcNum *d, size_t scale) {

  BcStatus s;
  BcNum num2, *ptr_a;
  int init = 0;
  size_t ts = maxof(scale + b->rdx, a->rdx), len = BC_NUM_MREQ(a, b, ts);

  if (c == a) {
    memcpy(&num2, c, sizeof(BcNum));
    ptr_a = &num2;
    bc_num_init(c, len);
    init = 1;
  }
  else {
    ptr_a = a;
    bc_num_expand(c, len);
  }

  s = bc_num_r(ptr_a, b, c, d, scale, ts);

  if (init) bc_num_free(&num2);

  return s;
}

int bc_id_cmp(const void *e1, const void *e2) {
  return strcmp(((const BcId*) e1)->name, ((const BcId*) e2)->name);
}

void bc_id_free(void *id) {
  free(((BcId*) id)->name);
}

BcStatus bc_func_insert(BcFunc *f, char *name, int var) {

  BcId a;
  size_t i;

  for (i = 0; i < f->autos.len; ++i) {
    if (!strcmp(name, ((BcId*) bc_vec_item(&f->autos, i))->name))
      return BC_STATUS_PARSE_DUPLICATE_LOCAL;
  }

  a.idx = var;
  a.name = name;

  bc_vec_push(&f->autos, &a);

  return BC_STATUS_SUCCESS;
}

void bc_func_init(BcFunc *f) {
  bc_vec_init(&f->code, sizeof(char), NULL);
  bc_vec_init(&f->autos, sizeof(BcId), bc_id_free);
  bc_vec_init(&f->labels, sizeof(size_t), NULL);
  f->nparams = 0;
}

void bc_func_free(void *func) {
  BcFunc *f = (BcFunc*) func;
  bc_vec_free(&f->code);
  bc_vec_free(&f->autos);
  bc_vec_free(&f->labels);
}

void bc_array_init(BcVec *a, int nums) {
  if (nums) bc_vec_init(a, sizeof(BcNum), bc_num_free);
  else bc_vec_init(a, sizeof(BcVec), bc_vec_free);
  bc_array_expand(a, 1);
}

void bc_array_copy(BcVec *d, const BcVec *s) {

  size_t i;

  bc_vec_npop(d, d->len);
  bc_vec_expand(d, s->cap);
  d->len = s->len;

  for (i = 0; i < s->len; ++i) {
    BcNum *dnum = bc_vec_item(d, i), *snum = bc_vec_item(s, i);
    bc_num_init(dnum, snum->len);
    bc_num_copy(dnum, snum);
  }
}

void bc_array_expand(BcVec *a, size_t len) {

  BcResultData data;

  if (a->size == sizeof(BcNum) && a->dtor == bc_num_free) {
    while (len > a->len) {
      bc_num_init(&data.n, BC_NUM_DEF_SIZE);
      bc_vec_push(a, &data.n);
    }
  }
  else {
    while (len > a->len) {
      bc_array_init(&data.v, 1);
      bc_vec_push(a, &data.v);
    }
  }
}

void bc_string_free(void *string) {
  free(*((char**) string));
}

void bc_result_free(void *result) {

  BcResult *r = (BcResult*) result;

  switch (r->t) {

    case BC_RESULT_TEMP:
    case BC_RESULT_IBASE:
    case BC_RESULT_SCALE:
    case BC_RESULT_OBASE:
    {
      bc_num_free(&r->d.n);
      break;
    }

    case BC_RESULT_VAR:
    case BC_RESULT_ARRAY:
    case BC_RESULT_ARRAY_ELEM:
    {
      free(r->d.id.name);
      break;
    }

    default:
    {
      // Do nothing.
      break;
    }
  }
}

void bc_lex_lineComment(BcLex *l) {
  l->t.t = BC_LEX_WHITESPACE;
  while (l->i < l->len && l->buf[l->i++] != '\n');
  --l->i;
}

void bc_lex_whitespace(BcLex *l) {
  char c;
  l->t.t = BC_LEX_WHITESPACE;
  for (c = l->buf[l->i]; c != '\n' && isspace(c); c = l->buf[++l->i]);
}

BcStatus bc_lex_number(BcLex *l, char start) {

  const char *buf = l->buf + l->i;
  size_t len, hits = 0, bslashes = 0, i = 0, j;
  char c = buf[i];
  int last_pt, pt = start == '.';

  last_pt = pt;
  l->t.t = BC_LEX_NUMBER;

  while (c && (isdigit(c) || (c >= 'A' && c <= 'F') ||
               (c == '.' && !pt) || (c == '\\' && buf[i + 1] == '\n')))
  {
    if (c != '\\') {
      last_pt = c == '.';
      pt = pt || last_pt;
    }
    else {
      ++i;
      bslashes += 1;
    }

    c = buf[++i];
  }

  len = i + 1 * !last_pt - bslashes * 2;
  if (len > BC_MAX_NUM) return BC_STATUS_EXEC_NUM_LEN;

  bc_vec_npop(&l->t.v, l->t.v.len);
  bc_vec_expand(&l->t.v, len + 1);
  bc_vec_push(&l->t.v, &start);

  for (buf -= 1, j = 1; j < len + hits * 2; ++j) {

    c = buf[j];

    // If we have hit a backslash, skip it. We don't have
    // to check for a newline because it's guaranteed.
    if (hits < bslashes && c == '\\') {
      ++hits;
      ++j;
      continue;
    }

    bc_vec_push(&l->t.v, &c);
  }

  bc_vec_pushByte(&l->t.v, '\0');
  l->i += i;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_name(BcLex *l) {

  size_t i = 0;
  const char *buf = l->buf + l->i - 1;
  char c = buf[i];

  l->t.t = BC_LEX_NAME;

  while ((c >= 'a' && c <= 'z') || isdigit(c) || c == '_') c = buf[++i];

  if (i > BC_MAX_STRING) return BC_STATUS_EXEC_NAME_LEN;
  bc_vec_string(&l->t.v, i, buf);

  // Increment the index. We minus 1 because it has already been incremented.
  l->i += i - 1;

  return BC_STATUS_SUCCESS;
}

void bc_lex_init(BcLex *l) {

  bc_vec_init(&l->t.v, sizeof(char), NULL);
}

void bc_lex_free(BcLex *l) {
  bc_vec_free(&l->t.v);
}

void bc_lex_file(BcLex *l, const char *file) {
  l->line = 1;
  l->newline = 0;
  l->f = file;
}

BcStatus bc_lex_next(BcLex *l) {

  BcStatus s;

  l->t.last = l->t.t;
  if (l->t.last == BC_LEX_EOF) return BC_STATUS_LEX_EOF;

  l->line += l->newline;
  l->t.t = BC_LEX_EOF;

  l->newline = (l->i == l->len);
  if (l->newline) return BC_STATUS_SUCCESS;

  // Loop until failure or we don't have whitespace. This
  // is so the parser doesn't get inundated with whitespace.
  do {
    s = bc_lex_token(l);
  } while (!s && l->t.t == BC_LEX_WHITESPACE);

  return s;
}

BcStatus bc_lex_text(BcLex *l, const char *text) {
  l->buf = text;
  l->i = 0;
  l->len = strlen(text);
  l->t.t = l->t.last = BC_LEX_INVALID;
  return bc_lex_next(l);
}

BcStatus bc_lex_identifier(BcLex *l) {

  BcStatus s;
  size_t i;
  const char *buf = l->buf + l->i - 1;

  for (i = 0; i < sizeof(bc_lex_kws) / sizeof(bc_lex_kws[0]); ++i) {

    unsigned long len = (unsigned long) bc_lex_kws[i].len;

    if (strncmp(buf, bc_lex_kws[i].name, len) == 0) {

      l->t.t = BC_LEX_KEY_AUTO + (BcLexType) i;

      if (!bc_lex_kws[i].posix) {
        s = bc_vm_posixError(BC_STATUS_POSIX_BAD_KW, l->f,
                             l->line, bc_lex_kws[i].name);
        if (s) return s;
      }

      // We minus 1 because the index has already been incremented.
      l->i += len - 1;
      return BC_STATUS_SUCCESS;
    }
  }

  s = bc_lex_name(l);
  if (s) return s;

  if (l->t.v.len - 1 > 1)
    s = bc_vm_posixError(BC_STATUS_POSIX_NAME_LEN, l->f, l->line, buf);

  return s;
}

BcStatus bc_lex_string(BcLex *l) {

  size_t len, nls = 0, i = l->i;
  char c;

  l->t.t = BC_LEX_STR;

  for (c = l->buf[i]; c && c != '"'; c = l->buf[++i]) nls += (c == '\n');

  if (c == '\0') {
    l->i = i;
    return BC_STATUS_LEX_NO_STRING_END;
  }

  len = i - l->i;
  if (len > BC_MAX_STRING) return BC_STATUS_EXEC_STRING_LEN;
  bc_vec_string(&l->t.v, len, l->buf + l->i);

  l->i = i + 1;
  l->line += nls;

  return BC_STATUS_SUCCESS;
}

void bc_lex_assign(BcLex *l, BcLexType with, BcLexType without) {
  if (l->buf[l->i] == '=') {
    ++l->i;
    l->t.t = with;
  }
  else l->t.t = without;
}

BcStatus bc_lex_comment(BcLex *l) {

  size_t i, nls = 0;
  const char *buf = l->buf;
  int end = 0;
  char c;

  l->t.t = BC_LEX_WHITESPACE;

  for (i = ++l->i; !end; i += !end) {

    for (c = buf[i]; c != '*' && c; c = buf[++i]) nls += (c == '\n');

    if (c == 0 || buf[i + 1] == '\0') {
      l->i = i;
      return BC_STATUS_LEX_NO_COMMENT_END;
    }

    end = buf[i + 1] == '/';
  }

  l->i = i + 2;
  l->line += nls;

  return BC_STATUS_SUCCESS;
}

BcStatus bc_lex_token(BcLex *l) {

  BcStatus s = BC_STATUS_SUCCESS;
  char c = l->buf[l->i++], c2;

  // This is the workhorse of the lexer.
  switch (c) {

    case '\0':
    case '\n':
    {
      l->newline = 1;
      l->t.t = !c ? BC_LEX_EOF : BC_LEX_NLINE;
      break;
    }

    case '\t':
    case '\v':
    case '\f':
    case '\r':
    case ' ':
    {
      bc_lex_whitespace(l);
      break;
    }

    case '!':
    {
      bc_lex_assign(l, BC_LEX_OP_REL_NE, BC_LEX_OP_BOOL_NOT);

      if (l->t.t == BC_LEX_OP_BOOL_NOT) {
        s = bc_vm_posixError(BC_STATUS_POSIX_BOOL, l->f, l->line, "!");
        if (s) return s;
      }

      break;
    }

    case '"':
    {
      s = bc_lex_string(l);
      break;
    }

    case '#':
    {
      s = bc_vm_posixError(BC_STATUS_POSIX_COMMENT, l->f, l->line, NULL);
      if (s) return s;

      bc_lex_lineComment(l);

      break;
    }

    case '%':
    {
      bc_lex_assign(l, BC_LEX_OP_ASSIGN_MODULUS, BC_LEX_OP_MODULUS);
      break;
    }

    case '&':
    {
      c2 = l->buf[l->i];
      if (c2 == '&') {

        s = bc_vm_posixError(BC_STATUS_POSIX_BOOL, l->f, l->line, "&&");
        if (s) return s;

        ++l->i;
        l->t.t = BC_LEX_OP_BOOL_AND;
      }
      else {
        l->t.t = BC_LEX_INVALID;
        s = BC_STATUS_LEX_BAD_CHAR;
      }

      break;
    }

    case '(':
    case ')':
    {
      l->t.t = (BcLexType) (c - '(' + BC_LEX_LPAREN);
      break;
    }

    case '*':
    {
      bc_lex_assign(l, BC_LEX_OP_ASSIGN_MULTIPLY, BC_LEX_OP_MULTIPLY);
      break;
    }

    case '+':
    {
      c2 = l->buf[l->i];
      if (c2 == '+') {
        ++l->i;
        l->t.t = BC_LEX_OP_INC;
      }
      else bc_lex_assign(l, BC_LEX_OP_ASSIGN_PLUS, BC_LEX_OP_PLUS);
      break;
    }

    case ',':
    {
      l->t.t = BC_LEX_COMMA;
      break;
    }

    case '-':
    {
      c2 = l->buf[l->i];
      if (c2 == '-') {
        ++l->i;
        l->t.t = BC_LEX_OP_DEC;
      }
      else bc_lex_assign(l, BC_LEX_OP_ASSIGN_MINUS, BC_LEX_OP_MINUS);
      break;
    }

    case '.':
    {
      if (isdigit(l->buf[l->i])) s = bc_lex_number(l, c);
      else {
        l->t.t = BC_LEX_KEY_LAST;
        s = bc_vm_posixError(BC_STATUS_POSIX_DOT, l->f, l->line, NULL);
      }
      break;
    }

    case '/':
    {
      c2 = l->buf[l->i];
      if (c2 =='*') s = bc_lex_comment(l);
      else bc_lex_assign(l, BC_LEX_OP_ASSIGN_DIVIDE, BC_LEX_OP_DIVIDE);
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
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    {
      s = bc_lex_number(l, c);
      break;
    }

    case ';':
    {
      l->t.t = BC_LEX_SCOLON;
      break;
    }

    case '<':
    {
      bc_lex_assign(l, BC_LEX_OP_REL_LE, BC_LEX_OP_REL_LT);
      break;
    }

    case '=':
    {
      bc_lex_assign(l, BC_LEX_OP_REL_EQ, BC_LEX_OP_ASSIGN);
      break;
    }

    case '>':
    {
      bc_lex_assign(l, BC_LEX_OP_REL_GE, BC_LEX_OP_REL_GT);
      break;
    }

    case '[':
    case ']':
    {
      l->t.t = (BcLexType) (c - '[' + BC_LEX_LBRACKET);
      break;
    }

    case '\\':
    {
      if (l->buf[l->i] == '\n') {
        l->t.t = BC_LEX_WHITESPACE;
        ++l->i;
      }
      else s = BC_STATUS_LEX_BAD_CHAR;
      break;
    }

    case '^':
    {
      bc_lex_assign(l, BC_LEX_OP_ASSIGN_POWER, BC_LEX_OP_POWER);
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
      s = bc_lex_identifier(l);
      break;
    }

    case '{':
    case '}':
    {
      l->t.t = (BcLexType) (c - '{' + BC_LEX_LBRACE);
      break;
    }

    case '|':
    {
      c2 = l->buf[l->i];

      if (c2 == '|') {

        s = bc_vm_posixError(BC_STATUS_POSIX_BOOL, l->f, l->line, "||");
        if (s) return s;

        ++l->i;
        l->t.t = BC_LEX_OP_BOOL_OR;
      }
      else {
        l->t.t = BC_LEX_INVALID;
        s = BC_STATUS_LEX_BAD_CHAR;
      }

      break;
    }

    default:
    {
      l->t.t = BC_LEX_INVALID;
      s = BC_STATUS_LEX_BAD_CHAR;
      break;
    }
  }

  return s;
}

void bc_parse_addFunc(BcParse *p, char *name, size_t *idx) {
  bc_program_addFunc(p->prog, name, idx);
  p->func = bc_vec_item(&p->prog->fns, p->fidx);
}

void bc_parse_pushName(BcParse *p, char *name) {

  size_t i = 0, len = strlen(name);

  for (; i < len; ++i) bc_parse_push(p, name[i]);
  bc_parse_push(p, BC_PARSE_STREND);

  free(name);
}

void bc_parse_pushIndex(BcParse *p, size_t idx) {

  unsigned char amt, i, nums[sizeof(size_t)];

  for (amt = 0; idx; ++amt) {
    nums[amt] = (char) idx;
    idx = (idx & ((unsigned long) ~(UCHAR_MAX))) >> sizeof(char) * CHAR_BIT;
  }

  bc_parse_push(p, amt);
  for (i = 0; i < amt; ++i) bc_parse_push(p, nums[i]);
}

void bc_parse_number(BcParse *p, BcInst *prev, size_t *nexs) {

  char *num = xstrdup(p->l.t.v.v);
  size_t idx = p->prog->consts.len;

  bc_vec_push(&p->prog->consts, &num);

  bc_parse_push(p, BC_INST_NUM);
  bc_parse_pushIndex(p, idx);

  ++(*nexs);
  (*prev) = BC_INST_NUM;
}

BcStatus bc_parse_text(BcParse *p, const char *text) {

  BcStatus s;

  p->func = bc_vec_item(&p->prog->fns, p->fidx);

  if (!strcmp(text, "") && !BC_PARSE_CAN_EXEC(p)) {
    p->l.t.t = BC_LEX_INVALID;
    s = bc_parse_parse(p);
    if (s) return s;
    if (!BC_PARSE_CAN_EXEC(p)) return BC_STATUS_EXEC_FILE_NOT_EXECUTABLE;
  }

  return bc_lex_text(&p->l, text);
}

BcStatus bc_parse_reset(BcParse *p, BcStatus s) {

  if (p->fidx != BC_PROG_MAIN) {

    p->func->nparams = 0;
    bc_vec_npop(&p->func->code, p->func->code.len);
    bc_vec_npop(&p->func->autos, p->func->autos.len);
    bc_vec_npop(&p->func->labels, p->func->labels.len);

    bc_parse_updateFunc(p, BC_PROG_MAIN);
  }

  p->l.i = p->l.len;
  p->l.t.t = BC_LEX_EOF;
  p->auto_part = (p->nbraces = 0);

  bc_vec_npop(&p->flags, p->flags.len - 1);
  bc_vec_npop(&p->exits, p->exits.len);
  bc_vec_npop(&p->conds, p->conds.len);
  bc_vec_npop(&p->ops, p->ops.len);

  return bc_program_reset(p->prog, s);
}

void bc_parse_free(BcParse *p) {
  bc_vec_free(&p->flags);
  bc_vec_free(&p->exits);
  bc_vec_free(&p->conds);
  bc_vec_free(&p->ops);
  bc_lex_free(&p->l);
}

void bc_parse_init(BcParse *p, BcProgram *prog, size_t func) {

  memset(p, 0, sizeof(BcParse));

  bc_lex_init(&p->l);
  bc_vec_init(&p->flags, sizeof(uint8_t), NULL);
  bc_vec_init(&p->exits, sizeof(BcInstPtr), NULL);
  bc_vec_init(&p->conds, sizeof(size_t), NULL);
  bc_vec_pushByte(&p->flags, 0);
  bc_vec_init(&p->ops, sizeof(BcLexType), NULL);

  p->prog = prog;
  p->auto_part = (p->nbraces = 0);
  bc_parse_updateFunc(p, func);
}

BcStatus bc_parse_else(BcParse *p);
BcStatus bc_parse_stmt(BcParse *p);

BcStatus bc_parse_operator(BcParse *p, BcLexType type, size_t start,
                           size_t *nexprs, int next)
{
  BcStatus s = BC_STATUS_SUCCESS;
  BcLexType t;
  char l, r = bc_parse_ops[type - BC_LEX_OP_INC].prec;
  int left = bc_parse_ops[type - BC_LEX_OP_INC].left;

  while (p->ops.len > start) {

    t = BC_PARSE_TOP_OP(p);
    if (t == BC_LEX_LPAREN) break;

    l = bc_parse_ops[t - BC_LEX_OP_INC].prec;
    if (l >= r && (l != r || !left)) break;

    bc_parse_push(p, BC_PARSE_TOKEN_INST(t));
    bc_vec_pop(&p->ops);
    *nexprs -= t != BC_LEX_OP_BOOL_NOT && t != BC_LEX_NEG;
  }

  bc_vec_push(&p->ops, &type);
  if (next) s = bc_lex_next(&p->l);

  return s;
}

BcStatus bc_parse_rightParen(BcParse *p, size_t ops_bgn, size_t *nexs) {

  BcLexType top;

  if (p->ops.len <= ops_bgn) return BC_STATUS_PARSE_BAD_EXP;
  top = BC_PARSE_TOP_OP(p);

  while (top != BC_LEX_LPAREN) {

    bc_parse_push(p, BC_PARSE_TOKEN_INST(top));

    bc_vec_pop(&p->ops);
    *nexs -= top != BC_LEX_OP_BOOL_NOT && top != BC_LEX_NEG;

    if (p->ops.len <= ops_bgn) return BC_STATUS_PARSE_BAD_EXP;
    top = BC_PARSE_TOP_OP(p);
  }

  bc_vec_pop(&p->ops);

  return bc_lex_next(&p->l);
}

BcStatus bc_parse_params(BcParse *p, uint8_t flags) {

  BcStatus s;
  int comma = 0;
  size_t nparams;

  s = bc_lex_next(&p->l);
  if (s) return s;

  for (nparams = 0; p->l.t.t != BC_LEX_RPAREN; ++nparams) {

    flags = (flags & ~(BC_PARSE_PRINT | BC_PARSE_REL)) | BC_PARSE_ARRAY;
    s = bc_parse_expr(p, flags, bc_parse_next_param);
    if (s) return s;

    comma = p->l.t.t == BC_LEX_COMMA;
    if (comma) {
      s = bc_lex_next(&p->l);
      if (s) return s;
    }
  }

  if (comma) return BC_STATUS_PARSE_BAD_TOKEN;
  bc_parse_push(p, BC_INST_CALL);
  bc_parse_pushIndex(p, nparams);

  return BC_STATUS_SUCCESS;
}

BcStatus bc_parse_call(BcParse *p, char *name, uint8_t flags) {

  BcStatus s;
  BcId entry, *entry_ptr;
  size_t idx;

  entry.name = name;

  s = bc_parse_params(p, flags);
  if (s) goto err;

  if (p->l.t.t != BC_LEX_RPAREN) {
    s = BC_STATUS_PARSE_BAD_TOKEN;
    goto err;
  }

  idx = bc_map_index(&p->prog->fn_map, &entry);

  if (idx == ((size_t) -1)) {
    name = xstrdup(entry.name);
    bc_parse_addFunc(p, name, &idx);
    idx = bc_map_index(&p->prog->fn_map, &entry);
    free(entry.name);
  }
  else free(name);

  entry_ptr = bc_vec_item(&p->prog->fn_map, idx);
  bc_parse_pushIndex(p, entry_ptr->idx);

  return bc_lex_next(&p->l);

err:
  free(name);
  return s;
}

BcStatus bc_parse_name(BcParse *p, BcInst *type, uint8_t flags) {

  BcStatus s;
  char *name;

  name = xstrdup(p->l.t.v.v);
  s = bc_lex_next(&p->l);
  if (s) goto err;

  if (p->l.t.t == BC_LEX_LBRACKET) {

    s = bc_lex_next(&p->l);
    if (s) goto err;

    if (p->l.t.t == BC_LEX_RBRACKET) {

      if (!(flags & BC_PARSE_ARRAY)) {
        s = BC_STATUS_PARSE_BAD_EXP;
        goto err;
      }

      *type = BC_INST_ARRAY;
    }
    else {

      *type = BC_INST_ARRAY_ELEM;

      flags &= ~(BC_PARSE_PRINT | BC_PARSE_REL);
      s = bc_parse_expr(p, flags, bc_parse_next_elem);
      if (s) goto err;
    }

    s = bc_lex_next(&p->l);
    if (s) goto err;
    bc_parse_push(p, *type);
    bc_parse_pushName(p, name);
  }
  else if (p->l.t.t == BC_LEX_LPAREN) {

    if (flags & BC_PARSE_NOCALL) {
      s = BC_STATUS_PARSE_BAD_TOKEN;
      goto err;
    }

    *type = BC_INST_CALL;
    s = bc_parse_call(p, name, flags);
  }
  else {
    *type = BC_INST_VAR;
     bc_parse_push(p, BC_INST_VAR);
    bc_parse_pushName(p, name);
  }

  return s;

err:
  free(name);
  return s;
}

BcStatus bc_parse_read(BcParse *p) {

  BcStatus s;

  s = bc_lex_next(&p->l);
  if (s) return s;
  if (p->l.t.t != BC_LEX_LPAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  s = bc_lex_next(&p->l);
  if (s) return s;
  if (p->l.t.t != BC_LEX_RPAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  bc_parse_push(p, BC_INST_READ);

  return bc_lex_next(&p->l);
}

BcStatus bc_parse_builtin(BcParse *p, BcLexType type,
                          uint8_t flags, BcInst *prev)
{
  BcStatus s;

  s = bc_lex_next(&p->l);
  if (s) return s;
  if (p->l.t.t != BC_LEX_LPAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  flags = (flags & ~(BC_PARSE_PRINT | BC_PARSE_REL)) | BC_PARSE_ARRAY;

  s = bc_lex_next(&p->l);
  if (s) return s;

  s = bc_parse_expr(p, flags, bc_parse_next_rel);
  if (s) return s;

  if (p->l.t.t != BC_LEX_RPAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  *prev = (type == BC_LEX_KEY_LENGTH) ? BC_INST_LENGTH : BC_INST_SQRT;
  bc_parse_push(p, *prev);

  return bc_lex_next(&p->l);
}

BcStatus bc_parse_scale(BcParse *p, BcInst *type, uint8_t flags) {

  BcStatus s;

  s = bc_lex_next(&p->l);
  if (s) return s;

  if (p->l.t.t != BC_LEX_LPAREN) {
    *type = BC_INST_SCALE;
    bc_parse_push(p, BC_INST_SCALE);
    return BC_STATUS_SUCCESS;
  }

  *type = BC_INST_SCALE_FUNC;
  flags &= ~(BC_PARSE_PRINT | BC_PARSE_REL);

  s = bc_lex_next(&p->l);
  if (s) return s;

  s = bc_parse_expr(p, flags, bc_parse_next_rel);
  if (s) return s;
  if (p->l.t.t != BC_LEX_RPAREN) return BC_STATUS_PARSE_BAD_TOKEN;
  bc_parse_push(p, BC_INST_SCALE_FUNC);

  return bc_lex_next(&p->l);
}

BcStatus bc_parse_incdec(BcParse *p, BcInst *prev, int *paren_expr,
                         size_t *nexprs, uint8_t flags)
{
  BcStatus s;
  BcLexType type;
  char inst;
  BcInst etype = *prev;

  if (etype == BC_INST_VAR || etype == BC_INST_ARRAY_ELEM ||
      etype == BC_INST_SCALE || etype == BC_INST_LAST ||
      etype == BC_INST_IBASE || etype == BC_INST_OBASE)
  {
    *prev = inst = BC_INST_INC_POST + (p->l.t.t != BC_LEX_OP_INC);
    bc_parse_push(p, inst);
    s = bc_lex_next(&p->l);
  }
  else {

    *prev = inst = BC_INST_INC_PRE + (p->l.t.t != BC_LEX_OP_INC);
    *paren_expr = 1;

    s = bc_lex_next(&p->l);
    if (s) return s;
    type = p->l.t.t;

    // Because we parse the next part of the expression
    // right here, we need to increment this.
    *nexprs = *nexprs + 1;

    switch (type) {

      case BC_LEX_NAME:
      {
        s = bc_parse_name(p, prev, flags | BC_PARSE_NOCALL);
        break;
      }

      case BC_LEX_KEY_IBASE:
      case BC_LEX_KEY_LAST:
      case BC_LEX_KEY_OBASE:
      {
        bc_parse_push(p, type - BC_LEX_KEY_IBASE + BC_INST_IBASE);
        s = bc_lex_next(&p->l);
        break;
      }

      case BC_LEX_KEY_SCALE:
      {
        s = bc_lex_next(&p->l);
        if (s) return s;
        if (p->l.t.t == BC_LEX_LPAREN) s = BC_STATUS_PARSE_BAD_TOKEN;
        else bc_parse_push(p, BC_INST_SCALE);
        break;
      }

      default:
      {
        s = BC_STATUS_PARSE_BAD_TOKEN;
        break;
      }
    }

    if (!s) bc_parse_push(p, inst);
  }

  return s;
}

BcStatus bc_parse_minus(BcParse *p, BcInst *prev, size_t ops_bgn,
                        int rparen, size_t *nexprs)
{
  BcStatus s;
  BcLexType type;
  BcInst etype = *prev;

  s = bc_lex_next(&p->l);
  if (s) return s;

  type = rparen || etype == BC_INST_INC_POST || etype == BC_INST_DEC_POST ||
         (etype >= BC_INST_NUM && etype <= BC_INST_SQRT) ?
                   BC_LEX_OP_MINUS : BC_LEX_NEG;
  *prev = BC_PARSE_TOKEN_INST(type);

  // We can just push onto the op stack because this is the largest
  // precedence operator that gets pushed. Inc/dec does not.
  if (type != BC_LEX_OP_MINUS) bc_vec_push(&p->ops, &type);
  else s = bc_parse_operator(p, type, ops_bgn, nexprs, 0);

  return s;
}

BcStatus bc_parse_string(BcParse *p, char inst) {

  char *str = xstrdup(p->l.t.v.v);

  bc_parse_push(p, BC_INST_STR);
  bc_parse_pushIndex(p, p->prog->strs.len);
  bc_vec_push(&p->prog->strs, &str);
  bc_parse_push(p, inst);

  return bc_lex_next(&p->l);
}

BcStatus bc_parse_print(BcParse *p) {

  BcStatus s;
  BcLexType type;
  int comma = 0;

  s = bc_lex_next(&p->l);
  if (s) return s;

  type = p->l.t.t;

  if (type == BC_LEX_SCOLON || type == BC_LEX_NLINE)
    return BC_STATUS_PARSE_BAD_PRINT;

  while (!s && type != BC_LEX_SCOLON && type != BC_LEX_NLINE) {

    if (type == BC_LEX_STR) s = bc_parse_string(p, BC_INST_PRINT_POP);
    else {
      s = bc_parse_expr(p, 0, bc_parse_next_print);
      if (s) return s;
      bc_parse_push(p, BC_INST_PRINT_POP);
    }

    if (s) return s;

    comma = p->l.t.t == BC_LEX_COMMA;
    if (comma) s = bc_lex_next(&p->l);
    type = p->l.t.t;
  }

  if (s) return s;
  if (comma) return BC_STATUS_PARSE_BAD_TOKEN;

  return bc_lex_next(&p->l);
}

BcStatus bc_parse_return(BcParse *p) {

  BcStatus s;
  BcLexType t;
  int paren;

  if (!BC_PARSE_FUNC(p)) return BC_STATUS_PARSE_BAD_TOKEN;

  s = bc_lex_next(&p->l);
  if (s) return s;

  t = p->l.t.t;
  paren = t == BC_LEX_LPAREN;

  if (t == BC_LEX_NLINE || t == BC_LEX_SCOLON) bc_parse_push(p, BC_INST_RET0);
  else {

    s = bc_parse_expr(p, 0, bc_parse_next_expr);
    if (s && s != BC_STATUS_PARSE_EMPTY_EXP) return s;
    else if (s == BC_STATUS_PARSE_EMPTY_EXP) {
      bc_parse_push(p, BC_INST_RET0);
      s = bc_lex_next(&p->l);
      if (s) return s;
    }

    if (!paren || p->l.t.last != BC_LEX_RPAREN) {
      s = bc_vm_posixError(BC_STATUS_POSIX_RET, p->l.f, p->l.line, NULL);
      if (s) return s;
    }

    bc_parse_push(p, BC_INST_RET);
  }

  return s;
}

BcStatus bc_parse_endBody(BcParse *p, int brace) {

  BcStatus s = BC_STATUS_SUCCESS;

  if (p->flags.len <= 1 || (brace && p->nbraces == 0))
    return BC_STATUS_PARSE_BAD_TOKEN;

  if (brace) {

    if (p->l.t.t == BC_LEX_RBRACE) {
      if (!p->nbraces) return BC_STATUS_PARSE_BAD_TOKEN;
      --p->nbraces;
      s = bc_lex_next(&p->l);
      if (s) return s;
    }
    else return BC_STATUS_PARSE_BAD_TOKEN;
  }

  if (BC_PARSE_IF(p)) {

    uint8_t *flag_ptr;

    while (p->l.t.t == BC_LEX_NLINE) {
      s = bc_lex_next(&p->l);
      if (s) return s;
    }

    bc_vec_pop(&p->flags);

    flag_ptr = BC_PARSE_TOP_FLAG_PTR(p);
    *flag_ptr = (*flag_ptr | BC_PARSE_FLAG_IF_END);

    if (p->l.t.t == BC_LEX_KEY_ELSE) s = bc_parse_else(p);
  }
  else if (BC_PARSE_ELSE(p)) {

    BcInstPtr *ip;
    size_t *label;

    bc_vec_pop(&p->flags);

    ip = bc_vec_top(&p->exits);
    label = bc_vec_item(&p->func->labels, ip->idx);
    *label = p->func->code.len;

    bc_vec_pop(&p->exits);
  }
  else if (BC_PARSE_FUNC_INNER(p)) {
    bc_parse_push(p, BC_INST_RET0);
    bc_parse_updateFunc(p, BC_PROG_MAIN);
    bc_vec_pop(&p->flags);
  }
  else {

    BcInstPtr *ip = bc_vec_top(&p->exits);
    size_t *label = bc_vec_top(&p->conds);

    bc_parse_push(p, BC_INST_JUMP);
    bc_parse_pushIndex(p, *label);

    label = bc_vec_item(&p->func->labels, ip->idx);
    *label = p->func->code.len;

    bc_vec_pop(&p->flags);
    bc_vec_pop(&p->exits);
    bc_vec_pop(&p->conds);
  }

  return s;
}

void bc_parse_startBody(BcParse *p, uint8_t flags) {
  uint8_t *flag_ptr = BC_PARSE_TOP_FLAG_PTR(p);
  flags |= (*flag_ptr & (BC_PARSE_FLAG_FUNC | BC_PARSE_FLAG_LOOP));
  flags |= BC_PARSE_FLAG_BODY;
  bc_vec_push(&p->flags, &flags);
}

void bc_parse_noElse(BcParse *p) {

  BcInstPtr *ip;
  size_t *label;
  uint8_t *flag_ptr = BC_PARSE_TOP_FLAG_PTR(p);

  *flag_ptr = (*flag_ptr & ~(BC_PARSE_FLAG_IF_END));

  ip = bc_vec_top(&p->exits);
  label = bc_vec_item(&p->func->labels, ip->idx);
  *label = p->func->code.len;

  bc_vec_pop(&p->exits);
}

BcStatus bc_parse_if(BcParse *p) {

  BcStatus s;
  BcInstPtr ip;

  s = bc_lex_next(&p->l);
  if (s) return s;
  if (p->l.t.t != BC_LEX_LPAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  s = bc_lex_next(&p->l);
  if (s) return s;
  s = bc_parse_expr(p, BC_PARSE_REL, bc_parse_next_rel);
  if (s) return s;
  if (p->l.t.t != BC_LEX_RPAREN) return BC_STATUS_PARSE_BAD_TOKEN;

  s = bc_lex_next(&p->l);
  if (s) return s;
  bc_parse_push(p, BC_INST_JUMP_ZERO);

  ip.idx = p->func->labels.len;
  ip.func = ip.len = 0;

  bc_parse_pushIndex(p, ip.idx);
  bc_vec_push(&p->exits, &ip);
  bc_vec_push(&p->func->labels, &ip.idx);
  bc_parse_startBody(p, BC_PARSE_FLAG_IF);

  return BC_STATUS_SUCCESS;
}

BcStatus bc_parse_else(BcParse *p) {

  BcInstPtr ip;

  if (!BC_PARSE_IF_END(p)) return BC_STATUS_PARSE_BAD_TOKEN;

  ip.idx = p->func->labels.len;
  ip.func = ip.len = 0;

  bc_parse_push(p, BC_INST_JUMP);
  bc_parse_pushIndex(p, ip.idx);

  bc_parse_noElse(p);

  bc_vec_push(&p->exits, &ip);
  bc_vec_push(&p->func->labels, &ip.idx);
  bc_parse_startBody(p, BC_PARSE_FLAG_ELSE);

  return bc_lex_next(&p->l);
}

BcStatus bc_parse_while(BcParse *p) {

  BcStatus s;
  BcInstPtr ip;

  s = bc_lex_next(&p->l);
  if (s) return s;
  if (p->l.t.t != BC_LEX_LPAREN) return BC_STATUS_PARSE_BAD_TOKEN;
  s = bc_lex_next(&p->l);
  if (s) return s;

  ip.idx = p->func->labels.len;

  bc_vec_push(&p->func->labels, &p->func->code.len);
  bc_vec_push(&p->conds, &ip.idx);

  ip.idx = p->func->labels.len;
  ip.func = 1;
  ip.len = 0;

  bc_vec_push(&p->exits, &ip);
  bc_vec_push(&p->func->labels, &ip.idx);

  s = bc_parse_expr(p, BC_PARSE_REL, bc_parse_next_rel);
  if (s) return s;
  if (p->l.t.t != BC_LEX_RPAREN) return BC_STATUS_PARSE_BAD_TOKEN;
  s = bc_lex_next(&p->l);
  if (s) return s;

  bc_parse_push(p, BC_INST_JUMP_ZERO);
  bc_parse_pushIndex(p, ip.idx);
  bc_parse_startBody(p, BC_PARSE_FLAG_LOOP | BC_PARSE_FLAG_LOOP_INNER);

  return BC_STATUS_SUCCESS;
}

BcStatus bc_parse_for(BcParse *p) {

  BcStatus s;
  BcInstPtr ip;
  size_t cond_idx, exit_idx, body_idx, update_idx;

  s = bc_lex_next(&p->l);
  if (s) return s;
  if (p->l.t.t != BC_LEX_LPAREN) return BC_STATUS_PARSE_BAD_TOKEN;
  s = bc_lex_next(&p->l);
  if (s) return s;

  if (p->l.t.t != BC_LEX_SCOLON) s = bc_parse_expr(p, 0, bc_parse_next_for);
  else s = bc_vm_posixError(BC_STATUS_POSIX_FOR1, p->l.f, p->l.line, NULL);

  if (s) return s;
  if (p->l.t.t != BC_LEX_SCOLON) return BC_STATUS_PARSE_BAD_TOKEN;
  s = bc_lex_next(&p->l);
  if (s) return s;

  cond_idx = p->func->labels.len;
  update_idx = cond_idx + 1;
  body_idx = update_idx + 1;
  exit_idx = body_idx + 1;

  bc_vec_push(&p->func->labels, &p->func->code.len);

  if (p->l.t.t != BC_LEX_SCOLON)
    s = bc_parse_expr(p, BC_PARSE_REL, bc_parse_next_for);
  else s = bc_vm_posixError(BC_STATUS_POSIX_FOR2, p->l.f, p->l.line, NULL);

  if (s) return s;
  if (p->l.t.t != BC_LEX_SCOLON) return BC_STATUS_PARSE_BAD_TOKEN;

  s = bc_lex_next(&p->l);
  if (s) return s;

  bc_parse_push(p, BC_INST_JUMP_ZERO);
  bc_parse_pushIndex(p, exit_idx);
  bc_parse_push(p, BC_INST_JUMP);
  bc_parse_pushIndex(p, body_idx);

  ip.idx = p->func->labels.len;

  bc_vec_push(&p->conds, &update_idx);
  bc_vec_push(&p->func->labels, &p->func->code.len);

  if (p->l.t.t != BC_LEX_RPAREN) s = bc_parse_expr(p, 0, bc_parse_next_rel);
  else s = bc_vm_posixError(BC_STATUS_POSIX_FOR3, p->l.f, p->l.line, NULL);

  if (s) return s;

  if (p->l.t.t != BC_LEX_RPAREN) return BC_STATUS_PARSE_BAD_TOKEN;
  bc_parse_push(p, BC_INST_JUMP);
  bc_parse_pushIndex(p, cond_idx);
  bc_vec_push(&p->func->labels, &p->func->code.len);

  ip.idx = exit_idx;
  ip.func = 1;
  ip.len = 0;

  bc_vec_push(&p->exits, &ip);
  bc_vec_push(&p->func->labels, &ip.idx);
  bc_lex_next(&p->l);
  bc_parse_startBody(p, BC_PARSE_FLAG_LOOP | BC_PARSE_FLAG_LOOP_INNER);

  return BC_STATUS_SUCCESS;
}

BcStatus bc_parse_loopExit(BcParse *p, BcLexType type) {

  BcStatus s;
  size_t i;
  BcInstPtr *ip;

  if (!BC_PARSE_LOOP(p)) return BC_STATUS_PARSE_BAD_TOKEN;

  if (type == BC_LEX_KEY_BREAK) {

    if (p->exits.len == 0) return BC_STATUS_PARSE_BAD_TOKEN;

    i = p->exits.len - 1;
    ip = bc_vec_item(&p->exits, i);

    while (!ip->func && i < p->exits.len) ip = bc_vec_item(&p->exits, i--);
    if (i >= p->exits.len && !ip->func) return BC_STATUS_PARSE_BAD_TOKEN;

    i = ip->idx;
  }
  else i = *((size_t*) bc_vec_top(&p->conds));

  bc_parse_push(p, BC_INST_JUMP);
  bc_parse_pushIndex(p, i);

  s = bc_lex_next(&p->l);
  if (s) return s;

  if (p->l.t.t != BC_LEX_SCOLON && p->l.t.t != BC_LEX_NLINE)
    return BC_STATUS_PARSE_BAD_TOKEN;

  return bc_lex_next(&p->l);
}

BcStatus bc_parse_func(BcParse *p) {

  BcStatus s;
  int var, comma = 0;
  uint8_t flags;
  char *name;

  s = bc_lex_next(&p->l);
  if (s) return s;
  if (p->l.t.t != BC_LEX_NAME) return BC_STATUS_PARSE_BAD_FUNC;

  name = xstrdup(p->l.t.v.v);
  bc_parse_addFunc(p, name, &p->fidx);

  s = bc_lex_next(&p->l);
  if (s) return s;
  if (p->l.t.t != BC_LEX_LPAREN) return BC_STATUS_PARSE_BAD_FUNC;
  s = bc_lex_next(&p->l);
  if (s) return s;

  while (p->l.t.t != BC_LEX_RPAREN) {

    if (p->l.t.t != BC_LEX_NAME) return BC_STATUS_PARSE_BAD_FUNC;

    ++p->func->nparams;

    name = xstrdup(p->l.t.v.v);
    s = bc_lex_next(&p->l);
    if (s) goto err;

    var = p->l.t.t != BC_LEX_LBRACKET;

    if (!var) {

      s = bc_lex_next(&p->l);
      if (s) goto err;

      if (p->l.t.t != BC_LEX_RBRACKET) {
        s = BC_STATUS_PARSE_BAD_FUNC;
        goto err;
      }

      s = bc_lex_next(&p->l);
      if (s) goto err;
    }

    comma = p->l.t.t == BC_LEX_COMMA;
    if (comma) {
      s = bc_lex_next(&p->l);
      if (s) goto err;
    }

    s = bc_func_insert(p->func, name, var);
    if (s) goto err;
  }

  if (comma) return BC_STATUS_PARSE_BAD_FUNC;

  flags = BC_PARSE_FLAG_FUNC | BC_PARSE_FLAG_FUNC_INNER | BC_PARSE_FLAG_BODY;
  bc_parse_startBody(p, flags);

  s = bc_lex_next(&p->l);
  if (s) return s;

  if (p->l.t.t != BC_LEX_LBRACE)
    s = bc_vm_posixError(BC_STATUS_POSIX_BRACE, p->l.f, p->l.line, NULL);

  return s;

err:
  free(name);
  return s;
}

BcStatus bc_parse_auto(BcParse *p) {

  BcStatus s;
  int comma, var, one;
  char *name;

  if (!p->auto_part) return BC_STATUS_PARSE_BAD_TOKEN;
  s = bc_lex_next(&p->l);
  if (s) return s;

  p->auto_part = comma = 0;
  one = p->l.t.t == BC_LEX_NAME;

  while (p->l.t.t == BC_LEX_NAME) {

    name = xstrdup(p->l.t.v.v);
    s = bc_lex_next(&p->l);
    if (s) goto err;

    var = p->l.t.t != BC_LEX_LBRACKET;
    if (!var) {

      s = bc_lex_next(&p->l);
      if (s) goto err;

      if (p->l.t.t != BC_LEX_RBRACKET) {
        s = BC_STATUS_PARSE_BAD_FUNC;
        goto err;
      }

      s = bc_lex_next(&p->l);
      if (s) goto err;
    }

    comma = p->l.t.t == BC_LEX_COMMA;
    if (comma) {
      s = bc_lex_next(&p->l);
      if (s) goto err;
    }

    s = bc_func_insert(p->func, name, var);
    if (s) goto err;
  }

  if (comma) return BC_STATUS_PARSE_BAD_FUNC;
  if (!one) return BC_STATUS_PARSE_NO_AUTO;

  if (p->l.t.t != BC_LEX_NLINE && p->l.t.t != BC_LEX_SCOLON)
    return BC_STATUS_PARSE_BAD_TOKEN;

  return bc_lex_next(&p->l);

err:
  free(name);
  return s;
}

BcStatus bc_parse_body(BcParse *p, int brace) {

  BcStatus s = BC_STATUS_SUCCESS;
  uint8_t *flag_ptr = bc_vec_top(&p->flags);

  *flag_ptr &= ~(BC_PARSE_FLAG_BODY);

  if (*flag_ptr & BC_PARSE_FLAG_FUNC_INNER) {

    if (!brace) return BC_STATUS_PARSE_BAD_TOKEN;
    p->auto_part = p->l.t.t != BC_LEX_KEY_AUTO;

    if (!p->auto_part) {
      s = bc_parse_auto(p);
      if (s) return s;
    }

    if (p->l.t.t == BC_LEX_NLINE) s = bc_lex_next(&p->l);
  }
  else {
    s = bc_parse_stmt(p);
    if (!s && !brace) s = bc_parse_endBody(p, 0);
  }

  return s;
}

BcStatus bc_parse_stmt(BcParse *p) {

  BcStatus s = BC_STATUS_SUCCESS;

  switch (p->l.t.t) {

    case BC_LEX_NLINE:
    {
      return bc_lex_next(&p->l);
    }

    case BC_LEX_KEY_ELSE:
    {
      p->auto_part = 0;
      break;
    }

    case BC_LEX_LBRACE:
    {
      if (!BC_PARSE_BODY(p)) return BC_STATUS_PARSE_BAD_TOKEN;

      ++p->nbraces;
      s = bc_lex_next(&p->l);
      if (s) return s;

      return bc_parse_body(p, 1);
    }

    case BC_LEX_KEY_AUTO:
    {
      return bc_parse_auto(p);
    }

    default:
    {
      p->auto_part = 0;

      if (BC_PARSE_IF_END(p)) {
        bc_parse_noElse(p);
        return BC_STATUS_SUCCESS;
      }
      else if (BC_PARSE_BODY(p)) return bc_parse_body(p, 0);

      break;
    }
  }

  switch (p->l.t.t) {

    case BC_LEX_OP_INC:
    case BC_LEX_OP_DEC:
    case BC_LEX_OP_MINUS:
    case BC_LEX_OP_BOOL_NOT:
    case BC_LEX_LPAREN:
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
      s = bc_parse_expr(p, BC_PARSE_PRINT, bc_parse_next_expr);
      break;
    }

    case BC_LEX_KEY_ELSE:
    {
      s = bc_parse_else(p);
      break;
    }

    case BC_LEX_SCOLON:
    {
      while (!s && p->l.t.t == BC_LEX_SCOLON) s = bc_lex_next(&p->l);
      break;
    }

    case BC_LEX_RBRACE:
    {
      s = bc_parse_endBody(p, 1);
      break;
    }

    case BC_LEX_STR:
    {
      s = bc_parse_string(p, BC_INST_PRINT_STR);
      break;
    }

    case BC_LEX_KEY_BREAK:
    case BC_LEX_KEY_CONTINUE:
    {
      s = bc_parse_loopExit(p, p->l.t.t);
      break;
    }

    case BC_LEX_KEY_FOR:
    {
      s = bc_parse_for(p);
      break;
    }

    case BC_LEX_KEY_HALT:
    {
      bc_parse_push(p, BC_INST_HALT);
      s = bc_lex_next(&p->l);
      break;
    }

    case BC_LEX_KEY_IF:
    {
      s = bc_parse_if(p);
      break;
    }

    case BC_LEX_KEY_LIMITS:
    {
      s = bc_lex_next(&p->l);
      if (s) return s;
      s = BC_STATUS_LIMITS;
      break;
    }

    case BC_LEX_KEY_PRINT:
    {
      s = bc_parse_print(p);
      break;
    }

    case BC_LEX_KEY_QUIT:
    {
      // Quit is a compile-time command. We don't exit directly,
      // so the vm can clean up. Limits do the same thing.
      s = BC_STATUS_QUIT;
      break;
    }

    case BC_LEX_KEY_RETURN:
    {
      s = bc_parse_return(p);
      break;
    }

    case BC_LEX_KEY_WHILE:
    {
      s = bc_parse_while(p);
      break;
    }

    default:
    {
      s = BC_STATUS_PARSE_BAD_TOKEN;
      break;
    }
  }

  return s;
}

BcStatus bc_parse_parse(BcParse *p) {

  BcStatus s;

  if (p->l.t.t == BC_LEX_EOF)
    s = p->flags.len > 0 ? BC_STATUS_PARSE_NO_BLOCK_END : BC_STATUS_LEX_EOF;
  else if (p->l.t.t == BC_LEX_KEY_DEFINE) {
    if (!BC_PARSE_CAN_EXEC(p)) return BC_STATUS_PARSE_BAD_TOKEN;
    s = bc_parse_func(p);
  }
  else s = bc_parse_stmt(p);

  if ((s && s != BC_STATUS_QUIT && s != BC_STATUS_LIMITS) || TT.signe)
    s = bc_parse_reset(p, s);

  return s;
}

BcStatus bc_parse_expr(BcParse *p, uint8_t flags, BcParseNext next) {

  BcStatus s = BC_STATUS_SUCCESS;
  BcInst prev = BC_INST_PRINT;
  BcLexType top, t = p->l.t.t;
  size_t nexprs = 0, ops_bgn = p->ops.len;
  uint32_t i, nparens, nrelops;
  int paren_first, paren_expr, rprn, done, get_token, assign, bin_last;

  paren_first = p->l.t.t == BC_LEX_LPAREN;
  nparens = nrelops = 0;
  paren_expr = rprn = done = get_token = assign = 0;
  bin_last = 1;

  for (; !TT.signe && !s && !done && bc_parse_exprs[t]; t = p->l.t.t)
  {
    switch (t) {

      case BC_LEX_OP_INC:
      case BC_LEX_OP_DEC:
      {
        s = bc_parse_incdec(p, &prev, &paren_expr, &nexprs, flags);
        rprn = get_token = bin_last = 0;
        break;
      }

      case BC_LEX_OP_MINUS:
      {
        s = bc_parse_minus(p, &prev, ops_bgn, rprn, &nexprs);
        rprn = get_token = 0;
        bin_last = prev == BC_INST_MINUS;
        break;
      }

      case BC_LEX_OP_ASSIGN_POWER:
      case BC_LEX_OP_ASSIGN_MULTIPLY:
      case BC_LEX_OP_ASSIGN_DIVIDE:
      case BC_LEX_OP_ASSIGN_MODULUS:
      case BC_LEX_OP_ASSIGN_PLUS:
      case BC_LEX_OP_ASSIGN_MINUS:
      case BC_LEX_OP_ASSIGN:
      {
        if (prev != BC_INST_VAR && prev != BC_INST_ARRAY_ELEM &&
            prev != BC_INST_SCALE && prev != BC_INST_IBASE &&
            prev != BC_INST_OBASE && prev != BC_INST_LAST)
        {
          s = BC_STATUS_PARSE_BAD_ASSIGN;
          break;
        }
      }
      // Fallthrough.
      case BC_LEX_OP_POWER:
      case BC_LEX_OP_MULTIPLY:
      case BC_LEX_OP_DIVIDE:
      case BC_LEX_OP_MODULUS:
      case BC_LEX_OP_PLUS:
      case BC_LEX_OP_REL_EQ:
      case BC_LEX_OP_REL_LE:
      case BC_LEX_OP_REL_GE:
      case BC_LEX_OP_REL_NE:
      case BC_LEX_OP_REL_LT:
      case BC_LEX_OP_REL_GT:
      case BC_LEX_OP_BOOL_NOT:
      case BC_LEX_OP_BOOL_OR:
      case BC_LEX_OP_BOOL_AND:
      {
        if (((t == BC_LEX_OP_BOOL_NOT) != bin_last) ||
            (t != BC_LEX_OP_BOOL_NOT && prev == BC_INST_BOOL_NOT))
        {
          return BC_STATUS_PARSE_BAD_EXP;
        }

        nrelops += t >= BC_LEX_OP_REL_EQ && t <= BC_LEX_OP_REL_GT;
        prev = BC_PARSE_TOKEN_INST(t);
        s = bc_parse_operator(p, t, ops_bgn, &nexprs, 1);
        rprn = get_token = 0;
        bin_last = t != BC_LEX_OP_BOOL_NOT;

        break;
      }

      case BC_LEX_LPAREN:
      {
        if (BC_PARSE_LEAF(prev, rprn)) return BC_STATUS_PARSE_BAD_EXP;

        ++nparens;
        paren_expr = rprn = bin_last = 0;
        get_token = 1;
        bc_vec_push(&p->ops, &t);

        break;
      }

      case BC_LEX_RPAREN:
      {
        if (bin_last || prev == BC_INST_BOOL_NOT)
          return BC_STATUS_PARSE_BAD_EXP;

        if (nparens == 0) {
          s = BC_STATUS_SUCCESS;
          done = 1;
          get_token = 0;
          break;
        }
        else if (!paren_expr) return BC_STATUS_PARSE_EMPTY_EXP;

        --nparens;
        paren_expr = rprn = 1;
        get_token = bin_last = 0;

        s = bc_parse_rightParen(p, ops_bgn, &nexprs);

        break;
      }

      case BC_LEX_NAME:
      {
        if (BC_PARSE_LEAF(prev, rprn)) return BC_STATUS_PARSE_BAD_EXP;

        paren_expr = 1;
        rprn = get_token = bin_last = 0;
        s = bc_parse_name(p, &prev, flags & ~BC_PARSE_NOCALL);
        ++nexprs;

        break;
      }

      case BC_LEX_NUMBER:
      {
        if (BC_PARSE_LEAF(prev, rprn)) return BC_STATUS_PARSE_BAD_EXP;

        bc_parse_number(p, &prev, &nexprs);
        paren_expr = get_token = 1;
        rprn = bin_last = 0;

        break;
      }

      case BC_LEX_KEY_IBASE:
      case BC_LEX_KEY_LAST:
      case BC_LEX_KEY_OBASE:
      {
        if (BC_PARSE_LEAF(prev, rprn)) return BC_STATUS_PARSE_BAD_EXP;

        prev = (char) (t - BC_LEX_KEY_IBASE + BC_INST_IBASE);
        bc_parse_push(p, (char) prev);

        paren_expr = get_token = 1;
        rprn = bin_last = 0;
        ++nexprs;

        break;
      }

      case BC_LEX_KEY_LENGTH:
      case BC_LEX_KEY_SQRT:
      {
        if (BC_PARSE_LEAF(prev, rprn)) return BC_STATUS_PARSE_BAD_EXP;

        s = bc_parse_builtin(p, t, flags, &prev);
        paren_expr = 1;
        rprn = get_token = bin_last = 0;
        ++nexprs;

        break;
      }

      case BC_LEX_KEY_READ:
      {
        if (BC_PARSE_LEAF(prev, rprn)) return BC_STATUS_PARSE_BAD_EXP;
        else if (flags & BC_PARSE_NOREAD) s = BC_STATUS_EXEC_REC_READ;
        else s = bc_parse_read(p);

        paren_expr = 1;
        rprn = get_token = bin_last = 0;
        ++nexprs;
        prev = BC_INST_READ;

        break;
      }

      case BC_LEX_KEY_SCALE:
      {
        if (BC_PARSE_LEAF(prev, rprn)) return BC_STATUS_PARSE_BAD_EXP;

        s = bc_parse_scale(p, &prev, flags);
        paren_expr = 1;
        rprn = get_token = bin_last = 0;
        ++nexprs;
        prev = BC_INST_SCALE;

        break;
      }

      default:
      {
        s = BC_STATUS_PARSE_BAD_TOKEN;
        break;
      }
    }

    if (!s && get_token) s = bc_lex_next(&p->l);
  }

  if (s) return s;
  if (TT.signe) return BC_STATUS_EXEC_SIGNAL;

  while (p->ops.len > ops_bgn) {

    top = BC_PARSE_TOP_OP(p);
    assign = top >= BC_LEX_OP_ASSIGN_POWER && top <= BC_LEX_OP_ASSIGN;

    if (top == BC_LEX_LPAREN || top == BC_LEX_RPAREN)
      return BC_STATUS_PARSE_BAD_EXP;

    bc_parse_push(p, BC_PARSE_TOKEN_INST(top));

    nexprs -= top != BC_LEX_OP_BOOL_NOT && top != BC_LEX_NEG;
    bc_vec_pop(&p->ops);
  }

  s = BC_STATUS_PARSE_BAD_EXP;
  if (prev == BC_INST_BOOL_NOT || nexprs != 1) return s;

  for (i = 0; s && i < next.len; ++i) s *= t != next.tokens[i];
  if (s) return s;

  if (!(flags & BC_PARSE_REL) && nrelops) {
    s = bc_vm_posixError(BC_STATUS_POSIX_REL_POS, p->l.f, p->l.line, NULL);
    if (s) return s;
  }
  else if ((flags & BC_PARSE_REL) && nrelops > 1) {
    s = bc_vm_posixError(BC_STATUS_POSIX_MULTIREL, p->l.f, p->l.line, NULL);
    if (s) return s;
  }

  if (flags & BC_PARSE_PRINT) {
    if (paren_first || !assign) bc_parse_push(p, BC_INST_PRINT);
    bc_parse_push(p, BC_INST_POP);
  }

  return s;
}

void bc_program_search(BcProgram *p, char *id, BcVec **ret, int var) {

  BcStatus s;
  BcId e, *ptr;
  BcVec *v, *map;
  size_t i;
  BcResultData data;
  int new;

  v = var ? &p->vars : &p->arrs;
  map = var ? &p->var_map : &p->arr_map;

  e.name = id;
  e.idx = v->len;
  s = bc_map_insert(map, &e, &i);
  new = s != BC_STATUS_VEC_ITEM_EXISTS;

  if (new) {
    bc_array_init(&data.v, var);
    bc_vec_push(v, &data.v);
  }

  ptr = bc_vec_item(map, i);
  if (new) ptr->name = xstrdup(e.name);
  *ret = bc_vec_item(v, ptr->idx);
}

BcStatus bc_program_num(BcProgram *p, BcResult *r, BcNum **num, int hex) {

  BcStatus s = BC_STATUS_SUCCESS;

  switch (r->t) {

    case BC_RESULT_STR:
    case BC_RESULT_TEMP:
    case BC_RESULT_IBASE:
    case BC_RESULT_SCALE:
    case BC_RESULT_OBASE:
    {
      *num = &r->d.n;
      break;
    }

    case BC_RESULT_CONSTANT:
    {
      char **str = bc_vec_item(&p->consts, r->d.id.idx);
      size_t base_t, len = strlen(*str);
      BcNum *base;

      bc_num_init(&r->d.n, len);

      hex = hex && len == 1;
      base = hex ? &p->hexb : &p->ib;
      base_t = hex ? BC_NUM_MAX_IBASE : p->ib_t;
      s = bc_num_parse(&r->d.n, *str, base, base_t);

      if (s) {
        bc_num_free(&r->d.n);
        return s;
      }

      *num = &r->d.n;
      r->t = BC_RESULT_TEMP;

      break;
    }

    case BC_RESULT_VAR:
    case BC_RESULT_ARRAY:
    case BC_RESULT_ARRAY_ELEM:
    {
      BcVec *v;

      bc_program_search(p, r->d.id.name, &v, r->t == BC_RESULT_VAR);

      if (r->t == BC_RESULT_ARRAY_ELEM) {
        v = bc_vec_top(v);
        if (v->len <= r->d.id.idx) bc_array_expand(v, r->d.id.idx + 1);
        *num = bc_vec_item(v, r->d.id.idx);
      }
      else *num = bc_vec_top(v);

      break;
    }

    case BC_RESULT_LAST:
    {
      *num = &p->last;
      break;
    }

    case BC_RESULT_ONE:
    {
      *num = &p->one;
      break;
    }
  }

  return s;
}

BcStatus bc_program_binOpPrep(BcProgram *p, BcResult **l, BcNum **ln,
                              BcResult **r, BcNum **rn, int assign)
{
  BcStatus s;
  int hex;
  BcResultType lt, rt;

  if (!BC_PROG_STACK(&p->results, 2)) return BC_STATUS_EXEC_STACK;

  *r = bc_vec_item_rev(&p->results, 0);
  *l = bc_vec_item_rev(&p->results, 1);

  lt = (*l)->t;
  rt = (*r)->t;
  hex = assign && (lt == BC_RESULT_IBASE || lt == BC_RESULT_OBASE);

  s = bc_program_num(p, *l, ln, 0);
  if (s) return s;
  s = bc_program_num(p, *r, rn, hex);
  if (s) return s;

  // We run this again under these conditions in case any vector has been
  // reallocated out from under the BcNums or arrays we had.
  if (lt == rt && (lt == BC_RESULT_VAR || lt == BC_RESULT_ARRAY_ELEM)) {
    s = bc_program_num(p, *l, ln, 0);
    if (s) return s;
  }

  if (!BC_PROG_NUM((*l), (*ln)) && (!assign || (*l)->t != BC_RESULT_VAR))
    return BC_STATUS_EXEC_BAD_TYPE;
  if (!assign && !BC_PROG_NUM((*r), (*ln))) return BC_STATUS_EXEC_BAD_TYPE;

  return s;
}

void bc_program_binOpRetire(BcProgram *p, BcResult *r) {
  r->t = BC_RESULT_TEMP;
  bc_vec_pop(&p->results);
  bc_vec_pop(&p->results);
  bc_vec_push(&p->results, r);
}

BcStatus bc_program_prep(BcProgram *p, BcResult **r, BcNum **n) {

  BcStatus s;

  if (!BC_PROG_STACK(&p->results, 1)) return BC_STATUS_EXEC_STACK;
  *r = bc_vec_top(&p->results);

  s = bc_program_num(p, *r, n, 0);
  if (s) return s;

  if (!BC_PROG_NUM((*r), (*n))) return BC_STATUS_EXEC_BAD_TYPE;

  return s;
}

void bc_program_retire(BcProgram *p, BcResult *r, BcResultType t) {
  r->t = t;
  bc_vec_pop(&p->results);
  bc_vec_push(&p->results, r);
}

BcStatus bc_program_op(BcProgram *p, char inst) {

  BcStatus s;
  BcResult *opd1, *opd2, res;
  BcNum *n1, *n2 = NULL;

  s = bc_program_binOpPrep(p, &opd1, &n1, &opd2, &n2, 0);
  if (s) return s;
  bc_num_init(&res.d.n, BC_NUM_DEF_SIZE);

  s = bc_program_ops[inst - BC_INST_POWER](n1, n2, &res.d.n, p->scale);
  if (s) goto err;
  bc_program_binOpRetire(p, &res);

  return s;

err:
  bc_num_free(&res.d.n);
  return s;
}

BcStatus bc_program_read(BcProgram *p) {

  BcStatus s;
  BcParse parse;
  BcVec buf;
  BcInstPtr ip;
  size_t i;
  BcFunc *f = bc_vec_item(&p->fns, BC_PROG_READ);

  for (i = 0; i < p->stack.len; ++i) {
    BcInstPtr *ip_ptr = bc_vec_item(&p->stack, i);
    if (ip_ptr->func == BC_PROG_READ) return BC_STATUS_EXEC_REC_READ;
  }

  bc_vec_npop(&f->code, f->code.len);
  bc_vec_init(&buf, sizeof(char), NULL);

  s = bc_read_line(&buf, "read> ");
  if (s) goto io_err;

  bc_parse_init(&parse, p, BC_PROG_READ);
  bc_lex_file(&parse.l, bc_program_stdin_name);

  s = bc_parse_text(&parse, buf.v);
  if (s) goto exec_err;
  s = bc_parse_expr(&parse, BC_PARSE_NOREAD, bc_parse_next_read);
  if (s) goto exec_err;

  if (parse.l.t.t != BC_LEX_NLINE && parse.l.t.t != BC_LEX_EOF) {
    s = BC_STATUS_EXEC_BAD_READ_EXPR;
    goto exec_err;
  }

  ip.func = BC_PROG_READ;
  ip.idx = 0;
  ip.len = p->results.len;

  // Update this pointer, just in case.
  f = bc_vec_item(&p->fns, BC_PROG_READ);

  bc_vec_pushByte(&f->code, BC_INST_POP_EXEC);
  bc_vec_push(&p->stack, &ip);

exec_err:
  bc_parse_free(&parse);
io_err:
  bc_vec_free(&buf);
  return s;
}

size_t bc_program_index(char *code, size_t *bgn) {

  char amt = code[(*bgn)++], i = 0;
  size_t res = 0;

  for (; i < amt; ++i, ++(*bgn))
    res |= (((size_t) ((int) code[*bgn]) & UCHAR_MAX) << (i * CHAR_BIT));

  return res;
}

char* bc_program_name(char *code, size_t *bgn) {

  size_t i;
  char c, *s, *str = code + *bgn, *ptr = strchr(str, BC_PARSE_STREND);

  s = xmalloc(ptr - str + 1);
  c = code[(*bgn)++];

  for (i = 0; c && c != BC_PARSE_STREND; c = code[(*bgn)++], ++i)
    s[i] = c;

  s[i] = '\0';

  return s;
}

void bc_program_printString(const char *str, size_t *nchars) {

  size_t i, len = strlen(str);

  for (i = 0; i < len; ++i, ++(*nchars)) {

    int c = str[i];

    if (c != '\\' || i == len - 1) bc_vm_putchar(c);
    else {

      c = str[++i];

      switch (c) {

        case 'a':
        {
          bc_vm_putchar('\a');
          break;
        }

        case 'b':
        {
          bc_vm_putchar('\b');
          break;
        }

        case '\\':
        case 'e':
        {
          bc_vm_putchar('\\');
          break;
        }

        case 'f':
        {
          bc_vm_putchar('\f');
          break;
        }

        case 'n':
        {
          bc_vm_putchar('\n');
          *nchars = SIZE_MAX;
          break;
        }

        case 'r':
        {
          bc_vm_putchar('\r');
          break;
        }

        case 'q':
        {
          bc_vm_putchar('"');
          break;
        }

        case 't':
        {
          bc_vm_putchar('\t');
          break;
        }

        default:
        {
          // Just print the backslash and following character.
          bc_vm_putchar('\\');
          ++(*nchars);
          bc_vm_putchar(c);
          break;
        }
      }
    }
  }
}

BcStatus bc_program_print(BcProgram *p, char inst, size_t idx) {

  BcStatus s = BC_STATUS_SUCCESS;
  BcResult *r;
  size_t len, i;
  char *str;
  BcNum *num = NULL;
  int pop = inst != BC_INST_PRINT;

  if (!BC_PROG_STACK(&p->results, idx + 1)) return BC_STATUS_EXEC_STACK;

  r = bc_vec_item_rev(&p->results, idx);
  s = bc_program_num(p, r, &num, 0);
  if (s) return s;

  if (BC_PROG_NUM(r, num)) {
    s = bc_num_print(num, &p->ob, p->ob_t, !pop, &p->nchars, p->len);
    if (!s) bc_num_copy(&p->last, num);
  }
  else {

    idx = (r->t == BC_RESULT_STR) ? r->d.id.idx : num->rdx;
    str = *((char**) bc_vec_item(&p->strs, idx));

    if (inst == BC_INST_PRINT_STR) {
      for (i = 0, len = strlen(str); i < len; ++i) {
        char c = str[i];
        bc_vm_putchar(c);
        if (c == '\n') p->nchars = SIZE_MAX;
        ++p->nchars;
      }
    }
    else {
      bc_program_printString(str, &p->nchars);
      if (inst == BC_INST_PRINT) bc_vm_putchar('\n');
    }
  }

  if (!s && pop) bc_vec_pop(&p->results);

  return s;
}

BcStatus bc_program_negate(BcProgram *p) {

  BcStatus s;
  BcResult res, *ptr;
  BcNum *num = NULL;

  s = bc_program_prep(p, &ptr, &num);
  if (s) return s;

  bc_num_init(&res.d.n, num->len);
  bc_num_copy(&res.d.n, num);
  if (res.d.n.len) res.d.n.neg = !res.d.n.neg;

  bc_program_retire(p, &res, BC_RESULT_TEMP);

  return s;
}

BcStatus bc_program_logical(BcProgram *p, char inst) {

  BcStatus s;
  BcResult *opd1, *opd2, res;
  BcNum *n1, *n2;
  int cond = 0;
  ssize_t cmp;

  s = bc_program_binOpPrep(p, &opd1, &n1, &opd2, &n2, 0);
  if (s) return s;
  bc_num_init(&res.d.n, BC_NUM_DEF_SIZE);

  if (inst == BC_INST_BOOL_AND)
    cond = bc_num_cmp(n1, &p->zero) && bc_num_cmp(n2, &p->zero);
  else if (inst == BC_INST_BOOL_OR)
    cond = bc_num_cmp(n1, &p->zero) || bc_num_cmp(n2, &p->zero);
  else {

    cmp = bc_num_cmp(n1, n2);

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
        cond = cmp;
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
    }
  }

  (cond ? bc_num_one : bc_num_zero)(&res.d.n);

  bc_program_binOpRetire(p, &res);

  return s;
}

BcStatus bc_program_copyToVar(BcProgram *p, char *name, int var) {

  BcStatus s;
  BcResult *ptr, r;
  BcVec *v;
  BcNum *n;

  if (!BC_PROG_STACK(&p->results, 1)) return BC_STATUS_EXEC_STACK;

  ptr = bc_vec_top(&p->results);
  if ((ptr->t == BC_RESULT_ARRAY) != !var) return BC_STATUS_EXEC_BAD_TYPE;
  bc_program_search(p, name, &v, var);

  s = bc_program_num(p, ptr, &n, 0);
  if (s) return s;

  // Do this once more to make sure that pointers were not invalidated.
  bc_program_search(p, name, &v, var);

  if (var) {
    bc_num_init(&r.d.n, BC_NUM_DEF_SIZE);
    bc_num_copy(&r.d.n, n);
  }
  else {
    bc_array_init(&r.d.v, 1);
    bc_array_copy(&r.d.v, (BcVec*) n);
  }

  bc_vec_push(v, &r.d);
  bc_vec_pop(&p->results);

  return s;
}

BcStatus bc_program_assign(BcProgram *p, char inst) {

  BcStatus s;
  BcResult *left, *right, res;
  BcNum *l = NULL, *r = NULL;
  unsigned long val, max;
  int assign = inst == BC_INST_ASSIGN, ib, sc;

  s = bc_program_binOpPrep(p, &left, &l, &right, &r, assign);
  if (s) return s;

  ib = left->t == BC_RESULT_IBASE;
  sc = left->t == BC_RESULT_SCALE;

  if (left->t == BC_RESULT_CONSTANT || left->t == BC_RESULT_TEMP)
    return BC_STATUS_PARSE_BAD_ASSIGN;

  if (inst == BC_INST_ASSIGN_DIVIDE && !bc_num_cmp(r, &p->zero))
    return BC_STATUS_MATH_DIVIDE_BY_ZERO;

  if (assign) bc_num_copy(l, r);
  else s = bc_program_ops[inst - BC_INST_ASSIGN_POWER](l, r, l, p->scale);

  if (s) return s;

  if (ib || sc || left->t == BC_RESULT_OBASE) {

    size_t *ptr;

    s = bc_num_ulong(l, &val);
    if (s) return s;
    s = left->t - BC_RESULT_IBASE + BC_STATUS_EXEC_BAD_IBASE;

    if (sc) {
      max = BC_MAX_SCALE;
      ptr = &p->scale;
    }
    else {
      if (val < BC_NUM_MIN_BASE) return s;
      max = ib ? BC_NUM_MAX_IBASE : BC_MAX_OBASE;
      ptr = ib ? &p->ib_t : &p->ob_t;
    }

    if (val > max) return s;
    if (!sc) bc_num_copy(ib ? &p->ib : &p->ob, l);

    *ptr = (size_t) val;
    s = BC_STATUS_SUCCESS;
  }

  bc_num_init(&res.d.n, l->len);
  bc_num_copy(&res.d.n, l);
  bc_program_binOpRetire(p, &res);

  return s;
}

BcStatus bc_program_pushVar(BcProgram *p, char *code, size_t *bgn,
                            int pop, int copy)
{
  BcStatus s = BC_STATUS_SUCCESS;
  BcResult r;
  char *name = bc_program_name(code, bgn);

  r.t = BC_RESULT_VAR;
  r.d.id.name = name;

  bc_vec_push(&p->results, &r);

  return s;
}

BcStatus bc_program_pushArray(BcProgram *p, char *code, size_t *bgn, char inst)
{
  BcStatus s = BC_STATUS_SUCCESS;
  BcResult r;
  BcNum *num;

  r.d.id.name = bc_program_name(code, bgn);

  if (inst == BC_INST_ARRAY) {
    r.t = BC_RESULT_ARRAY;
    bc_vec_push(&p->results, &r);
  }
  else {

    BcResult *operand;
    unsigned long temp;

    s = bc_program_prep(p, &operand, &num);
    if (s) goto err;
    s = bc_num_ulong(num, &temp);
    if (s) goto err;

    if (temp > BC_MAX_DIM) {
      s = BC_STATUS_EXEC_ARRAY_LEN;
      goto err;
    }

    r.d.id.idx = (size_t) temp;
    bc_program_retire(p, &r, BC_RESULT_ARRAY_ELEM);
  }

err:
  if (s) free(r.d.id.name);
  return s;
}

BcStatus bc_program_incdec(BcProgram *p, char inst) {

  BcStatus s;
  BcResult *ptr, res, copy;
  BcNum *num = NULL;
  char inst2 = inst;

  s = bc_program_prep(p, &ptr, &num);
  if (s) return s;

  if (inst == BC_INST_INC_POST || inst == BC_INST_DEC_POST) {
    copy.t = BC_RESULT_TEMP;
    bc_num_init(&copy.d.n, num->len);
    bc_num_copy(&copy.d.n, num);
  }

  res.t = BC_RESULT_ONE;
  inst = inst == BC_INST_INC_PRE || inst == BC_INST_INC_POST ?
             BC_INST_ASSIGN_PLUS : BC_INST_ASSIGN_MINUS;

  bc_vec_push(&p->results, &res);
  bc_program_assign(p, inst);

  if (inst2 == BC_INST_INC_POST || inst2 == BC_INST_DEC_POST) {
    bc_vec_pop(&p->results);
    bc_vec_push(&p->results, &copy);
  }

  return s;
}

BcStatus bc_program_call(BcProgram *p, char *code, size_t *idx) {

  BcStatus s = BC_STATUS_SUCCESS;
  BcInstPtr ip;
  size_t i, nparams = bc_program_index(code, idx);
  BcFunc *func;
  BcVec *v;
  BcId *a;
  BcResultData param;
  BcResult *arg;

  ip.idx = 0;
  ip.func = bc_program_index(code, idx);
  func = bc_vec_item(&p->fns, ip.func);

  if (func->code.len == 0) return BC_STATUS_EXEC_UNDEFINED_FUNC;
  if (nparams != func->nparams) return BC_STATUS_EXEC_MISMATCHED_PARAMS;
  ip.len = p->results.len - nparams;

  for (i = 0; i < nparams; ++i) {

    a = bc_vec_item(&func->autos, nparams - 1 - i);
    arg = bc_vec_top(&p->results);

    if ((!a->idx) != (arg->t == BC_RESULT_ARRAY) || arg->t == BC_RESULT_STR)
      return BC_STATUS_EXEC_BAD_TYPE;

    s = bc_program_copyToVar(p, a->name, a->idx);
    if (s) return s;
  }

  for (; i < func->autos.len; ++i) {

    a = bc_vec_item(&func->autos, i);
    bc_program_search(p, a->name, &v, a->idx);

    if (a->idx) {
      bc_num_init(&param.n, BC_NUM_DEF_SIZE);
      bc_vec_push(v, &param.n);
    }
    else {
      bc_array_init(&param.v, 1);
      bc_vec_push(v, &param.v);
    }
  }

  bc_vec_push(&p->stack, &ip);

  return BC_STATUS_SUCCESS;
}

BcStatus bc_program_return(BcProgram *p, char inst) {

  BcStatus s;
  BcResult res;
  BcFunc *f;
  size_t i;
  BcInstPtr *ip = bc_vec_top(&p->stack);

  if (!BC_PROG_STACK(&p->results, ip->len + inst == BC_INST_RET))
    return BC_STATUS_EXEC_STACK;

  f = bc_vec_item(&p->fns, ip->func);
  res.t = BC_RESULT_TEMP;

  if (inst == BC_INST_RET) {

    BcNum *num;
    BcResult *operand = bc_vec_top(&p->results);

    s = bc_program_num(p, operand, &num, 0);
    if (s) return s;
    bc_num_init(&res.d.n, num->len);
    bc_num_copy(&res.d.n, num);
  }
  else {
    bc_num_init(&res.d.n, BC_NUM_DEF_SIZE);
    bc_num_zero(&res.d.n);
  }

  // We need to pop arguments as well, so this takes that into account.
  for (i = 0; i < f->autos.len; ++i) {

    BcVec *v;
    BcId *a = bc_vec_item(&f->autos, i);

    bc_program_search(p, a->name, &v, a->idx);
    bc_vec_pop(v);
  }

  bc_vec_npop(&p->results, p->results.len - ip->len);
  bc_vec_push(&p->results, &res);
  bc_vec_pop(&p->stack);

  return BC_STATUS_SUCCESS;
}

unsigned long bc_program_scale(BcNum *n) {
  return (unsigned long) n->rdx;
}

unsigned long bc_program_len(BcNum *n) {

  unsigned long len = n->len;
  size_t i;

  if (n->rdx != n->len) return len;
  for (i = n->len - 1; i < n->len && n->num[i] == 0; --len, --i);

  return len;
}

BcStatus bc_program_builtin(BcProgram *p, char inst) {

  BcStatus s;
  BcResult *opnd;
  BcNum *num = NULL;
  BcResult res;
  int len = inst == BC_INST_LENGTH;

  if (!BC_PROG_STACK(&p->results, 1)) return BC_STATUS_EXEC_STACK;
  opnd = bc_vec_top(&p->results);

  s = bc_program_num(p, opnd, &num, 0);
  if (s) return s;

  bc_num_init(&res.d.n, BC_NUM_DEF_SIZE);

  if (inst == BC_INST_SQRT) s = bc_num_sqrt(num, &res.d.n, p->scale);
  else if (len && opnd->t == BC_RESULT_ARRAY) {
    s = bc_num_ulong2num(&res.d.n, (unsigned long) ((BcVec*) num)->len);
  }
  else {
    BcProgramBuiltIn f = len ? bc_program_len : bc_program_scale;
    s = bc_num_ulong2num(&res.d.n, f(num));
    if (s) goto err;
  }

  bc_program_retire(p, &res, BC_RESULT_TEMP);

  return s;

err:
  bc_num_free(&res.d.n);
  return s;
}

BcStatus bc_program_pushGlobal(BcProgram *p, char inst) {

  BcStatus s;
  BcResult res;
  unsigned long val;

  res.t = inst - BC_INST_IBASE + BC_RESULT_IBASE;
  if (inst == BC_INST_IBASE) val = (unsigned long) p->ib_t;
  else if (inst == BC_INST_SCALE) val = (unsigned long) p->scale;
  else val = (unsigned long) p->ob_t;

  bc_num_init(&res.d.n, BC_NUM_DEF_SIZE);
  s = bc_num_ulong2num(&res.d.n, val);
  if (s) goto err;
  bc_vec_push(&p->results, &res);

  return s;

err:
  bc_num_free(&res.d.n);
  return s;
}

void bc_program_free(BcProgram *p) {
  bc_num_free(&p->ib);
  bc_num_free(&p->ob);
  bc_num_free(&p->hexb);
  bc_vec_free(&p->fns);
  bc_vec_free(&p->fn_map);
  bc_vec_free(&p->vars);
  bc_vec_free(&p->var_map);
  bc_vec_free(&p->arrs);
  bc_vec_free(&p->arr_map);
  bc_vec_free(&p->strs);
  bc_vec_free(&p->consts);
  bc_vec_free(&p->results);
  bc_vec_free(&p->stack);
  bc_num_free(&p->last);
  bc_num_free(&p->zero);
  bc_num_free(&p->one);
}

void bc_program_init(BcProgram *p, size_t line_len) {

  size_t idx;
  BcInstPtr ip;

  memset(p, 0, sizeof(BcProgram));
  memset(&ip, 0, sizeof(BcInstPtr));

  p->nchars = p->scale = 0;
  p->len = line_len;

  bc_num_init(&p->ib, BC_NUM_DEF_SIZE);
  bc_num_ten(&p->ib);
  p->ib_t = 10;

  bc_num_init(&p->ob, BC_NUM_DEF_SIZE);
  bc_num_ten(&p->ob);
  p->ob_t = 10;

  bc_num_init(&p->hexb, BC_NUM_DEF_SIZE);
  bc_num_ten(&p->hexb);
  p->hexb.num[0] = 6;

  bc_num_init(&p->last, BC_NUM_DEF_SIZE);
  bc_num_zero(&p->last);

  bc_num_init(&p->zero, BC_NUM_DEF_SIZE);
  bc_num_zero(&p->zero);

  bc_num_init(&p->one, BC_NUM_DEF_SIZE);
  bc_num_one(&p->one);

  bc_vec_init(&p->fns, sizeof(BcFunc), bc_func_free);
  bc_map_init(&p->fn_map);

  bc_program_addFunc(p, xstrdup(bc_func_main), &idx);
  bc_program_addFunc(p, xstrdup(bc_func_read), &idx);

  bc_vec_init(&p->vars, sizeof(BcVec), bc_vec_free);
  bc_map_init(&p->var_map);

  bc_vec_init(&p->arrs, sizeof(BcVec), bc_vec_free);
  bc_map_init(&p->arr_map);

  bc_vec_init(&p->strs, sizeof(char*), bc_string_free);
  bc_vec_init(&p->consts, sizeof(char*), bc_string_free);
  bc_vec_init(&p->results, sizeof(BcResult), bc_result_free);
  bc_vec_init(&p->stack, sizeof(BcInstPtr), NULL);
  bc_vec_push(&p->stack, &ip);
}

void bc_program_addFunc(BcProgram *p, char *name, size_t *idx) {

  BcStatus s;
  BcId entry, *entry_ptr;
  BcFunc f;

  entry.name = name;
  entry.idx = p->fns.len;

  s = bc_map_insert(&p->fn_map, &entry, idx);
  if (s) free(name);

  entry_ptr = bc_vec_item(&p->fn_map, *idx);
  *idx = entry_ptr->idx;

  if (s == BC_STATUS_VEC_ITEM_EXISTS) {

    BcFunc *func = bc_vec_item(&p->fns, entry_ptr->idx);

    // We need to reset these, so the function can be repopulated.
    func->nparams = 0;
    bc_vec_npop(&func->autos, func->autos.len);
    bc_vec_npop(&func->code, func->code.len);
    bc_vec_npop(&func->labels, func->labels.len);
  }
  else {
    bc_func_init(&f);
    bc_vec_push(&p->fns, &f);
  }
}

BcStatus bc_program_reset(BcProgram *p, BcStatus s) {

  BcFunc *f;
  BcInstPtr *ip;

  bc_vec_npop(&p->stack, p->stack.len - 1);
  bc_vec_npop(&p->results, p->results.len);

  f = bc_vec_item(&p->fns, 0);
  ip = bc_vec_top(&p->stack);
  ip->idx = f->code.len;

  if (!s && TT.signe && !TT.tty) return BC_STATUS_QUIT;

  TT.sigc += TT.signe;
  TT.signe = TT.sig != TT.sigc;

  if (!s || s == BC_STATUS_EXEC_SIGNAL) {
    if (TT.ttyin) {
      bc_vm_puts(bc_program_ready_msg, stderr);
      bc_vm_fflush(stderr);
      s = BC_STATUS_SUCCESS;
    }
    else s = BC_STATUS_QUIT;
  }

  return s;
}

BcStatus bc_program_exec(BcProgram *p) {

  BcStatus s = BC_STATUS_SUCCESS;
  size_t idx;
  BcResult r, *ptr;
  BcNum *num;
  BcInstPtr *ip = bc_vec_top(&p->stack);
  BcFunc *func = bc_vec_item(&p->fns, ip->func);
  char *code = func->code.v;
  int cond = 0;

  while (!s && ip->idx < func->code.len) {

    char inst = code[(ip->idx)++];

    switch (inst) {

      case BC_INST_JUMP_ZERO:
      {
        s = bc_program_prep(p, &ptr, &num);
        if (s) return s;
        cond = !bc_num_cmp(num, &p->zero);
        bc_vec_pop(&p->results);
      }
      // Fallthrough.
      case BC_INST_JUMP:
      {
        size_t *addr;
        idx = bc_program_index(code, &ip->idx);
        addr = bc_vec_item(&func->labels, idx);
        if (inst == BC_INST_JUMP || cond) ip->idx = *addr;
        break;
      }

      case BC_INST_CALL:
      {
        s = bc_program_call(p, code, &ip->idx);
        break;
      }

      case BC_INST_INC_PRE:
      case BC_INST_DEC_PRE:
      case BC_INST_INC_POST:
      case BC_INST_DEC_POST:
      {
        s = bc_program_incdec(p, inst);
        break;
      }

      case BC_INST_HALT:
      {
        s = BC_STATUS_QUIT;
        break;
      }

      case BC_INST_RET:
      case BC_INST_RET0:
      {
        s = bc_program_return(p, inst);
        break;
      }

      case BC_INST_BOOL_OR:
      case BC_INST_BOOL_AND:
      case BC_INST_REL_EQ:
      case BC_INST_REL_LE:
      case BC_INST_REL_GE:
      case BC_INST_REL_NE:
      case BC_INST_REL_LT:
      case BC_INST_REL_GT:
      {
        s = bc_program_logical(p, inst);
        break;
      }

      case BC_INST_READ:
      {
        s = bc_program_read(p);
        break;
      }

      case BC_INST_VAR:
      {
        s = bc_program_pushVar(p, code, &ip->idx, 0, 0);
        break;
      }

      case BC_INST_ARRAY_ELEM:
      case BC_INST_ARRAY:
      {
        s = bc_program_pushArray(p, code, &ip->idx, inst);
        break;
      }

      case BC_INST_LAST:
      {
        r.t = BC_RESULT_LAST;
        bc_vec_push(&p->results, &r);
        break;
      }

      case BC_INST_IBASE:
      case BC_INST_SCALE:
      case BC_INST_OBASE:
      {
        s = bc_program_pushGlobal(p, inst);
        break;
      }

      case BC_INST_SCALE_FUNC:
      case BC_INST_LENGTH:
      case BC_INST_SQRT:
      {
        s = bc_program_builtin(p, inst);
        break;
      }

      case BC_INST_NUM:
      {
        r.t = BC_RESULT_CONSTANT;
        r.d.id.idx = bc_program_index(code, &ip->idx);
        bc_vec_push(&p->results, &r);
        break;
      }

      case BC_INST_POP:
      {
        if (!BC_PROG_STACK(&p->results, 1)) s = BC_STATUS_EXEC_STACK;
        else bc_vec_pop(&p->results);
        break;
      }

      case BC_INST_POP_EXEC:
      {
        bc_vec_pop(&p->stack);
        break;
      }

      case BC_INST_PRINT:
      case BC_INST_PRINT_POP:
      case BC_INST_PRINT_STR:
      {
        s = bc_program_print(p, inst, 0);
        break;
      }

      case BC_INST_STR:
      {
        r.t = BC_RESULT_STR;
        r.d.id.idx = bc_program_index(code, &ip->idx);
        bc_vec_push(&p->results, &r);
        break;
      }

      case BC_INST_POWER:
      case BC_INST_MULTIPLY:
      case BC_INST_DIVIDE:
      case BC_INST_MODULUS:
      case BC_INST_PLUS:
      case BC_INST_MINUS:
      {
        s = bc_program_op(p, inst);
        break;
      }

      case BC_INST_BOOL_NOT:
      {
        s = bc_program_prep(p, &ptr, &num);
        if (s) return s;

        bc_num_init(&r.d.n, BC_NUM_DEF_SIZE);
        (!bc_num_cmp(num, &p->zero) ? bc_num_one : bc_num_zero)(&r.d.n);
        bc_program_retire(p, &r, BC_RESULT_TEMP);

        break;
      }

      case BC_INST_NEG:
      {
        s = bc_program_negate(p);
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
        s = bc_program_assign(p, inst);
        break;
      }
    }

    if ((s && s != BC_STATUS_QUIT) || TT.signe) s = bc_program_reset(p, s);

    // If the stack has changed, pointers may be invalid.
    ip = bc_vec_top(&p->stack);
    func = bc_vec_item(&p->fns, ip->func);
    code = func->code.v;
  }

  return s;
}

void bc_vm_sig(int sig) {
  int err = errno;
  size_t len = strlen(bc_sig_msg);
  if (sig == SIGINT && write(2, bc_sig_msg, len) == (ssize_t) len) {
    TT.signe = TT.sig == TT.sigc;
    TT.sig += TT.signe;
  }
  errno = err;
}

void bc_vm_info(void) {
  bc_vm_printf(stdout, "%s %s\n", toys.which->name, "1.1");
  bc_vm_puts(bc_copyright, stdout);
}

BcStatus bc_vm_error(BcStatus s, const char *file, size_t line) {

  if (!s || s > BC_STATUS_VEC_ITEM_EXISTS) return s;

  bc_vm_printf(stderr, bc_err_fmt, bc_errs[bc_err_ids[s]], bc_err_msgs[s]);
  bc_vm_printf(stderr, "    %s", file);
  bc_vm_printf(stderr, bc_err_line + 4 * !line, line);

  return s * (!TT.ttyin || !!strcmp(file, bc_program_stdin_name));
}

BcStatus bc_vm_posixError(BcStatus s, const char *file,
                          size_t line, const char *msg)
{
  int p = (int) (toys.optflags & FLAG_s), w = (int) (toys.optflags & FLAG_w);
  const char* const fmt = p ? bc_err_fmt : bc_warn_fmt;

  if (!(p || w) || s < BC_STATUS_POSIX_NAME_LEN) return BC_STATUS_SUCCESS;

  bc_vm_printf(stderr, fmt, bc_errs[bc_err_ids[s]], bc_err_msgs[s]);
  if (msg) bc_vm_printf(stderr, "    %s\n", msg);
  bc_vm_printf(stderr, "    %s", file);
  bc_vm_printf(stderr, bc_err_line + 4 * !line, line);

  return s * (!TT.ttyin && !!p);
}

size_t bc_vm_envLen(const char *var) {

  char *lenv = getenv(var);
  size_t i, len = BC_NUM_PRINT_WIDTH;
  int num;

  if (!lenv) return len;

  len = strlen(lenv);

  for (num = 1, i = 0; num && i < len; ++i) num = isdigit(lenv[i]);
  if (num) {
    len = (size_t) atoi(lenv) - 1;
    if (len < 2 || len >= INT32_MAX) len = BC_NUM_PRINT_WIDTH;
  }
  else len = BC_NUM_PRINT_WIDTH;

  return len;
}

void bc_vm_exit(BcStatus s) {
  bc_vm_printf(stderr, bc_err_fmt, bc_errs[bc_err_ids[s]], bc_err_msgs[s]);
  exit((int) s);
}

void bc_vm_printf(FILE *restrict f, const char *fmt, ...) {

  va_list args;
  int bad;

  va_start(args, fmt);
  bad = vfprintf(f, fmt, args) < 0;
  va_end(args);

  if (bad) bc_vm_exit(BC_STATUS_IO_ERR);
}

void bc_vm_puts(const char *str, FILE *restrict f) {
  if (fputs(str, f) == EOF) bc_vm_exit(BC_STATUS_IO_ERR);
}

void bc_vm_putchar(int c) {
  if (putchar(c) == EOF) bc_vm_exit(BC_STATUS_IO_ERR);
}

void bc_vm_fflush(FILE *restrict f) {
  if (fflush(f) == EOF) bc_vm_exit(BC_STATUS_IO_ERR);
}

BcStatus bc_vm_process(BcVm *vm, const char *text) {

  BcStatus s = bc_parse_text(&vm->prs, text);

  s = bc_vm_error(s, vm->prs.l.f, vm->prs.l.line);
  if (s) return s;

  while (vm->prs.l.t.t != BC_LEX_EOF) {

    s = bc_parse_parse(&vm->prs);

    if (s == BC_STATUS_LIMITS) {

      bc_vm_putchar('\n');
      bc_vm_printf(stdout, "BC_BASE_MAX     = %lu\n", BC_MAX_OBASE);
      bc_vm_printf(stdout, "BC_DIM_MAX      = %lu\n", BC_MAX_DIM);
      bc_vm_printf(stdout, "BC_SCALE_MAX    = %lu\n", BC_MAX_SCALE);
      bc_vm_printf(stdout, "BC_STRING_MAX   = %lu\n", BC_MAX_STRING);
      bc_vm_printf(stdout, "BC_NAME_MAX     = %lu\n", BC_MAX_NAME);
      bc_vm_printf(stdout, "BC_NUM_MAX      = %lu\n", BC_MAX_NUM);
      bc_vm_printf(stdout, "Max Exponent    = %lu\n", BC_MAX_EXP);
      bc_vm_printf(stdout, "Number of Vars  = %lu\n", BC_MAX_VARS);
      bc_vm_putchar('\n');

      s = BC_STATUS_SUCCESS;
    }
    else {
      if (s == BC_STATUS_QUIT) return s;
      s = bc_vm_error(s, vm->prs.l.f, vm->prs.l.line);
      if (s) return s;
    }
  }

  if (BC_PARSE_CAN_EXEC(&vm->prs)) {
    s = bc_program_exec(&vm->prog);
    if (!s && TT.tty) bc_vm_fflush(stdout);
    if (s && s != BC_STATUS_QUIT)
      s = bc_vm_error(bc_program_reset(&vm->prog, s), vm->prs.l.f, 0);
  }

  return s;
}

BcStatus bc_vm_file(BcVm *vm, const char *file) {

  BcStatus s;
  char *data;
  BcFunc *main_func;
  BcInstPtr *ip;

  vm->prog.file = file;
  s = bc_read_file(file, &data);
  if (s) return bc_vm_error(s, file, 0);

  bc_lex_file(&vm->prs.l, file);
  s = bc_vm_process(vm, data);
  if (s) goto err;

  main_func = bc_vec_item(&vm->prog.fns, BC_PROG_MAIN);
  ip = bc_vec_item(&vm->prog.stack, 0);

  if (main_func->code.len < ip->idx)
    s = BC_STATUS_EXEC_FILE_NOT_EXECUTABLE;

err:
  free(data);
  return s;
}

BcStatus bc_vm_stdin(BcVm *vm) {

  BcStatus s = BC_STATUS_SUCCESS;
  BcVec buf, buffer;
  char c;
  size_t len, i, str = 0;
  int comment = 0, notend;

  vm->prog.file = bc_program_stdin_name;
  bc_lex_file(&vm->prs.l, bc_program_stdin_name);

  bc_vec_init(&buffer, sizeof(char), NULL);
  bc_vec_init(&buf, sizeof(char), NULL);
  bc_vec_pushByte(&buffer, '\0');

  // This loop is complex because the vm tries not to send any lines that end
  // with a backslash to the parser. The reason for that is because the parser
  // treats a backslash+newline combo as whitespace, per the bc spec. In that
  // case, and for strings and comments, the parser will expect more stuff.
  for (s = bc_read_line(&buf, ">>> "); !s; s = bc_read_line(&buf, ">>> ")) {

    char *string = buf.v;

    len = buf.len - 1;

    if (len == 1) {
      if (str && buf.v[0] == '"') str -= 1;
      else if (buf.v[0] == '"') str += 1;
    }
    else if (len > 1 || comment) {

      for (i = 0; i < len; ++i) {

        notend = len > i + 1;
        c = string[i];

        if ((i - 1 > len || string[i - 1] != '\\') && c == '"') str = !str;

        if (c == '/' && notend && !comment && string[i + 1] == '*') {
          comment = 1;
          break;
        }
        else if (c == '*' && notend && comment && string[i + 1] == '/')
          comment = 0;
      }

      if (str || comment || string[len - 2] == '\\') {
        bc_vec_concat(&buffer, buf.v);
        continue;
      }
    }

    bc_vec_concat(&buffer, buf.v);
    s = bc_vm_process(vm, buffer.v);
    if (s) goto err;

    bc_vec_npop(&buffer, buffer.len);
  }

  if (s == BC_STATUS_BIN_FILE) s = bc_vm_error(s, vm->prs.l.f, 0);

  // I/O error will always happen when stdin is
  // closed. It's not a problem in that case.
  s = s == BC_STATUS_IO_ERR || s == BC_STATUS_QUIT ? BC_STATUS_SUCCESS : s;

  if (str) s = bc_vm_error(BC_STATUS_LEX_NO_STRING_END,
                           vm->prs.l.f, vm->prs.l.line);
  else if (comment) s = bc_vm_error(BC_STATUS_LEX_NO_COMMENT_END,
                                    vm->prs.l.f, vm->prs.l.line);

err:
  bc_vec_free(&buf);
  bc_vec_free(&buffer);
  return s;
}

BcStatus bc_vm_exec(BcVm *vm) {

  BcStatus s = BC_STATUS_SUCCESS;
  size_t i;

  if (toys.optflags & FLAG_l) {

    bc_lex_file(&vm->prs.l, bc_lib_name);
    s = bc_parse_text(&vm->prs, bc_lib);

    while (!s && vm->prs.l.t.t != BC_LEX_EOF) s = bc_parse_parse(&vm->prs);

    if (s) return s;
    s = bc_program_exec(&vm->prog);
    if (s) return s;
  }

  for (i = 0; !s && i < toys.optc; ++i)
    s = bc_vm_file(vm, toys.optargs[i]);
  if (s && s != BC_STATUS_QUIT) return s;

  s = bc_vm_stdin(vm);
  if (!s && !BC_PARSE_CAN_EXEC(&vm->prs)) s = bc_vm_process(vm, "");

  return s == BC_STATUS_QUIT ? BC_STATUS_SUCCESS : s;
}

void bc_vm_free(BcVm *vm) {
  bc_program_free(&vm->prog);
  bc_parse_free(&vm->prs);
}

BcStatus bc_vm_init(BcVm *vm) {

  BcStatus s = BC_STATUS_SUCCESS;
  size_t len = bc_vm_envLen("BC_LINE_LENGTH");
  struct sigaction sa;

  sigemptyset(&sa.sa_mask);
  sa.sa_handler = bc_vm_sig;
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);

  memset(vm, 0, sizeof(BcVm));

  toys.optflags |= FLAG_s * (getenv("POSIXLY_CORRECT") != NULL);

  bc_program_init(&vm->prog, len);
  bc_parse_init(&vm->prs, &vm->prog, BC_PROG_MAIN);

  return s;
}

void bc_main(void) {

  BcStatus st;
  BcVm vm;

  st = bc_vm_init(&vm);
  if (st) goto exit;

  TT.ttyin = isatty(0);
  TT.tty = TT.ttyin || (toys.optflags & FLAG_i) || isatty(1);

  if (TT.ttyin && !(toys.optflags & FLAG_q)) bc_vm_info();
  st = bc_vm_exec(&vm);

exit:
  if (CFG_TOYBOX_FREE) bc_vm_free(&vm);
  toys.exitval = (int) st;
}
