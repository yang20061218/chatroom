#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "net.h"
#include "protocol.h"
#include <time.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define MAX_CLIENTS 2000
#define RDBUF_SIZE 4096
#define WRBUF_SIZE 8192

typedef struct
{
    int fd;
    char name[MAX_NAMELEN];
    int is_log;
    time_t last_active;

    char read_buf[RDBUF_SIZE];
    ssize_t read_buf_unread;
    char write_buf[WRBUF_SIZE];
    ssize_t write_buf_used_unsend;

} client;

client clients[MAX_CLIENTS];
int clientnum = 0;
int client_delet(int fd)
{
    int pos = -1;
    for (int i = 0; i < clientnum; i++)
    {
        if (clients[i].fd == fd)
        {
            pos = i;
            clients[i].fd = 0;
            for (int p = i + 1; p < clientnum; p++)
            {
                clients[p - 1] = clients[p];
            }
            memset(&clients[clientnum - 1], 0, sizeof(client));
            clientnum--;
            break;
        }
    }
    return pos;
}

int login(int fd, char *name)
{
    if (name == NULL || strlen(name) >= MAX_NAMELEN)
    {
        return -1;
    }
    int pos = -1;
    for (int i = 0; i < clientnum; i++)
    {
        if (clients[i].fd == fd)
            pos = i;
    }
    for (int i = 0; i < clientnum; i++)
    {
        if (i == pos || !clients[i].is_log)
            continue;
        if (strcmp(clients[i].name, name) == 0)
        {
            return -1;
        }
    }
    if (pos == -1)
    {
        return -1;
    }
    strncpy(clients[pos].name, name, MAX_NAMELEN);
    clients[pos].name[MAX_NAMELEN - 1] = '\0';
    clients[pos].is_log = 1;
    return strlen(name);
}
int sendtoall(int epollfd, int fd_myself, int pos, char *mes, uint32_t meslen)
{
    if (epollfd < 0 || pos < 0 || pos >= clientnum)
        return -1;

    if (mes == NULL || !clients[pos].is_log)
        return -1;

    if (meslen > MES_MAX - MAX_NAMELEN - 16)
        return -1;

    size_t temp_size = MES_MAX + MAX_NAMELEN + 32;
    char *temp = malloc(temp_size);

    if (temp == NULL)
        return -1;

    int temp_len = snprintf(temp, temp_size, "[%s] : %.*s\n", clients[pos].name, (int)meslen, mes);

    if (temp_len < 0 || (size_t)temp_len >= temp_size || (size_t)temp_len > MES_MAX)
    {
        free(temp);
        return -1;
    }

    char *packed = temp;
    int packed_len = protocol_pack(&packed, CMD_CHAT);
    free(temp);

    if (packed_len < 0)
    {
        return -1;
    }

    for (int i = 0; i < clientnum; i++)
    {
        if (clients[i].fd < 0 || clients[i].fd == fd_myself || !clients[i].is_log)
        {
            continue;
        }

        size_t write_used = (size_t)clients[i].write_buf_used_unsend;

        if (write_used > WRBUF_SIZE || (size_t)packed_len > WRBUF_SIZE - write_used)
        {
            continue;
        }

        memcpy(clients[i].write_buf + write_used, packed, (size_t)packed_len);
        clients[i].write_buf_used_unsend += packed_len;

        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.data.fd = clients[i].fd;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;

        if (epoll_ctl(epollfd, EPOLL_CTL_MOD, clients[i].fd, &ev) < 0)
        {
            free(packed);
            return -1;
        }
    }

    free(packed);
    return 0;
}
int main()
{
    signal(SIGPIPE, SIG_IGN);

    int listenfd = tcp_listen("0.0.0.0", 7777, 100);
    if (listenfd == -1)
    {
        exit(EXIT_FAILURE);
    }

    int flag = fcntl(listenfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(listenfd, F_SETFL, flag);

    int epollfd = epoll_create(1);
    if (epollfd < 0)
    {
        perror("epoll");
        exit(EXIT_FAILURE);
    }
    struct epoll_event ev;
    struct epoll_event events_todo[MAX_CLIENTS + 1];
    ev.data.fd = listenfd;
    ev.events = EPOLLIN;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &ev);

    while (1)
    {
        int wait_res = epoll_wait(epollfd, events_todo, MAX_CLIENTS, 1000);
        if (wait_res < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        for (int i = 0; i < wait_res; i++)
        {
            int fd = events_todo[i].data.fd;
            if (fd == listenfd)
            {
                while (1)
                {
                    if (clientnum == MAX_CLIENTS)
                    {
                        printf("system is full\n");
                        break;
                    }
                    int clientfd = accept(listenfd, NULL, NULL);
                    if (clientfd < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break;
                        }
                        if (errno == EINTR)
                            continue;
                        perror("accept");
                        continue;
                    }
                    int flag = fcntl(clientfd, F_GETFL);
                    flag |= O_NONBLOCK;
                    fcntl(clientfd, F_SETFL, flag);
                    ev.data.fd = clientfd;
                    ev.events = EPOLLIN | EPOLLET;
                    int res = epoll_ctl(epollfd, EPOLL_CTL_ADD, clientfd, &ev);
                    if (res < 0)
                    {
                        close(clientfd);
                        break;
                    }
                    clients[clientnum].fd = clientfd;
                    clients[clientnum].is_log = 0;
                    clients[clientnum].read_buf_unread = 0;
                    clients[clientnum].write_buf_used_unsend = 0;
                    clients[clientnum].last_active = time(NULL);
                    clientnum++;
                }
            }
            else if (events_todo[i].events & EPOLLIN)
            {
                int pos = -1;
                for (int j = 0; j < clientnum; j++)
                {
                    if (clients[j].fd == fd)
                    {
                        pos = j;
                        break;
                    }
                }
                if (pos < 0)
                    continue;
                char buf[1024];
                while (1)
                {
                    int closed = 0;
                    ssize_t res = recv(fd, buf, 1023, 0);
                    if (res < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break;
                        }
                        if (errno == EINTR)
                            continue;
                        perror("read");
                        client_delet(fd);
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                        break;
                    }
                    if (res == 0)
                    {
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
                        client_delet(fd);
                        close(fd);
                        break;
                    }
                    if (res > RDBUF_SIZE - clients[pos].read_buf_unread)
                    {
                        client_delet(fd);
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                        break;
                    }
                    memcpy(clients[pos].read_buf + clients[pos].read_buf_unread, buf, res);
                    clients[pos].read_buf_unread += res;
                    clients[pos].last_active = time(NULL);
                    while (1)
                    {
                        uint16_t cmd;
                        uint32_t len;
                        if (clients[pos].read_buf_unread < HEAD_LEN)
                        {
                            break;
                        }
                        int res = protocol_unpack(clients[pos].read_buf, HEAD_LEN, &cmd, &len);
                        if (res < 0 || len > RDBUF_SIZE - HEAD_LEN)
                        {
                            closed = 1;
                            break;
                        }
                        else if ((size_t)clients[pos].read_buf_unread < len + HEAD_LEN)
                        {
                            break;
                        }
                        switch (cmd)
                        {
                        case CMD_CHAT:
                            if (sendtoall(epollfd, fd, pos, clients[pos].read_buf + HEAD_LEN, len))
                            {
                                closed = 1;
                            }
                            break;
                        case CMD_EXIT:
                            closed = 1;
                            break;
                        case CMD_HEART:
                            clients[pos].last_active = time(NULL);
                            break;
                        case CMD_LOGIN:
                            if (len == 0 || len >= MAX_NAMELEN)
                            {
                                closed = 1;
                                break;
                            }
                            int meslen;
                            char name[MAX_NAMELEN];
                            memcpy(name, clients[pos].read_buf + HEAD_LEN, len);
                            name[len] = '\0';
                            char *mes = login(fd, name) < 0 ? "no" : "ok";
                            meslen = protocol_pack(&mes, CMD_LOGIN_RES);
                            if (meslen < 0)
                            {
                                closed = 1;
                                break;
                            }
                            if (clients[pos].write_buf_used_unsend > WRBUF_SIZE || meslen > WRBUF_SIZE - clients[pos].write_buf_used_unsend)
                            {
                                free(mes);
                                closed = 1;
                                break;
                            }
                            memcpy(clients[pos].write_buf + clients[pos].write_buf_used_unsend, mes, meslen);
                            clients[pos].write_buf_used_unsend += meslen;
                            free(mes);
                            memset(&ev, 0, sizeof(ev));
                            ev.data.fd = clients[pos].fd;
                            ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                            if (epoll_ctl(epollfd, EPOLL_CTL_MOD, clients[pos].fd, &ev) < 0)
                            {
                                closed = 1;
                            }
                            clients[pos].last_active = time(NULL);
                            break;
                        default:
                            closed = 1;
                            break;
                        }
                        if (closed)
                        {
                            epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
                            client_delet(fd);
                            close(fd);
                            break;
                        }
                        memmove(clients[pos].read_buf, clients[pos].read_buf + HEAD_LEN + len, clients[pos].read_buf_unread - HEAD_LEN - len);
                        clients[pos].read_buf_unread -= HEAD_LEN + len;
                    }
                    if (closed)
                    {
                        break;
                    }
                }
            }
            if (events_todo[i].events & EPOLLOUT)
            {
                int pos = -1;
                for (int j = 0; j < clientnum; j++)
                {
                    if (clients[j].fd == fd)
                    {
                        pos = j;
                        break;
                    }
                }
                if (pos < 0)
                    continue;
                int closed = 0;
                while (clients[pos].write_buf_used_unsend > 0)
                {
                    int res = send(clients[pos].fd, clients[pos].write_buf, clients[pos].write_buf_used_unsend, 0);
                    if (res < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            closed = 0;
                            break;
                        }
                        if (errno == EINTR)
                            continue;
                        perror("send");

                        closed = 1;
                        break;
                    }
                    if (res == 0)
                    {
                        closed = 1;
                        break;
                    }
                    else
                    {
                        memmove(clients[pos].write_buf, clients[pos].write_buf + res, clients[pos].write_buf_used_unsend - res);
                        clients[pos].write_buf_used_unsend -= res;
                    }
                }
                if (closed)
                {
                    client_delet(fd);
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }
                if (clients[pos].write_buf_used_unsend == 0)
                {
                    memset(&ev, 0, sizeof(ev));
                    ev.data.fd = clients[pos].fd;
                    ev.events = EPOLLIN | EPOLLET;
                    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, clients[pos].fd, &ev) < 0)
                    {
                        epoll_ctl(epollfd,EPOLL_CTL_DEL,clients[pos].fd,NULL);
                        client_delet(clients[pos].fd);
                        close(clients[pos].fd);
                    }
                }
                else
                {
                    continue;
                }
            }
        }
    }
    close(listenfd);
    return 0;
}
