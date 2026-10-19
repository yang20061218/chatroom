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
#include"protocol.h"
void* read_thread(void* arg)
{
    int client_fd = (int)(intptr_t)arg;
    char *buf=NULL;
    ssize_t len;
    uint16_t cmd;
    while (1) {           
        len = protocol_recv(client_fd,&buf,&cmd);
        if (len <=0) {
            free(buf);
            printf("server ask to close\n");
            break;
        }
        switch (cmd)
        {
        case CMD_CHAT:
            fputs(buf, stdout);
            fflush(stdout);         
            break;  
        default:
            break;
        }    
        free(buf);
        buf=NULL;                             
    }
    shutdown(client_fd,SHUT_RD);
    return NULL;
}
void* write_thread(void* arg)
{
    int client_fd = (int)(intptr_t)arg;
    char buf[1200];
    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        if (strcmp(buf, "\n") == 0) {
        continue;                   
    }
        int len = protocol_send(client_fd,CMD_CHAT,buf);  
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
void* heartbeat_thread(void* arg)
{
    char* empty="";
    while(1){
        int client_fd = (int)(intptr_t)arg;
        if(protocol_send(client_fd,CMD_HEART,empty)<0) break;
        sleep(10);
    }
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
    char name[MAX_NAMELEN];
    printf("please put in your name\n");
    if(scanf("%31s", name)!=1)
    {
        printf("recive no name");
        close(sockfd);
        return 1;    
    }
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }
    protocol_send(sockfd,CMD_LOGIN,name);
    
    
    char* resp=NULL;
    uint16_t cmd;
    int res=protocol_recv(sockfd,&resp,&cmd);
    if (res< 0) {                              
    printf("server closed the connectio\n");
    free(resp);
    close(sockfd);
    return 1;
}

    if (cmd != CMD_LOGIN_RES || strncmp(resp, "login success", 13) != 0) {
    printf("log in failed: %s\n", resp ? resp : "");   
    free(resp);
    close(sockfd);
    return 1;
}
    free(resp);
    printf("log in successs\n");




    pthread_t pid_read, pid_write,pid_heartbeat;
    pthread_create(&pid_read, NULL, read_thread, (void*)(intptr_t)sockfd);
    pthread_create(&pid_write, NULL, write_thread, (void*)(intptr_t)sockfd);
    pthread_create(&pid_heartbeat, NULL,heartbeat_thread, (void*)(intptr_t)sockfd);
    
    pthread_detach(pid_heartbeat);
    pthread_join(pid_read, NULL);
    pthread_join(pid_write, NULL);

    close(sockfd);
    return 0;
}