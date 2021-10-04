#include "PressureGenerator.hpp"
#include <vector>

#define CONN_SIZE 10
#define ERROR_MAX 10

int PressureGenerator::timeoutFlag = 0;
int PressureGenerator::exitFlag    = 0;

const char* str1          = "hjjjjjfdsafdasf dasfdasfewqreqw";
const char* str2          = "fdsafasfdasfdasfdasfsaf";
const char* msgs[MSG_NUM] = { str1, str2 };

int PressureGenerator::start(const char* ip, const char* port, int sessCount, int runTime, int logFlag) {
    if (sessCount <= 0) {
        printf("The number of sessions must be positive\n");
        return -1;
    }
    if (sessCount * 2 > MAX_EVENT_NUMBER) {
        printf("The number of sessions must be less than %d\n", MAX_EVENT_NUMBER / 2);
        return -1;
    }
    this->sessCount = (size_t)sessCount;
    if (runTime <= 0) {
        printf("The test time must be positive\n");
    }
    this->runTime = runTime;
    if (status != 0) {
        printf("This PressureGenerator has been started.\n");
        return -1;
    }
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
    alarm(this->runTime);
    logInfo(0, logfp, "PressureGenerator - generator - plan to run %d seconds", this->runTime);
    int r = doit(ip, port);
    logInfo(0, logfp, "PressureGenerator - generator - generator shutdown");
    if (logfp != nullptr) {
        fclose(logfp);
        logfp = nullptr;
    }
    status = 0;
    return r;
}

int PressureGenerator::doit(const char* ip, const char* port) {
    /* 初始化服务器地址结构 */
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    if (inetPton(AF_INET, ip, &servaddr.sin_addr, logfp) < 0)
        return -1;
    if (setPort(port, &servaddr.sin_port, logfp) < 0)
        return -1;

    /* 创建epoll事件表描述符 */
    struct epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(1);
    assert(epollfd >= 0);

    while (!timeoutFlag && !exitFlag) {
        if (clients.size() < sessCount) {
            if (addClients(events) < 0) {
                logInfo(0, logfp, "PressureGenerator - generator - too many errors during adding clients");
                closeGenerator();
                return 0;
            }
        }
        int ready = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ready < 0) {
            logError(-1, logfp, "PressureGenerator - generator - epoll_wait error");
            closeGenerator();
            return 0;
        }
        if (handle_events(events, ready) < 0) {
            closeGenerator();
            return 0;
        }
    }
    closeGenerator();
    return 0;
}

int PressureGenerator::addClients(struct epoll_event* events) {
    int errorTimes = ERROR_MAX;
    int connTimes  = CONN_SIZE;
    while (clients.size() < sessCount && connTimes > 0) {
        int sockfd;
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            logError(0, logfp, "PressureGenerator - generator - socket error");
            errorTimes--;
            if (errorTimes < 0) {
                return -1;
            }
            continue;
        }
        setnonblocking(sockfd); /* 非阻塞 */
        errno   = 0;
        int ret = 0;
        if ((ret = connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr))) < 0) {
            if (errno != EINPROGRESS) {
                logError(0, logfp, "PressureGenerator - client %d - connect error", sockfd);
                close(sockfd);
                errorTimes--;
                if (errorTimes < 0) {
                    return -1;
                }
                continue;
            }
            else {
                ClientInfo client;
                client.connfd   = sockfd;
                client.status   = -1; /* 设置状态为未连接 */
                clients[sockfd] = client;
                addfd(epollfd, sockfd, 1, 0); /* 添加套接字到epoll事件表 */
                uncnNum++;
            }
        }
        /* 直接连接建立 */
        else {
            ClientInfo client;
            client.connfd   = sockfd;
            client.status   = 0;                /* 设置状态为已连接(等待接收头部) */
            client.buffer   = new ClientBuffer; /* 分配缓冲区 */
            clients[sockfd] = client;
            addfd(epollfd, sockfd, 1, 0); /* 添加套接字到epoll事件表 */
            logInfo(0, logfp, "PressureGenerator - client %d - new client (%zd connected; %zd unconnected)", sockfd,
                    ++connNum, uncnNum);
        }
        connTimes--;
    }
    return 0;
}

