/*
 * TCP WebSocket server
 * Author: Spencer Fricke
 *
 * To compile:
 *     gcc web_socket_server.c -o server -lpthreads
 *
 * To run
 *     ./server <optional_port_number>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>
#include <pthread.h>

#define DEFAULT_PORT 5000
#define MSG_SIZE 512

pthread_mutex_t server_status_lock = PTHREAD_MUTEX_INITIALIZER;
int server_status = 0;
char* RED = "0";
char* GREEN = "1";
char* BLUE = "2";

// wrapper for throwing error
void error(const char *msg) {
    perror(msg);
    exit(1);
}

//the thread function
void *connection_handler(void *);

int main(int argc, char *argv[]) {

  int status; // used to check status of c functions return values
  int port;

  struct sockaddr_in dest; // socket info about the machine connecting to us
  struct sockaddr_in server; // socket info about our server
  int mySocket;            // socket used to listen for incoming connections
  int consocket;           // used to hold status of connect to socket
  int *new_socket;
  socklen_t socksize = sizeof(struct sockaddr_in);
  
  // see if passed port in argument
  if (argc == 2) {
    port = atoi(argv[1]);
  } else {
    port = DEFAULT_PORT;
  }

  memset(&server, 0, sizeof(server)); // zero the struct before filling the fields
  server.sin_family = AF_INET; // set to use Internet address family
  server.sin_addr.s_addr = htonl(INADDR_ANY); // sets our local IP address
  server.sin_port = htons(port); // sets the server port number

  // creates the socket
  // AF_INET refers to the Internet Domain
  // SOCK_STREAM sets a stream to send data
  // 0 will have the OS pick TCP for SOCK_STREAM
  mySocket = socket(AF_INET, SOCK_STREAM, 0);
  if (mySocket < 0) { error("ERROR: Opening socket\n"); }
  else { printf("TCP Socket Created! \n"); }

  // bind server information to mySocket
  status = bind(mySocket, (struct sockaddr *)&server, sizeof(struct sockaddr));

  // checks for TIME_WAIT socket
  // when daemon is closed there is a delay to make sure all TCP data is propagated
  if (status < 0) {
    printf("ERROR opening socket: %d , possible TIME_WAIT\n", status);
    printf("USE: netstat -ant|grep %d to find out\n", port);
    exit(1);
  } else {
    printf("Socket Binded!\n");
  }

  // start listening, allowing a queue of up to 1 pending connection
  listen(mySocket, 5);
  printf("Socket Listening on port %d!\n", port);

  // prevents daemon from closing on a closed client
  signal(SIGPIPE, SIG_IGN);

    // blocks until a TCP handshake is made
  while( (consocket = accept(mySocket, (struct sockaddr*)&dest, &socksize)) ) {

    // a connection has been been received can see the client IP
    printf("Incoming connection from %s \n", inet_ntoa(dest.sin_addr));

    pthread_t sniffer_thread;
    new_socket = malloc(1);
    *new_socket = consocket;

    if ( pthread_create( &sniffer_thread, NULL, connection_handler, (void*) new_socket) < 0) { error("ERROR: could not create thread"); }

  }

  if (consocket < 0 ) { error("ERROR: accept failed");}

  return 0;
}

void *connection_handler(void *socket_desc) {

  int socket = *(int*)socket_desc;
  int status;  
  char receiveMsg[MSG_SIZE]; // message buffer
  char returnMsg[1] = "0";
  int msgSize; // used to store incoming message size

 // main loop to wait for a request
  while( ( msgSize = recv(socket, receiveMsg, MSG_SIZE , 0) ) > 0 ) {

    printf("got message %s\n", receiveMsg);

    if (strncmp(receiveMsg, RED, 1) == 0) {
      pthread_mutex_lock(&server_status_lock);
      server_status = 0;
      pthread_mutex_unlock(&server_status_lock);
    } else if (strncmp(receiveMsg, GREEN, 1) == 0) {
      pthread_mutex_lock(&server_status_lock);
      server_status = 1;
      pthread_mutex_unlock(&server_status_lock);
    } else if (strncmp(receiveMsg, BLUE, 1) == 0) {
      pthread_mutex_lock(&server_status_lock);
      server_status = 2;
      pthread_mutex_unlock(&server_status_lock);
    }

    if (server_status == 0) {
      returnMsg[0] = '0';
    } else if (server_status == 1) {
      returnMsg[0] = '1';
    } else if (server_status == 2) {
      returnMsg[0] = '2';
    }
    
    
    // sends back response message
    status = send(socket, returnMsg, 1, 0);
    if (status < 0) { error("ERROR on send\n"); }

  } // end of connection while loop

  close(socket); // ends current TCP connection and frees server thread
  
  if (msgSize == 0) {
    puts("client dropped connection");
  } else {
    error("ERROR: Recv failed");
  }
  return 0;
}
