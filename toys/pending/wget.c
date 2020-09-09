/* wget.c - Simple downloader to get the resource file in HTTP server
 *
 * Copyright 2016 Lipi C.H. Lee <lipisoft@gmail.com>
 *

USE_WGET(NEWTOY(wget, "(no-check-certificate)O:", TOYFLAG_USR|TOYFLAG_BIN))

config WGET
  bool "wget"
  default n
  help
    usage: wget -O filename URL
    -O filename: specify output filename
    URL: uniform resource location, FTP/HTTP only, not HTTPS

    examples:
      wget -O index.html http://www.example.com
      wget -O sample.jpg ftp://ftp.example.com:21/sample.jpg
*/

#define FOR_wget
#include "toys.h"

GLOBALS(
  char *filename;
)

// extract hostname and port from url
static unsigned get_hn(const char *url, char *hostname) {
  unsigned i;

  for (i = 0; url[i] != '\0' && url[i] != '/'; i++) {
    if(i >= 1024) error_exit("too long hostname in URL");
    hostname[i] = url[i];
  }
  hostname[i] = '\0';

  return i;
}

// extract port number
static unsigned get_port(const char *url, char *port, unsigned url_i) {
  unsigned i;
  for (i = 0; url[i] != '\0' && url[i] != '/'; i++, url_i++) {
    if('0' <= url[i] && url[i] <= '9') port[i] = url[i];
    else error_exit("wrong decimal port number");
  }
  if(i <= 6) port[i] = '\0';
  else error_exit("too long port number");

  return url_i;
}

static void strip_v6_brackets(char* hostname) {
  size_t len = strlen(hostname);
  if (len > 1023) {
    error_exit("hostname too long, %d bytes\n", len);
  }
  char * closing_bracket = strchr(hostname, ']');
  if (closing_bracket && closing_bracket == hostname + len - 1) {
    if (strchr(hostname, '[') == hostname) {
      hostname[len-1] = 0;
      memmove(hostname, hostname + 1, len - 1);
    }
  }
}

// get http infos in URL
static void get_info(const char *url, char* hostname, char *port, char *path) {
  unsigned i = 7, len;
  char ftp = !strncmp(url, "ftp://", 6);

  if (ftp) i--;
  else if (strncmp(url, "http://", i)) error_exit("only FTP/HTTP support");
  len = get_hn(url+i, hostname);
  i += len;

  // `hostname` now contains `host:port`, where host can be any of: a raw IPv4
  // address; a bracketed, raw IPv6 address, or a hostname. Extract port, if it exists,
  // by searching for the last ':' in the hostname string.
  char *port_delim = strrchr(hostname, ':');
  char use_default_port = 1;
  if (port_delim) {
    // Found a colon; is there a closing bracket after it? If so,
    // then this colon was in the middle of a bracketed IPv6 address
    if (!strchr(port_delim, ']')) {
      // No closing bracket; this is a real port
      use_default_port = 0;
      get_port(port_delim + 1, port, 0);

      // Mark the new end of the hostname string
      *port_delim = 0;
    }
  }

  if (use_default_port) {
    strcpy(port, "80");
  }

  // This is a NOP if hostname is not a bracketed IPv6 address
  strip_v6_brackets(hostname);

  // get uri in URL
  if (url[i] == '\0') strcpy(path, "/");
  else if (url[i] == '/') {
    if (strlen(url+i) < 1024) strcpy(path, url+i);
    else error_exit("too long path in URL");
  } else error_exit("wrong URL");

  if (ftp) xexec((char *[]){"ftpget", hostname, TT.filename, path, 0});
}

// connect to any IPv4 or IPv6 server
static int conn_svr(const char *hostname, const char *port) {
  struct addrinfo hints, *result, *rp;
  int sock;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  if ((errno = getaddrinfo(hostname, port, &hints, &result)))
    error_exit("getaddrinfo: %s", gai_strerror(errno));

  // try all address list(IPv4 or IPv6) until success
  for (rp = result; rp; rp = rp->ai_next) {
    if ((sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol))
        == -1) {
      perror_msg("socket error");
      continue;
    }
    if (connect(sock, rp->ai_addr, rp->ai_addrlen) != -1)
      break; // succeed in connecting to any server IP
    else perror_msg("connect error");
    close(sock);
  }
  freeaddrinfo(result);
  if(!rp) error_exit("can't connect");

  return sock;
}

// make HTTP request header field
static void mk_fld(char *name, char *value) {
  strcat(toybuf, name);
  strcat(toybuf, ": ");
  strcat(toybuf, value);
  strcat(toybuf, "\r\n");
}

// get http response body starting address and its length
static char *get_body(ssize_t len, ssize_t *body_len) {
  int i;

  for (i = 0; i < len-4; i++)
    if (!strncmp(toybuf+i, "\r\n\r\n", 4)) break;

  *body_len = len - i - 4;
  return toybuf+i+4;
}

void wget_main(void)
{
  int sock, redirects = 10;
  FILE *fp;
  ssize_t len, body_len;
  char *body, *result, *rc, *r_str, *redir_loc = 0;
  char ua[] = "toybox wget/" TOYBOX_VERSION, hostname[1024], port[6], path[1024];

  // TODO extract filename to be saved from URL
  if (!(toys.optflags & FLAG_O)) help_exit("no filename");
  if (fopen(TT.filename, "r")) error_exit("'%s' already exists", TT.filename);

  if(!toys.optargs[0]) help_exit("no URL");
  get_info(toys.optargs[0], hostname, port, path);

  for (;; redirects--) {
    sock = conn_svr(hostname, port);
    // compose HTTP request
    sprintf(toybuf, "GET %s HTTP/1.1\r\n", path);
    mk_fld("Host", hostname);
    mk_fld("User-Agent", ua);
    mk_fld("Connection", "close");
    strcat(toybuf, "\r\n");

    // send the HTTP request
    len = strlen(toybuf);
    if (write(sock, toybuf, len) != len) perror_exit("write error");

    // read HTTP response
    if ((len = read(sock, toybuf, 4096)) == -1) perror_exit("read error");
    if (!strstr(toybuf, "\r\n\r\n")) error_exit("too long HTTP response");
    body = get_body(len, &body_len);
    redir_loc = strstr(toybuf, "Location: ");
    result = strtok(toybuf, "\r");
    strtok(result, " ");
    rc = strtok(NULL, " ");
    r_str = strtok(NULL, " ");

    // HTTP res code check
    if (!strcmp(rc, "301") || !strcmp(rc, "302")) {
      char* eol = 0;
      if ((eol = strchr(redir_loc, '\r')) > 0) *eol = 0;
      else if (redir_loc) error_exit("Could not parse redirect URL");
      if (redirects < 0) error_exit("Too many redirects");

      printf("Redirection: %s %s \n", rc, r_str);
      printf("%s \n", redir_loc);
      redir_loc = redir_loc+strlen("Location: ");
      close(sock);
      get_info(redir_loc, hostname, port, path);
    } else if (!strcmp(rc, "200")) break;
    else error_exit("res: %s(%s)", rc, r_str);
  }


  if (!(fp = fopen(TT.filename, "w"))) perror_exit("fopen error");
  if (fwrite(body, 1, body_len, fp) != body_len)
    error_exit("fwrite error");
  while ((len = read(sock, toybuf, 4096)) > 0)
    if (fwrite(toybuf, 1, len, fp) != len)
      error_exit("fwrite error");
  if (fclose(fp) == EOF) perror_exit("fclose error");
}
