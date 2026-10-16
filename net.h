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
// int  tcp_listen(const char *ip, uint16_t port, int backlog);
// int  tcp_accept(int listen_fd, char *peer_ip, size_t ip_cap, uint16_t *peer_port);
// int  tcp_connect(const char *ip, uint16_t port);
// int  set_nonblock(int fd);
// int  set_block(int fd);
// int  tcp_send_all(int fd, const char *buf, size_t len);
int tcp_listen(char* ip,int port,int num);
int tcp_accept(int server_fd,struct sockaddr_in* client_addr);
int tcp_connect(const char* ip,int port);
int tcp_broadcast(int client_fd, const char *buf, size_t len);
int tcp_send(int fd,char *buf,int len);
int tcp_write(int fd,char *buf,int len);
int tcp_read(int fd,char* mes,int len);
int tcp_recv(int fd,char** buf);
#endif 