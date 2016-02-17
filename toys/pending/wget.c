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

    Simple downloader to get the resource file in HTTP server

*/

#define FOR_wget
#include "toys.h"

GLOBALS(
  char *filename;
)

#define TOYBOX_VERSION "0.7.0"
#define HN_LEN 128 // MAX HOSTNAME LENGTH
#define PATH_LEN 256 // MAX PATH LENGTH

struct httpinfo {
  char hostname[HN_LEN];
  char port[6];      // MAX port string: "65535"
  char path[PATH_LEN];
};

// get the hostname's size in URL
static unsigned int get_hnsz(char *url) {
  unsigned int i;

  for (i=0; url[i] != '\0' && url[i] != ':' && url[i] != '/'; i++)
    ;

  return i;
}

// get hostname
static void get_hn(char *url, char *hn) {
  int i;

  for (i = 0; url[i] != '\0' && url[i] != ':' && url[i] != '/'; i++)
    hn[i] = url[i];
  hn[i] = '\0';
}

// get port number
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
  unsigned int i, j, len;
  char *path, *port;

  if (strncmp(url, "http://", 7)) error_exit("Only HTTP can be supported.");
  if ((len = get_hnsz(url+7)) >= HN_SZ)
    error_exit("Hostname length is lower than 128");
  get_hn(url+7, hi->hn);
  i += len;

  // get port if exists
  if (url[i] == ':') get_port(url+i+1, hi->port, &i);
  else strcpy(hi->port, "80");

  // get uri in URL
  if (url[i] == '\0') hi->uri = "/";
  else if (url[i] == '/') hi->uri = url+i;
  else error_exit("URL is NOT valid.");
}

// merge UA profile with toybox version info
static char *make_ua(char *name, char* ver) {
  unsigned int name_len, ver_len;
  char *ua_str;

  name_len = strlen(name);
  ver_len = strlen(ver);

  // 4 in the end of below line means " \r\n" + NULL
  ua_str = xmalloc(sizeof(char) * (name_len + ver_len + 4));
  strcpy(ua_str, name);
  strcat(ua_str, ver);

  return ua_str;
}

// connect to any IPv4 or IPv6 server
static int conn_svr(const char *hostname, const char *port) {
  struct addrinfo hints, *result, *rp;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  if ((errno = getaddrinfo(hostname, port, &hints, &result)))
    error_exit("getaddrinfo: %s", gai_strerror(errno));

  // try all address list(IPv4 or IPv6) until success
  for (rp = result; rp; rp = rp->ai_next) {
    if ((sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol))
        == -1) {
      perror_msg("Socket Error");
      continue;
    }
    if (connect(sock_fd, rp->ai_addr, rp->ai_addrlen) != -1)
      break; // succeed in connecting to any server IP 
    else perror_msg("Connect Error");
    close(sock_fd);
  }
  freeaddrinfo(result);
  if(!rp) error_exit("Can not connect to HTTP server");

  return sock_fd;
}

// make HTTP request header field
static void mk_fld(char *name, char *value) {
  strcat(toybuf, name);
  strcat(field, ": ");
  strcat(field, value);
  strcat(filed, "\r\n");
}

// make http request
static void mk_rq(char *path) {
  strcpy(toybuf, "GET ");
  strcat(toybuf, path);
  strcat(toybuf, " HTTP/1.1\r\n");
}

void wget_main(void)
{
  int sock_fd;
  char *req, *hn, *full_ua, *ua, *conn_type, *hdr;
  char res[10];
  unsigned int len;
  struct httpinfo hi;
  FILE *fp;

  if (!(toys.optflags & FLAG_f)) help_exit("Filename should be needed.");
  if (fopen(TT.filename, "r")) error_exit("The file already exists.");

  if(!toys.optargs[0]) help_exit("URL should be needed");
  get_info(&hi, toys.optargs[0]);

  sock_fd = conn_svr(hi.hostname, hi.port);

  // make HTTP request
  mk_rq(hi.path);

  // make HTTP header fields
  mk_fld("Host", hi.hostname);
  full_ua = make_ua(usr_agnt, TOYBOX_VERSION);
  mk_fld("User-Agent", full_ua); 
  mk_fld("Connection", "close");
  strcat(toybuf, "\r\n");

  // send the request and HTTP header
  if (write(sock_fd, toybuf, strlen(toybuf)) != len) {
    perror_msg("HTTP GET failed.");
    goto cleanup;
  }

  // read HTTP response
  if ((len = read(sock_fd, res, 3)) != 3)
    error_exit("HTTP response read error");
  if (strncmp(res, "200", 3))
  fp = fopen(TT.filename, "w");

  while ((len = read(sock_fd, res, 10)) > 0)
    if (pos = skip_hdr(res, &len)) break;
  for (i = 0; i < len; i++) fputc(res[i], fp);
  while ((len = read(sock_fd, res, 10)) > 0)
    for (i = 0; i < len; i++)
      fputc(res[i], fp);
  fputc(EOF, fp);

  close(sock_fd);
  if (fclose(fp)) perror_msg("File Close Error");

cleanup:
  if (full_ua) free(full_ua);
  if (hi.hostname) free(hi.hostname);
  if (hi.path) free(hi.path);
}
