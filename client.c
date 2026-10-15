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

void *read_thread(void *arg)
{
    int client_fd = (int)(intptr_t)arg;
    char buf[1024];
    ssize_t len;

    while (1)
    {
        memset(buf, 0, sizeof(buf));
        len = recv(client_fd, buf, sizeof(buf) - 1, 0);

        if (len == 0) break;                    /* 服务端关闭了连接 */
        if (len < 0)
        {
            if (errno == EINTR) continue;       /* 被信号打断，重试 */
            perror("recv");
            break;                              /* 真正的错误 */
        }

        buf[len] = '\0';
        fputs(buf, stdout);
        fflush(stdout);
    }

    printf("server ask to close\n");
    return NULL;
}

void *write_thread(void *arg)
{
    int client_fd = (int)(intptr_t)arg;
    char buf[1024];

    while (fgets(buf, sizeof(buf), stdin) != NULL)
    {
        ssize_t len = send(client_fd, buf, strlen(buf), 0);
        if (len < 0)
        {
            perror("send");
            break;
        }
    }

    printf("stdin closed, stop sending to server\n");
    shutdown(client_fd, SHUT_WR);
    return NULL;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(7777);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &server_addr.sin_addr, ip, sizeof(ip));
    printf("connect server ip:%s port:%d\n", ip, ntohs(server_addr.sin_port));

    pthread_t pid_read, pid_write;
    if (pthread_create(&pid_read, NULL, read_thread, (void *)(intptr_t)sockfd) != 0)
    {
        perror("pthread_create");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&pid_write, NULL, write_thread, (void *)(intptr_t)sockfd) != 0)
    {
        perror("pthread_create");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    pthread_join(pid_read, NULL);
    pthread_join(pid_write, NULL);

    close(sockfd);
    return 0;
}
