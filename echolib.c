/* echolib.c

   Functions to hide complexity of stream I/O, in particular the
   problem of short reads and short writes.

   These started out with an example in W. Richard Stevens' book
   "Advanced Programming in the Unix Environment".

   I rewrote them to be re-entrant, replacing the static (shared)
   buffers by per-connection structures, and added a few
   comments..

   These routines assuming we are working on "binary" char
   streams, i.e.  bytes. Take care when using them, as there is
   also some nastiness related to binary vs. text modes of C I/O.
   In binary mode, each character is one byte, and newline is one
   byte ("\n").  In text mode, the one-char/byte-per character
   rule does not apply.  In text mode, a newline could be a single
   byte ("\n") or a sequence (e.g., "\r\n"), This is actually the
   tip of the binary vs. text "iceberg", since the present
   international information processing and communication world a
   text character may be one byte, two bytes, or a
   context-sensitive variable number of bytes. Writing software
   that deals correctly with multiple character representations is
   a complexity that few computer science students are even aware
   of, but becomes critical when writing productions software.

   Ted Baker
   February 2015

*/

#include "config.h"
#include "echolib.h"

volatile int
shutting_down = 0;

static ssize_t
get_one_char (connection_t *conn, char *ptr) {
  if (conn->read_cnt <= 0) {
again:
    /* try to refill the buffer */
    if ((conn->read_cnt = read (conn->sockfd, conn->read_buf, sizeof(conn->read_buf))) < 0) {
      if ((errno == EINTR) && ! shutting_down) {
        goto again; /* retry */
      }
      return (-1); /* error, errno passed through from read () */
    } else if (conn->read_cnt == 0) {
      return 0; /* EOF */
    }
    conn->read_ptr = conn->read_buf;
  }
  /* take the next char from the buffer */
  *ptr = *conn->read_ptr++;
  conn->read_cnt--;
  return 1;
}

ssize_t
readline (connection_t *conn, void *vptr, size_t maxlen) {
  int n, rc;
  char c, *ptr;
  ptr = vptr;
  for (n = 1; n < maxlen; n++) {
    if ((rc = get_one_char (conn, &c)) == 1) {
      *ptr++ = c;
      if (c == '\n') break; /* newline is stored, like fgets */
    } else if (rc == 0) {
      if (n == 1) return 0; /* EOF, no data read */
      else break; /* EOF, some data was read */
    } else return -1; /* error, errno passed through from read */
  }
  *ptr = '\0';  /* null terminate, like fgets */
  return n;
}

ssize_t 
writen (connection_t *conn, const void *vptr, size_t n) {
  size_t nleft;
  ssize_t nwritten;
  const char *ptr;
  ptr = vptr;
  nleft = n;
  while (nleft > 0) {
    /* try writing some out */
    if ((nwritten = write (conn->sockfd, ptr, nleft)) <= 0) {
      if ((errno == EINTR) && ! shutting_down) {
        nwritten = 0;  /* try again */
      } else {
       return -1; /* error, errno passed through from write */
      }
    }
    nleft -= nwritten;
    ptr   += nwritten;
  }
  return n;
}

extern void 
connection_init (connection_t * conn) {
  memset (conn, 0, sizeof (connection_t));
  /* relies that zero makes sense as a value for all fields */
}
