/* vi: set ts=4 :*/
/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */


// When #included from main.c, provide the guts for toy_list[]

#ifdef FROM_MAIN
#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name, flags) {#name, name##_main, flags},
#define OLDTOY(name, oldname, flags) {#name, oldname##_main, flags},

// When #included from toys.h, provide function declarations and structs.
// The #else is because main.c #includes this file twice.

#else
#define NEWTOY(name, flags) int name##_main(void);
#define OLDTOY(name, oldname, flags)

struct df_data {
	struct string_list *fstype;
	long units;
};

union toy_union {
	struct df_data df;
} toy;

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

#endif

// List of all the applets toybox can provide.

// This one is out of order on purpose.

NEWTOY(toybox, 0)

// The rest of these are alphabetical, for binary search.

USE_TOYSH(NEWTOY(cd, TOYFLAG_NOFORK))
USE_DF(NEWTOY(df, TOYFLAG_USR|TOYFLAG_SBIN))
USE_TOYSH(NEWTOY(exit, TOYFLAG_NOFORK))
USE_HELLO(NEWTOY(hello, TOYFLAG_NOFORK|TOYFLAG_USR))
USE_PWD(NEWTOY(pwd, TOYFLAG_BIN))
USE_TOYSH(OLDTOY(sh, toysh, TOYFLAG_BIN))
USE_TOYSH(NEWTOY(toysh, TOYFLAG_BIN))
USE_WHICH(NEWTOY(which, TOYFLAG_USR|TOYFLAG_BIN))
