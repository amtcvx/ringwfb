/*
gcc -o server server.c
sudo ./server
*/ 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <poll.h>

#define BUFFER_SIZE 1024

int server_socket;

static void sigint_handler(int signo)
{
  (void)close(server_socket);
  sleep(2);
  printf("Caught sigINT!\n");
  exit(EXIT_SUCCESS);
}

void register_signal_handler(
int signum,
void (*handler)(int))
{
  if (signal(signum, handler) ==
  SIG_ERR) {
     printf("Cannot handle signal\n");
     exit(EXIT_FAILURE);
  }
}

int main(void) 
{
  int ret, i;
  struct sockaddr_ll 
  server_addr, 
  client_addr;
  char buffer[BUFFER_SIZE];
  struct pollfd fds[1];

  register_signal_handler(SIGINT,
  sigint_handler);

  server_socket = socket(AF_PACKET, 
                  SOCK_DGRAM, 
                  htons(ETH_P_ALL));

  if (server_socket < 0) {
    perror("socket");
    return -1;
  }

  memset(&server_addr, 0, 
  sizeof(server_addr));
  server_addr.sll_family = AF_PACKET;
  server_addr.sll_protocol = 
  htons(ETH_P_ALL);
  server_addr.sll_ifindex = 
  if_nametoindex("lo");

  ret = bind(server_socket, 
  (struct sockaddr*)&server_addr, 
  sizeof(server_addr));
  
  if (ret < 0) {
    perror("bind");
    (void)close(server_socket);
    return -2;
  }

  printf("UDP is listening\n");

  socklen_t client_addr_len = 
  sizeof(client_addr);

  memset(fds, 0, sizeof(fds));
  fds[0].fd = server_socket;
  fds[0].events = POLLIN;

  while (1) {
    ret = poll(fds, 1, -1);

    if (ret < 0) {
      perror("poll");
      break;
    }
 
    if (fds[0].revents & POLLIN) {

      memset(buffer, 0, 
      sizeof(buffer));
/*
      ret = recvfrom(server_socket, 
      buffer, BUFFER_SIZE, 0, 
      (struct sockaddr*)&client_addr, 
      &client_addr_len);
*/
     struct iovec iov_dum_in = { .iov_base = buffer, .iov_len = sizeof(buffer)};
     struct iovec iovtab_in[1] = { iov_dum_in };
     struct msghdr msg_in = { .msg_iov = iovtab_in, .msg_iovlen = 1 };

     ssize_t rawlen_in = recvmsg(server_socket, &msg_in, MSG_DONTWAIT);
/*
     if (ret > 0) {
        buffer[ret] = '\0';
*/
     if (rawlen_in > 0) {
        buffer[rawlen_in] = '\0';
        printf("Received: %s\n", 
        buffer);

	memset(buffer, 0,
        sizeof(buffer));
        strncpy(buffer, "HELLO",
        strlen("HELLO") + 1);
        buffer[strlen(buffer)
        + 1] = '\0';
/*	
	ret = sendto(server_socket,
        buffer, strlen(buffer), 0,
        (struct sockaddr *)&client_addr,
        client_addr_len);
*/
        struct iovec iov_dum_out = { .iov_base = buffer, .iov_len = sizeof(buffer)};
        struct iovec iovtab_out[1] = { iov_dum_out };
        struct msghdr msg_out = { .msg_iov = iovtab_out, .msg_iovlen = 1 };

	ssize_t rawlen_out = sendmsg(server_socket, &msg_out, MSG_DONTWAIT);


        if (ret == -1) {
           perror("sendto error");
           break;
	}
     } else if (ret < 0) {
       perror("recvfrom error");
       break;
     } 
   }
  }

  (void)close(server_socket);

  return 0;
}

