#include "common.hpp"
#include <map>
#include <string>

const char* str1 = "hjjjjjfdsafdasf dasfdasfewqreqw";

#define RECVBUF_MAX 1024 /* 客户端接收缓冲区大小 */
#define SENDBUF_MAX 2048 /* 客户端发送缓冲区大小 */

typedef struct ClientBuffer {
    char   recvBuf[RECVBUF_MAX];     /* 接收缓冲区 */
    char   sendBuf[SENDBUF_MAX];     /* 发送缓冲区 */
    size_t unrecv  = sizeof(Header); /* 期望接收的数据大小 */
    char*  recvPtr = recvBuf;        /* 接收缓冲区指针 */
    char*  sendPtr = sendBuf;        /* 发送缓冲区指针 */
} ClientBuffer;

typedef struct ClientInfo {
    int           connfd;      /* 套接字 */
    int           status = -1; /* -1: 未连接 0: 正在接收头部，非0：正在接收载荷 */
    ClientBuffer* buffer = nullptr;
} ClientInfo;

class PressureGenerator {
private:
    struct sockaddr_in        servaddr;              /* 服务器地址结构 */
    std::map<int, ClientInfo> clients;               /* 客户端集合 */
    int                       status = 0;            /* 发生器状态 */
    FILE*                     logfp  = nullptr;      /* log文件指针 */
    char                      logFilename[NAME_MAX]; /* log文件名 */
    pid_t                     pid;                   /* 进程ID */
    int                       epollfd;               /* epoll描述符 */
    struct timeval            tv;                    /* 读写操作超时限制 */
    size_t                    hSize     = 0;         /* 报文头部长度 */
    size_t                    sessCount = 0;         /* 要求的会话数 */
    int                       time      = 0;         /* 要求的运行时间 */
    static int                timeoutFlag;           /* SIGALARM发生标志 */
    static int                exitFlag;              /* SIGINT发生标志 */

    int  doit(const char* ip, const char* port);
    int  addClients(struct epoll_event* events);
    void closeGenerator();
    int  handle_events(struct epoll_event* events, const int& number);
    int  recvfromServer(const int& sockfd);
    int  sendToServer(const int& sockfd);
    int  etRecv(const int& sockfd);
    int  ltRecv(const int& sockfd);
    int  blockingRecv(const int& sockfd, void* buf, const size_t& size);
    int  sendToServer(const int& sockfd, const void* buf, const size_t& size);
    int  removeClient(const int& sockfd);

public:
    PressureGenerator() {
        timeoutFlag = 0;
        exitFlag    = 0;
        pid         = getpid();
        snprintf(logFilename, NAME_MAX - 1, "GENERATOR_%d.log", pid);
        tv.tv_sec  = 3;
        tv.tv_usec = 0;
        hSize      = sizeof(Header);
    }

    int start(const char* ip, const char* port, int sessCount, int time, int logFlag = 0);
};
