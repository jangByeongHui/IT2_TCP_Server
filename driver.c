/* file: driver.c

   Test-driver for echo client, to test echo server.

   This was written rather hurriedly, and so is not a good example
   of clean design or robust coding.  In retrospect, I should
   probably have used threads rather than processes, for the
   virtue of providing an extended example of the use of
   threads. However, it does illustrate the use of a variety of
   process and I/O management system calls.

   The program forks off DRIVER_CONCURRENCY "supervisor"
   processes, which each send the server a stream of data (the
   server process id) and check that it is echoed back correctly.
   Stderr of each supervisory thread is redirected to a pipe,
   which is read by the main process at the end.  This means the
   supervisor process needs to limit its stderr output to what
   fits in a pipe buffer, or else it will block.

   Each of the supervisors completes the write-read cycle
   DRIVER_ITERATIONS times.

   Commmand-line arguments are just passed on to the client.
   The following environment variables may be used to change behaviors:
     DRIVER_CONCURRENCY
     DRIVER_ITERATIONS

   Normal termination is via the DRIVER_ITERATIONS limit.
   However, failure of a server can lock up the clients.
   Therefore, this driver implements a timeout. The main program
   sets and alarm.  When the alarm goes off the SIGALRM is caught
   and the handler sets the flag shutting_down.  This also has the
   effect of interrupting any system call on which the driver's
   main process is blocked, so we need to check shutting_down
   after every such call.  The same mechanism is used for
   termination via SIGINT.

   Ted Baker
   February 2015

 */

#include "config.h"
#include "echolib.h"
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <limits.h>

extern char **environ;

#define DEFAULT_CONCURRENCY 5
#define MAX_CONCURRENCY 20
#define DEFAULT_ITERATIONS 1000

char *this_process = "driver";
char errmsgbuff[1000];

void local_perror (const char * msg) {
  fprintf (stderr, "%s: %s \n", msg, strerror (errno));
}

