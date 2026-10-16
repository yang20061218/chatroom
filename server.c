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

#define MAX_CLIENTS 2000


int clientfds[MAX_CLIENTS]={0};
int clientnum=0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


void* sendtoall_thread(void* arg)
{
    char *read_buf=NULL;
    int clientfd=(int)(intptr_t)arg;
    ssize_t read_len;
    while(1)
    {
        read_len=tcp_recv(clientfd,&read_buf);
        if (read_len<=0) {
            free(read_buf);
            break;
        }
        else
        {
            int clientfds_copy[MAX_CLIENTS];
            int clientnum_copy;
            char temp[1300];
            snprintf(temp, sizeof(temp), "[client%d] : %s\n",clientfd,read_buf);

            pthread_mutex_lock(&mutex);
            for(int i=0;i<clientnum;i++)
            {
                clientfds_copy[i]=clientfds[i];
            }
            clientnum_copy=clientnum;
            pthread_mutex_unlock(&mutex);
            for(int i=0;i<clientnum_copy;i++)
            {
                if(clientfds_copy[i] == clientfd)
                {
                    continue;
                }
                tcp_send(clientfds_copy[i],temp,strlen(temp));
            }
            free(read_buf);  
            read_buf=NULL;
        }
    }
    int clientfds_copy[MAX_CLIENTS];
    int clientnum_copy;
    
    pthread_mutex_lock(&mutex);
    for(int i=0;i<clientnum;i++){
        clientfds_copy[i]=clientfds[i];
    }
    clientnum_copy=clientnum;
    for(int i=0;i<clientnum;i++){
        if(clientfds[i] == clientfd ){
            clientfds[i]=0;
            for(int p=i+1;p<clientnum;p++){
                clientfds[p-1]=clientfds[p];
            }
            clientfds[clientnum-1]=0;
            clientnum--;
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
    char temp[256];
    snprintf(temp, sizeof(temp), "------client %d exit------\n",clientfd);
    int len=strlen(temp);
    for(int j=0;j<clientnum_copy;j++){
    if(clientfds_copy[j]==clientfd) continue;
    tcp_send(clientfds_copy[j],temp,len);
}
    close(clientfd);
    return NULL;
}
int main()
{
    signal(SIGPIPE, SIG_IGN);
    int serverfd=tcp_listen("0.0.0.0",7777,100);
    if(serverfd==-1)
    {
        exit(EXIT_FAILURE);
    }
    while(1)
    {
        pthread_t pid_sendtoall;
        int fd=tcp_accept(serverfd,NULL);
        if(fd<0)
        {
            perror("accept");
            continue;
        }           
        pthread_mutex_lock(&mutex);
        if(clientnum>=MAX_CLIENTS)
        {
            fprintf(stderr, "连接数已满，拒绝新连接 fd=%d\n", fd);
            close(fd); 
            pthread_mutex_unlock(&mutex);          
            continue;             
        }
        clientfds[clientnum++]=fd;
        pthread_mutex_unlock(&mutex);
        pthread_create(&pid_sendtoall,NULL,sendtoall_thread,(void*)(intptr_t)fd);
        pthread_detach(pid_sendtoall);
    }

    close(serverfd);
    return 0;
}
