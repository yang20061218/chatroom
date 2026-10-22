/*
 * Legacy multithread chat server.
 * One reader thread per client; kept for comparison with the epoll server.
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
#include"protocol.h"
#include<time.h>

#define MAX_CLIENTS 2000

typedef struct
{
    int  fd;
    char name[MAX_NAMELEN];
    int is_log;
    time_t last_active;

}client;

client clients[MAX_CLIENTS];
int clientnum=0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int broadcast(char* mes)
{
    if (mes == NULL) return -1;

    int clientfds_copy[MAX_CLIENTS];
    int n = 0;

    pthread_mutex_lock(&mutex);
    for (int i = 0; i < clientnum; i++) {
        if (clients[i].is_log) {
            clientfds_copy[n++] = clients[i].fd;      
        }
    }
    pthread_mutex_unlock(&mutex);
    for (int i = 0; i < n; i++) {                 
        protocol_send(clientfds_copy[i], CMD_CHAT, mes);
    }
    return 0;
}

int broadcast_nomyself(char* mes, int fd_myself)
{
    if (mes == NULL) return -1;

    int clientfds_copy[MAX_CLIENTS];
    int n = 0;

    pthread_mutex_lock(&mutex);
    for (int i = 0; i < clientnum; i++) {
        if (clients[i].is_log) {
            clientfds_copy[n++] = clients[i].fd;
        }
    }
    pthread_mutex_unlock(&mutex);

    for (int i = 0; i < n; i++) {
        if (clientfds_copy[i] == fd_myself) continue;
        protocol_send(clientfds_copy[i], CMD_CHAT, mes);
    }
    return 0;
}
int client_delet(int fd)
{
    int pos=-1;
    pthread_mutex_lock(&mutex);
    for(int i=0;i<clientnum;i++){
        if(clients[i].fd == fd ){
            pos =i;
            clients[i].fd = 0;
            for(int p=i+1;p<clientnum;p++){
                clients[p-1]=clients[p];
            }
            memset(&clients[clientnum-1], 0, sizeof(client));
            clientnum--;
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
    return pos;
}
int getname(int fd,char* name)
{
    name[0]='\0';
    pthread_mutex_lock(&mutex);
    for(int i=0;i<clientnum;i++){
        if(clients[i].fd==fd)
        {
            snprintf(name, MAX_NAMELEN, "%s", clients[i].name);
            break;
        }    
    }
    name[MAX_NAMELEN-1]='\0';
    pthread_mutex_unlock(&mutex);
    return 0;
}
int login(int fd,char* name)
{
    if(name==NULL||strlen(name)>=MAX_NAMELEN) 
    {
        return -1;
    }
    pthread_mutex_lock(&mutex);
    int pos=-1;
     for(int i=0;i<clientnum;i++){
        if(clients[i].fd==fd) pos=i;
    }
    for(int i=0;i<clientnum;i++){
        if (i == pos) continue;
        if (!clients[i].is_log) continue;
        if(strcmp(clients[i].name,name)==0)
        {
            pthread_mutex_unlock(&mutex);
            return -1;
        }    
    }
    if(pos==-1) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    strncpy(clients[pos].name,name,MAX_NAMELEN);
    clients[pos].name[MAX_NAMELEN-1]='\0';
    clients[pos].is_log = 1;    
    pthread_mutex_unlock(&mutex);
    return strlen(name);
}
void* sendtoall_thread(void* arg)
{
    char *read_buf=NULL;
    int clientfd=(int)(intptr_t)arg;
    ssize_t read_len;
    uint16_t cmd; 
    char name[MAX_NAMELEN]={0};
    while(1)
    {
        read_len=protocol_recv(clientfd,&read_buf,&cmd);
        if (read_len<0) {
            free(read_buf);
            break;
        }
        else if(read_len==0){
             free(read_buf);
            read_buf = NULL;
        }
        else
        {
            switch (cmd)
            {
            case CMD_CHAT:
                {
                    int logged = 0;
                    pthread_mutex_lock(&mutex);
                    for (int i = 0; i < clientnum; i++) {
                    if (clients[i].fd == clientfd) { 
                        logged = clients[i].is_log; 
                        if(logged) clients[i].last_active=time(NULL);
                        break;
                    }
                    }
                    pthread_mutex_unlock(&mutex);
                    if (!logged) break;


                char temp[MES_MAX+64];
                getname(clientfd,name);
                snprintf(temp, sizeof(temp), "[%s] : %s\n",name,read_buf);
                broadcast_nomyself(temp,clientfd);
                break;
                }
            case CMD_LOGIN:
                if(login(clientfd,read_buf)>0)
                {
                    protocol_send(clientfd,CMD_LOGIN_RES,"login success");
                }
                else
                {
                    protocol_send(clientfd,CMD_LOGIN_RES,"login failed,please try again");
                }
                break;
            case CMD_HEART:
                pthread_mutex_lock(&mutex);
                for(int i=0;i<clientnum;i++)
                {
                    if(clients[i].fd==clientfd)
                    {
                        clients[i].last_active=time(NULL);
                        break;
                    }
                }
                pthread_mutex_unlock(&mutex);
                break;
            case CMD_EXIT:
            free(read_buf);
                goto done;
            default:
            free(read_buf);
                goto done;
            }   
            free(read_buf);      
        }
    }
    done:
    getname(clientfd,name);
    client_delet(clientfd);
    close(clientfd);
    if(name[0]!='\0'){
        char temp[256];
        snprintf(temp, sizeof(temp), "------%s exit------\n",name);
        broadcast(temp);
    }
    return NULL;
}
void* checkheartbeat_thread(void* arg)
{
    
    (void)arg;
    while(1){
    pthread_mutex_lock(&mutex);
    for(int i=0;i<clientnum;i++)
    {
        if(time(NULL)-clients[i].last_active>30&&clients[i].is_log)
        {
            shutdown(clients[i].fd,SHUT_RDWR);
        }
        else if(time(NULL)-clients[i].last_active>60&&!clients[i].is_log)
        {
            shutdown(clients[i].fd,SHUT_RDWR);
        }
    }
    pthread_mutex_unlock(&mutex);
    sleep(5);
}
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
    pthread_t pid_checkheartbeat;
    pthread_create(&pid_checkheartbeat,NULL,checkheartbeat_thread,NULL);
    pthread_detach(pid_checkheartbeat);
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
            fprintf(stderr, "system is full,refuse client: fd=%d\n", fd);
            close(fd); 
            pthread_mutex_unlock(&mutex);          
            continue;             
        }
        clients[clientnum].fd = fd;
        clients[clientnum].last_active=time(NULL);
        clientnum++;
        pthread_mutex_unlock(&mutex);

        pthread_create(&pid_sendtoall,NULL,sendtoall_thread,(void*)(intptr_t)fd);
        pthread_detach(pid_sendtoall);
    }
    close(serverfd);
    return 0;
}
