/* modprobe.c - modprobe utility.
 *
 * Copyright 2012 Madhur Verma <mad.flexi@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard.

USE_MODPROBE(NEWTOY(modprobe, "alrqvsDb", TOYFLAG_SBIN))

config MODPROBE
  bool "modprobe"
  default n
  help
    usage: modprobe [-alrqvsDb] MODULE [symbol=value][...]

    modprobe utility - inserts modules and dependencies.

    -a  Load multiple MODULEs
    -l  List (MODULE is a pattern)
    -r  Remove MODULE (stacks) or do autoclean
    -q  Quiet
    -v  Verbose
    -s  Log to syslog
    -D  Show dependencies
    -b  Apply blacklist to module names too
*/
#define FOR_modprobe
#include "toys.h"
#include <sys/syscall.h>

GLOBALS(
  struct arg_list *probes;
  struct arg_list *dbase[256];
  char *cmdopts;
  int nudeps;
  uint8_t symreq;
  void (*dbg)(char *format, ...);
)

/* Note: if "#define DBASE_SIZE" modified, 
 * Please update GLOBALS dbase[256] accordingly.
 */
#define DBASE_SIZE  256
#define MODNAME_LEN 256

// Modules flag definations
#define MOD_ALOADED   0x0001
#define MOD_BLACKLIST 0x0002
#define MOD_FNDDEPMOD 0x0004
#define MOD_NDDEPS    0x0008

// dummy interface for debugging.
static void dummy(char *format, ...)
{
}

// Current probing modules info
struct module_s {
  uint32_t flags;
  char *cmdname, *name, *depent, *opts;
  struct arg_list *rnames, *dep;
};

// Converts path name FILE to module name.
static char *path2mod(char *file, char *mod)
{
  int i;
  char *from, *lslash;

  if (!file) return NULL;
  if (!mod) mod = xmalloc(MODNAME_LEN);
	
  lslash = strrchr(file, '/');
  if (!lslash || (lslash == file && !lslash[1])) from = file;
  else from = lslash + 1;
  
  for (i = 0; i < (MODNAME_LEN-1) && from[i] && from[i] != '.'; i++)
    mod[i] = (from[i] == '-') ? '_' : from[i];
  mod[i] = '\0';
  return mod;
}

// Add options in opts from toadd.
static char *add_opts(char *opts, char *toadd)
{
  if (toadd) {
    int optlen = 0;

    if (opts) optlen = strlen(opts);
    opts = xrealloc(opts, optlen + strlen(toadd) + 2);
    sprintf(opts + optlen, " %s", toadd);
  }
  return opts;
}

// Remove first element from the list and return it.
static void *llist_popme(struct arg_list **head)
{
  char *data = NULL;
  struct arg_list *temp = *head;

  if (temp) {
    data = temp->arg;
    *head = temp->next;
    free(temp);
  }
  return data;
}

// Add new node at the beginning of the list.
static void llist_add(struct arg_list **old, void *data)
{
  struct arg_list *new = xmalloc(sizeof(struct arg_list));

  new->arg = (char*)data;
  new->next = *old;
  *old = new;
}

// Add new node at tail of list.
static void llist_add_tail(struct arg_list **head, void *data)
{
  while (*head) head = &(*head)->next;
  *head = xzalloc(sizeof(struct arg_list));
  (*head)->arg = (char*)data;
}

// Reverse list order.
static struct arg_list *llist_rev(struct arg_list *list)
{
  struct arg_list *rev = NULL;

  while (list) {
    struct arg_list *next = list->next;

    list->next = rev;
    rev = list;
    list = next;
  }
  return rev;
}

/*
 * Returns struct module_s from the data base if found, NULL otherwise.
 * if add - create module entry, add it to data base and return the same mod.
 */
static struct module_s *get_mod(char *mod, uint8_t add)
{
  char name[MODNAME_LEN];
  struct module_s *modentry;
  struct arg_list *temp;
  unsigned i, hash = 0;

  path2mod(mod, name);
  for (i = 0; name[i]; i++) hash = ((hash*31) + hash) + name[i];
  hash %= DBASE_SIZE;
  for (temp = TT.dbase[hash]; temp; temp = temp->next) {
    modentry = (struct module_s *) temp->arg;
    if (!strcmp(modentry->name, name)) return modentry;
  }
  if (!add) return NULL;
  modentry = xzalloc(sizeof(*modentry));
  modentry->name = xstrdup(name);
  llist_add(&TT.dbase[hash], modentry);
  return modentry;
}

