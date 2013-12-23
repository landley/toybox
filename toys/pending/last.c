/* last.c - Show listing of last logged in users.
 *
 * Copyright 2013 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard.

USE_LAST(NEWTOY(last, "f:W", TOYFLAG_BIN))

config LAST
  bool "last"
  default n
  help
    Usage: last [-W] [-f FILE]

    Show listing of last logged in users.

    -W      Display the information without host-column truncation.
    -f FILE Read from file FILE instead of /var/log/wtmp.
*/
#define FOR_last

#include "toys.h"
#include <utmp.h>

#ifndef SHUTDOWN_TIME
#define SHUTDOWN_TIME 254
#endif

GLOBALS(
  char *file;
  struct arg_list *list;
)

static void free_node(void *data)
{
  void *arg = ((struct arg_list*)data)->arg;
  
  if (arg) free(arg);
  free(data);
}

static void free_list()
{
  if (TT.list) {
    llist_traverse(TT.list, free_node);
    TT.list = NULL;
  }
}

static void llist_add_node(struct arg_list **old, void *data)
{
  struct arg_list *new = xmalloc(sizeof(struct arg_list));
  
  new->arg = (char*)data;
  new->next = *old;
  *old = new;
}

// Find a node and dlink it from the list.
static struct arg_list *find_and_dlink(struct arg_list **list, char *devname)
{
  struct arg_list *l = *list;
  
  while (*list) {
    struct utmp *ut = (struct utmp *)l->arg;

    if (!strncmp(ut->ut_line, devname, UT_LINESIZE)) {
      *list = (*list)->next;
      return l;
    }
    list = &(*list)->next;
    l = *list;
  }
  return NULL;
}

// Compute login, logout and duration of login.
static void seize_duration(time_t tm0, time_t tm1)
{
  unsigned days, hours, mins;
  double diff = difftime(tm1, tm0);
  
  diff = (diff > 0) ? (tm1 - tm0) : 0;
  toybuf[0] = toybuf[18] = toybuf[28] = '\0';
  strncpy(toybuf, ctime(&tm0), 16); // Login Time.
  snprintf(toybuf+18, 8, "- %s", ctime(&tm1) + 11); // Logout Time.
  days = (mins = diff/60)/(24*60);
  hours = (mins = (mins%(24*60)))/60;
  mins = mins%60;
  sprintf(toybuf+28, "(%u+%02u:%02u)", days, hours, mins); // Duration.
}

