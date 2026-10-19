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
#include"protocol.h"
//head = magic(4) + version(1) + cmd(2) + length(4)

int protocol_pack(char** mes,uint16_t cmd)
{
    if(*mes==NULL) return -1;
    if (strlen(*mes) > MES_MAX) return -1;
    int len=strlen(*mes)+HEAD_LEN;
    char* buf=malloc(len);
    if(buf==NULL) return -1;
    memcpy(buf,"1218",4);
    buf[4]=VERSION;
    uint16_t temp1=htons(cmd);
    uint32_t temp2=htonl(strlen(*mes));
    memcpy(buf+5,&temp1,2);
    memcpy(buf+7,&temp2,4);
    memcpy(buf+11,*mes,strlen(*mes));
    *mes=buf;
    return len;
}
int protocol_unpack(char*mes,int meslen,uint16_t *cmd,uint32_t* len)
{
    char* buf = mes;
    if(meslen<HEAD_LEN) return -1;
    if(memcmp(buf,"1218",4)!=0) return -1;
    if((uint8_t)buf[4]!=VERSION) return -1;

    uint16_t tempc;
    memcpy(&tempc,buf+5,2);
    *cmd=ntohs(tempc);

    uint32_t templen;
    memcpy(&templen,buf+7,4);
    *len=ntohl(templen);
    return 1;
}
int protocol_write(int fd,char* buf,int len)
{
    char * temp=buf;
    int left=len;
    while(left>0)
    {
        int send_len = send(fd,temp,left,0);
        if (send_len<=0) {
            if (errno == EINTR) continue;
            return -1;
        }
        temp+=send_len;
        left-=send_len;
    }
    return len;
}
int protocol_send(int fd, uint16_t cmd,char *mes)
{
    if(mes==NULL) return -1;
    int len=protocol_pack(&mes,cmd);
    if(len<0) return -1;
    int res=protocol_write(fd,mes,len);
    free(mes);
    return  res;//len of mes+head
}
int protocol_read(int fd,char* mes,int len)
{
    int left=len;
    char* pos=mes;
    while(left>0)
    {
        int read_len=recv(fd,pos,left,0);
        if(read_len==0)
        {
            return len-left;
        }
        if(read_len<0)
        {
            if(errno==EINTR) continue;
            return -1;
        }
        pos+=read_len;
        left-=read_len;
    }
    return len;
}
int protocol_recv(int fd,char** mes,uint16_t* cmd1)
{
    *mes=NULL;
    uint32_t len;
    uint16_t cmd;
   
    char head[HEAD_LEN];
    if(protocol_read(fd,head,HEAD_LEN)!=HEAD_LEN) return -1;
    int res=protocol_unpack(head,HEAD_LEN,&cmd,&len);
    if(res<0) return -1;
    if(len>MES_MAX) return -1;
    if(cmd!=CMD_CHAT&&cmd!=CMD_EXIT&&cmd!=CMD_HEART&&cmd!=CMD_LOGIN&&cmd!=CMD_LOGIN_RES) return -1;
    *cmd1=cmd;
    char* buf=(char*)malloc(len+1);
    if(buf==NULL) return -1;
    int read_len=protocol_read(fd,buf,len);
    if((uint32_t)read_len!=len)
    {
        free(buf);
        return -1;
    }
    buf[len]='\0';
    *mes=buf;
    return len+HEAD_LEN;
}