/*
 * Read a line from file with \ continuation and escape commented line.
 * Return the line in allocated string (*li)
 */
static int read_line(FILE *fl, char **li)
{
  char *nxtline = NULL, *line;
  int len, nxtlen, linelen, nxtlinelen;

  while (1) {
    line = NULL;
    linelen = nxtlinelen = 0;
    len = getline(&line, (size_t*)&linelen, fl);
    if (len <= 0) {
      free(line);
      return len;
    }
    // checking for commented lines.
    if (line[0] != '#') break;
    free(line);
  }
  for (;;) {
    if (line[len - 1] == '\n') len--;
    if (!len) { 
      free(line);
      return len;
    } else if (line[len - 1] != '\\') break;
    
    len--;
    nxtlen = getline(&nxtline, (size_t*)&nxtlinelen, fl);
    if (nxtlen <= 0) break;
    if (linelen < len + nxtlen + 1) {
      linelen = len + nxtlen + 1;
      line = xrealloc(line, linelen);
    }
    memcpy(&line[len], nxtline, nxtlen);
    len += nxtlen;
  }
  line[len] = '\0';
  *li = xstrdup(line);
  free(line);
  if (nxtline) free(nxtline);
  return len;
}

/*
 * Action to be taken on all config files in default directories
 * checks for aliases, options, install, remove and blacklist
 */
static int config_action(struct dirtree *node)
{
  FILE *fc;
  char *filename, *tokens[3], *line, *linecp;
  struct module_s *modent;
  int tcount = 0;

  if (!dirtree_notdotdot(node)) return 0;
  if (S_ISDIR(node->st.st_mode)) return DIRTREE_RECURSE;

  if (!S_ISREG(node->st.st_mode)) return 0; // process only regular file
  filename = dirtree_path(node, NULL);
  if (!(fc = fopen(filename, "r"))) {
    free(filename);
    return 0;
  }
  for (line = linecp = NULL; read_line(fc, &line) > 0; 
      free(line), free(linecp), line = linecp = NULL) {
    char *tk = NULL;

    if (!strlen(line)) continue; 
    linecp = xstrdup(line);
    for (tk = strtok(linecp, "# \t"), tcount = 0; tk;
        tk = strtok(NULL, "# \t"), tcount++) {
      tokens[tcount] = tk;
      if (tcount == 2) {
        tokens[2] = line + strlen(tokens[0]) + strlen(tokens[1]) + 2;
        break;
      }
    }
    if (!tk) continue; 
    // process the tokens[0] contains first word of config line.
    if (!strcmp(tokens[0], "alias")) {
      struct arg_list *temp;
      char aliase[MODNAME_LEN], *realname;

      if (!tokens[2]) continue;
      path2mod(tokens[1], aliase);
      for (temp = TT.probes; temp; temp = temp->next) {
        modent = (struct module_s *) temp->arg;
        if (fnmatch(aliase, modent->name, 0)) continue;
        realname = path2mod(tokens[2], NULL);
        llist_add(&modent->rnames, realname);
        if (modent->flags & MOD_NDDEPS) {
          modent->flags &= ~MOD_NDDEPS;
          TT.nudeps--;
        }
        modent = get_mod(realname, 1);
        if (!(modent->flags & MOD_NDDEPS)) {
          modent->flags |= MOD_NDDEPS;
          TT.nudeps++;
        }
      }
    } else if (!strcmp(tokens[0], "options")) {
      if (!tokens[2]) continue;
      modent = get_mod(tokens[1], 1);
      modent->opts = add_opts(modent->opts, tokens[2]);
    } else if (!strcmp(tokens[0], "include"))
      dirtree_read(tokens[1], config_action);
    else if (!strcmp(tokens[0], "blacklist"))
      get_mod(tokens[1], 1)->flags |= MOD_BLACKLIST;
    else if (!strcmp(tokens[0], "install")) continue;
    else if (!strcmp(tokens[0], "remove")) continue;
    else error_msg("Invalid option %s found in file %s", tokens[0], filename);
  }
  fclose(fc);
  free(filename);
  return 0;
}

