/* csplit.c - split files on context
 *
 * Copyright 2023 Oliver Webb <aquahobbyist@proton.me>
 *
 * See https://pubs.opengroup.org/onlinepubs/9699919799/utilities/csplit.html
 * Deviations From POSIX:
 *	Does not use %d for file size output
 *	Doesn't do negitive offsets
 *	GNU Extension: "{*}"
 *

USE_CSPLIT(NEWTOY(csplit, "<2skf:n#", TOYFLAG_USR|TOYFLAG_BIN))

config CSPLIT
  bool "csplit"
  default n
  help
    usage: csplit [-ks] [-f PREFIX] [-n INTEGER] file arg...

	Split files into multiple files based on list of rules

	-k Does not delete Files on error
	-s No file output size messages
	-f [PREFIX] Use [PREFIX] as filename prefix instead of "xx"
	-n [INTEGER] Make all filename numbers [INTEGER] characters long

	Valid Rules:
	/regexp/[INTEGER] Break file before line that regexp matches,
	%regexp%[INTEGER]
	If a offset is specified for these rules, the break will happen [INTEGER]
	lines after the regexp match
	if a offset is specified, it will break at [INTEGER] lines after the offset
	[INTEGER] Break file at line before [INTEGER]
	{INTEGER} Repeat Previous Pattern INTEGER Number of times if INTEGER is *
	The pattern repeats forever

*/

#define FOR_csplit
#include "toys.h"

GLOBALS(
  long n;
  char *f;
)

size_t indx = 1, findx = 0, lineno = 1, btc = 0;
int eg = 0, offset = -1, withld = 0;
char *filefmt, *flname, *prefix;

// This is only int so we can exit cleanly in ternary operators
int abort_csplit(char *err) {
  // Cycle down through index instead of keeping track of what files we made
  if (!FLAG(k)) for (; indx>=1; indx--)
	remove(xmprintf(filefmt, prefix, findx));
  error_exit("%s\n", err);
  return 1;
}

int rgmatch(char *rxrl, char *line, char *fmt) {
  regex_t rxp;
  sscanf(rxrl,fmt, toybuf, &offset);
  xregcomp(&rxp, toybuf, 0);
  int rr = regexec(&rxp, line, 0, 0, 0);
  if (!rr) return 1;
  else if (rr == REG_NOMATCH) return 0;
  return abort_csplit("bad regex");
}

int cntxt(char *line, char *rule) {
  if (eg) return 0;

  if (offset < 0);
  else if (offset == 0) {
	offset = -1;
	return 1;
  } else {
	offset--;
	return 0;
  }

  switch (rule[0]) {

	case '/':
	  return rgmatch(rule, line, "/%[^/%]/%d");
	  break;
	case '%':
	  withld = 1;
	  return rgmatch(rule, line, "%%%[^/%]%%%d");
	  break;

	case '{':
	  // GNU extention: {*}
	  if (!strcmp(rule,"{*}"))
		btc = -1;
	  else if (!sscanf(rule,"{%lu}",&btc))
		abort_csplit("bad rule");

	  // Reset the lineno so we can do things like "10 {*}"
	  lineno = 1;

	  if (cntxt(line, toys.optargs[indx-1])) {
		// Manipulate the rule then return to it later so we create a
		// new file but are still on the same rule. This is the only
		// reason why we differentiate between rule and file Index
		if (btc != 1) {
		  toys.optargs[indx] = xmprintf("{%lu}",btc-1);
		  indx--;
		}
		return 1;
	  }
	  return 0;
	  break;

	default:
	 if (lineno > ((size_t)atoll(rule))) {
	   abort_csplit("bad rule order");
	 } else if (!(atoll(rule))) {
	   abort_csplit("bad rule");
	 } else {
	   return (lineno == (size_t)atoll(rule));
	 }
	 break;
  }

  // The code should never get to this point without returning something
  perror_exit("Error");
  return 1;
}

void csplit_main(void)
{
  FILE *fin = (*toys.optargs[0] != '-') ? xfopen(toys.optargs[0], "r") : stdin;

  struct stat st;

  // -f and -n formatting
  filefmt = xmprintf("%%s%%0%dd", TT.n ? (int)TT.n : 2);
  prefix = TT.f ? TT.f : "xx";

  flname = xmprintf(filefmt, prefix, findx);
  FILE *actvfile = xfopen(flname, "w+");
  for (char *line; (line = xgetline(fin)); free(line)) {
	lineno++;
	if (cntxt(line, toys.optargs[indx])) {

	  if (!withld) {
		fclose(actvfile);
		if (!FLAG(s)) {
		  stat(flname, &st);
		  printf("%ld\n", st.st_size);
		}
		findx++;
		flname = xmprintf(filefmt, prefix, findx);
		actvfile = xfopen(flname, "w+");
	  }

	  indx++;
	  withld = 0;
	  if (indx == toys.optc) eg = 1;
	}
	if (!withld) fprintf(actvfile, "%s\n", line);
  }

  fclose(actvfile);
  if (!FLAG(s)) {
	stat(flname, &st);
	printf("%ld\n", st.st_size);
  }

  // Abort Case: Not All Rules Processed
  if (indx < toys.optc-1) abort_csplit("Rules not processed");
}
