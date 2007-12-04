// Included after toys.h, for network stuff.  Some build environments
// don't include network support, so we shouldn't include it unless we're
// going to build it.

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
