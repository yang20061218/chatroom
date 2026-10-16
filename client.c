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
void* read_thread(void* arg)
{
    int client_fd = (int)(intptr_t)arg;
    char *buf=NULL;
    ssize_t len;
    while (1) {           
        len = tcp_recv(client_fd,&buf);
        if (len <=0) {
            free(buf);
            printf("server ask to close\n");
            break;
        }                 
        fputs(buf, stdout);
        fflush(stdout);
        free(buf);
        buf=NULL;
                               
    }

    return NULL;
}

void* write_thread(void* arg)
{
    int client_fd = (int)(intptr_t)arg;
    char buf[1200];

    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        int len = tcp_send(client_fd, buf, strlen(buf));  
        if(len==0){
            printf("recive stop flag, stop send to server\n");
            break;
        }
        if (len < 0) {
            perror("send");
            break;
        }
    }
    shutdown(client_fd, SHUT_WR);
    return NULL;
}

int main()
{
    int sockfd=tcp_connect("127.0.0.1",7777);
    if (sockfd < 0) {                  
    perror("tcp_connect");
    exit(EXIT_FAILURE);
    }
    printf("connect server ip: 127.0.0.1 port:7777\n");
    pthread_t pid_read, pid_write;
    pthread_create(&pid_read, NULL, read_thread, (void*)(intptr_t)sockfd);
    pthread_create(&pid_write, NULL, write_thread, (void*)(intptr_t)sockfd);

    pthread_join(pid_read, NULL);
    pthread_join(pid_write, NULL);

    close(sockfd);
    return 0;
}