// Show matched modules else return -1 on failure.
static int depmode_read_entry(char *cmdname)
{
  char *line;
  int ret = -1;
  FILE *fe = xfopen("modules.dep", "r");

  while (read_line(fe, &line) > 0) {
    char *tmp = strchr(line, ':');

    if (tmp) {
      *tmp = '\0';
     char *name = basename(line);

      tmp = strchr(name, '.');
      if (tmp) *tmp = '\0';
      if (!cmdname || !fnmatch(cmdname, name, 0)) {
        if (tmp) *tmp = '.';
        TT.dbg("%s\n", line);
        ret = 0;
      }
    }
    free(line);
  }
  fclose(fe);
  return ret;
}

// Finds dependencies for modules from the modules.dep file.
static void find_dep(void)
{
  char *line = NULL;
  struct module_s *mod;
  FILE *fe = xfopen("modules.dep", "r");

  for (; read_line(fe, &line) > 0; free(line)) {
    char *tmp = strchr(line, ':');

    if (tmp) {
      *tmp = '\0';
      mod = get_mod(line, 0);
      if (!mod) continue;
      if ((mod->flags & MOD_ALOADED) &&
          !(toys.optflags & (FLAG_r | FLAG_D))) continue;
      
      mod->flags |= MOD_FNDDEPMOD;
      if ((mod->flags & MOD_NDDEPS) && (!mod->dep)) {
        TT.nudeps--;
        llist_add(&mod->dep, xstrdup(line));
        tmp++;
        if (*tmp) {
          char *tok;

          while ((tok = strsep(&tmp, " \t"))) {
            if (!*tok) continue;
            llist_add_tail(&mod->dep, xstrdup(tok));
          }
        }
      }
    }
  }
  fclose(fe);
}

// Remove a module from the Linux Kernel. if !modules does auto remove.
static int rm_mod(char *modules, uint32_t flags)
{
  if (modules) {
    int len = strlen(modules);

    if (len > 3 && !strcmp(modules+len-3, ".ko" )) modules[len-3] = 0;
  }

  errno = 0;
  syscall(__NR_delete_module, modules, flags ? flags : O_NONBLOCK|O_EXCL);

  return errno;
}

// Insert module same as insmod implementation.
static int ins_mod(char *modules, char *flags)
{
  char *buf = NULL;
  int len, res;
  int fd = xopen(modules, O_RDONLY);

  len = fdlength(fd);
  buf = xmalloc(len);
  xreadall(fd, buf, len);
  xclose(fd);

  while (flags && strlen(toybuf) + strlen(flags) + 2 < sizeof(toybuf)) {
    strcat(toybuf, flags);
    strcat(toybuf, " ");
  }
  res = syscall(__NR_init_module, buf, len, toybuf);
  if (CFG_TOYBOX_FREE && buf != toybuf) free(buf);
  if (res) perror_exit("failed to load %s ", toys.optargs[0]);
  return res;
}

// Add module in probes list, if not loaded.
static void add_mod(char *name)
{
  struct module_s *mod = get_mod(name, 1);

  if (!(toys.optflags & (FLAG_r | FLAG_D)) && (mod->flags & MOD_ALOADED)) {
    TT.dbg("skipping %s, it is already loaded\n", name);
    return;
  }
  TT.dbg("queuing %s\n", name);
  mod->cmdname = name;
  mod->flags |= MOD_NDDEPS;
  llist_add_tail(&TT.probes, mod);
  TT.nudeps++;
  if (!strncmp(mod->name, "symbol:", 7)) TT.symreq = 1;
}

// Parse cmdline options suplied for module.
static char *add_cmdopt(char **argv)
{
  char *opt = xzalloc(1);
  int lopt = 0;

  while (*++argv) {
    char *fmt, *var, *val;

    var = *argv;
    opt = xrealloc(opt, lopt + 2 + strlen(var) + 2);
    // check for key=val or key = val.
    fmt = "%.*s%s ";
    for (val = var; *val && *val != '='; val++);
    if (*val && strchr(++val, ' ')) fmt = "%.*s\"%s\" ";
    lopt += sprintf(opt + lopt, fmt, (int) (val - var), var, val);
  }
  return opt;
}

