/* expr.c - evaluate expression
 *
 * Copyright 2016 Google Inc.
 * Copyright 2013 Daniel Verkamp <daniel@drv.nu>
 *
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/expr.html
 *
 * The web standard is incomplete (precedence grouping missing), see:
 * http://permalink.gmane.org/gmane.comp.standards.posix.austin.general/10141
 *
 * eval_expr() uses the recursive "Precedence Climbing" algorithm:
 *
 * Clarke, Keith. "The top-down parsing of expressions." University of London.
 * Queen Mary College. Department of Computer Science and Statistics, 1986.
 *
 * http://www.antlr.org/papers/Clarke-expr-parsing-1986.pdf
 *
 * Nice explanation and Python implementation:
 * http://eli.thegreenplace.net/2012/08/02/parsing-expressions-by-precedence-climbing

USE_EXPR(NEWTOY(expr, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config EXPR
  bool "expr"
  default n
  help
    usage: expr ARG1 OPERATOR ARG2...

    Evaluate expression and print result. For example, "expr 1 + 2".

    The supported operators are (grouped from highest to lowest priority):

      ( )    :    * / %    + -    != <= < >= > =    &    |

    Each constant and operator must be a separate command line argument.
    All operators are infix, meaning they expect a constant (or expression
    that resolves to a constant) on each side of the operator. Operators of
    the same priority (within each group above) are evaluated left to right.
    Parentheses may be used (as separate arguments) to elevate the priority
    of expressions.

    Calling expr from a command shell requires a lot of \( or '*' escaping
    to avoid interpreting shell control characters.

    The & and | operators are logical (not bitwise) and may operate on
    strings (a blank string is "false"). Comparison operators may also
    operate on strings (alphabetical sort).

    Constants may be strings or integers. Comparison, logical, and regex
    operators may operate on strings (a blank string is "false"), other
    operators require integers.
*/

// TODO: int overflow checking

#define FOR_expr
#include "toys.h"

GLOBALS(
  char **tok; // current token, not on the stack since recursive calls mutate it

  char *refree;
)

// Scalar value.  If s != NULL, it's a string, otherwise it's an int.
struct value {
  char *s;
  long long i;
};

// Get the value as a string.
char *get_str(struct value *v)
{
  if (v->s) return v->s;
  else return xmprintf("%lld", v->i);
}

// Get the value as an integer and return 1, or return 0 on error.
int get_int(struct value *v, long long *ret)
{
  if (v->s) {
    char *endp;

    *ret = strtoll(v->s, &endp, 10);

    if (*endp) return 0; // If endp points to NUL, all chars were converted
  } else *ret = v->i;

  return 1;
}

// Preserve the invariant that v.s is NULL when the value is an integer.
void assign_int(struct value *v, long long i)
{
  v->i = i;
  v->s = NULL;
}

// Check if v is 0 or the empty string.
static int is_false(struct value *v)
{
  return get_int(v, &v->i) && !v->i;
}

// 'ret' is filled with a string capture or int match position.
static void re(char *target, char *pattern, struct value *ret)
{
  regex_t pat;
  regmatch_t m[2];

  xregcomp(&pat, pattern, 0);
  // must match at pos 0
  if (!regexec(&pat, target, 2, m, 0) && !m[0].rm_so) {
    // Return first parenthesized subexpression as string, or length of match
    if (pat.re_nsub>0) {
      ret->s = xmprintf("%.*s", (int)(m[1].rm_eo-m[1].rm_so),
          target+m[1].rm_so);
      if (TT.refree) free(TT.refree);
      TT.refree = ret->s;
    } else assign_int(ret, m[0].rm_eo);
  } else {
    if (pat.re_nsub>0) ret->s = "";
    else assign_int(ret, 0);
  }
  regfree(&pat);
}

// 4 different signatures of operators.  S = string, I = int, SI = string or
// int.
enum { SI_TO_SI = 1, SI_TO_I, I_TO_I, S_TO_SI };

enum { OR = 1, AND, EQ, NE, GT, GTE, LT, LTE, ADD, SUB, MUL, DIVI, MOD, RE };

