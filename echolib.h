/* echolib.h

   Functions to hide complexity of stream I/O, in particular the
   problem of short reads and short writes.

   These started out with an example in W. Richard Stevens' book
   "Advanced Programming in the Unix Environment".

   Ted Baker
   February 2015

*/

#ifndef ECHOLIB_H
#define ECHOLIB_H

/* per-connection input buffer length; determines maximum line
   length for inputs */
#define MAXLINE 1024 

/* state of a connection, including input buffer */
typedef struct connection {
  int   sockfd;                /* socket file descriptor */
  int   read_cnt;              /* number of characters read into read_buf[] */
  char *read_ptr;              /* next location to read from in read_buf[]  */
  char  read_buf[MAXLINE];     /* the input buffer */
 } connection_t;

/* used to shut down program gently, without resorting to exit ()
   actual variable is in echolib.c; "volatile" is a hint to the
   compiler not to optimize uses of this variable based on an
   assumption that it is only modified in the current thread of
   control, since we plan to update it from a signal handler; this
   is normall zero, but set to one when we want to shut down */
extern volatile int 
shutting_down;

extern void 
connection_init (connection_t * conn);

/* read one line from a connection, into buffer of given length,
   in binary mode, and return actual number of bytes read;
   behavior similar to fgets */
extern ssize_t 
readline (connection_t * conn, void *vptr, size_t maxlen);

/* write buffer of given length to connection, in binary mode, and
   return actual number of bytes written */
ssize_t 
writen (connection_t *conn, const void *vptr, size_t n);

#endif
