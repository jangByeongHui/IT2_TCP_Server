/* file: echosrv.c

   Bare-bones single-threaded TCP server. Listens for connections
   on "ephemeral" socket, assigned dynamically by the OS.

   This started out with an example in W. Richard Stevens' book
   "Advanced Programming in the Unix Environment".  I have
   modified it quite a bit, including changes to make use of my
   own re-entrant version of functions in echolib.

   NOTE: See comments starting with "NOTE:" for indications of
   places where code needs to be added to make this multithreaded.
   Remove those comments from your solution before turning it in,
   and replace them by comments explaining the parts you have
   changed.

   Ted Baker
   February 2015

 */

#include "config.h"
/* not needed now, but will be needed in multi-threaded version */
#include "pthread.h"
#include "echolib.h"
#include "checks.h"

#define MAXCLIENT 4
#define MAX 100
#define T_MAX 1000000
int clientNum = 0;
int getFD = -1;
int id = 0;

pthread_mutex_t mutex_q;
pthread_mutex_t mutex;
pthread_cond_t cond;
pthread_cond_t q_cond;

// typedef struct Work {  
//     int connfd;           
//     int age;                 
// }Work;

// 세마포어 생성
sem_t consume;
int front=-1;
int rear=-1;
int queue[MAX];
 
typedef struct task{
  int sockfd;
  int num;
}task_t;

task_t t_queue[T_MAX];
int t_front = -1;
int t_rear = -1;

int Task_IsEmpty(void){
    if(t_front==t_rear)//front와 rear가 같으면 큐는 비어있는 상태 
        return 1;
    else return 0;
}
int Task_IsFull(void){
    int tmp=(t_rear+1)%T_MAX; //원형 큐에서 rear+1을 MAX로 나눈 나머지값이
    if(tmp==t_front)//front와 같으면 큐는 가득차 있는 상태 
        return 1;
    else
        return 0;
}
void Task_addq(int fd, int n){
    if(Task_IsFull())
        printf("Queue is Full.\n");
    else{
         t_rear = (t_rear+1)%T_MAX;
         t_queue[t_rear].sockfd = fd;
         t_queue[t_rear].num = n;
        }

}
task_t Task_deleteq(){
    if(Task_IsEmpty())
        printf("Queue is Empty.\n");
    else{
        t_front = (t_front+1)%T_MAX;
        return t_queue[t_front];
    }
}

int IsEmpty(void){
    if(front==rear)//front와 rear가 같으면 큐는 비어있는 상태 
        return 1;
    else return 0;
}
int IsFull(void){
    int tmp=(rear+1)%MAX; //원형 큐에서 rear+1을 MAX로 나눈 나머지값이
    if(tmp==front)//front와 같으면 큐는 가득차 있는 상태 
        return 1;
    else
        return 0;
}
void addq(int value){
    if(IsFull())
        printf("Queue is Full.\n");
    else{
         rear = (rear+1)%MAX;
         queue[rear]=value;
        }

}
int deleteq(){
    if(IsEmpty())
        printf("Queue is Empty.\n");
    else{
        front = (front+1)%MAX;
        return queue[front];
    }
}
int isPrime(int num){
  if(num <= 1) return 0;
  for (int i=2; i<num; i++)
  {
    if (num % i == 0) return 0;
  }
  return 1;
}

void*
serve_connection (void* sockfd);

void
server_handoff (int sockfd) {

/* NOTE: You will need to completely rewrite this function, so
   that it hands off the connection to one of your server threads,
   moving the call to serve_connection() into the server thread
   body.  To do that, you will need to declare a lot of new stuff,
   including thread, mutex, and condition variable structures,
   functions, etc.  You can insert the new bode before and after
   this function, but you do not need to modify serve_connection()
   or anything else between it and the note in the main program
   body.  However, you are free to change anything in this file if
   you feel it is necessary for your design. */
  
  addq(sockfd);
  sem_post(&consume);
  fprintf(stdout,"getfd: %d clientNUM:%d\n",sockfd,clientNum);
  // pthread_create(&t_id[clientNum],NULL,serve_connection,(void *)&sockfd);
}


void* worker_thread(void* arg)
{
  ssize_t result;
  char send[1024];
  connection_t conn;
  connection_init (&conn);
  pthread_t id;
  id = pthread_self();
  printf("2. %ud\n", id);
  while(1)
  {
    task_t task;
    pthread_mutex_lock(&mutex_q);
    while(Task_IsEmpty() == 1){
      pthread_cond_wait(&q_cond, &mutex_q);
    }
    task = Task_deleteq();
    conn.sockfd = task.sockfd;
    int num = task.num;
    sprintf(send, "%d", task.num);
    if(isPrime(num) == 1)
    {
      strcat(send, " is prime number\n");
    }else{
      strcat(send, " is not prime number\n");
    }
    result = writen (&conn, send, strlen(send));

    printf("item: %d fd: %d tid: %u\n", task.num, task.sockfd, id);
    pthread_mutex_unlock(&mutex_q);
  }
}

