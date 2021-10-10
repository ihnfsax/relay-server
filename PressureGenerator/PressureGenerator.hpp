#include "common.hpp"
#include <map>
#include <string>

#define BUFFER_SIZE 4096

typedef void sigfunc(int);

typedef struct ClientBuffer {
    char   usrBuf[BUFFER_SIZE];       /* 用户缓冲区（用来接收） */
    size_t unrecv   = sizeof(Header); /* 期望接收的数据大小（仅用于判断是否读到报文头） */
    size_t recved   = 0;              /* 已经接收的数据量 */
    int    recvFlag = 0;              /* 0: 正在接收头部，非0：正在接收载荷 */
    Header recvHeader;                /* 正在接收报文的报头 */
    Header sendHeader;                /* 正在发送的报文的报头 */
    char*  sendPtr = nullptr;         /* 发送缓冲区指针 */
    size_t sended  = 0;               /* 已发送的数据 */
} ClientBuffer;

typedef struct ClientInfo {
    int           connfd;      /* 套接字 */
    int           state  = -1; /* -1: 未连接 0: 正常连接，1：关闭写的一端 */
    ClientBuffer* buffer = nullptr;
} ClientInfo;

class PressureGenerator {
private:
    struct sockaddr_in        servaddr;                /* 服务器地址结构 */
    std::map<int, ClientInfo> clients;                 /* 客户端集合 */
    int                       status = 0;              /* 发生器状态 */
    FILE*                     logfp  = nullptr;        /* log文件指针 */
    char                      logFilename[NAME_MAX];   /* log文件名 */
    pid_t                     pid;                     /* 进程ID */
    int                       epollfd;                 /* epoll描述符 */
    size_t                    cliCount    = 0;         /* 要求的会话数 */
    size_t                    payloadSize = 0;         /* 每个报文的载荷大小 */
    size_t                    runTime     = 0;         /* 要求的运行时间 */
    size_t                    connNum     = 0;         /* 已连接客户端数量 */
    size_t                    uncnNum     = 0;         /* 未连接客户端数量 */
    struct timespec           totalDelay;              /* 总延迟 */
    u_long                    recvPacketNum = 0;       /* 接收到的报文总数量 */
    char*                     payload       = nullptr; /* 初始化的数据包 */
    int                       shutFlag      = 0;       /* 是否已经把所有套接字写的一端关闭 */
    int                       recordFlag    = 0;       /* 是否开始发送报文 */
    static int                alrmFlag;                /* 写SIGALRM的log的标志 */
    static int                intFlag;                 /* 写SIGINT的log的标志 */
    static int                exitFlag;                /* 捕获信号后，退出标志 */

    void        generatePacket();
    int         doit(const char* ip, const char* port);
    int         addClients(struct epoll_event* events);
    void        addOneClient(int sockfd, int state);
    void        shutdownAll();
    int         handleEvents(struct epoll_event* events, const int& number);
    void        prepareExit();
    int         removeClient(const int& sockfd);
    void        addDelay(struct timespec* timestamp);
    uint16_t    handleHeader(struct Header* header, const int& sockfd);
    static void sigIntHandler(int signum);
    static void sigAlrmHandler(int signum);
    static void sigPipeHandler(int signum);
    sigfunc*    signal(int signo, sigfunc* func);

public:
    PressureGenerator() {
        srand((unsigned int)time(NULL));
        alrmFlag           = 0;
        intFlag            = 0;
        exitFlag           = 0;
        totalDelay.tv_nsec = 0;
        totalDelay.tv_sec  = 0;
        signal(SIGINT, sigIntHandler);
        signal(SIGALRM, sigAlrmHandler);
        signal(SIGPIPE, sigPipeHandler);
        pid = getpid();
        snprintf(logFilename, NAME_MAX - 1, "GENERATOR_%d.log", pid);
    }

    ~PressureGenerator() {
        if (logfp != nullptr) {
            fclose(logfp);
        }
    }

    int start(const char* ip, const char* port, int sessCount, int runTime, int packetSize, int logFlag = 0);
};
