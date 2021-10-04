#include <arpa/inet.h>
#include <assert.h>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wait.h>

#define NAME_MAX 255                                       /* chars in a file name */
#define LINE_MAX 255                                       /* char in one line of log file */
#define MAX_EVENT_NUMBER 30000                             /* 事件数 */
#define counterPart(self) (self % 2 ? self - 1 : self + 1) /* 得到对端客户端ID */

/* 报文头结构 */
#pragma pack(1)
typedef struct Header {
    uint16_t length; /* payload长度 */
} Header;
#pragma pack()

/* 获取时间字符串 */
std::string prettyTime();

/* 打印非errno消息到log文件 */
int logInfo(int returnValue, FILE* fp, const char* fmt, ...);

/* 打印errno消息到log文件 */
int logError(int returnValue, FILE* fp, const char* fmt, ...);

/* 创建套接字 */
int createSocket(int family, int type, int protocol, FILE* fp = nullptr);

/* 为套接字绑定网络地址 */
int toBind(int fd, const struct sockaddr* sa, socklen_t salen, FILE* fp = nullptr);

/* 转换为监听套接字 */
int toListen(int fd, int backlog, FILE* fp = nullptr);

/* 将IP地址的字符串转换为网络形式 */
int inetPton(int family, const char* strptr, void* addrptr, FILE* fp = nullptr);

/* 将端口字符串转换为网络字节序 */
int setPort(const char* strptr, in_port_t* addrptr, FILE* fp = nullptr);

/* 将文件描述符设置为非阻塞 */
int setnonblocking(int fd);

/* 将文件描述符设置为阻塞 */
int setblocking(int fd);

/* 将文件描述符fd上的EPOLLIN注册到epollfd指示的epoll内核事件表中，参数enable_et指定是否对fd启用ET模式 */
void addfd(int epollfd, int fd, int enable_out, int enable_et);

/* 将文件描述符从epoll事件表中删除 */
void delfd(int epollfd, int fd);