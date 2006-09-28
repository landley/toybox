/* vi: set ts=4 :*/
/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * Licensed under GPL version 2, see file LICENSE in this tarball for details.
 */

#include <stdio.h>
#include <strings.h>

/*
name
main()
struct
usage (short long example info)
path (/usr/sbin)
*/

int toybox_main(void);
int toysh_main(void);
int df_main(void);

extern struct toy_list {
	char *name;
	int (*toy_main)(void);
} toy_list[];
struct toy_list *find_toy_by_name(char *name);

// Global context for this applet.

extern struct toy_context {
	struct toy_list *which;
	int argc;
	char **argv;
	char buf[4096];
} toys;

struct toybox_data {;};
struct toysh_data {;};
struct df_data {;};

union toy_union {
	struct toybox_data toybox;
	struct toysh_data toysh;
	struct df_data df;
} toy;

