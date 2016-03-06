/* wget.c - Simple downloader to get the resource file in HTTP server
 *
 * Copyright 2016 Lipi C.H. Lee <lipisoft@gmail.com>
 *

USE_WGET(NEWTOY(wget, "f:", TOYFLAG_USR|TOYFLAG_BIN))

config WGET
  bool "wget"
  default n
  help
    usage: wget -f filename URL
    -f filename: specify the filename to be saved
    URL: HTTP uniform resource location and only HTTP, not HTTPS

    examples:
      wget -f index.html http://www.example.com
      wget -f sample.jpg http://www.example.com:8080/sample.jpg
*/

#define FOR_wget
#include "toys.h"

GLOBALS(
  char *filename;
)

// extract hostname from url
static unsigned get_hn(const char *url, char *hostname) {
  unsigned i;

  for (i = 0; url[i] != '\0' && url[i] != ':' && url[i] != '/'; i++) {
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

// get http infos in URL
static void get_info(const char *url, char* hostname, char *port, char *path) {
  unsigned i = 7, len;

  if (strncmp(url, "http://", i)) error_exit("only HTTP support");
  len = get_hn(url+i, hostname);
  i += len;

  // get port if exists
  if (url[i] == ':') {
    i++;
    i = get_port(url+i, port, i);
  } else strcpy(port, "80");

  // get uri in URL
  if (url[i] == '\0') strcpy(path, "/");
  else if (url[i] == '/') {
    if (strlen(url+i) < 1024) strcpy(path, url+i);
    else error_exit("too long path in URL");
  } else error_exit("wrong URL");
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
  int sock;
  FILE *fp;
  ssize_t len, body_len;
  char *body, *result, *rc, *r_str;
  char ua[18] = "toybox wget/", ver[6], hostname[1024], port[6], path[1024];

  // TODO extract filename to be saved from URL
  if (!(toys.optflags & FLAG_f)) help_exit("no filename");
  if (fopen(TT.filename, "r")) perror_exit("file already exists");

  if(!toys.optargs[0]) help_exit("no URL");
  get_info(toys.optargs[0], hostname, port, path);

  sock = conn_svr(hostname, port);

  // compose HTTP request
  sprintf(toybuf, "GET %s HTTP/1.1\r\n", path);
  mk_fld("Host", hostname);
  strncpy(ver, TOYBOX_VERSION, 5);
  strcat(ua, ver);
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
  result = strtok(toybuf, "\r");
  strtok(result, " ");
  rc = strtok(NULL, " ");
  r_str = strtok(NULL, " ");

  // HTTP res code check
  // TODO handle HTTP 302 Found(Redirection)
  if (strcmp(rc, "200")) error_exit("res: %s(%s)", rc, r_str);

  if (!(fp = fopen(TT.filename, "w"))) perror_exit("fopen error");
  if (fwrite(body, 1, body_len, fp) != body_len)
    error_exit("fwrite error");
  while ((len = read(sock, toybuf, 4096)) > 0)
    if (fwrite(toybuf, 1, len, fp) != len)
      error_exit("fwrite error");
  if (fclose(fp) == EOF) perror_exit("fclose error");
}
