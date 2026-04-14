/*
gcc -o client client.c
sudo ./client
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

int client_socket;

static void sigint_handler(int signo)
{
  (void)close(client_socket);
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
  struct pollfd fds[1];
  char buffer[BUFFER_SIZE];

  register_signal_handler(SIGINT,
  sigint_handler);

  client_socket = socket(AF_PACKET, 
                  SOCK_DGRAM, 
                  htons(ETH_P_ALL));

  if (client_socket < 0) {
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

  while (1) {
    sprintf(buffer, "%s", "hello server!");

    struct iovec iov_dum_out = { .iov_base = buffer, .iov_len = sizeof(buffer)};
    struct iovec iovtab_out[1] = { iov_dum_out };
    struct msghdr msg_out = { .msg_iov = iovtab_out, .msg_iovlen = 1 };

    ssize_t rawlen_out = sendmsg(client_socket, &msg_out, MSG_DONTWAIT);
/*
    ret = sendto(client_socket, buffer, 
    strlen(buffer), 0, 
    (struct sockaddr*)&server_addr, 
    sizeof(server_addr));
  
    if (ret < 0) {
*/
    if (rawlen_out < 0) {
      perror("sendto");
      break;
    }
  
    printf("Sentbuffer : %s\n",
    buffer);

    memset(fds, 0, sizeof(fds));
    fds[0].fd = client_socket;
    fds[0].events = POLLIN;

    ret = poll(fds, 1, -1);

    if (ret < 0) {
	perror("poll");
	break;
    }

    if (fds[0].revents & POLLIN) {
/*
       ret = recvfrom(client_socket, buffer, 
       BUFFER_SIZE, 0, NULL, NULL);
*/	    
       struct iovec iov_dum_in = { .iov_base = buffer, .iov_len = sizeof(buffer)};
       struct iovec iovtab_in[1] = { iov_dum_in };
       struct msghdr msg_in = { .msg_iov = iovtab_in, .msg_iovlen = 1 };

       ssize_t rawlen_in = recvmsg(client_socket, &msg_in, MSG_DONTWAIT);

       if (rawlen_in < 0) {
	  perror("recvfrom");
          break;
       } else if (rawlen_in > 0) {
          buffer[rawlen_in] = '\0';
          printf("Received : %s\n", 
	  buffer);
       }
/*
       if (ret < 0) {
	  perror("recvfrom");
          break;
       } else if (ret > 0) {
          buffer[ret] = '\0';
          printf("Received : %s\n", 
	  buffer);
       }
*/
    }
  }

  (void)close(client_socket);

  return 0;
}
