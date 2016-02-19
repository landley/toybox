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

#define HN_LEN 128 // HOSTNAME MAX LENGTH 
#define PATH_LEN 256 // PATH MAX LENGTH

struct httpinfo {
  char hostname[HN_LEN];
  char port[6];      // MAX port value: 65535
  char path[PATH_LEN];
};

// extract hostname from url
static unsigned int get_hn(char *url, char *hn) {
  unsigned int i;

  for (i = 0; url[i] != '\0' && url[i] != ':' && url[i] != '/'; i++) {
    if(i >= HN_LEN)
      error_exit("The hostname's length is lower than %d.", HN_LEN);
    hn[i] = url[i];
  }
  hn[i] = '\0';

  return i;
}

// extract port number
static void get_port(char *url, char *port, unsigned int *url_i) {
  unsigned int i;

  for (i = 0; url[i] != '\0' && url[i] != '/'; i++, (*url_i)++) {
    if('0' <= url[i] && url[i] <= '9') port[i] = url[i];
    else error_exit("Port is invalid");
  }
  if(i <= 6) port[i] = '\0';
  else error_exit("Port number is too long");
}

// get http infos in URL
static void get_info(struct httpinfo *hi, char *url) {
  unsigned int i = 7, len;

  if (strncmp(url, "http://", i)) error_exit("Only HTTP can be supported.");
  len = get_hn(url+i, hi->hostname);
  i += len;

  // get port if exists
  if (url[i] == ':') {
    i++;
    get_port(url+i, hi->port, &i);
  } else strcpy(hi->port, "80");

  // get uri in URL
  if (url[i] == '\0') strcpy(hi->path, "/");
  else if (url[i] == '/') {
    if (strlen(url+i) < PATH_LEN) strcpy(hi->path, url+i);
    else error_exit("The URL path's length is less than %d.", PATH_LEN);
  } else error_exit("The URL is NOT valid.");
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
      perror_msg("Socket Error");
      continue;
    }
    if (connect(sock, rp->ai_addr, rp->ai_addrlen) != -1)
      break; // succeed in connecting to any server IP 
    else perror_msg("Connect Error");
    close(sock);
  }
  freeaddrinfo(result);
  if(!rp) error_exit("Can not connect to HTTP server");

  return sock;
}

// make HTTP request header field
static void mk_fld(char *name, char *value) {
  strcat(toybuf, name);
  strcat(toybuf, ": ");
  strcat(toybuf, value);
  strcat(toybuf, "\r\n");
}

// make http request
static void mk_rq(char *path) {
  strcpy(toybuf, "GET ");
  strcat(toybuf, path);
  strcat(toybuf, " HTTP/1.1\r\n");
}

// get http response body starting address and its length
static char *get_body(size_t len, size_t *body_len) {
  unsigned int i;

  for (i = 0; i < len-4; i++)
    if (!strncmp(toybuf+i, "\r\n\r\n", 4)) break;

  *body_len = len-i-4;
  return toybuf+i+4;
}

void wget_main(void)
{
  int sock;
  struct httpinfo hi;
  FILE *fp;
  size_t len, body_len;
  char *body, *result, *rc, *r_str, ua[18] = "toybox wget/", ver[6];

  // TODO extract filename to be saved from URL
  if (!(toys.optflags & FLAG_f))
    help_exit("The filename to be saved should be needed.");
  if (fopen(TT.filename, "r"))
    error_exit("The file(%s) you specified already exists.", TT.filename);

  if(!toys.optargs[0]) help_exit("The URL should be specified.");
  get_info(&hi, toys.optargs[0]);

  sock = conn_svr(hi.hostname, hi.port);

  // compose HTTP request
  mk_rq(hi.path);
  mk_fld("Host", hi.hostname);
  strncpy(ver, TOYBOX_VERSION, 5);
  strcat(ua, ver);
  mk_fld("User-Agent", ua); 
  mk_fld("Connection", "close");
  strcat(toybuf, "\r\n");

  // send the HTTP request
  len = strlen(toybuf);
  if (write(sock, toybuf, len) != len) perror_exit("HTTP GET failed.");

  // read HTTP response
  if ((len = read(sock, toybuf, 4096)) == -1)
    perror_exit("HTTP response failed.");
  if (!strstr(toybuf, "\r\n\r\n"))
    error_exit("HTTP response header is too long.");
  body = get_body(len, &body_len);
  result = strtok(toybuf, "\r");
  strtok(result, " ");
  rc = strtok(NULL, " ");
  r_str = strtok(NULL, " ");

  // HTTP res code check
  // TODO handle HTTP 302 Found(Redirection)
  if (strcmp(rc, "200")) error_exit("HTTP response: %s(%s).", rc, r_str);

  if (!(fp = fopen(TT.filename, "w"))) perror_exit("File write error");
  if (fwrite(body, sizeof(char), body_len, fp) != body_len)
    error_exit("File write is not successful.");
  while ((len = read(sock, toybuf, 4096)) > 0)
    if (fwrite(toybuf, sizeof(char), len, fp) != len)
      error_exit("File write is not successful.");
  if (fclose(fp) == EOF) perror_exit("File Close Error");
}
