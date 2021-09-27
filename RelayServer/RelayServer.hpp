#include "common.hpp"
#include <map>
#include <string>

typedef struct clientInfo {
    uint16_t cliID;
    int      connfd;
} clientInfo;

class RelayServer {
private:
    std::map<uint16_t, clientInfo*> clientIDs;             /* 已连接客户端集合1 */
    std::map<int, clientInfo*>      clientFDs;             /* 已连接客户端集合2 */
    std::map<uint16_t, FILE*>       msgUnsend;             /* 未发送的数据 */
    int                             status = 0;            /* 服务器状态 */
    FILE*                           logfp  = nullptr;      /* log文件指针 */
    pid_t                           pid;                   /* 进程ID */
    char                            logFilename[NAME_MAX]; /* log文件名 */
    int                             listenfd;              /* 监听套接字 */
    int                             epollfd;               /* epoll描述符 */
    uint16_t                        nextID = 0;            /* 下一个可用的ID */
    struct timeval                  tv;                    /* 读写操作超时限制 */
    int                             recvID;                /* 正在接收的客户端ID */
    int                             sendID;                /* 正在发送的客户端ID */
    size_t                          hSize = 0;             /* 报文头部长度 */
    int                             doit(const char* ip, const char* port);
    int                             setTimeout(const int& connfd);
    void                            handle_events(struct epoll_event* events, const int& number);
    void                            closeServer();
    int                             addClient(clientInfo* client);
    int                             removeClient(const int& connfd);
    void                            updateNextID();
    int                             recvFromClient(const int& connfd, void* buf, const size_t& size);
    int                             sendToClient(const int& connfd, const void* buf, const size_t& size);
    int                             writeMsgToFile(const int& id, const void* buf, const size_t& size);
    int                             sendSavedMsgToClient(const int& connfd, const int& id);

public:
    RelayServer() {
        pid = getpid();
        snprintf(logFilename, NAME_MAX - 1, "SERVER_%d.log", pid);
        tv.tv_sec  = 3;
        tv.tv_usec = 0;
        hSize      = sizeof(Header);
    }

    ~RelayServer() {
        if (logfp != nullptr) {
            fclose(logfp);
        }
    }

    int start(const char* ip, const char* port, int logFlag = 0);
    int start(std::string ip, std::string port, int logFlag = 0);
};