#include "common.hpp"
#include <map>
#include <string>

#define RECVBUF_MAX 1024 /* 服务器接收缓冲区大小 */
#define SENDBUF_MAX 2048 /* 服务器发送缓冲区大小 */

typedef struct ClientInfo {
    uint16_t cliID;
    int      connfd;
    char     recvBuf[RECVBUF_MAX];        /* 接收缓冲区 */
    char     sendBuf[SENDBUF_MAX];        /* 发送缓冲区 */
    size_t   unrecv     = sizeof(Header); /* 期望接收的数据大小 */
    size_t   unsend     = 0;              /* 期望发送的数据大小 */
    char*    recvPtr    = recvBuf;        /* 接收缓冲区指针 */
    char*    sendPtr    = sendBuf;        /* 发送缓冲区指针 */
    int      recvStatus = 0;              /* 0: 正在接收头部，非0：正在接收载荷 */
} ClientInfo;

typedef struct File {
    FILE* fp;
    char  filename[NAME_MAX];
} File;

typedef void sigfunc(int);

/* 采用LT非阻塞模式 */
class RelayServer {
private:
    std::map<uint16_t, ClientInfo*> clientIDs;             /* 已连接客户端集合1 */
    std::map<int, ClientInfo*>      clientFDs;             /* 已连接客户端集合2 */
    std::map<uint16_t, File>        msgAppend;             /* 未发送的数据 */
    std::map<uint16_t, File>        msgRead;               /* 未发送的数据 */
    int                             status = 0;            /* 服务器状态 */
    FILE*                           logfp  = nullptr;      /* log文件指针 */
    pid_t                           pid;                   /* 进程ID */
    char                            logFilename[NAME_MAX]; /* log文件名 */
    int                             listenfd;              /* 监听套接字 */
    int                             epollfd;               /* epoll描述符 */
    uint16_t                        nextID = 0;            /* 下一个可用的ID */
    size_t                          hSize  = 0;            /* 报文头部长度 */
    static int                      exitFlag;

    int         doit(const char* ip, const char* port);
    int         handle_events(struct epoll_event* events, const int& number);
    void        closeServer();
    int         addClient(ClientInfo* client);
    int         removeClient(const int& connfd);
    void        updateNextID();
    int         writeMsgToFile(const int& id, const void* buf, const size_t& size);
    int         copySavedMsg(const int& id);
    static void sigIntHandler(int signum);
    sigfunc*    signal(int signo, sigfunc* func);

public:
    RelayServer() {
        exitFlag = 0;
        signal(SIGINT, sigIntHandler);
        pid = getpid();
        snprintf(logFilename, NAME_MAX - 1, "SERVER_%d.log", pid);
    }

    ~RelayServer() {
        if (logfp != nullptr) {
            fclose(logfp);
        }
    }

    int start(const char* ip, const char* port, int logFlag = 0);
};