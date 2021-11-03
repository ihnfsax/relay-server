#include "../common/common.hpp"
#include <map>
#include <string>

#define BUFFER_SIZE 8192 /* 服务器为每个客户端分配的用户缓冲区大小 */
#define BACKLOG 128      /* listen队列总大小 */

typedef struct ClientInfo {
    uint16_t    cliID;                     /* 客户ID（仅用于服务器区分客户端） */
    int         connfd;                    /* 套接字文件描述符 */
    char        usrBuf[BUFFER_SIZE];       /* 缓冲区 */
    size_t      unrecv   = sizeof(Header); /* 期望接收的数据大小（仅用于判断是否读到报文头） */
    size_t      recved   = 0;              /* 已经接收的数据量 */
    int         recvFlag = 0;              /* 0: 正在接收头部，非0：正在接收载荷 */
    Header      header;                    /* 正在接收报文的报头 */
    ClientInfo* fakePeer = nullptr;        /* 用于保存文件内容假客户端 */
    int         state    = 0;              /* 0:未关闭套接字 1:已关闭写的一端 */
    uint32_t    id;                        /* 报文中的id，DEBUG用 */
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
    uint16_t                        nextID   = 0;          /* 下一个可用的ID */
    size_t                          hSize    = 0;          /* 报文头部长度 */
    int                             shutFlag = 0;          /* 是否已经把所有套接字写的一端关闭 */
    static int                      exitFlag;              /* SIGINT退出标志 */
    static int                      logFlag;               /* 写SIGINT的log */
    uint64_t                        s_recvNoSpace = 0;     /* 可以接收但没有足够的应用缓冲区的次数 */
    uint64_t                        s_recvBytes   = 0;     /* 接收到的数据数量 */
    uint64_t                        s_recvPackets = 0;     /* 收到到报文数量 */
    uint64_t                        s_recvSuccess = 0;     /* 接收到数据的次数 */
    uint64_t                        s_recvEAGAIN  = 0;     /* recv 返回EWOULDBLOCK的次数 */
    uint64_t                        s_recvError   = 0;     /* recv 返回其他错误的次数 */
    uint64_t                        s_recvFINs    = 0;     /* recv 返回0的次数 */
    uint64_t                        s_sendNoData  = 0;     /* 可写但无数据可发的次数 */
    uint64_t                        s_sendBytes   = 0;     /* 发送的数据量 */
    uint64_t                        s_sendSuccess = 0;     /* 成功发送数据的次数 */
    uint64_t                        s_sendEAGAIN  = 0;     /* send 返回EWOULDBLOCK的次数 */
    uint64_t                        s_sendError   = 0;     /* send 返回其他错误的次数 */

    int         doit(const char* ip, const char* port);
    int         handleEvents(struct epoll_event* events, const int& number);
    void        shutdownAll();
    void        prepareExit();
    int         addClient(ClientInfo* client);
    int         removeClient(const int& connfd);
    void        updateNextID();
    int         writeMsgToFile(const int& cliID, const void* buf, const size_t& size);
    int         copySavedMsg(ClientInfo* selfC);
    static void sigIntHandler(int signum);
    static void sigPipeHandler(int signum);
    sigfunc*    signal(int signo, sigfunc* func);
    uint16_t    handleHeader(struct Header* header, const uint16_t& cliID);
    void        printStatistics();

public:
    RelayServer() {
        logFlag  = 0;
        exitFlag = 0;
        signal(SIGINT, sigIntHandler);
        signal(SIGPIPE, sigPipeHandler);
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