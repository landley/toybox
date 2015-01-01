/* ftpget.c - Get a remote file from FTP.
 *
 * Copyright 2013 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard.
 * 
USE_FTPGET(NEWTOY(ftpget, "<2cvu:p:P#<0=21>65535", TOYFLAG_BIN))
USE_FTPGET(OLDTOY(ftpput, ftpget, TOYFLAG_BIN))

config FTPGET
  bool "ftpget/ftpput"
  default n
  help
    usage: ftpget [-cv] [-u USER -p PASSWORD -P PORT] HOST_NAME [LOCAL_FILENAME] REMOTE_FILENAME
    usage: ftpput [-v] [-u USER -p PASSWORD -P PORT] HOST_NAME [REMOTE_FILENAME] LOCAL_FILENAME

    ftpget - Get a remote file from FTP.
    ftpput - Upload a local file on remote machine through FTP.

    -c Continue previous transfer.
    -v Verbose.
    -u User name.
    -p Password.
    -P Port Number (default 21).
*/
#define FOR_ftpget
#include "toys.h"

GLOBALS(
  long port; //  char *port;
  char *password;
  char *username;

  FILE *sockfp;
  int c;
  int isget;
  char buf[sizeof(struct sockaddr_storage)];
)

#define DATACONNECTION_OPENED   125
#define FTPFILE_STATUSOKAY      150
#define FTP_COMMAND_OKAY        200
#define FTPFILE_STATUS          213
#define FTPSERVER_READY         220
#define CLOSE_DATACONECTION     226
#define PASSIVE_MODE            227
#define USERLOGGED_SUCCESS      230
#define PASSWORD_REQUEST        331
#define REQUESTED_PENDINGACTION 350


static void setport(unsigned port_num)
{
  int af = ((struct sockaddr *)TT.buf)->sa_family;

  if (af == AF_INET) ((struct sockaddr_in*)TT.buf)->sin_port = port_num;
  else if (af == AF_INET6) ((struct sockaddr_in6*)TT.buf)->sin6_port = port_num;
}

static int connect_to_stream()
{
  int sockfd, af = ((struct sockaddr *)TT.buf)->sa_family;

  sockfd = xsocket(af, SOCK_STREAM, 0);
  if (connect(sockfd, (struct sockaddr*)TT.buf,((af == AF_INET)? 
          sizeof(struct sockaddr_in):sizeof(struct sockaddr_in6))) < 0) {
    close(sockfd);
    perror_exit("can't connect to remote host");
  }
  return sockfd;
}

//close ftp connection and print the message.
static void close_stream(char *msg_str)
{
  char *str = toybuf; //toybuf holds response data.

  //Remove garbage chars (from ' ' space to '\x7f') DEL remote server response.
  while ((*str >= 0x20) && (*str < 0x7f)) str++; 
  *str = '\0';
  if (TT.sockfp) fclose(TT.sockfp);
  error_exit("%s server response: %s", (msg_str) ? msg_str:"", toybuf);
}

//send command to ftp and get return status.
static int get_ftp_response(char *command, char *param)
{
  unsigned cmd_status = 0;
  char *fmt = "%s %s\r\n";

  if (command) {
    if (!param) fmt += 3;
    fprintf(TT.sockfp, fmt, command, param);
    fflush(TT.sockfp);
    if (toys.optflags & FLAG_v) 
      fprintf(stderr, "FTP Request: %s %s\r\n", command, param);
  }

  do {
    if (!fgets(toybuf, sizeof(toybuf)-1, TT.sockfp)) close_stream(NULL);
  } while (!isdigit(toybuf[0]) || toybuf[3] != ' ');

  toybuf[3] = '\0';
  cmd_status = atolx_range(toybuf, 0, INT_MAX);
  toybuf[3] = ' ';
  return cmd_status;
}

static void send_requests(void)
{
  int cmd_status = 0;

  //FTP connection request.
  if (get_ftp_response(NULL, NULL) != FTPSERVER_READY) close_stream(NULL);

  //230 User authenticated, password please; 331 Password request.
  cmd_status = get_ftp_response("USER", TT.username);
  if (cmd_status == PASSWORD_REQUEST) { //user logged in. Need Password.
    if (get_ftp_response("PASS", TT.password) != USERLOGGED_SUCCESS) 
      close_stream("PASS");
  } else if (cmd_status == USERLOGGED_SUCCESS); //do nothing
  else close_stream("USER");
  //200 Type Binary. Command okay.
  if (get_ftp_response("TYPE I", NULL) != FTP_COMMAND_OKAY) 
    close_stream("TYPE I");
}

