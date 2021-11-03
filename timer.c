/* file: timer.c

   Starts up arg[1], allows it to run until this process received
   SIGINT, then kills it and reports resource usage.

   Ted Baker
   February 2015

 */

#include "config.h"
#include <signal.h>
#include <stdio.h>
#include <errno.h>  
#include <libgen.h>  
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/fcntl.h>  /* needed for O_CREAT */
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <limits.h>

extern char **environ;

#define CHECK(CALL)\
 if ((CALL) < 0) {perror (#CALL); exit (EXIT_FAILURE);}

#define TIMEOUT 30  /* seconds */
volatile int interrupted = 0;
volatile int timed_out = 0;

void
alrmhandler (int sig, siginfo_t *info, void *ignored) {
  fprintf (stderr, "timer: received %s\n", strsignal(sig));
  if (sig == SIGALRM) timed_out = 1;
  if (sig == SIGINT) interrupted = 1;
}

void
setalrmhandler () {
  /* set up timeout */
  struct sigaction act;
  CHECK (sigaction (SIGALRM, NULL, &act));
  act.sa_sigaction = alrmhandler;
  CHECK (sigaction (SIGALRM, &act, NULL));
  CHECK (sigaction (SIGINT, NULL, &act));
  act.sa_sigaction = alrmhandler;
  CHECK (sigaction (SIGINT, &act, NULL));
}

long int
ms (struct timeval *tv) {
  long int t;
  t = (long int) tv->tv_usec / 1000;
  t = t + 1000 * tv->tv_sec;
  return t;
}

int
main(int argc, char **argv) {
  char *args[100];
  pid_t child;
  char * osname;
  char * parentdir;

  setalrmhandler ();

  { struct utsname unamebuf;
    int len;
    CHECK (uname (&unamebuf));
    osname = unamebuf.sysname;
    len = strlen (unamebuf.sysname) + 1; /* +1 for zero-byte */
    osname = malloc (len);
    strncpy (osname, unamebuf.sysname, len);
  }

  { char buf[512];
    char * p;
    if (getcwd (buf, 1024) == NULL) {
       perror ("getcwd failed"); exit (-1);
    }
    p = basename (buf);
    parentdir = malloc (strlen (p) + 1);
    strcpy (parentdir, p);
  }

  /* fork & exec multisrv */
  CHECK (child = fork());      
  if (child == 0) {

    /* we are the child */

    /* set resource limits */
    { struct rlimit rl;
      CHECK (getrlimit (RLIMIT_CPU, &rl));
      rl.rlim_cur = 10; 
      CHECK (setrlimit (RLIMIT_CPU, &rl));
      CHECK (getrlimit (RLIMIT_FSIZE, &rl));
      rl.rlim_cur = 100000;
      CHECK (setrlimit (RLIMIT_FSIZE, &rl));
    }

    /* pass on the arguments to the new program */
    fprintf (stdout, "timer: testing ");
    { int i;
      for (i = 0; i < argc - 1; i++) {
        args[i] = argv[i + 1];
        fprintf (stdout, " %s", args[i]);
      }
      args[i] = NULL;
    }

    fprintf (stdout, " in %s on %s ---------------\n", parentdir, osname);
    fflush (stdout);
    CHECK (execve (args[0], args, environ));
    /* should never reach here unless exec failed */
    exit (-1);
   }

  /* we are the parent */

  alarm (TIMEOUT);

  /* wait for child to complete */
  { struct rusage rusage;
    pid_t result;
    int status;
    result = wait4 (child, &status, WUNTRACED, &rusage);
    if (result > 0) { /* one of our children has terminated */
      if (WIFEXITED (status)) {
        if (WEXITSTATUS (status) != 0)
          fprintf (stderr, "timer: child exited with abnormal status: %d\n", WEXITSTATUS (status));
      } else if (WIFSIGNALED (status)) {
        fprintf (stderr, "timer: child killed by signal %s\n",
                 strsignal (WTERMSIG (status)));
      } else {
        fprintf (stderr, "timer: unexpected child exit status %d\n", status); 
      }
    } else if (result == -1) {
      if (timed_out) fprintf (stderr, "timer: timed out\n");
      else if (interrupted) fprintf (stderr, "timer: interrupted\n");
      else perror ("timer: wait for child interrupted");
      CHECK (kill (child, SIGINT))
      CHECK (wait4 (child, &status, WUNTRACED, &rusage));
    }

    fprintf (stderr, "timer: user %ld ms system %ld ms \n",
             ms (&rusage.ru_utime), ms (&rusage.ru_stime));
    /* fprintf (stderr, "timer: unshared data size: %ld\n", rusage.ru_idrss); */
    fprintf (stderr, "timer: messages sent: %ld received: %ld\n", rusage.ru_msgsnd, rusage.ru_msgrcv);
    fprintf (stderr, "timer: max resident set: %ld page faults: major %ld minor: %ld\n",
             rusage.ru_maxrss, rusage.ru_majflt, rusage.ru_minflt);
  }
  alarm (0);

  return 0;
}

