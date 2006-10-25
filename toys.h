/* vi: set ts=4 :*/
/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * Licensed under GPL version 2, see file LICENSE in this tarball for details.
 */

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/lib.h"

int cd_main(void);
int df_main(void);
int exit_main(void);
int toybox_main(void);
int toysh_main(void);

#define TOYFLAG_USR      (1<<0)
#define TOYFLAG_BIN      (1<<1)
#define TOYFLAG_SBIN     (1<<2)
#define TOYMASK_LOCATION ((1<<4)-1)

#define TOYFLAG_NOFORK   (1<<4)

extern struct toy_list {
	char *name;
	int (*toy_main)(void);
	int flags;
} toy_list[];
struct toy_list *toy_find(char *name);
void toy_init(struct toy_list *which, char *argv[]);
void toy_exec(char *argv[]);

// Global context for this applet.

extern struct toy_context {
	struct toy_list *which;  // Which entry in toy_list is this one?
	int exitval;             // Value error_exit feeds to exit()
	int argc;
	char **argv;
	char buf[4096];
} toys;

struct exit_data {;};
struct cd_data {;};
struct toybox_data {;};
struct toysh_data {;};
struct df_data {;};

union toy_union {
	struct exit_data exit;
	struct cd_data cd;
	struct toybox_data toybox;
	struct toysh_data toysh;
	struct df_data df;
} toy;

// Pending the addition of menuconfig...

#define CFG_TOYS_FREE     0

#define CFG_TOYSH_TTY     0  // Terminal control
#define CFG_TOYSH_JOBCTL  0  // &, fg, bg, jobs.  ctrl-z with tty.
#define CFG_TOYSH_FLOWCTL 0  // if, while, for, functions { }
#define CFG_TOYSH_ENVVARS 0  // Environment variables
#define CFG_TOYSH_LOCVARS 0  // Local, synthetic, fancy prompts, set, $?
#define CFG_TOYSH_PIPES   0  // Pipes and redirects: | > < >> << && || & () ;