void last_main(void)
{
  struct utmp ut;
  struct stat sb;
  time_t tm[3] = {0,}; //array for time avlues, previous, current
  char *file = "/var/log/wtmp";
  int fd, pwidth, curlog_type = EMPTY;
  off_t loc;

  if (toys.optflags & FLAG_f) file = TT.file;

  TT.list = NULL;
  pwidth = (toys.optflags & FLAG_W) ? 46 : 16;
  time(&tm[1]);
  fd = xopen(file, O_RDONLY);
  loc = xlseek(fd, 0, SEEK_END);
  // in case of empty file or 'filesize < sizeof(ut)'
  fstat(fd, &sb);
  if (sizeof(ut) > sb.st_size) {
    xclose(fd);
    printf("\n%s begins %-24.24s\n", basename(file), ctime(&sb.st_ctime));
    return;
  }
  loc = xlseek(fd, loc - sizeof(ut), SEEK_SET);

  while (1) {
    xreadall(fd, &ut, sizeof(ut));
    tm[0] = (time_t)ut.ut_tv.tv_sec;
    if (ut.ut_line[0] == '~') {
      if (!strcmp(ut.ut_user, "runlevel")) ut.ut_type = RUN_LVL;
      else if (!strcmp(ut.ut_user, "reboot")) ut.ut_type = BOOT_TIME;
      else if (!strcmp(ut.ut_user, "shutdown")) ut.ut_type = SHUTDOWN_TIME;
    } 
    else if (ut.ut_user[0] == '\0') ut.ut_type = DEAD_PROCESS;
    else if (ut.ut_user[0] && ut.ut_line[0] && (ut.ut_type != DEAD_PROCESS)
        && (strcmp(ut.ut_user, "LOGIN")) ) ut.ut_type = USER_PROCESS;
    /* The pair of terminal names '|' / '}' logs the
     * old/new system time when date changes it.
     */ 
    if (!strcmp(ut.ut_user, "date")) {
      if (ut.ut_line[0] == '|') ut.ut_type = OLD_TIME;
      if (ut.ut_line[0] == '{') ut.ut_type = NEW_TIME;
    }
    if ( (ut.ut_type == SHUTDOWN_TIME) || ((ut.ut_type == RUN_LVL) && 
        (((ut.ut_pid & 255) == '0') || ((ut.ut_pid & 255) == '6')))) {
      tm[1] = tm[2] = (time_t)ut.ut_tv.tv_sec;
      free_list();
      curlog_type = RUN_LVL;
    } else if (ut.ut_type == BOOT_TIME) {
      seize_duration(tm[0], tm[1]);
      strncpy(ut.ut_line, "system boot", sizeof("system boot"));
      free_list();
      printf("%-8.8s %-12.12s %-*.*s %-16.16s %-7.7s %s\n", ut.ut_user, 
          ut.ut_line, pwidth, pwidth, ut.ut_host, 
          toybuf, toybuf+18, toybuf+28);
      curlog_type = BOOT_TIME;
      tm[2] = (time_t)ut.ut_tv.tv_sec;
    } else if (ut.ut_type == USER_PROCESS && *ut.ut_line) {
      struct arg_list *l = find_and_dlink(&TT.list, ut.ut_line);
      if (l) {
        struct utmp *u = (struct utmp *)l->arg;
        seize_duration(tm[0], u->ut_tv.tv_sec);
        printf("%-8.8s %-12.12s %-*.*s %-16.16s %-7.7s %s\n", ut.ut_user, 
            ut.ut_line, pwidth, pwidth, ut.ut_host, 
            toybuf, toybuf+18, toybuf+28);
        free(l->arg);
        free(l);
      } else {
        int type = !tm[2] ? EMPTY : curlog_type;
        if (!tm[2]) { //check process's current status (alive or dead).
          if ((ut.ut_pid > 0) && (kill(ut.ut_pid, 0)!=0) && (errno == ESRCH))
            type = INIT_PROCESS;
        }
        seize_duration(tm[0], tm[2]);
        switch (type) {
          case EMPTY:
            strncpy(toybuf+18, "  still", sizeof("  still"));
            strncpy(toybuf+28, "logged in", sizeof("logged in")); 
            break;
          case RUN_LVL:
            strncpy(toybuf+18, "- down ", sizeof("- down "));
            break;
          case BOOT_TIME:
            strncpy(toybuf+18, "- crash", sizeof("- crash"));
            break;
          case INIT_PROCESS:
            strncpy(toybuf+18, "   gone", sizeof("   gone"));
            strncpy(toybuf+28, "- no logout", sizeof("- no logout"));
            break;
          default:
            break;
        }
        printf("%-8.8s %-12.12s %-*.*s %-16.16s %-7.7s %s\n", ut.ut_user, 
            ut.ut_line, pwidth, pwidth, ut.ut_host, 
            toybuf, toybuf+18, toybuf+28);
      }
      llist_add_node(&TT.list, memcpy(xmalloc(sizeof(ut)), &ut, sizeof(ut)));
    } else if (ut.ut_type == DEAD_PROCESS && *ut.ut_line)
      llist_add_node(&TT.list, memcpy(xmalloc(sizeof(ut)), &ut, sizeof(ut)));

    loc -= sizeof(ut);
    if(loc < 0) break;
    xlseek(fd, loc, SEEK_SET);
  } // End of while.

  fflush(stdout);
  xclose(fd);
  if (CFG_TOYBOX_FREE) free_list();
  printf("\n%s begins %-24.24s\n", basename(file), ctime(&tm[0]));
}
