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
    usage: sysctl [-aAeNnqw] [-p [FILE] | KEY[=VALUE]...]

    Read/write system control data (under /proc/sys).

    -a,A	Show all values
    -e	Don't warn about unknown keys
    -N	Don't print key values
    -n	Don't print key names
    -p [FILE]	Read values from FILE (default /etc/sysctl.conf)
    -q	Don't show value after write
    -w	Only write values (object to reading)
*/
#define FOR_sysctl
#include "toys.h"

// Null terminate at =, return value
static char *split_key(char *key)
{
  char *value = strchr(key, '=');

  if (value) *(value++)=0;

  return value;
}

static void replace_char(char *str, char old, char new)
{
  for (; *str; str++) if (*str == old) *str = new;
}

static void key_error(char *key)
{
  if (!(errno == ENOENT && (toys.optflags & FLAG_e)))
    perror_msg("key '%s'", key);
}

static int write_key(char *path, char *key, char *value)
{
  int fd = open(path, O_WRONLY);;

  if (fd < 0) {
    key_error(key);

    return 0;
  }
  xwrite(fd, value, strlen(value));
  xclose(fd);

  return 1;
}

// Display all keys under a path
static int do_show_keys(struct dirtree *dt)
{
  char *path, *data, *key;

  if (!dirtree_notdotdot(dt)) return 0; // Skip . and ..
  if (S_ISDIR(dt->st.st_mode)) return DIRTREE_RECURSE;

  path = dirtree_path(dt, 0);
  data = readfile(path, 0, 0);
  replace_char(key = path + 10, '/', '.'); // skip "/proc/sys/"
  if (!data) key_error(key);
  else {
    // Print the parts that aren't switched off by flags.
    if (!(toys.optflags & FLAG_n)) xprintf("%s", key);
    if (!(toys.optflags & (FLAG_N|FLAG_n))) xprintf(" = ");
    for (key = data+strlen(data); key > data && isspace(*--key); *key = 0);
    if (!(toys.optflags & FLAG_N)) xprintf("%s", data);
    if ((toys.optflags & (FLAG_N|FLAG_n)) != (FLAG_N|FLAG_n)) xputc('\n');
  }

  free(data);
  free(path);

  return 0;
}

// Read/write entries under a key
static void process_key(char *key, char *value)
{
  char *path, *pattern = "%s/%s/%s";

  if ((toys.optflags & FLAG_w) && !value && !strchr(key, '=')) {
    error_msg("'%s' not key=value");

    return;
  }

  path = xmprintf(pattern + 3*!value, "/proc/sys", key, value);
  value = split_key(path);
  replace_char(path, '.', '/');
  // Note: failure to assign to a non-leaf node suppresses the display.
  if (!(value && (write_key(path, key, value) || (toys.optflags & FLAG_q))))
    dirtree_read(path, do_show_keys);
  free(path);
}

void sysctl_main()
{
  char **args = 0;

  // Display all keys
  if (toys.optflags & FLAG_a) dirtree_read("/proc/sys", do_show_keys);

  // read file
  else if (toys.optflags & FLAG_p) {
    FILE *fp = xfopen(*toys.optargs ? *toys.optargs : "/etc/sysctl.conf", "r");
    char *line = 0;
    size_t len;

    for (;-1 != (len = getline(&line, &len, fp)); free(line), line = 0) {
      char *key = line, *val;

      while (isspace(*key)) key++;
      if (*key == '#' || *key == ';' || !*key) continue;
      if (!(val = split_key(line))) {
        error_msg("'%s' not key=value");
        continue;
      }

      // Trim whitespace off key and value
      while (len && isspace(line[len-1])) line[--len] = 0;
      len = (val-line)-1;
      while (len && isspace(line[len-1])) line[--len] = 0;
      while (isspace(*val)) val++;;

      process_key(key, val);
    }
    fclose(fp);

  // Loop through arguments, displaying or assigning as appropriate
  } else for (args = toys.optargs; *args; args++) process_key(*args, 0);
}