// Probes a single module and loads all its dependencies.
static int go_probe(struct module_s *m)
{
  int rc = 0, first = 1;

  if (!(m->flags & MOD_FNDDEPMOD)) {
    if (!(toys.optflags & FLAG_s))
      error_msg("module %s not found in modules.dep", m->name);
    return -ENOENT;
  }
  TT.dbg("go_prob'ing %s\n", m->name);
  if (!(toys.optflags & FLAG_r)) m->dep = llist_rev(m->dep);
  
  while (m->dep) {
    struct module_s *m2;
    char *fn, *options;

    rc = 0;
    fn = llist_popme(&m->dep);
    m2 = get_mod(fn, 1);
    // are we removing ?
    if (toys.optflags & FLAG_r) {
      if (m2->flags & MOD_ALOADED) {
        if ((rc = rm_mod(m2->name, O_EXCL))) {
          if (first) {
            perror_msg("can't unload module %s", m2->name);
            break;
          }
        } else m2->flags &= ~MOD_ALOADED;
      }
      first = 0;
      continue;
    }
    options = m2->opts;
    m2->opts = NULL;
    if (m == m2) options = add_opts(options, TT.cmdopts);

    // are we only checking dependencies ?
    if (toys.optflags & FLAG_D) {
      TT.dbg(options ? "insmod %s %s\n" : "insmod %s\n", fn, options);
      if (options) free(options);
      continue;
    }
    if (m2->flags & MOD_ALOADED) {
      TT.dbg("%s is already loaded, skipping\n", fn);
      if (options) free(options);
      continue;
    }
    // none of above is true insert the module.
    rc = ins_mod(fn, options);
    TT.dbg("loaded %s '%s', rc:%d\n", fn, options, rc);
    if (rc == EEXIST) rc = 0;
    if (options) free(options);
    if (rc) {
      perror_msg("can't load module %s (%s)", m2->name, fn);
      break;
    }
    m2->flags |= MOD_ALOADED;
  }
  return rc;
}

void modprobe_main(void)
{
  struct utsname uts;
  char **argv = toys.optargs, *procline = NULL;
  FILE *fs;
  struct module_s *module;
  unsigned flags = toys.optflags;

  TT.dbg = (flags & FLAG_v) ? xprintf : dummy;

  if ((toys.optc < 1) && (((flags & FLAG_r) && (flags & FLAG_l))
        ||(!((flags & FLAG_r)||(flags & FLAG_l)))))
  {
	  toys.exithelp++;
	  error_exit("bad syntax");
  }
  // Check for -r flag without arg if yes then do auto remove.
  if ((flags & FLAG_r) && !toys.optc) {
    if (rm_mod(NULL, O_NONBLOCK | O_EXCL)) perror_exit("rmmod");
    return;
  }

  // change directory to /lib/modules/<release>/ 
  xchdir("/lib/modules");
  uname(&uts);
  xchdir(uts.release);

  // modules.dep processing for dependency check.
  if (flags & FLAG_l) {
    if (depmode_read_entry(toys.optargs[0])) error_exit("no module found.");
    return;
  }
  // Read /proc/modules to get loaded modules.
  fs = xfopen("/proc/modules", "r");
  
  while (read_line(fs, &procline) > 0) {
    *(strchr(procline, ' ')) = '\0';
    get_mod(procline, 1)->flags = MOD_ALOADED;
    free(procline);
    procline = NULL;
  }
  fclose(fs);
  if ((flags & FLAG_a) || (flags & FLAG_r)) {
    do {
      add_mod(*argv++);
    } while (*argv);
  } else {
    add_mod(argv[0]);
    TT.cmdopts = add_cmdopt(argv);
  }
  if (!TT.probes) {
    TT.dbg("All modules loaded\n");
    return;
  }
  dirtree_read("/etc/modprobe.conf", config_action);
  dirtree_read("/etc/modprobe.d", config_action);
  if (TT.symreq) dirtree_read("modules.symbols", config_action);
  if (TT.nudeps) dirtree_read("modules.alias", config_action);
  find_dep();
  while ((module = llist_popme(&TT.probes))) {
    if (!module->rnames) {
      TT.dbg("probing by module name\n");
      /* This is not an alias. Literal names are blacklisted
       * only if '-b' is given.
       */
      if (!(flags & FLAG_b) || !(module->flags & MOD_BLACKLIST))
        go_probe(module);
      continue;
    }
    do { // Probe all real names for the alias.
      char *real = ((struct arg_list*)llist_pop(&module->rnames))->arg;
      struct module_s *m2 = get_mod(real, 0);
      
      TT.dbg("probing alias %s by realname %s\n", module->name, real);
      if (!m2) continue;
      if (!(m2->flags & MOD_BLACKLIST) 
          && (!(m2->flags & MOD_ALOADED) || (flags & (FLAG_r | FLAG_D))))
        go_probe(m2);
      free(real);
    } while (module->rnames);
  }
}
