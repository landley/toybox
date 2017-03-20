/* Flags values for the third argument of NEWTOY()
 *
 * Included from both main.c (runs in toys.h context) and scripts/install.c
 * (which may build on crazy things like macosx when cross compiling).
 */

// Flags describing command behavior.

#define TOYFLAG_USR      (1<<0)
#define TOYFLAG_BIN      (1<<1)
#define TOYFLAG_SBIN     (1<<2)
#define TOYMASK_LOCATION ((1<<4)-1)

// This is a shell built-in function, running in the same process context.
#define TOYFLAG_NOFORK   (1<<4)

// Start command with a umask of 0 (saves old umask in this.old_umask)
#define TOYFLAG_UMASK    (1<<5)

// This command runs as root.
#define TOYFLAG_STAYROOT (1<<6)
#define TOYFLAG_NEEDROOT (1<<7)
#define TOYFLAG_ROOTONLY (TOYFLAG_STAYROOT|TOYFLAG_NEEDROOT)

// Call setlocale to listen to environment variables.
// This invalidates sprintf("%.*s", size, string) as a valid length constraint.
#define TOYFLAG_LOCALE   (1<<8)

// Suppress default --help processing
#define TOYFLAG_NOHELP   (1<<9)

#if CFG_TOYBOX_PEDANTIC_ARGS
#define NO_ARGS ">0"
#else
#define NO_ARGS 0
#endif
