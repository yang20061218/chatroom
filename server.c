/*
 * Current epoll chat server.
 * Single-thread event loop with nonblocking accept/read/write, connection
 * buffers, login, heartbeat timeout and the shared 11-byte protocol.
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
int set_nonblocking(int fd)
{
    int flag = fcntl(fd, F_GETFL);
    if (flag < 0)
    {
        return -1;
    }
    flag |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flag) < 0)
    {
        return -1;
    }
    return 0;
}
int update_interest(int epollfd, int pos)
{
    struct epoll_event ev;
    if (clients[pos].write_buf_used_unsend == 0)
    {
        memset(&ev, 0, sizeof(ev));
        ev.data.fd = clients[pos].fd;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        if (epoll_ctl(epollfd, EPOLL_CTL_MOD, clients[pos].fd, &ev) < 0)
        {
            return -1;
        }
    }
    else
    {
        memset(&ev, 0, sizeof(ev));
        ev.data.fd = clients[pos].fd;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
        if (epoll_ctl(epollfd, EPOLL_CTL_MOD, clients[pos].fd, &ev) < 0)
        {
            return -1;
        }
    }
    return 0;
}
int find_client(int fd)
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
int client_write_buf_add(int epollfd, int pos, const char *data, size_t data_len)
{
    if (epollfd < 0 || pos < 0 || pos >= clientnum || data == NULL || data_len == 0)
    {
        return -1;
    }
    size_t old_used = (size_t)clients[pos].write_buf_used_unsend;
    size_t write_used = (size_t)clients[pos].write_buf_used_unsend;

    if (write_used > WRBUF_SIZE || (size_t)data_len > WRBUF_SIZE - write_used)
    {
        return -1;
    }

    memcpy(clients[pos].write_buf + write_used, data, (size_t)data_len);
    clients[pos].write_buf_used_unsend += data_len;

    if (update_interest(epollfd, pos))
    {
        clients[pos].write_buf_used_unsend = old_used;
        return -1;
    }
    return 0;
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
        client_write_buf_add(epollfd, i, packed, packed_len);
    }
    free(packed);
    return 0;
}
int broadcast(int epollfd, char *mes, uint32_t meslen)
{
    if (epollfd < 0)
        return -1;

    if (mes == NULL)
        return -1;

    if (meslen > MAX_NAMELEN + 50)
        return -1;

    char *packed = mes;
    int packed_len = protocol_pack(&packed, CMD_CHAT);

    if (packed_len < 0)
    {
        return -1;
    }
    for (int i = 0; i < clientnum; i++)
    {
        if (clients[i].fd < 0 || !clients[i].is_log)
        {
            continue;
        }
        client_write_buf_add(epollfd, i, packed, packed_len);
    }

    free(packed);
    return 0;
}
int client_close(int epollfd, int pos)
{
    if (pos < 0 || pos >= clientnum)
    {
        return -1;
    }
    int clientfd = clients[pos].fd;
    if (clientfd < 0)
    {
        return -1;
    }
    int is_login = clients[pos].is_log;
    char *name = clients[pos].name;
    char temp[MAX_NAMELEN + 50];
    int temp_len = snprintf(temp, MAX_NAMELEN + 50, "------%s exit------\n", name);
    epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL);
    close(clientfd);
    clients[pos].fd = 0;
    for (int p = pos + 1; p < clientnum; p++)
    {
        clients[p - 1] = clients[p];
    }
    memset(&clients[clientnum - 1], 0, sizeof(client));
    clientnum--;
    if (is_login)
    {
        broadcast(epollfd, temp, temp_len);
    }
    return 0;
}
int check_timeout(int epollfd)
{
    time_t time_now = time(NULL);
    for (int i = 0; i < clientnum; i++)
    {
        if (time_now - clients[i].last_active > 30 && clients[i].is_log)
        {
            if (client_close(epollfd, i) == 0)
            {
                i--;
            }
        }
        else if (time_now - clients[i].last_active > 60 && !clients[i].is_log)
        {
            if (client_close(epollfd, i) == 0)
            {
                i--;
            }
        }
    }
    return 0;
}
int accept_clients(int epollfd, int listenfd)
{
    if (listenfd < 0 || epollfd < 0)
    {
        return -1;
    }
    while (1)
    {
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
            break;
        }
        if (clientnum == MAX_CLIENTS)
        {
            printf("system is full\n");
            close(clientfd);
            continue;
        }
        if (set_nonblocking(clientfd) < 0)
        {
            close(clientfd);
            continue;
        }
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.data.fd = clientfd;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientfd, &ev) < 0)
        {
            close(clientfd);
            continue;
        }
        memset(&clients[clientnum], 0, sizeof(client));
        clients[clientnum].fd = clientfd;
        clients[clientnum].is_log = 0;
        clients[clientnum].read_buf_unread = 0;
        clients[clientnum].write_buf_used_unsend = 0;
        clients[clientnum].last_active = time(NULL);
        clientnum++;
    }
    return 0;
}
int handle_message(int epollfd, int pos, uint16_t cmd, char *mes, uint32_t len)
{
    if (epollfd < 0 || pos < 0)
    {
        return -1;
    }
    int fd = clients[pos].fd;
    switch (cmd)
    {
    case CMD_CHAT:
        if (sendtoall(epollfd, fd, pos, mes, len))
        {
            return -1;
        }
        break;
    case CMD_EXIT:
        return -1;
    case CMD_HEART:
        if (clients[pos].is_log)
            clients[pos].last_active = time(NULL);
        break;
    case CMD_LOGIN:
        if (len == 0 || len >= MAX_NAMELEN)
        {
            return -1;
        }
        int meslen;
        char name[MAX_NAMELEN];
        memcpy(name, mes, len);
        name[len] = '\0';
        char *remes = login(fd, name) < 0 ? "no" : "ok";
        meslen = protocol_pack(&remes, CMD_LOGIN_RES);
        if (meslen < 0)
        {
            return -1;
        }
        if (client_write_buf_add(epollfd, pos, remes, meslen) < 0)
        {
            free(remes);
            return -1;
        }
        free(remes);
        clients[pos].last_active = time(NULL);
        break;
    default:
        return -1;
    }
    return 0;
}
int process_messages(int epollfd, int pos)
{
    if (epollfd < 0 || pos < 0)
    {
        return -1;
    }
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
            return -1;
        }
        else if ((size_t)clients[pos].read_buf_unread < len + HEAD_LEN)
        {
            break;
        }
        char *mes = clients[pos].read_buf + HEAD_LEN;
        if (handle_message(epollfd, pos, cmd, mes, len) < 0)
        {
            return -1;
        }
        memmove(clients[pos].read_buf, clients[pos].read_buf + HEAD_LEN + len, clients[pos].read_buf_unread - HEAD_LEN - len);
        clients[pos].read_buf_unread -= HEAD_LEN + len;
    }
    return 0;
}
int client_read(int epollfd, int pos)
{
    if (pos < 0 || epollfd < 0)
    {
        return -1;
    }
    int fd = clients[pos].fd;
    char buf[1024];
    while (1)
    {
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
            return -1;
        }
        if (res == 0)
        {
            return -1;
        }
        if (res > RDBUF_SIZE - clients[pos].read_buf_unread)
        {
            return -1;
        }
        if (clients[pos].is_log)
            clients[pos].last_active = time(NULL);
        memcpy(clients[pos].read_buf + clients[pos].read_buf_unread, buf, res);
        clients[pos].read_buf_unread += res;
        if (process_messages(epollfd, pos) < 0)
        {
            return -1;
        }
    }
    return 0;
}
int client_write(int epollfd, int pos)
{
    if (epollfd < 0 || pos < 0 || pos >= clientnum || clients[pos].fd < 0)
    {
        return -1;
    }
    while (clients[pos].write_buf_used_unsend > 0)
    {
        int res = send(clients[pos].fd, clients[pos].write_buf, clients[pos].write_buf_used_unsend, 0);
        if (res < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return 0;
            }
            if (errno == EINTR)
                continue;
            perror("send");
            return -1;
        }
        if (res == 0)
        {
            return -1;
        }
        else
        {
            memmove(clients[pos].write_buf, clients[pos].write_buf + res, clients[pos].write_buf_used_unsend - res);
            clients[pos].write_buf_used_unsend -= res;
        }
    }
    if (clients[pos].write_buf_used_unsend == 0)
    {
        if (update_interest(epollfd, pos) < 0)
        {
            return -1;
        }
    }
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
    if (set_nonblocking(listenfd) < 0)
    {
        exit(EXIT_FAILURE);
    }
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
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &ev) < 0)
    {
        exit(EXIT_FAILURE);
    }
    while (1)
    {
        int wait_res = epoll_wait(epollfd, events_todo, MAX_CLIENTS + 1, 1000);
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
                if (events_todo[i].events & (EPOLLERR | EPOLLHUP))
                {
                    exit(EXIT_FAILURE);
                }
                if (accept_clients(epollfd, listenfd) < 0)
                {
                    printf("accept clients failed\n");
                }
                continue;
            }
            if (events_todo[i].events & EPOLLIN)
            {
                int pos = find_client(fd);
                if (pos < 0)
                {
                    continue;
                }
                if (client_read(epollfd, pos) < 0)
                {
                    client_close(epollfd, pos);
                    continue;
                }
            }
            if (events_todo[i].events & EPOLLOUT)
            {
                int pos = find_client(fd);
                if (pos < 0)
                {
                    continue;
                }
                if (client_write(epollfd, pos) < 0)
                {
                    client_close(epollfd, pos);
                    continue;
                }
            }
            if (events_todo[i].events & (EPOLLERR | EPOLLRDHUP | EPOLLHUP))
            {
                int pos = find_client(fd);
                if (pos >= 0)
                {
                    client_close(epollfd, pos);
                }
                continue;
            }
        }
        check_timeout(epollfd);
    }
    close(listenfd);
    return 0;
}
