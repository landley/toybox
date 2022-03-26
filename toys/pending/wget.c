/* wget.c - Simple downloader to get the resource file from a HTTP server
 *
 * Copyright 2016 Lipi C.H. Lee <lipisoft@gmail.com>
 * Copyright 2021 Eric Molitor <eric@molitor.org>
 *
 * Relevant sources of information
 * -------------------------------
 * HTTP 1.1: https://www.rfc-editor.org/rfc/rfc7230
 * Chunked Encoding: https://www.rfc-editor.org/rfc/rfc7230#section-4.1
 * UTF-8 Encoded Header Values https://www.rfc-editor.org/rfc/rfc5987
 *
 * Test URLs
 * ---------
 * Chunked Encoding: https://jigsaw.w3.org/HTTP/ChunkedScript
 * Redirect 301: https://jigsaw.w3.org/HTTP/300/301.html
 * Redirect 302: https://jigsaw.w3.org/HTTP/300/302.html
 * TLS 1.0: https://tls-v1-0.badssl.com:1010/
 * TLS 1.1: https://tls-v1-1.badssl.com:1011/
 * TLS 1.2: https://tls-v1-2.badssl.com:1012/
 * TLS 1.3: https://tls13.1d.pw/
 * Transfer Encoding [gzip|deflate]: https://jigsaw.w3.org/HTTP/TE/bar.txt
 *
 *
 * todo: Add support for configurable TLS versions
 * todo: Add support for ftp
 * todo: Add support for Transfer Encoding (gzip|deflate)
 * todo: Add support for RFC5987

USE_WGET(NEWTOY(wget, "<1>1(max-redirect)#<0=20d(debug)O(output-document):p(post-data):", TOYFLAG_USR|TOYFLAG_BIN))

config WGET
  bool "wget"
  default n
  help
    usage: wget [OPTIONS]... [URL]
        --max-redirect          maximum redirections allowed
    -d, --debug                 print lots of debugging information
    -O, --output-document=FILE  specify output filename
    -p, --post-data=DATA        send data in body of POST request

    examples:
      wget http://www.example.com

config WGET_LIBTLS
  bool "Enable HTTPS support for wget via LibTLS"
  default n
  depends on WGET && !WGET_OPENSSL
  help
    Enable HTTPS support for wget by linking to LibTLS.
    Supports using libtls, libretls or libtls-bearssl.

config WGET_OPENSSL
  bool "Enable HTTPS support for wget via OpenSSL"
  default n
  depends on WGET && !WGET_LIBTLS
  help
    Enable HTTPS support for wget by linking to OpenSSL.
*/

#define FOR_wget
#include "toys.h"

#if CFG_WGET_LIBTLS
#define WGET_SSL 1
#include <tls.h>
#elif CFG_WGET_OPENSSL
#define WGET_SSL 1
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#else
#define WGET_SSL 0
#endif


#define WGET_IS_HTTP  (strncmp(TT.url, "http://", 7) == 0)
#define WGET_IS_HTTPS (WGET_SSL && (strncmp(TT.url, "https://", 8) == 0))

GLOBALS(
  char *O;
  char *postdata;
  long max_redirect;

  int sock;
  char *url;
#if CFG_WGET_LIBTLS
  struct tls *tls;
#elif CFG_WGET_OPENSSL
  struct ssl_ctx_st *ctx;
  struct ssl_st *ssl;
#endif
)

// get http info in URL
static void wget_info(char *url, char **host, char **port, char **path)
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

  if (!*port && WGET_IS_HTTP) *port = "80";
  else if (!*port && WGET_IS_HTTPS) *port = "443";
  else if (!*port) error_exit("unsupported protocol");
}

