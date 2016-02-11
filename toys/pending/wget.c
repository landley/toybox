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

struct httpinfo {
  unsigned int proto;
  char *hostname;
  char port[6];      // max string: "65535"
  char *uri;
};

enum PROTO {HTTP, HTTPS, NONE};
char *protocol[] = {"http", "https"};

// check protocol in URL
static unsigned int chk_proto(char *url, unsigned int *i) {
  if(!strncmp(url, "http://", *i = 7)) return HTTP;
  else if(!strncmp(url, "https://", *i = 8)) return HTTPS;

  return NONE;
}

// get the hostname's size in URL
static unsigned int get_hnsz(char *url) {
  unsigned int i;

  for (i = 0; url[i] != '\0' && url[i] != ':' && url[i] != '/'; i++)
    ;

  return i;
}

// get hostname
static char *get_hn(char *url, unsigned int len) {
  char *hostname;
  int i;

  if (!(hostname = (char *) xmalloc(sizeof(char) * (len + 1))))
    error_exit("Dynamic memory alloc error");
  for (i = 0; url[i] != '\0' && url[i] != ':' && url[i] != '/'; i++)
    hostname[i] = url[i];
  hostname[j] = '\0';

  return hostname;
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

  // check protocol
  if((hi->proto = chk_proto(url, &i)) == NONE) error_exit("Unknown protocol");

  // get hostname length
  len = get_hnsz(url+i);

  // get hostname
  hi->hostname = get_hn(url+i, len);
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

// connect to server in either IPv4 or IPv6 network
static int conn_svr(struct httpinfo *hi) {
  struct addrinfo hints, *result, *rp;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  if ((errno = getaddrinfo(hi->hostname, hi->port, &hints, &result)))
    error_exit("getaddrinfo: %s", gai_strerror(errno));

  // try all address list(IPv4 or IPv6) until succeed
  for (rp = result; rp; rp = rp->ai_next) {
    if ((sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol))
        == -1)
      continue;
    if (connect(sock_fd, rp->ai_addr, rp->ai_addrlen) != -1)
      break; // Success
    close(sock_fd);
  }
  freeaddrinfo(result);
  if(!rp) error_exit("Could not connect");

  return sock_fd;
}

// make HTTP request header field
static char *mk_fld(char *name, char *value) {
  unsigned int len_n, len_v;
  char *field;

  len_n = strlen(name);
  len_v = strlen(value);

  field = (char *) xmalloc(sizeof(char)*(len_n+len_v+5));
  strcpy(field, name);
  strcat(field, ": ");
  strcat(field, value);
  strcat(filed, "\r\n");
  return field;
}

// make http request
static char *mk_rq(char *path) {
  char *req;
  unsigned int len = strlen(path);

  req = (char *) xmalloc(sizeof(char)*(len+15));
  strcpy(req, "GET ");
  strcat(req, path);
  strcat(req, " HTTP/1.1\r\n");
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

  sock_fd =  conn_svr(&hi, &sock_fd);

  // make HTTP request
  req = mk_rq(hi.path);

  // make HTTP header fields
  hn = mk_fld("Host", hi.hostname);
  full_ua = make_ua(usr_agnt, TOYBOX_VERSION);
  ua = mk_fld("User-Agent", full_ua); 
  conn_type = mk_fld("Connection", "close");

  len = strlen(req) + strlen(hn) + strlen(ua) + strlen(conn_type) + 3;
  hdr = (char *) xmalloc(sizeof(char)*len);
  strcpy(hdr, req);
  strcat(hdr, hn);
  strcat(hdr, ua);
  strcat(hdr, conn_type);
  strcat(hdr, "\r\n");

  // send the request and HTTP header
  if (write(sock_fd, hdr, len) != len)
    "write failed.");

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
  // TODO free dynamic memory before exit
  if (fclose(fp)) perror_exit("File Close Error");

error:
  if (full_ua) free(full_ua);
  if (req) free(req);
  if (hn) free(hn);
  if (ua) free(ua);
  if (conn_type) free(conn_type);  
  if (hdr) free(hdr);
  if (hi.hostname) free(hi.hostname);
  if (hi.path) free(hi.path);
}
