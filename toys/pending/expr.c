/* expr.c - evaluate expression
 *
 * Copyright 2013 Daniel Verkamp <daniel@drv.nu>
 *
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/expr.html

USE_EXPR(NEWTOY(expr, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config EXPR
  bool "expr"
  default n
  help
    usage: expr args

    Evaluate expression and print result.

    The supported operators, in order of increasing precedence, are:

    | & = > >= < <= != + - * / %

    In addition, parentheses () are supported for grouping.
*/

// TODO: int overflow checking

#define FOR_expr
#include "toys.h"


GLOBALS(
  int argidx;
)

// Scalar value.
// If s is NULL, the value is an integer (i).
// If s is not NULL, the value is a string (s).
struct value {
  char *s;
  long i;
};

static void parse_expr(struct value *ret, struct value *v);

static void get_value(struct value *v)
{
  char *endp, *arg;

  if (TT.argidx == toys.optc) {
    v->i = 0;
    v->s = ""; // signal end of expression
    return;
  }

  if (TT.argidx >= toys.optc) {
    error_exit("syntax error");
  }

  arg = toys.optargs[TT.argidx++];

  v->i = strtol(arg, &endp, 10);
  v->s = *endp ? arg : NULL;
}


// check if v matches a token, and consume it if so
static int match(struct value *v, const char *tok)
{
  if (v->s && !strcmp(v->s, tok)) {
    get_value(v);
    return 1;
  }

  return 0;
}

// check if v is the integer 0 or the empty string
static int is_zero(const struct value *v)
{
  return ((v->s && *v->s == '\0') || v->i == 0);
}

static char *num_to_str(long num)
{
  static char num_buf[21];
  snprintf(num_buf, sizeof(num_buf), "%ld", num);
  return num_buf;
}

static int cmp(const struct value *lhs, const struct value *rhs)
{
  if (lhs->s || rhs->s) {
    // at least one operand is a string
    char *ls = lhs->s ? lhs->s : num_to_str(lhs->i);
    char *rs = rhs->s ? rhs->s : num_to_str(rhs->i);
    return strcmp(ls, rs);
  } else {
    return lhs->i - rhs->i;
  }
}


// operators

struct op {
  const char *tok;

  // calculate "lhs op rhs" (e.g. lhs + rhs) and store result in lhs
  void (*calc)(struct value *lhs, const struct value *rhs);
};


static void re(struct value *lhs, const struct value *rhs)
{
  error_exit("regular expression match not implemented");
}

static void mod(struct value *lhs, const struct value *rhs)
{
  if (lhs->s || rhs->s) error_exit("non-integer argument");
  if (is_zero(rhs)) error_exit("division by zero");
  lhs->i %= rhs->i;
}

static void divi(struct value *lhs, const struct value *rhs)
{
  if (lhs->s || rhs->s) error_exit("non-integer argument");
  if (is_zero(rhs)) error_exit("division by zero");
  lhs->i /= rhs->i;
}

static void mul(struct value *lhs, const struct value *rhs)
{
  if (lhs->s || rhs->s) error_exit("non-integer argument");
  lhs->i *= rhs->i;
}

static void sub(struct value *lhs, const struct value *rhs)
{
  if (lhs->s || rhs->s) error_exit("non-integer argument");
  lhs->i -= rhs->i;
}

static void add(struct value *lhs, const struct value *rhs)
{
  if (lhs->s || rhs->s) error_exit("non-integer argument");
  lhs->i += rhs->i;
}

static void ne(struct value *lhs, const struct value *rhs)
{
  lhs->i = cmp(lhs, rhs) != 0;
  lhs->s = NULL;
}

static void lte(struct value *lhs, const struct value *rhs)
{
  lhs->i = cmp(lhs, rhs) <= 0;
  lhs->s = NULL;
}

static void lt(struct value *lhs, const struct value *rhs)
{
  lhs->i = cmp(lhs, rhs) < 0;
  lhs->s = NULL;
}

static void gte(struct value *lhs, const struct value *rhs)
{
  lhs->i = cmp(lhs, rhs) >= 0;
  lhs->s = NULL;
}

static void gt(struct value *lhs, const struct value *rhs)
{
  lhs->i = cmp(lhs, rhs) > 0;
  lhs->s = NULL;
}

static void eq(struct value *lhs, const struct value *rhs)
{
  lhs->i = cmp(lhs, rhs) == 0;
  lhs->s = NULL;
}

static void and(struct value *lhs, const struct value *rhs)
{
  if (is_zero(lhs) || is_zero(rhs)) {
    lhs->i = 0;
    lhs->s = NULL;
  }
}

static void or(struct value *lhs, const struct value *rhs)
{
  if (is_zero(lhs)) {
    *lhs = *rhs;
  }
}


// operators in order of increasing precedence
static const struct op ops[] = {
  {"|",   or  },
  {"&",   and },
  {"=",   eq  },
  {">",   gt  },
  {">=",  gte },
  {"<",   lt  },
  {"<=",  lte },
  {"!=",  ne  },
  {"+",   add },
  {"-",   sub },
  {"*",   mul },
  {"/",   divi},
  {"%",   mod },
  {":",   re  },
  {"(",   NULL}, // special case - must be last
};


static void parse_parens(struct value *ret, struct value *v)
{
  if (match(v, "(")) {
    parse_expr(ret, v);
    if (!match(v, ")")) error_exit("syntax error"); // missing closing paren
  } else {
    // v is a string or integer - return it and get the next token
    *ret = *v;
    get_value(v);
  }
}

static void parse_op(struct value *lhs, struct value *tok, const struct op *op)
{
  // special case parsing for parentheses
  if (*op->tok == '(') {
    parse_parens(lhs, tok);
    return;
  }

  parse_op(lhs, tok, op + 1);
  while (match(tok, op->tok)) {
    struct value rhs;
    parse_op(&rhs, tok, op + 1);
    if (rhs.s && !*rhs.s) error_exit("syntax error"); // premature end of expression
    op->calc(lhs, &rhs);
  }
}

static void parse_expr(struct value *ret, struct value *v)
{
  parse_op(ret, v, ops); // start at the top of the ops table
}

void expr_main(void)
{
  struct value tok, ret = {0};

  toys.exitval = 2; // if exiting early, indicate invalid expression

  TT.argidx = 0;

  get_value(&tok); // warm up the parser with the initial value
  parse_expr(&ret, &tok);

  if (!tok.s || *tok.s) error_exit("syntax error"); // final token should be end of expression

  if (ret.s) printf("%s\n", ret.s);
  else printf("%ld\n", ret.i);

  exit(is_zero(&ret));
}
