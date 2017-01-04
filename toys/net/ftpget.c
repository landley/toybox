/* ftpget.c - Fetch file(s) from ftp server
 *
 * Copyright 2016 Rob Landley <rob@landley.net>
 *
 * No standard for the command, but see https://www.ietf.org/rfc/rfc959.txt
 * TODO: local can be -
 * TEST: -g -s (when local and remote exist) -gc, -sc
 * zero length file

USE_FTPGET(NEWTOY(ftpget, "<2>3P:cp:u:vgslLmMdD[-gs][!gslLmMdD]", TOYFLAG_USR|TOYFLAG_BIN))
USE_FTPPUT(OLDTOY(ftpput, ftpget, TOYFLAG_USR|TOYFLAG_BIN))

config FTPGET
  bool "ftpget"
  default y
  help
    usage: ftpget [-cvgslLmMdD] [-P PORT] [-p PASSWORD] [-u USER] HOST [LOCAL] REMOTE

    Talk to ftp server. By default get REMOTE file via passive anonymous
    transfer, optionally saving under a LOCAL name. Can also send, list, etc.

    -c	Continue partial transfer
    -p	Use PORT instead of "21"
    -P	Use PASSWORD instead of "ftpget@"
    -u	Use USER instead of "anonymous"
    -v	Verbose

    Ways to interact with FTP server:
    -d	Delete file
    -D	Remove directory
    -g	Get file (default)
    -l	List directory
    -L	List (filenames only)
    -m	Move file on server from LOCAL to REMOTE
    -M	mkdir
    -s	Send file

config FTPPUT
  bool "ftpput"
  default y
  help
    An ftpget that defaults to -s instead of -g
*/

#define FOR_ftpget
#include "toys.h"

GLOBALS(
  char *user;
  char *port;
  char *password;
)

// we should get one line of data, but it may be in multiple chunks
int xread2line(int fd, char *buf, int len)
{
  int i, total = 0;

  len--;
  while (total<len && (i = xread(fd, buf, len-total))) {
    total += i;
    if (buf[total-1] == '\n') break;
  }
  if (total>=len) error_exit("overflow");
  while (total--)
    if (buf[total]=='\r' || buf[total]=='\n') buf[total] = 0;
    else break;
  if (toys.optflags & FLAG_v) fprintf(stderr, "%s\n", toybuf);

  return total+1;
}

static int ftp_check(int fd)
{
  int rc;

  xread2line(fd, toybuf, sizeof(toybuf));
  if (!sscanf(toybuf, "%d", &rc)) error_exit_raw(toybuf);

  return rc;
}

void ftpget_main(void)
{
  struct sockaddr_in6 si6;
  int fd, rc, i, port;
  socklen_t sl = sizeof(si6);
  char *s, *remote = toys.optargs[2];
  unsigned long long lenl, lenr;

  if (!(toys.optflags&(FLAG_v-1)))
    toys.optflags |= (toys.which->name[3]=='g') ? FLAG_g : FLAG_s;

  if (!TT.user) TT.user = "anonymous";
  if (!TT.password) TT.password = "ftpget@";
  if (!TT.port) TT.port = "21";
  if (!remote) remote = toys.optargs[1];

  // connect
  fd = xconnect(*toys.optargs, TT.port, 0, SOCK_STREAM, 0, AI_ADDRCONFIG);
  if (getpeername(fd, (void *)&si6, &sl)) perror_exit("getpeername");

  // Login
  if (ftp_check(fd) != 220) error_exit_raw(toybuf);
  dprintf(fd, "USER %s\r\n", TT.user);
  rc = ftp_check(fd);
  if (rc == 331) {
    dprintf(fd, "PASS %s\r\n", TT.password);
    rc = ftp_check(fd);
  }
  if (rc != 230) error_exit_raw(toybuf);

  // Only do passive binary transfers
  dprintf(fd, "TYPE I\r\n");
  ftp_check(fd);
  dprintf(fd, "PASV\r\n");

  // PASV means the server opens a port you connect to instead of the server
  // dialing back to the client. (Still insane, but less so.) So we need port #

  // PASV output is "227 PASV ok (x,x,x,x,p1,p2)" where x,x,x,x is the IP addr
  // (must match the server you're talking to???) and port is (256*p1)+p2
  s = 0;
  if (ftp_check(fd) == 227) for (s = toybuf; (s = strchr(s, ',')); s++) {
    int p1, got = 0;

    sscanf(s, ",%u,%u)%n", &p1, &port, &got);
    if (!got) continue;
    port += 256*p1;
    break;
  }
  if (!s || port<1 || port>65535) error_exit_raw(toybuf);
  si6.sin6_port = SWAP_BE16(port); // same field size/offset for v4 and v6
  port = xsocket(si6.sin6_family, SOCK_STREAM, 0);
  if (connect(port, (void *)&si6, sizeof(si6))) perror_exit("connect");

  if (toys.optflags & (FLAG_s|FLAG_g)) {
    int get = toys.optflags&FLAG_g, cnt = toys.optflags&FLAG_c;

    // RETR blocks until file data read from data port, so use SIZE to check
    // if file exists before creating local copy
    lenr = 0;
    dprintf(fd, "SIZE %s\r\n", remote);
    if (ftp_check(fd) == 213) sscanf(toybuf, "%*u %llu", &lenr);
    else if (get) error_exit("no %s", remote);

    // Open file for reading or writing
    i = xcreate(toys.optargs[1],
      get ? (cnt ? O_APPEND : O_TRUNC)|O_CREAT|O_WRONLY : O_RDONLY, 0666);
    lenl = fdlength(i);
    if (get) {
      if (cnt) {
        if (lenl>=lenr) goto done;
        dprintf(fd, "REST %llu\r\n", lenl);
        if (ftp_check(fd) != 350) error_exit_raw(toybuf);
      } else lenl = 0;

      dprintf(fd, "RETR %s\r\n", remote);
      lenl += xsendfile(port, i);
      if (ftp_check(fd) != 226) error_exit_raw(toybuf);
      if (lenl != lenr) error_exit("short read");
    } else if (toys.optflags & FLAG_s) {
      char *send = "STOR";

      if (cnt && lenr) {
        send = "APPE";
        xlseek(i, lenl, SEEK_SET);
      } else lenr = 0;
      dprintf(fd, "%s %s\r\n", send, remote);
      if (ftp_check(fd) != 150) error_exit_raw(toybuf);
      lenr += xsendfile(i, port);
      if (lenl != lenr) error_exit("short write %lld %lld", lenl, lenr);
      close(port);
    }
  }
  dprintf(fd, "QUIT\r\n");
  ftp_check(fd);

  // gslLmMdD
  // STOR - upload
  // APPE - append
  // REST - must immediately precede RETR or STOR
  // LIST - list directory contents
  // NLST - just list names, one per line
  // RNFR RNTO - rename from, rename to
  // DELE - delete
  // RMD - rmdir
  // MKD - mkdir

done:
  if (CFG_TOYBOX_FREE) {
    xclose(i);
    xclose(port);
    xclose(fd);
  }
}