// operators grouped by precedence
static struct op_def {
  char *tok;
  char prec, sig, op; // precedence, signature for type coercion, operator ID
} OPS[] = {
  // logical ops, precedence 1 and 2, signature SI_TO_SI
  {"|", 1, SI_TO_SI, OR  },
  {"&", 2, SI_TO_SI, AND },
  // comparison ops, precedence 3, signature SI_TO_I
  {"=", 3, SI_TO_I, EQ }, {"==", 3, SI_TO_I, EQ  }, {"!=", 3, SI_TO_I, NE },
  {">", 3, SI_TO_I, GT }, {">=", 3, SI_TO_I, GTE },
  {"<", 3, SI_TO_I, LT }, {"<=", 3, SI_TO_I, LTE }, 
  // arithmetic ops, precedence 4 and 5, signature I_TO_I
  {"+", 4, I_TO_I, ADD }, {"-",  4, I_TO_I, SUB },
  {"*", 5, I_TO_I, MUL }, {"/",  5, I_TO_I, DIVI }, {"%", 5, I_TO_I, MOD },
  // regex match, precedence 6, signature S_TO_SI
  {":", 6, S_TO_SI, RE },
  {NULL, 0, 0, 0}, // sentinel
};

void eval_op(struct op_def *o, struct value *ret, struct value *rhs)
{
  long long a, b, x = 0; // x = a OP b for ints.
  char *s, *t; // string operands
  int cmp;

  switch (o->sig) {

  case SI_TO_SI:
    switch (o->op) {
    case OR:  if (is_false(ret)) *ret = *rhs; break;
    case AND: if (is_false(ret) || is_false(rhs)) assign_int(ret, 0); break;
    }
    break;  

  case SI_TO_I:
    if (get_int(ret, &a) && get_int(rhs, &b)) { // both are ints
      cmp = a - b;
    } else { // otherwise compare both as strings
      cmp = strcmp(s = get_str(ret), t = get_str(rhs));
      if (ret->s != s) free(s);
      if (rhs->s != t) free(t);
    }
    switch (o->op) {
    case EQ:  x = cmp == 0; break;
    case NE:  x = cmp != 0; break;
    case GT:  x = cmp >  0; break;
    case GTE: x = cmp >= 0; break;
    case LT:  x = cmp <  0; break;
    case LTE: x = cmp <= 0; break;
    }
    assign_int(ret, x);
    break;

  case I_TO_I:
    if (!get_int(ret, &a) || !get_int(rhs, &b))
      error_exit("non-integer argument");
    switch (o->op) {
    case ADD: x = a + b; break;
    case SUB: x = a - b; break;
    case MUL: x = a * b; break;
    case DIVI: if (b == 0) error_exit("division by zero"); x = a / b; break;
    case MOD:  if (b == 0) error_exit("division by zero"); x = a % b; break;
    }
    assign_int(ret, x);
    break;

  case S_TO_SI: // op == RE
    s = get_str(ret);
    cmp = ret->s!=s; // ret overwritten by re so check now
    re(s, t = get_str(rhs), ret);
    if (cmp) free(s);
    if (rhs->s!=t) free(t);
    break;
  }
}

// Evalute a compound expression using recursive "Precedence Climbing"
// algorithm, setting 'ret'.
static void eval_expr(struct value *ret, int min_prec)
{
  if (!*TT.tok) error_exit("Unexpected end of input");

  // Evaluate LHS atom, setting 'ret'.
  if (!strcmp(*TT.tok, "(")) { // parenthesized expression
    TT.tok++;  // consume (

    eval_expr(ret, 1);        // We're inside ( ), so min_prec = 1
    if (ret->s && !strcmp(ret->s, ")")) error_exit("empty ( )");
    if (!*TT.tok) error_exit("Expected )");
    if (strcmp(*TT.tok, ")")) error_exit("Expected ) but got %s", *TT.tok);
  } else ret->s = *TT.tok;  // simple literal, all values start as strings
  TT.tok++;

  // Evaluate RHS and apply operator until precedence is too low.
  struct value rhs;
  while (*TT.tok) {
    struct op_def *o = OPS;

    while (o->tok) { // Look up operator
      if (!strcmp(*TT.tok, o->tok)) break;
      o++;
    }
    if (!o->tok) break; // Not an operator (extra input will fail later)
    if (o->prec < min_prec) break; // Precedence too low, pop a stack frame
    TT.tok++;

    eval_expr(&rhs, o->prec + 1); // Evaluate RHS, with higher min precedence
    eval_op(o, ret, &rhs); // Apply operator, setting 'ret'
  }
}

void expr_main(void)
{
  struct value ret = {0};

  toys.exitval = 2; // if exiting early, indicate error
  TT.tok = toys.optargs; // initialize global token
  eval_expr(&ret, 1);
  if (*TT.tok) error_exit("Unexpected extra input '%s'\n", *TT.tok);

  if (ret.s) printf("%s\n", ret.s);
  else printf("%lld\n", ret.i);

  toys.exitval = is_false(&ret);

  if (TT.refree) free(TT.refree);
}
