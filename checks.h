/* checks.h

   Macros for use in checking return values from system calls.

   Take care that you understand each of these before you use it,
   and do not assume that you will necessarily find an appropriate
   macro here for every system call.

   Ted Baker
   February 2015

*/

#ifndef CHECKS_H
#define CHECKS_H

#include <string.h>
   
/* CHECK is used to check that the value returned by a system call
   is nonzero, and terminate the program immediately if this check
   fails. It assumes the usual convention that -1 indicates
   failure of the call, with more detailed failure information in
   the global variable errno. */

#define CHECK(CALL)\
 if ((CALL) < 0) {perror (#CALL); exit (EXIT_FAILURE);}

/* CHECKV is similar to CHECK, but for functions that follow a
   different convention for reporting failures, i.e. to return
   zero for success and a non-zero value for failure, where the
   return value corresponds to an errno-like error code.  These
   include the pthread_ calls. */

#define CHECKV(CALL)\
 {int result;\
   if ((result = (CALL)) != 0) {\
     fprintf (stderr, "%s: %s\n", (#CALL), strerror (result));     \
     exit (EXIT_FAILURE);}}

/* QUIT is used to output a failure message and terminate the
   program. ERR_QUIT is a version that also prints out the value of errno.  */

#define QUIT(MSG) {\
 fprintf (stderr, MSG "\n");\
 exit (EXIT_FAILURE);\
 }

#define ERR_QUIT(MSG) {\
 perror (MSG);\
 exit (EXIT_FAILURE);\
 }

/* ERR_QUITV is similar to ERR_QUIT, but for functions that follow
   the failures reporting convention described for CHECKV above. */

#define ERR_QUIT_V(MSG, ERRNO) {\
 fprintf (stderr, "%s: %s\n", (MSG), strerror(ERRNO));\
 exit (EXIT_FAILURE);\
 }

#endif

