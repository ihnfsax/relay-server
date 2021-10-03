#include "common.hpp"
#include <map>
#include <string>
#define ET_MODE 0 /* 1：开启ET模式，0：开启LT模式 */

const char* str1 = "hjjjjjfdsafdasf dasfdasfewqreqw";

class PressureGenerator {
private:
    struct sockaddr_in servaddr;              /* 服务器地址结构 */
    std::map<int, int> clientFDs;             /* 已连接客户端集合 */
    int                status = 0;            /* 发生器状态 */
    FILE*              logfp  = nullptr;      /* log文件指针 */
    char               logFilename[NAME_MAX]; /* log文件名 */
    pid_t              pid;                   /* 进程ID */
    int                epollfd;               /* epoll描述符 */
    struct timeval     tv;                    /* 读写操作超时限制 */
    size_t             hSize       = 0;       /* 报文头部长度 */
    size_t             sessCount   = 0;       /* 要求的会话数 */
    int                time        = 0;       /* 要求的运行时间 */
    int                timeoutFlag = 0;       /* 超时关闭发生器标志 */

    int  doit(const char* ip, const char* port);
    int  setTimeout(const int& sockfd);
    int  addClients(struct epoll_event* events);
    void closeGenerator();
    void handle_events(struct epoll_event* events, const int& number);
    int  recvfromServer(const int& sockfd);
    int  sendToServer(const int& sockfd);
    int  etRecv(const int& sockfd);
    int  ltRecv(const int& sockfd);
    int  blockingRecv(const int& sockfd, void* buf, const size_t& size);
    int  sendToServer(const int& sockfd, const void* buf, const size_t& size);
    int  removeClient(const int& sockfd);

public:
    PressureGenerator() {
        pid = getpid();
        snprintf(logFilename, NAME_MAX - 1, "GENERATOR_%d.log", pid);
        tv.tv_sec  = 3;
        tv.tv_usec = 0;
        hSize      = sizeof(Header);
    }

    int start(const char* ip, const char* port, int sessCount, int time, int logFlag = 0);
};
