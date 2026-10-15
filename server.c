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
#include <stdint.h>

#define MAX_CLIENTS 2000

int clientfds[MAX_CLIENTS] = {0};
int clientnum = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *sendtoall_thread(void *arg)
{
    char read_buf[1200];
    int clientfd = (int)(intptr_t)arg;
    ssize_t read_len;

    while (1)
    {
        read_len = recv(clientfd, read_buf, sizeof(read_buf) - 1, 0);

        if (read_len < 0)
        {
            if (errno == EINTR) continue;   /* 被信号打断，重试 */
            perror("recv");
            break;                          /* 真正的错误 */
        }
        if (read_len == 0)
        {
            break;                          /* 对端正常关闭，此时 errno 无意义 */
        }

        read_buf[read_len] = '\0';

        char temp[1300];
        snprintf(temp, sizeof(temp), "[client%d] : %s\n", clientfd, read_buf);

        int clientfds_copy[MAX_CLIENTS];
        int clientnum_copy;

        pthread_mutex_lock(&mutex);
        for (int i = 0; i < clientnum; i++) clientfds_copy[i] = clientfds[i];
        clientnum_copy = clientnum;
        pthread_mutex_unlock(&mutex);

        for (int i = 0; i < clientnum_copy; i++)
        {
            int fd_send = clientfds_copy[i];
            if (fd_send == 0 || fd_send == clientfd) continue;

            ssize_t n = send(fd_send, temp, strlen(temp), 0);
            if (n < 0 && (errno == EPIPE || errno == ECONNRESET))
            {
                /* 对端已断开，它自己的线程会在 recv 里发现并清理 */
                continue;
            }
            /* n < strlen(temp) 表示只发出去一部分，这里暂不处理，
               第 2 步写协议时用写缓冲区解决 */
        }
    }

    int clientfds_copy[MAX_CLIENTS];
    int clientnum_copy;

    pthread_mutex_lock(&mutex);
    for (int i = 0; i < clientnum; i++) clientfds_copy[i] = clientfds[i];
    clientnum_copy = clientnum;

    for (int i = 0; i < clientnum; i++)
    {
        if (clientfds[i] == clientfd)
        {
            clientfds[i] = 0;
            for (int p = i + 1; p < clientnum; p++) clientfds[p - 1] = clientfds[p];
            clientfds[clientnum - 1] = 0;
            clientnum--;
            break;
        }
    }
    pthread_mutex_unlock(&mutex);

    char temp[256];
    snprintf(temp, sizeof(temp), "------client %d exit------\n", clientfd);
    size_t len = strlen(temp);

    for (int j = 0; j < clientnum_copy; j++)
    {
        int fd_send = clientfds_copy[j];
        if (fd_send == 0 || fd_send == clientfd) continue;

        ssize_t n = send(fd_send, temp, len, 0);
        if (n < 0 && (errno == EPIPE || errno == ECONNRESET)) continue;
    }

    close(clientfd);
    return NULL;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    struct sockaddr_in server_addr, client_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "0.0.0.0", &server_addr.sin_addr);
    server_addr.sin_port = htons(7777);

    int serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(serverfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    if (listen(serverfd, 100) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    socklen_t clientaddr_len = sizeof(client_addr);

    while (1)
    {
        int fd = accept(serverfd, (struct sockaddr *)&client_addr, &clientaddr_len);
        if (fd < 0)
        {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        pthread_mutex_lock(&mutex);
        if (clientnum >= MAX_CLIENTS)
        {
            pthread_mutex_unlock(&mutex);
            fprintf(stderr, "连接数已满，拒绝新连接 fd=%d\n", fd);
            close(fd);
            continue;
        }
        clientfds[clientnum++] = fd;
        pthread_mutex_unlock(&mutex);

        pthread_t tid;
        if (pthread_create(&tid, NULL, sendtoall_thread, (void *)(intptr_t)fd) != 0)
        {
            perror("pthread_create");
            pthread_mutex_lock(&mutex);
            for (int i = 0; i < clientnum; i++)
            {
                if (clientfds[i] == fd)
                {
                    for (int p = i + 1; p < clientnum; p++) clientfds[p - 1] = clientfds[p];
                    clientfds[clientnum - 1] = 0;
                    clientnum--;
                    break;
                }
            }
            pthread_mutex_unlock(&mutex);
            close(fd);
            continue;
        }
        pthread_detach(tid);
    }

    close(serverfd);
    return 0;
}
