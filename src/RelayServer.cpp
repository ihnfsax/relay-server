#include "RelayServer.hpp"

int RelayServer::start(std::string ip, std::string port, int logFlag) {
    return start(ip.c_str(), port.c_str(), logFlag);
}

int RelayServer::start(const char* ip, const char* port, int logFlag) {
    /* 确定log文件 */
    if (logFlag) {
        logfp = fopen(logFilename, "w");
        if (logfp == nullptr) {
            printf("Failed to open log file.\n");
            return -1;
        }
        printf("The log file is specified as %s.\n", logFilename);
    }
    else {
        logfp = nullptr;
        printf("No log file specified.\n");
    }
    int r = doit(ip, port);
    if (logfp != nullptr) {
        fclose(logfp);
        logfp = nullptr;
    }
    return r;
}

int RelayServer::doit(const char* ip, const char* port) {
    int r;
    /* 初始化地址结构 */
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    if (inetPton(AF_INET, ip, &servaddr.sin_addr, logfp) < 0)
        return -1;
    if (setPort(port, &servaddr.sin_port, logfp) < 0)
        return -1;

    /* 创建套接字 */
    int listenfd;
    if ((listenfd = createSocket(AF_INET, SOCK_STREAM, 0, logfp)) < 0)
        return -1;

    /* 设置套接字选项 */
    r = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, (const void*)&r, sizeof(int)) < 0) {
        logError(-1, logfp, "setsockopt");
        toClose(listenfd, logfp);
        return -1;
    }

    /* 绑定地址和套接字 */
    if (toBind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr), logfp) < 0) {
        toClose(listenfd, logfp);
        return -1;
    }

    logInfo(0, logfp, "[LOG] Relay server has binded to %s:%s", ip, port);
    return toClose(listenfd, logfp);
}