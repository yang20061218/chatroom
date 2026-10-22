/* Shared 11-byte application protocol constants and declarations. */
#ifndef PROTOCOL_H
#define PROTOCOL_H
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
#include<stdint.h>

/* Commands: chat=1, exit=2, heartbeat=3, login=4, login response=5. */
#define VERSION 1
#define HEAD_LEN 11

#define CMD_CHAT 1
#define CMD_EXIT 2
#define CMD_HEART 3
#define CMD_LOGIN 4
#define CMD_LOGIN_RES 5

#define MES_MAX (64*1024)
#define MAX_NAMELEN 32
int protocol_send(int fd, uint16_t cmd,char *mes);
int protocol_write(int fd,char* buf,int len);
int protocol_read(int fd,char* mes,int len);
int protocol_recv(int fd,char** mes,uint16_t* cmd1);
int protocol_pack(char** mes,uint16_t cmd);
int protocol_unpack(char*mes,int meslen,uint16_t *cmd,uint32_t* len);

#endif
