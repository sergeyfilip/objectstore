#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/un.h>

// Definitions used in this application
#define SERVER_PATH     "/opt/serverbackup/var/kservd.socket"
#define BUFFER_LENGTH    512
#define FALSE              0


void error(const char *msg)
{
  perror(msg);
  exit(0);
}

int main(int argc, char *argv[])
{
  int sockfd, n, i;
  struct sockaddr_un serveraddr;
  char buffer[BUFFER_LENGTH];
  char outbuffer[BUFFER_LENGTH];
  if((argc == 2) && (strncmp(argv[1],"-h",2) == 0)) {
    printf("usage: connector [path to server socket\n");
    printf("without argument default path is used:\n /opt/serverbackup/var/\n"); 
    return 0;
  }
  // Send to socket and read it forever    
  while(1) {
                                                                          // usual unix socket is used
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) 
      error("ERROR opening socket");

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sun_family = AF_UNIX;                                      // local unix socket
    if (argc > 1)                                                         //we can set socket path in comand line
      strncpy(serveraddr.sun_path, argv[1],strlen(argv[1]));
    else
      strncpy(serveraddr.sun_path, SERVER_PATH, sizeof(SERVER_PATH));


    if (connect(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) 
      error("ERROR connecting");

      printf("Please enter the input command: > ");
      bzero(buffer,BUFFER_LENGTH);

    if(fgets(buffer,BUFFER_LENGTH,stdin) != 0) {
      n = write(sockfd, buffer, strlen(buffer));
      if (n < 0) 
	error("ERROR writing to socket");
    }
    for(i =0; i< 2 ; i++)
      {
	bzero(outbuffer,BUFFER_LENGTH);   
	n = read(sockfd,outbuffer, BUFFER_LENGTH-1);
	if (n < 0) 
	  error("ERROR reading from socket");
	printf("%s\n", outbuffer);
        if(n < BUFFER_LENGTH-1) outbuffer[n] = '\n';
	else outbuffer[BUFFER_LENGTH-1] = '\n';
      }
  }
  if (sockfd != -1)
    close(sockfd);
  return 0;
}
