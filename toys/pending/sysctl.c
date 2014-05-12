/* sysctl.c - A utility to read and manipulate the sysctl parameters.
 *
 * Copyright 2014 Bilal Qureshi <bilal.jmi@gmail.com>
 * Copyright 2014 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard
 
USE_SYSCTL(NEWTOY(sysctl, "^neNqwpaA[!ap][!aq][!aw][+aA]", TOYFLAG_USR|TOYFLAG_BIN))

config SYSCTL
  bool "sysctl"
  default n
  help
    usage: sysctl [OPTIONS] [KEY[=VALUE]]...

    -a, A      Show all values
    -e         Don't warn about unknown keys
    -N         Show only key names
    -n         Show only key values
    -p FILE    Set values from FILE (default /etc/sysctl.conf)
    -q         Set values silently
    -w         Set values
*/
#define FOR_sysctl
#include "toys.h"

#define PROC_SYS_DIR	"/proc/sys"
#define MAX_BYTES_LINE	1024

static void parse_key_name_value(char *param, char **key_name, char **key_value)
{
  char *name, *value;

  if (!(name = strchr(param, '='))) goto show_error_msg;
  value = name;
  value++;
  if (!(*value)) goto show_error_msg;
  *name = '\0';
  *key_name = param;
  *key_value = value;
  return;
show_error_msg:
  error_msg("error: '%s' must be of the form name=value", param);
}

static void replace_char(char *str, char old, char new)
{
  char *tmp = str;

  for (; *tmp; tmp++) 
    if (*tmp == old) *tmp = new;
}

static void handle_file_error(char *key_name)
{
  replace_char(key_name, '/', '.');
  if (errno == ENOENT) { 
    if (!(toys.optflags & FLAG_e)) 
      error_msg("error: '%s' is an unknown key", key_name);
    else toys.exitval = 1;
  } else perror_msg("error: %s key '%s'", (toys.optflags & FLAG_w) ?
      "setting" : "reading", key_name);
}

static void write_to_file(char *fpath, char *key_name, char *key_value)
{
  int fd;

  replace_char(key_name, '.', '/');
  sprintf(toybuf, "%s/%s", fpath, key_name);
  if ((fd = open(toybuf, O_WRONLY)) < 0) { 
    handle_file_error(key_name);
    return;
  }
  xwrite(fd, key_value, strlen(key_value));
  xclose(fd);
  if (!(toys.optflags & FLAG_q)) {
    replace_char(key_name, '/', '.');
    if (toys.optflags & FLAG_N) xprintf("%s\n", key_name);
    else if (toys.optflags & FLAG_n) xprintf("%s\n", key_value);
    else xprintf("%s = %s\n", key_name, key_value);
  }
}

static char *get_key_value(char *buff, int *offset)
{
  char *line, *tmp = (char *) (buff + *offset);
  int index = 0, multiplier = 1;

  if (!(*tmp)) return NULL;
  line = (char *) xmalloc(sizeof(char) * MAX_BYTES_LINE);
  for (; *tmp != '\n'; tmp++) {
    line[index++] = *tmp;
    if (MAX_BYTES_LINE == index) { // buffer overflow
      multiplier++;
      line = (char *) xrealloc(line, multiplier * MAX_BYTES_LINE);
    }
  }
  line[index++] = '\0';
  *offset += index;
  return line;
}

// Open file for each and every key name and read file contents
void read_key_values(char *fpath)
{
  char *key_value, *fdata, *key_name, *tmp = xstrdup(fpath);
  int offset = 0;

  key_name = (tmp + strlen(PROC_SYS_DIR) + 1);
  replace_char(key_name, '/', '.');
  if (!(fdata = readfile(fpath, NULL, 0))) {
    handle_file_error(key_name);
    free(tmp);
    return;
  }
  if (toys.optflags & FLAG_N) {
    xprintf("%s\n", key_name);
    free(tmp);
    free(fdata);
    return;
  }
  for (; (key_value = get_key_value(fdata, &offset)); free(key_value)) {
    if (!(toys.optflags & FLAG_q)) {
      if (!(toys.optflags & FLAG_n)) xprintf("%s = ", key_name);
      xprintf("%s\n", key_value);
    }
  }
  free(tmp);
  free(fdata);
}

static void trim_spaces(char **param)
{
  int len = 0;
  char *str = *param, *p_start = str, *p_end;

  if (p_start) {   // start pointer to string 
    p_end = str + strlen(str) - 1; // end pointer to string
    while (*p_start == ' ') p_start++;
    str = p_start;
    while (*p_end == ' ') p_end--;
    p_end++;
    *p_end = '\0';
    len = (int) (p_end - str) + 1;
    memmove(*param, str, len);
  }
}

// Read config file and write values to there corresponding key name files
static void read_config_file(char *fname)
{
  char *line, *name = NULL, *value = NULL;
  int fd = xopen(fname, O_RDONLY);

  for (; (line = get_line(fd)); free(line), name = NULL, value = NULL) {
    char *ptr = line;

    while (*ptr == ' ' || *ptr == '\t') ptr++;
    if (*ptr != '#' && *ptr != ';' && *ptr !='\n' && *ptr) {
      parse_key_name_value(ptr, &name, &value);
      trim_spaces(&name);
      trim_spaces(&value);
      if (name && value) write_to_file(PROC_SYS_DIR, name, value);
    }
  }
  xclose(fd);
}

static int do_process(struct dirtree *dt)
{
  char *fpath;

  if (!dirtree_notdotdot(dt)) return 0; // Skip . and ..
  if (S_ISDIR(dt->st.st_mode)) return DIRTREE_RECURSE; 
  if ((fpath = dirtree_path(dt, 0))) {
    read_key_values(fpath);
    free(fpath);
  }
  return 0;
}

void sysctl_main()
{
  char *name = NULL, *value = NULL, **args = NULL;

  if (toys.optflags & FLAG_a) {
    dirtree_read(PROC_SYS_DIR, do_process);
    return;
  }
  if (toys.optflags & FLAG_p) {
    if (*toys.optargs) read_config_file(*toys.optargs);
    else read_config_file("/etc/sysctl.conf");
    return;
  }
  if (toys.optflags & FLAG_w) {
    for (args = toys.optargs; *args; args++, name = NULL, value = NULL) {
      parse_key_name_value(*args, &name, &value);
      if (name && value) write_to_file(PROC_SYS_DIR, name, value);
    }
    return;
  }
  for (args = toys.optargs; *args; args++) {
    replace_char(*args, '.', '/');
    sprintf(toybuf, "%s/%s", PROC_SYS_DIR, *args);
    read_key_values(toybuf);
  }
}