static void wget_connect(char *host, char *port)
{
  if (WGET_IS_HTTP)
    TT.sock = xconnectany(xgetaddrinfo(host, port, AF_UNSPEC, SOCK_STREAM, 0, 0));
  else if (WGET_IS_HTTPS) {
#if CFG_WGET_LIBTLS
    struct tls_config *cfg = NULL;
    uint32_t protocols;
    if (!(TT.tls = tls_client()))
      error_exit("tls_client: %s", tls_error(TT.tls));
    if (!(cfg = tls_config_new()))
      error_exit("tls_config_new: %s", tls_config_error(cfg));
    if (tls_config_parse_protocols(&protocols, "tlsv1.2"))
      error_exit("tls_config_parse_protocols");
    if (tls_config_set_protocols(cfg, protocols))
      error_exit("tls_config_set_protocols: %s", tls_config_error(cfg));
    if (tls_configure(TT.tls, cfg))
      error_exit("tls_configure: %s", tls_error(TT.tls));
    tls_config_free(cfg);

    if (tls_connect(TT.tls, host, port))
      error_exit("tls_connect: %s", tls_error(TT.tls));
#elif CFG_WGET_OPENSSL
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    ERR_load_crypto_strings();

    TT.ctx = SSL_CTX_new(TLS_client_method());
    if (!TT.ctx) error_exit("SSL_CTX_new");

    TT.sock = xconnectany(xgetaddrinfo(host, port, AF_UNSPEC, SOCK_STREAM, 0, 0));

    TT.ssl = SSL_new(TT.ctx);
    if (!TT.ssl)
      error_exit("SSL_new: %s", ERR_error_string(ERR_get_error(), NULL));

    if (!SSL_set_tlsext_host_name(TT.ssl, host))
      error_exit("SSL_set_tlsext_host_name: %s",
                 ERR_error_string(ERR_get_error(), NULL));

    SSL_set_fd(TT.ssl, TT.sock);
    if (SSL_connect(TT.ssl) == -1)
      error_exit("SSL_set_fd: %s", ERR_error_string(ERR_get_error(), NULL));

    if (FLAG(d)) printf("TLS: %s\n", SSL_get_cipher(TT.ssl));
#else
    error_exit("unsupported protocol");
#endif
  } else error_exit("unsupported protocol");
}

static size_t wget_read(void *buf, size_t len)
{
  if (WGET_IS_HTTP) return xread(TT.sock, buf, len);
  else if (WGET_IS_HTTPS) {
#if CFG_WGET_LIBTLS
   ssize_t ret = tls_read(TT.tls, buf, len);
   if (ret < 0) error_exit("tls_read: %s", tls_error(TT.tls));
   return ret;
#elif CFG_WGET_OPENSSL
   int ret = SSL_read(TT.ssl, buf, (int) len);
   if (ret < 0)
     error_exit("SSL_read: %s", ERR_error_string(ERR_get_error(), NULL));
   return ret;
#endif
  } else error_exit("unsupported protocol");
}

static void wget_write(void *buf, size_t len)
{
  if (WGET_IS_HTTP) xwrite(TT.sock, buf, len);
  else if (WGET_IS_HTTPS) {
#if CFG_WGET_LIBTLS
    if (len != tls_write(TT.tls, buf, len))
      error_exit("tls_write: %s", tls_error(TT.tls));
#elif CFG_WGET_OPENSSL
    if (len != SSL_write(TT.ssl, buf, (int) len))
      error_exit("SSL_write: %s", ERR_error_string(ERR_get_error(), NULL));
#endif
  } else error_exit("unsupported protocol");
}

static void wget_close()
{
  if (TT.sock) {
      xclose(TT.sock);
      TT.sock = 0;
  }

#if CFG_WGET_LIBTLS
  if (TT.tls) {
    tls_close(TT.tls);
    tls_free(TT.tls);
    TT.tls = NULL;
  }
#elif CFG_WGET_OPENSSL
  if (TT.ssl) {
    SSL_shutdown(TT.ssl);
    SSL_free(TT.ssl);
    TT.ssl = NULL;
  }

  if (TT.ctx) {
    SSL_CTX_free(TT.ctx);
    TT.ctx = NULL;
  }
#endif
}

static char *wget_find_header(char *header, char *val)
{
  char *result = strcasestr(chomp(header), val);

  return result ? result + strlen(val) : 0;
}

static char *wget_redirect(char *header)
{
  char *redir = wget_find_header(header, "Location: ");

  if (!redir) error_exit("could not parse redirect URL");

  return xstrdup(redir);
}

static char *wget_filename(char *header, char *path)
{
  char *f = wget_find_header(header,
    "Content-Disposition: attachment; filename=");

  if (!f && strchr(path, '/')) f = getbasename(path);
  if (!f || !*f ) f = "index.html";

  return f;
}

