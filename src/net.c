/*
 * TCP socket helpers shared by the current epoll server and the legacy
 * multithread implementation.
 */
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
#include"net.h"
int tcp_listen(char* ip,int port,int num)
{
    int fd = socket(AF_INET,SOCK_STREAM,0);
    if(fd<0)
    {
        perror("socket");
        return -1;
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family=AF_INET;
    inet_aton(ip,&server_addr.sin_addr);
    server_addr.sin_port=htons(port);
    int res1=bind(fd,(struct sockaddr*)&server_addr,sizeof(server_addr));
    if(res1<0)
    {
        perror("bind");
        close(fd);
        return -1;
    }
    int res2=listen(fd,num);
    if(res2<0)
    {
        perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

int tcp_accept(int server_fd,struct sockaddr_in* client_addr)
{
    socklen_t len=sizeof(*client_addr);
    int client_fd=accept(server_fd,(struct sockaddr*)client_addr,&len);
    if(client_fd<0)
    {
        perror("accept");
        return -1;
    }
    return client_fd;
}

int tcp_connect(const char* ip,int port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_aton(ip, &addr.sin_addr);
    addr.sin_port = htons(port);
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { 
        perror("socket");
        return -1;
    }
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }
    return sockfd;
}