void PressureGenerator::closeGenerator() {
    if (timeoutFlag)
        logInfo(0, logfp, "PressureGenerator - generator - received SIGALARM signal");
    if (exitFlag)
        logInfo(0, logfp, "PressureGenerator - generator - received SIGINT signal");
    std::vector<int> connfds;
    for (auto const& cli : clients)
        connfds.push_back(cli.first);
    for (auto const& connfd : connfds)
        removeClient(connfd);
    logInfo(0, logfp, "PressureGenerator - generator - all sockets are closed");
}

int PressureGenerator::handle_events(struct epoll_event* events, const int& number) {
    for (int i = 0; i < number; ++i) {
        int sockfd = events[i].data.fd;
        assert(clients.find(sockfd) != clients.end());
        /* 如果是未连接套接字 */
        if (clients[sockfd].status == -1) {
            int       error;
            socklen_t len = sizeof(error);
            if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) != 0) {
                errno = 0;
                logError(-1, logfp, "PressureGenerator - client %d - connect error: %s", sockfd, strerror(error));
                removeClient(sockfd);
                continue;
            }
            clients[sockfd].status = 0;                /* 设置状态为已连接(等待接收头部) */
            clients[sockfd].buffer = new ClientBuffer; /* 分配缓冲区 */
            logInfo(0, logfp, "PressureGenerator - client %d - new client (%zd connected; %zd unconnected)", sockfd,
                    ++connNum, --uncnNum);
        }
        assert(clients[sockfd].status != -1);
        assert(clients[sockfd].buffer != nullptr);
        ClientBuffer* buffer = clients[sockfd].buffer;
        /* 如果可读 */
        if (events[i].events & EPOLLIN) {
            /* 接收头部 */
            if (clients[sockfd].status == 0) {
                assert(buffer->unrecv != 0);
                ssize_t n = recv(sockfd, buffer->recvPtr, buffer->unrecv, 0);
                if (n == (ssize_t)buffer->unrecv) { /* 接收完毕 */
                    Header header;
                    memcpy(&header, buffer->recvBuf, sizeof(Header));
                    size_t msgLen = (size_t)ntohs(header.length);
                    if (msgLen <= RECVBUF_MAX && msgLen > 0) {
                        buffer->recvPtr        = buffer->recvBuf;
                        buffer->unrecv         = msgLen;
                        clients[sockfd].status = 1;
                    }
                    else if (msgLen == 0) {
                        buffer->recvPtr        = buffer->recvBuf;
                        buffer->unrecv         = sizeof(Header);
                        clients[sockfd].status = 0;
                    }
                    else {
                        logInfo(-1, logfp,
                                "PressureGenerator - client %d - insufficient space available in receive buffer",
                                sockfd);
                        removeClient(sockfd);
                        continue;
                    }
                }
                else if (n > 0 && n < (ssize_t)buffer->unrecv) { /* 接收了一部分 */
                    buffer->recvPtr += n;
                    buffer->unrecv -= n;
                }
                else if (n == 0) { /* 遇到FIN */
                    logInfo(-1, logfp, "PressureGenerator - client %d - receive FIN from server", sockfd);
                    removeClient(sockfd);
                    continue;
                }
                else { /* 遇到错误 */
                    if (errno != EWOULDBLOCK) {
                        logError(-1, logfp, "PressureGenerator - client %d - recv error", sockfd);
                        removeClient(sockfd);
                        continue;
                    }
                }
            }
            /* 接收载荷 */
            if (clients[sockfd].status == 1) {
                assert(buffer->unrecv != 0);
                ssize_t n = recv(sockfd, buffer->recvPtr, buffer->unrecv, 0);
                if (n == (ssize_t)buffer->unrecv) { /* 接收完毕 */
                    // TODO
                    // size_t msgLen = (buffer->recvPtr - buffer->recvBuf) + n;
                    // logInfo(0, logfp, "PressureGenerator - client %d - receive %zd bytes message from server",
                    // sockfd,
                    //         msgLen);
                    /* 设置状态以接收新的报文 */
                    buffer->recvPtr        = buffer->recvBuf;
                    buffer->unrecv         = sizeof(Header);
                    clients[sockfd].status = 0;
                }
                else if (n > 0 && n < (ssize_t)buffer->unrecv) { /* 接收了一部分 */
                    buffer->recvPtr += n;
                    buffer->unrecv -= n;
                }
                else if (n == 0) { /* 遇到FIN */
                    logInfo(-1, logfp, "PressureGenerator - client %d - receive FIN from server", sockfd);
                    removeClient(sockfd);
                    continue;
                }
                else { /* 遇到错误 */
                    if (errno != EWOULDBLOCK) {
                        logError(-1, logfp, "PressureGenerator - client %d - recv error", sockfd);
                        removeClient(sockfd);
                        continue;
                    }
                }
            }
        }
        /* 有空间可以发送数据 */
        if (events[i].events & EPOLLOUT) {
            /* 复制数据 */
            copyMsg(sockfd);
            long unsend = buffer->sendPtr - buffer->sendBuf;
            if (unsend > 0) {
                ssize_t n = send(sockfd, buffer->sendBuf, unsend, 0);
                if (n > 0) {
                    unsend -= n;
                    char* unsendPtr = buffer->sendPtr - unsend;
                    memcpy(buffer->sendBuf, unsendPtr, unsend);
                    buffer->sendPtr = buffer->sendBuf + unsend;
                }
                else if (n == 0) { /* 空间不足 */
                    continue;
                }
                else { /* 遇到错误 */
                    if (errno != EWOULDBLOCK) {
                        logError(-1, logfp, "PressureGenerator - client %d - send error", sockfd);
                        removeClient(sockfd);
                        continue;
                    }
                }
            }
        }
    }
    return 0;
}