void wget_main(void)
{
  long status = 0;
  size_t len, c_len = 0;
  int fd;
  char *body, *index, *host, *port, *path, *chunked;
  char agent[] = "toybox wget/" TOYBOX_VERSION;

  TT.url = xstrdup(toys.optargs[0]);

  while (status != 200) {
    if (!TT.max_redirect--) error_exit("Too many redirects");

    wget_info(TT.url, &host, &port, &path);
    if (!FLAG(p)) {
      sprintf(toybuf, "GET /%s HTTP/1.1\r\nHost: %s\r\n"
                      "User-Agent: %s\r\nConnection: close\r\n\r\n",
                      path, host, agent);
    } else {
      sprintf(toybuf, "POST /%s HTTP/1.1\r\nHost: %s\r\n"
                      "User-Agent: %s\r\nConnection: close\r\n"
                      "Content-Length: %ld\r\n\r\n"
                      "%s",
                      path, host, agent, sizeof(TT.postdata), TT.postdata);
    }
    if (FLAG(d)) printf("--- Request\n%s", toybuf);

    wget_connect(host, port);
    wget_write(toybuf, strlen(toybuf));

    // Greedily read the HTTP response until either complete or toybuf is full
    index = toybuf;
    while ((len = wget_read(index, sizeof(toybuf) - (index - toybuf))) > 0)
      index += len;

    //Process the response such that
    //  Valid ranges  toybuf[0...index)      valid length is (index - toybuf)
    //  Header ranges toybuf[0...body)       header length strlen(toybuf)
    //  Remnant Body  toybuf[body...index)   valid remnant body length is len
    //
    // Per RFC7230 the header cannot contain a NUL octet so we NUL terminate at
    // the footer of the header. This allows for normal string functions to be
    // used when processing the header.
    body = memmem(toybuf, index - toybuf, "\r\n\r\n", 4);
    if (!body) error_exit("response header too large");
    body[0] = '\0'; // NUL terminate the headers
    body += 4; // Skip to the head of body
    len = index - body; // Adjust len to be body length
    if (FLAG(d)) printf("--- Response\n%s\n\n", toybuf);

    status = strtol(strafter(toybuf, " "), NULL, 10);
    if ((status == 301) || (status == 302)) {
      free(TT.url);
      TT.url = wget_redirect(toybuf);
      wget_close();
    } else if (status != 200) error_exit("response: %ld", status);
  }

  if (!FLAG(O)) {
    TT.O = wget_filename(toybuf, path);
    if (!access(TT.O, F_OK)) error_exit("%s already exists", TT.O);
  }
  fd = !strcmp(TT.O, "-") ? 1 : xcreate(TT.O, (O_WRONLY|O_CREAT|O_TRUNC), 0644);

  chunked = wget_find_header(toybuf, "transfer-encoding: chunked");

  // If chunked we offset the first buffer by 2 character, meaning it is
  // pointing at half of the header boundary, aka '\r\n'. This simplifies
  // parsing of the first c_len length by allowing the do while loop to fall
  // through on the first iteration and parse the first c_len size.
  if (chunked) {
    len = len + 2;
    memmove(toybuf, body - 2, len);
  } else memmove(toybuf, body, len);

  // len is the size remaining in toybuf
  // c_len is the size of the remaining bytes in the current chunk
  do {
    if (chunked) {
      if (c_len > 0) { // We have an incomplete c_len to write
        if (len <= c_len) { // Buffer is less than the c_len so full write
          xwrite(fd, toybuf, len);
          c_len = c_len - len;
          len = 0;
        } else { // Buffer is larger than the c_len so partial write
          xwrite(fd, toybuf, c_len);
          len = len - c_len;
          memmove(toybuf, toybuf + c_len, len);
          c_len = 0;
        }
      }

      // If len is less than 2 we can't validate the chunk boundary so fall
      // through and go read more into toybuf.
      if (!c_len && (len > 2)) {
        char *c;
        if (strncmp(toybuf, "\r\n", 2) != 0) error_exit("chunk boundary");

        // If we can't find the end of the new chunk signature fall through and
        // read more into toybuf.
        c = memmem(toybuf + 2, len - 2, "\r\n",2);
        if (c) {
          c_len = strtol(toybuf + 2, NULL, 16);
          if (!c_len) break; // A c_len of zero means we are complete
          len = len - (c - toybuf) - 2;
          memmove(toybuf, c + 2, len);
        }
      }

      if (len == sizeof(toybuf)) error_exit("chunk overflow");
    } else {
      xwrite(fd, toybuf, len);
      len = 0;
    }
  } while ((len += wget_read(toybuf + len, sizeof(toybuf) - len)) > 0);

  wget_close();
  free(TT.url);
}
