/* TCP socket helper declarations for the archived multithread build. */
#ifndef NET_H
#define NET_H
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include<signal.h>
#include<errno.h>
int tcp_listen(char* ip,int port,int num);
int tcp_accept(int server_fd,struct sockaddr_in* client_addr);
int tcp_connect(const char* ip,int port);
#endif