#define CHECK(CALL)\
 if ((CALL) < 0) {local_perror (#CALL); exit (EXIT_FAILURE);}
#define CHECKs(CALL)\
 if ((CALL) < 0) {local_perror (#CALL); shutting_down = 1;}
#define CHECKV(CALL)\
 {int result;\
   if ((result = (CALL)) != 0) {\
     local_perror (#CALL)\
     exit (EXIT_FAILURE);}}
#define ERR_QUIT(MSG) {\
 local_perror (MSG);\
 exit (EXIT_FAILURE);\
 }
#define QUIT(MSG) {\
 fprintf (stderr, "%s\n", MSG);\
 exit (EXIT_FAILURE);\
 }

int concurrency = DEFAULT_CONCURRENCY;
int iterations = DEFAULT_ITERATIONS;

int childpipe[MAX_CONCURRENCY][2]; /* used for stderr */

struct sockaddr_in servaddr;

/* fetch server port number from main program argument list */
int
get_server_port (int argc, char **argv) {
  int val;
  char * endptr;
  if (argc != 2) goto fail;
  errno = 0;
  val = (int) strtol (argv [1], &endptr, 10);
  if (*endptr) goto fail;
  if ((val < 0) || (val > 0xffff)) goto fail;
#ifdef DEBUG
  fprintf (stderr, "port number = %d\n", val);
#endif
  return val;
fail:
   fprintf (stderr, "usage: driver [port number]\n");
   { int i;
     for (i = 0; i < argc; i++) fprintf (stderr, " %s", argv[i]);
     fprintf (stderr, "\n");
   }
   exit (-1);
}

/* set up IP address of host, using DNS lookup based on SERVERHOST
   environment variable, and port number provided in main program
   argument list. */
void
set_server_address (int argc, char **argv) {
  struct hostent *hosts;
  char *server;
  const int server_port = get_server_port (argc, argv);
  if ( !(server = getenv ("SERVERHOST"))) {
    QUIT ("usage: SERVERHOST undefined.  Set it to name of server host, and export it.");
  }
  memset (&servaddr, 0, sizeof(struct sockaddr_in));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons (server_port);
  if ( !(hosts = gethostbyname (server))) {
    ERR_QUIT ("usage: gethostbyname call failed");
  }
  servaddr.sin_addr = *(struct in_addr *) (hosts->h_addr_list[0]);
}

typedef struct supervisor_info {
  pid_t pid;
  int id;
  int pfd;
  int sockfd;
} supervisor_t;

long int clock_tick;

int
ms (clock_t t) {
  long int x = (long int) t;
  return (int ) x * (1000 / clock_tick);
}

void
supervisor (supervisor_t *sup) {
  clock_t start, connected, done;
  struct tms times_buf;
  connection_t conn;
  char sendline[MAXLINE], recvline[MAXLINE];
  const int self = (int) getpid ();
  int i;

  if ((sup->sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
   ERR_QUIT ("usage: socket call failed");
  start = times (&times_buf);
  sched_yield();
  CHECK (connect(sup->sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)));
  connected = times (&times_buf);
  connection_init (&conn);
  conn.sockfd = sup->sockfd;

  /* send data to server and check that it returns it exactly */
  for (i = 0; i < iterations; i++) {
    if (shutting_down) goto quit;
    sprintf (sendline, "%d\n", self);
    sched_yield();
    CHECK (writen (&conn, sendline, strlen (sendline)));
    if (shutting_down) goto quit;
    strcpy (recvline, "poison"); /* to detect read failure */
    sched_yield();
    CHECKs (readline (&conn, recvline, sizeof (recvline)));
    if (shutting_down) goto quit;
    if ((strlen (recvline) != strlen (sendline)) ||
        strcmp (recvline, sendline)) {
      fprintf (stderr, "FAILURE: send and receive lines mismatch\n");
      shutting_down = 1;
    }
    if (shutting_down) goto quit;
  }
 quit:
  done = times (&times_buf);
  fprintf (stderr, "completed %4d iterations: ", i);
  fprintf (stderr, "connection wait time %4d ms, ", ms (connected - start));
  fprintf (stderr, "real time %4d ms, CPU time %4d ms\n",
           ms (done - connected), ms (times_buf.tms_utime + times_buf.tms_stime));
  close (sup->sockfd);
}

#define TIMEOUT 10  /* seconds */
volatile int timed_out = 0;

void
alrmhandler (int sig, siginfo_t *info, void *ignored) {
  fprintf (stderr, "%s: received %s\n", this_process, strsignal(sig));
  if (sig == SIGALRM) timed_out = 1;
  shutting_down = 1;
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

void
remove_handlers () {
  struct sigaction act;
  CHECK (sigaction (SIGALRM, NULL, &act));
  act.sa_sigaction = NULL;
  CHECK (sigaction (SIGALRM, &act, NULL));
  CHECK (sigaction (SIGINT, NULL, &act));
  act.sa_sigaction = NULL;
  CHECK (sigaction (SIGINT, &act, NULL));
}

void
set_concurrency () {
  char * p;
  if ((p = getenv ("DRIVER_CONCURRENCY"))) {
    sscanf (p, "%d", &concurrency);
   }
  if (concurrency > MAX_CONCURRENCY) {
    fprintf (stderr, "concurrency must be <= %d\n", MAX_CONCURRENCY);
    exit (EXIT_FAILURE);
  }
 }

void
set_iterations () {
  char * p;
  if ((p = getenv ("DRIVER_ITERATIONS"))) {
    sscanf (p, "%d", &iterations);
  }
}

void
make_nonblocking (int pfd) {
  int flags;
  CHECK ((flags = fcntl (pfd, F_GETFL)));
  CHECK (fcntl (pfd, F_SETFL, flags | O_NONBLOCK));
}

void
create_supervisor (supervisor_t *sup) {
  int pfd[2];
  /* create pipe to receive stderr from child */
  CHECKs (pipe (pfd));
  /* redirect stderr to pfd [1] */
  close (STDERR_FILENO);
  dup2 (pfd[1], STDERR_FILENO);
  close (pfd[1]);
  sup->pfd = pfd[0];
  /* create child */
  CHECK (sup->pid = fork());      
  if (sup->pid == 0) { /* we are the child */
    this_process = malloc (5);
    snprintf (this_process, 5, "%3d", sup->id);
    supervisor (sup);
    exit (0);
  }
  /* we are the parent */
  make_nonblocking (sup->pfd);
  /*  The above should not be necessary, given we are using select(), 
  but is provided for debugging, to prevent unbounded blocking on pipe reads */
} 

void
empty_pipe (supervisor_t *sup) {
  int nread, count; 
  char outbuf[MAXLINE];
  fprintf (stdout, "%3d: ", sup->id); fflush (stdout);
  count = 0;
  fflush (stdout);
  while ((nread = read (sup->pfd, outbuf, sizeof (outbuf))) > 0) {
    write (STDOUT_FILENO, outbuf, nread); count++;
  }
  if ((nread == -1) && (errno != EAGAIN)) perror ("driver: reading child pipe");
  if (count == 0) {
    fprintf (stdout, "\n");
    fflush (stdout);
  }
  close (sup->pfd);
}

int 
find_child_id (supervisor_t *children, pid_t pid) {
  int i;
  for (i = 0; i < concurrency; i++) {
    if (children[i].pid == pid) return i;
  }
  QUIT ("unexpected child pid");
}

void
await_children (supervisor_t *children) {
  int status;
  int i;
  pid_t result;
  for ( ; ; ) {
    result = wait (&status);
    if (result > 0) { /* one of our children has terminated */
      i = find_child_id (children, result);
      children[i].pid = 0;
      if (WIFEXITED (status)) {
        if (WEXITSTATUS (status) != 0)
          fprintf (stderr, "child %d exited with abnormal status: %d\n", i, WEXITSTATUS (status));
      } else if (WIFSIGNALED (status)) {
        fprintf (stderr, "child %d killed by signal %s\n",
                 i,
                 strsignal (WTERMSIG (status)));
      } else {
        fprintf (stderr, "unexpected child exit status %d\n", status); 
      }
    } else if (result == -1) {
      if (errno == EINTR) {
        if (! shutting_down) continue;
        if (timed_out) {
          fprintf (stderr, "driver: signaling children to shut down\n");
          for (i = 0; i < concurrency; i++) {
            if (children[i].pid != 0) {
              CHECK (kill (children[i].pid, SIGINT));
            }
          }
        }
      } else if (errno == ECHILD) return; /* all children have terminated */
        else {
          ERR_QUIT ("wait: unexpected errno");
        }
    } else ERR_QUIT ("wait: strange status");
  }
}

int
main(int argc, char **argv) {
   supervisor_t child[MAX_CONCURRENCY];
   int i;

   CHECK ((clock_tick = sysconf (_SC_CLK_TCK)));
   set_server_address (argc, argv);
   set_concurrency (); 
   set_iterations ();
   fprintf (stderr, "driver: (version 2) concurrency = %d; iterations = %d\n", concurrency, iterations); 
   setalrmhandler ();
   /* create one child to supervise each concurrent test */
   for (i = 0; i < concurrency; i++) {
     child[i].id = i;
     create_supervisor (&child[i]);
   }
   alarm (TIMEOUT);
   await_children (child);
   remove_handlers ();
   /* empty all child pipes */
   for (i = 0; i < concurrency; i++) {
     empty_pipe (&child[i]);
   }
   return 0;
}

