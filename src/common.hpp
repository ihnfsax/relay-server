#include <arpa/inet.h>
#include <cstdint>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <wait.h>

#define NAME_MAX 255                                       /* chars in a file name */
#define LINE_MAX 255                                       /* char in one line of log file */
#define counterPart(self) (self % 2 ? self - 1 : self + 1) /* 得到对端客户端ID */

/* 报文头结构 */
#pragma pack(1)
typedef struct Header {
    uint16_t length; /* payload长度 */
    uint16_t target; /* 目的客户端编号 */
} Header;
#pragma pack()

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

/* 关闭文件描述符 */
int toClose(int fd, FILE* fp = nullptr);

/* 将IP地址的字符串转换为网络形式 */
int inetPton(int family, const char* strptr, void* addrptr, FILE* fp = nullptr);

/* 将端口字符串转换为网络字节序 */
int setPort(const char* strptr, in_port_t* addrptr, FILE* fp = nullptr);