static void get_sockaddr(char *host)
{  
  struct addrinfo hints, *result;
  char port[6];
  int status;
  
  errno = 0;
  snprintf(port, 6, "%ld", TT.port);

  memset(&hints, 0 , sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  status = getaddrinfo(host, port, &hints, &result); 
  if (status) error_exit("bad address '%s' : %s", host, gai_strerror(status));

  memcpy(TT.buf, result->ai_addr, result->ai_addrlen);
  freeaddrinfo(result);
} 

// send commands to ftp fo PASV mode.
static void verify_pasv_mode(char *r_filename)
{
  char *pch;
  unsigned portnum;

  //vsftpd reply like:- "227 Entering Passive Mode (125,19,39,117,43,39)".
  if (get_ftp_response("PASV", NULL) != PASSIVE_MODE) goto close_stream;

  //Response is "NNN <some text> (N1,N2,N3,N4,P1,P2) garbage.
  //Server's IP is N1.N2.N3.N4
  //Server's port for data connection is P1*256+P2.
  if (!(pch = strrchr(toybuf, ')'))) goto close_stream;
  *pch = '\0';
  if (!(pch = strrchr(toybuf, ','))) goto close_stream;
  *pch = '\0';

  portnum = atolx_range(pch + 1, 0, 255);

  if (!(pch = strrchr(toybuf, ','))) goto close_stream;
  *pch = '\0';
  portnum = portnum + (atolx_range(pch + 1, 0, 255) * 256);
  setport(htons(portnum));

  if (TT.isget && get_ftp_response("SIZE", r_filename) != FTPFILE_STATUS)
    TT.c = 0;
  return;

close_stream:
  close_stream("PASV");
}

/*
 * verify the local file presence.
 * if present, get the size of the file.
 */
static void is_localfile_present(char *l_filename)
{
  struct stat sb;

  if (stat(l_filename, &sb) < 0) perror_exit("stat");
  //if local file present, then request for pending file action.
  if (sb.st_size > 0) {
    sprintf(toybuf, "REST %lu", (unsigned long) sb.st_size);
    if (get_ftp_response(toybuf, NULL) != REQUESTED_PENDINGACTION) TT.c = 0;
  } else TT.c = 0;
}

static void transfer_file(int local_fd, int remote_fd)
{
  int len, rfd = (TT.isget)?remote_fd:local_fd,
      wfd = (TT.isget)?local_fd:remote_fd;

  if (rfd < 0 || wfd < 0) error_exit("Error in file creation:");
  while ((len = xread(rfd, toybuf, sizeof(toybuf)))) xwrite(wfd, toybuf, len);
}

static void get_file(char *l_filename, char *r_filename)
{
  int local_fd = -1, remote_fd;

  verify_pasv_mode(r_filename);
  remote_fd = connect_to_stream(); //Connect to data socket.

  //if local file name will be '-' then local fd will be stdout.
  if ((l_filename[0] == '-') && !l_filename[1]) {
    local_fd = 1; //file descriptor will become stdout.
    TT.c = 0;
  }

  //if continue, check for local file existance.
  if (TT.c) is_localfile_present(l_filename);

  //verify the remote file presence.
  if (get_ftp_response("RETR", r_filename) > FTPFILE_STATUSOKAY) 
    close_stream("RETR");

  //if local fd is not stdout, create a file descriptor.
  if (local_fd == -1) {
    int flags = O_WRONLY;

    flags |= (TT.c)? O_APPEND : (O_CREAT | O_TRUNC);
    local_fd = xcreate((char *)l_filename, flags, 0666);
  }
  transfer_file(local_fd, remote_fd);
  xclose(remote_fd);
  xclose(local_fd);
  if (get_ftp_response(NULL, NULL) != CLOSE_DATACONECTION) close_stream(NULL);
  get_ftp_response("QUIT", NULL);
  toys.exitval = EXIT_SUCCESS;
}

static void put_file(char *r_filename, char *l_filename)
{
  int local_fd = 0, remote_fd;
  unsigned cmd_status = 0;

  verify_pasv_mode(r_filename);
  remote_fd = connect_to_stream(); //Connect to data socket.

  //open the local file for transfer.
  if ((l_filename[0] != '-') || l_filename[1]) 
    local_fd = xcreate((char *)l_filename, O_RDONLY, 0666);

  //verify for the remote file status, Ok or Open: transfer File.
  cmd_status = get_ftp_response("STOR", r_filename);
  if ( (cmd_status == DATACONNECTION_OPENED) || 
      (cmd_status == FTPFILE_STATUSOKAY)) {
    transfer_file(local_fd, remote_fd);
    if (get_ftp_response(NULL, NULL) != CLOSE_DATACONECTION) close_stream(NULL);
    get_ftp_response("QUIT", NULL);
    toys.exitval = EXIT_SUCCESS;
  } else {
    toys.exitval = EXIT_FAILURE;
    close_stream("STOR");
  }
  xclose(remote_fd);
  xclose(local_fd);
}

void ftpget_main(void)
{
  char **argv = toys.optargs; //host name + file name.

  TT.isget = toys.which->name[3] == 'g';
  TT.c = 1;
  //if user name is not specified.
  if (!(toys.optflags & FLAG_u) && (toys.optflags & FLAG_p)) 
    error_exit("Missing username:");
  //if user name and password is not specified in command line.
  if (!(toys.optflags & FLAG_u) && !(toys.optflags & FLAG_p))
    TT.username = TT.password ="anonymous";

  //if continue is not in the command line argument.
  if (TT.isget && !(toys.optflags & FLAG_c)) TT.c = 0;

  if (toys.optflags & FLAG_v) fprintf(stderr, "Connecting to %s\n", argv[0]);
  get_sockaddr(argv[0]);

  TT.sockfp = xfdopen(connect_to_stream(), "r+");
  send_requests();

  if (TT.isget) get_file(argv[1], argv[2] ? argv[2] : argv[1]); 
  else put_file(argv[1], argv[2] ? argv[2] : argv[1]);
}