void PressureGenerator::copyMsg(int sockfd) {
    assert(clients.find(sockfd) != clients.end());
    ClientBuffer* buffer = clients[sockfd].buffer;
    size_t        idx    = (size_t)rand() % msgNum;
    size_t        msgLen = strlen(msgs[idx]) + 1;
    if (SENDBUF_MAX - (buffer->sendPtr - buffer->sendBuf) > (long int)sizeof(Header) + (long int)msgLen) {
        Header header;
        header.length = htons((uint16_t)msgLen);
        memcpy(buffer->sendPtr, &header, sizeof(Header));
        buffer->sendPtr += sizeof(Header);
        memcpy(buffer->sendPtr, msgs[idx], msgLen);
        buffer->sendPtr += msgLen;
    }
}

int PressureGenerator::removeClient(const int& sockfd) {
    assert(clients.find(sockfd) != clients.end());
    if (clients[sockfd].status == -1) {
        uncnNum--;
    }
    else {
        connNum--;
    }
    if (clients[sockfd].buffer != nullptr)
        delete clients[sockfd].buffer;
    clients.erase(sockfd);
    if (close(sockfd) < 0) {
        logError(-1, logfp, "PressureGenerator - client %d - close error", sockfd);
    }
    delfd(epollfd, sockfd);
    logInfo(0, logfp, "PressureGenerator - client %d - client left (%zd connected; %zd unconnected)", sockfd, connNum,
            uncnNum);
    return 0;
}

void PressureGenerator::sigIntHandler(int signum) {
    exitFlag = 1;
}

void PressureGenerator::sigAlrmHandler(int signum) {
    timeoutFlag = 1;
}

void PressureGenerator::sigPipeHandler(int signum) {
    // do nothing
}

sigfunc* PressureGenerator::signal(int signo, sigfunc* func) {
    struct sigaction act, oact;
    act.sa_handler = func;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (signo == SIGPIPE || signo == SIGCHLD)
        act.sa_flags |= SA_RESTART;

    if (sigaction(signo, &act, &oact) < 0)
        return (SIG_ERR);

    return (oact.sa_handler);
}