/* the main per-connection service loop of the server; assumes
   sockfd is a connected socket */
void*
serve_connection (void* i) {
  pthread_t id;
  printf("count: %d\n", *(int*)i);
  id = pthread_self();
  connection_t conn;
  connection_init (&conn);
  while(1)
  {
    sem_wait(&consume);
    ssize_t  n, result;
    char line[MAXLINE];
    conn.sockfd = deleteq();
    char st[20];
    int num;
    while (!shutting_down) {
      printf("1. %ud\n", id);
      if ((n = readline (&conn, line, MAXLINE)) == 0) goto quit;
      /* connection closed by other end */
      if (shutting_down) goto quit;
      if (n < 0) {
        perror ("readline failed");
        goto quit;
      }
      num = atoi(line);
      if(Task_IsFull != 1){
        pthread_mutex_lock(&mutex_q);
        Task_addq(conn.sockfd, num);
        pthread_cond_signal(&q_cond);
        pthread_mutex_unlock(&mutex_q);
      }
    }
  quit:
    clientNum--;
    CHECK (close (conn.sockfd));
  }
}

/* set up socket to use in listening for connections */
void
open_listening_socket (int *listenfd) {
  struct sockaddr_in servaddr;
  const int server_port = 0; /* use ephemeral port number */
  socklen_t namelen;
  memset (&servaddr, 0, sizeof(struct sockaddr_in));
  servaddr.sin_family = AF_INET;
  /* htons translates host byte order to network byte order; ntohs
     translates network byte order to host byte order */
  servaddr.sin_addr.s_addr = htonl (INADDR_ANY);
  servaddr.sin_port = htons (server_port);
  /* create the socket */
  CHECK (*listenfd = socket(AF_INET, SOCK_STREAM, 0))
  /* bind it to the ephemeral port number */
  CHECK (bind (*listenfd, (struct sockaddr *) &servaddr, sizeof (servaddr)));
  /* extract the ephemeral port number, and put it out */
  namelen = sizeof (servaddr);
  CHECK (getsockname (*listenfd, (struct sockaddr *) &servaddr, &namelen));
  fprintf (stderr, "server using port %d\n", ntohs(servaddr.sin_port));
}

/* handler for SIGINT, the signal conventionally generated by the
   control-C key at a Unix console, to allow us to shut down
   gently rather than having the entire process killed abruptly. */ 
void
siginthandler (int sig, siginfo_t *info, void *ignored) {
  shutting_down = 1;
}

void
install_siginthandler () {
  struct sigaction act;
  /* get current action for SIGINT */
  CHECK (sigaction (SIGINT, NULL, &act));
  /* add our handler */
  act.sa_sigaction = siginthandler;
  /* update action for SIGINT */
  CHECK (sigaction (SIGINT, &act, NULL));
}

int
main (int argc, char **argv) {
  int connfd, listenfd;
  socklen_t clilen;
  struct sockaddr_in cliaddr;
  pthread_t t_id;

  pthread_mutex_init(&mutex, NULL);
  pthread_cond_init(&cond, NULL);
  int a=0;

  /* NOTE: To make this multi-threaded, You may need insert
     additional initialization code here, but you will not need to
     modify anything below here, though you are permitted to
     change anything in this file if you feel it is necessary for
     your design */
    
  sem_init(&consume, 0, 0);
  for(int i = 0;i<MAXCLIENT;i++){
      pthread_create(&t_id,NULL,serve_connection,(void *)&a);
      a++;
    }
  for(int i = 0;i<10;i++){
    pthread_create(&t_id,NULL,worker_thread,(void *)&i);
  }


  install_siginthandler();
  open_listening_socket (&listenfd);
  CHECK (listen (listenfd, 4));
  /* allow up to 4 queued connection requests before refusing */
  while (! shutting_down) {
    errno = 0;
    clientNum++;
    clilen = sizeof (cliaddr); /* length of address can vary, by protocol */
    if ((connfd = accept (listenfd, (struct sockaddr *) &cliaddr, &clilen)) < 0) {
      if (errno != EINTR) ERR_QUIT ("accept"); 
      /* otherwise try again, unless we are shutting down */
      clientNum--;
    } else {
      server_handoff(connfd); /* process the connection */
    }
  }
  CHECK (close (listenfd));
  return 0;
}