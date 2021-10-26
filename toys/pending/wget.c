/* wget.c - Simple downloader to get the resource file in HTTP server
 *
 * Copyright 2016 Lipi C.H. Lee <lipisoft@gmail.com>
 * Copyright 2021 Eric Molitor <eric@molitor.org>
 *

USE_WGET(NEWTOY(wget, "<1>1d(debug)O(output-document):", TOYFLAG_USR|TOYFLAG_BIN))

config WGET
  bool "wget"
  default n
  help
    Usage: wget [OPTION]... [URL]
    -d, --debug                 print lots of debugging information
    -O, --output-document=FILE  specify output filename

    examples:
      wget http://www.example.com
*/

// todo(emolitor): Add support for chunked encoding
// todo(emolitor): Add support for ftp

#define FOR_wget
#include "toys.h"

#define WGET_FILENAME "Content-Disposition: attachment; filename="
#define WGET_CHUNKED "transfer-encoding: chunked"
#define WGET_LOCATION "Location: "

GLOBALS(
  char *filename;
)

static char *wget_strncasestr(char *haystack, char *needle, size_t h_len)
{
  size_t n_len = strlen(needle);
  for (int i=0; i < (h_len - n_len); i++) {
    if (strncasecmp(haystack + i, needle, n_len) == 0) return haystack + i;
  }

  return NULL;
}

static char *wget_strncaseafter(char *haystack, char *needle, size_t h_len)
{
  char *result = wget_strncasestr(haystack, needle, h_len);
  if (result) result = result + strlen(needle);
  return result;
}

// get http info in URL
static void get_info(char *url, char **host, char **port, char **path)
{
  *host = strafter(url, "://");
  *path = strchr(*host, '/');

  if ((*path = strchr(*host, '/'))) {
    **path = '\0';
    *path = *path + 1;
  } else {
    *path = "";
  }

  if ( *host[0] == '[' && strchr(*host, ']') ) { // IPv6
    *port = strafter(*host, "]:");
    *host = *host + 1;
    strchr(*host, ']')[0] = '\0';
  } else { // IPv4
    if ((*port = strchr(*host, ':'))) {
      **port = '\0';
      *port = *port + 1;
    }
  }

  if (!*port) *port="80";
}

// connect to any IPv4 or IPv6 server
static int conn_svr(char *host, char *port)
{
  struct addrinfo *ai = xgetaddrinfo(host, port, AF_UNSPEC, SOCK_STREAM, 0,0);
  return xconnectany(ai);
}

// make HTTP request header field
static void mk_fld(char *name, char *value)
{
  strcat(toybuf, name);
  strcat(toybuf, ": ");
  strcat(toybuf, value);
  strcat(toybuf, "\r\n");
}

// get http response body starting address and its length
static char *get_body(ssize_t len, ssize_t *body_len)
{
  char *body = memmem(toybuf, len, "\r\n\r\n", 4);
  if (!body) error_exit("response too large");
  body += 4;
  *body_len = len - (body - toybuf);
  return body;
}

void wget_main(void)
{
  int sock, fd, redirects = 10;
  ssize_t len, body_len;
  char *body, *result, *rc, *r_str, *host, *port, *path, *f, *url, *redir;
  char ua[] = "toybox wget/" TOYBOX_VERSION;

  if(!toys.optargs[0]) help_exit("no URL");
  url = xstrdup(toys.optargs[0]);

  get_info(url, &host, &port, &path);

  for (;; redirects--) {
    sock = conn_svr(host, port);
    // compose HTTP request
    sprintf(toybuf, "GET /%s HTTP/1.0\r\n", path);
    mk_fld("Host", host);
    mk_fld("User-Agent", ua);
    mk_fld("Connection", "close");
    strcat(toybuf, "\r\n");

    // send the HTTP request
    xwrite(sock, toybuf, strlen(toybuf));

    // read HTTP response
    len = xread(sock, toybuf, sizeof(toybuf));
    body = get_body(len, &body_len);
    redir = wget_strncaseafter(toybuf, WGET_LOCATION, body - toybuf);
    result = strtok(toybuf, "\r");
    strtok(result, " ");
    rc = strtok(NULL, " ");
    r_str = strtok(NULL, " ");

    // HTTP res code check
    if (!strcmp(rc, "301") || !strcmp(rc, "302")) {
      if (redir) strchr(redir, '\r')[0] = '\0';
      else error_exit("Could not parse redirect URL");
      if (redirects < 0) error_exit("Too many redirects");

      if (FLAG(d))
        printf("Redirection: %s %s %s\n", rc, r_str, redir);

      free(url);
      url = xstrdup(redir);
      close(sock);
      get_info(url, &host, &port, &path);
    } else if (!strcmp(rc, "200")) break;
    else error_exit("res: %s(%s)", rc, r_str);
  }

  if (wget_strncaseafter(toybuf, WGET_CHUNKED, body - toybuf))
    error_exit("chunked encoding not supported");

  // Extract filename from content disposition
  f = wget_strncaseafter(toybuf, WGET_FILENAME, body - toybuf);
  if (!FLAG(O) && f) {
    strchr(f, '\r')[0] = '\0';
    TT.filename = f;
  }

  // Extract filename from path
  if (!TT.filename && strchr(path, '/')) TT.filename = getbasename(path);

  // If all else fails default to index.html
  if (!(TT.filename) || !(*TT.filename) ) TT.filename = "index.html";

  if (!FLAG(O) && !access(TT.filename, F_OK))
    error_exit("%s already exists", TT.filename);

  fd = xcreate(TT.filename, (O_WRONLY|O_CREAT|O_TRUNC), 0644);
  if (*body) xwrite(fd, body, body_len);
  while ((len = read(sock, toybuf, sizeof(toybuf))) > 0) xwrite(fd, toybuf, len);

  free(url);
}
