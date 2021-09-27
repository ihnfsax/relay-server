#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_CMD_STR 100

#pragma pack(1)
typedef struct Header {
    uint16_t len;
} Header;
#pragma pack()

void    send_msg(FILE* fp, int sockfd);
void    my_err_quit(const char* fmt);
void    my_inet_pton(int family, const char* strptr, void* addrptr);
void    init_port(const char* strptr, in_port_t* addrptr);
int     my_socket(int family, int type, int protocol);
void    my_bind(int fd, const struct sockaddr* sa, socklen_t salen);
void    my_listen(int fd, int backlog);
void    my_close(int fd);
ssize_t my_readn(int fd, void* vptr, size_t n);
ssize_t my_writen(int fd, const void* vptr, size_t n);
int     my_connect(int fd, const struct sockaddr* sa, socklen_t salen);

int main(int argc, char** argv) {
    /* 检查参数数量 */
    if (argc != 3)
        my_err_quit("usage: tcp_echo_cli <IP_Address> <Port>");

    /* 生成套接字描述符 */
    int sockfd = my_socket(AF_INET, SOCK_STREAM, 0);

    /* 创建地址结构 */
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    my_inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
    init_port(argv[2], &servaddr.sin_port);

    my_connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

    printf("[cli] server[%s:%s] is connected!\n", argv[1], argv[2]);

    send_msg(stdin, sockfd);

    my_close(sockfd);
    printf("[cli] connfd is closed!\n");
    printf("[cli] client is exiting!\n");

    return 0;
}

void send_msg(FILE* fp, int sockfd) {
    char   sendline[MAX_CMD_STR + 8 + 1], recvline[MAX_CMD_STR + 1], _exit_[5] = { 0 };
    int    sendlen, recvlen, n_len;
    Header header;  // 收到的数据包的Header

    while (1) {
        if (my_readn(sockfd, &header, sizeof(header)) != sizeof(header)) {  // 读取header
            fprintf(stderr, "echo_rpt: server terminated prematurely\n");
            return;
        }
        recvlen = ntohs(header.len);
        if (my_readn(sockfd, recvline, recvlen) != recvlen)  // 读取payload
        {
            fprintf(stderr, "echo_rpt: server terminated prematurely\n");
            return;
        }
        fprintf(stdout, "[recv_client] %s\n", recvline);
    }
}

/************************************
 在没有错误信息的情况下报错并退出程序
************************************/
void my_err_quit(const char* fmt) {
    fflush(stdout);
    fputs(fmt, stderr);
    fputc('\n', stderr);
    fflush(stderr);
    exit(1);
}

/************************************
 地址转换函数:presentation to numeric
************************************/
void my_inet_pton(int family, const char* strptr, void* addrptr) {
    int n;
    if ((n = inet_pton(family, strptr, addrptr)) < 0)
        perror("inet_pton error"), exit(1); /* errno set */
    else if (n == 0)
        my_err_quit("inet_pton error"); /* errno not set */
}

/************************************
 端口赋值函数:检测是否合法
************************************/
void init_port(const char* strptr, in_port_t* addrptr) {
    int port;
    if (strptr[0] == '0' && strlen(strptr) == 1)
        (*addrptr) = htons(0);
    else {
        port = (int)strtol(strptr, NULL, 10);
        if (port <= 0 || port > 65535)
            my_err_quit("port must be a number between 0 and 65535");
        else
            (*addrptr) = htons(port);
    }
}

/************************************
 创建套接字函数
************************************/
int my_socket(int family, int type, int protocol) {
    int n;
    if ((n = socket(family, type, protocol)) < 0)
        perror("socket error"), exit(1);
    return (n);
}

/************************************
 绑定地址和套接字函数(Server)
************************************/
void my_bind(int fd, const struct sockaddr* sa, socklen_t salen) {
    if (bind(fd, sa, salen) < 0)
        perror("bind error"), exit(1);
}

/************************************
 监听套接字函数(Server)
************************************/
void my_listen(int fd, int backlog) {
    char* ptr;
    /*4can override 2nd argument with environment variable */
    if ((ptr = getenv("LISTENQ")) != NULL)
        backlog = atoi(ptr);

    if (listen(fd, backlog) < 0)
        perror("listen error"), exit(1);
}

/************************************
 关闭文件描述符函数
************************************/
void my_close(int fd) {
    if ((close(fd)) < 0)
        perror("error close"), exit(1);
}

/************************************
 从套接字读取n字节函数
************************************/
ssize_t my_readn(int fd, void* vptr, size_t n) {
    size_t  nleft;
    ssize_t nread;
    char*   ptr;

    ptr   = vptr;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0) {
            if (errno == EINTR)
                nread = 0; /* and call read() again */
            else
                return (-1);
        }
        else if (nread == 0)
            break; /* EOF */

        nleft -= nread;
        ptr += nread;
    }
    if (n - nleft < 0)
        perror("readn error"), exit(1);

    return (n - nleft); /* return >= 0 */
}

/************************************
 向套接字写入n字节函数
************************************/
ssize_t my_writen(int fd, const void* vptr, size_t n) {
    size_t      nleft;
    ssize_t     nwritten;
    const char* ptr;
    ptr   = vptr;
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0) {
            if (nwritten < 0 && errno == EINTR)
                continue; /* and call write() again */
            else
                return (-1); /* error */
        }
        nleft -= nwritten;
        ptr += nwritten;
    }
    if (n - nleft < 0)
        perror("writen error"), exit(1);
    return (n);
}

/************************************
 建立连接函数(Client)
************************************/
int my_connect(int fd, const struct sockaddr* sa, socklen_t salen) {
    int n;
    if ((n = connect(fd, sa, salen)) < 0)
        printf("%d\n", errno), perror("connect error");
}