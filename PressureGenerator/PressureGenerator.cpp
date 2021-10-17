#include "PressureGenerator.hpp"
#include <vector>

#define CONN_SIZE 2
#define ERROR_MAX 10
#define WAIT_CONN_MAX 200
#define NANO_SEC 1000000000

int PressureGenerator::alrmFlag = 0;
int PressureGenerator::intFlag  = 0;
int PressureGenerator::exitFlag = 0;

int PressureGenerator::start(const char* ip, const char* port, int sessCount, int runTime, int packetSize,
                             int logFlag) {
    if (sessCount <= 0) {
        printf("The number of sessions must be positive\n");
        return -1;
    }
    if (sessCount * 2 > MAX_EVENT_NUMBER) {
        printf("The number of sessions must be less than %d\n", MAX_EVENT_NUMBER / 2);
        return -1;
    }
    this->cliCount = (size_t)sessCount * 2;
    if (runTime <= 0) {
        printf("The test time must be positive\n");
    }
    this->runTime = runTime;
    if (packetSize <= (int)sizeof(Header)) {
        printf("The packet size must be larger than header size (%zd)\n", sizeof(Header));
    }
    this->payloadSize = packetSize - sizeof(Header);
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
    /* 开始生成报文，并启动测试 */
    generatePacket();
    logInfo(0, logfp, "PressureGenerator - generator - plan to run %d seconds", this->runTime);
    alarm(this->runTime);
    int r = doit(ip, port);
    logInfo(0, logfp, "PressureGenerator - generator - generator shutdowns");
    if (recordFlag == 0) {
        logInfo(0, logfp, "PressureGenerator - generator - shutdowns before the number of clients reaches %zd",
                this->cliCount);
        logInfo(0, logfp, "PressureGenerator - generator - no statistics were recorded");
    }
    else {
        double averageDelay =
            (double)totalDelay.tv_sec / recvPacketNum + (double)totalDelay.tv_nsec / recvPacketNum / NANO_SEC;
        logInfo(0, logfp, "PressureGenerator - generator - receive %lu packets; average delay: %.06lf", recvPacketNum,
                averageDelay);
        // printf("tv_sec: %lu tv_nsec: %lu\n", totalDelay.tv_sec, totalDelay.tv_nsec);
        printf("receive %lu packets; average delay: %.06lf\n", recvPacketNum, averageDelay);
    }
    if (logfp != nullptr) {
        fclose(logfp);
        logfp = nullptr;
    }
    status = 0;
    return r;
}

void PressureGenerator::generatePacket() {
    assert(payload == nullptr);
    payload = new char[payloadSize];
    memset(payload, 'M', payloadSize);
    logInfo(0, logfp, "PressureGenerator - generator - generate %zd bytes payload", payloadSize);
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

    while (true) {
        /* 添加新客户端 */
        if (clients.size() < cliCount && uncnNum < WAIT_CONN_MAX && shutFlag == 0) {
            if (addClients(events) < 0) {
                logInfo(0, logfp, "PressureGenerator - generator - too many errors during adding clients");
                shutdownAll();
            }
        }
        /* 等待事件 */
        int ready = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ready < 0) {
            logError(0, logfp, "PressureGenerator - generator - epoll_wait error");
            shutdownAll();
        }
        /* 处理事件 */
        if (handleEvents(events, ready) < 0) {
            shutdownAll();
        }
        if (exitFlag || shutFlag) {
            shutdownAll();
            if (clients.size() == 0) {
                logInfo(0, logfp, "PressureGenerator - generator - all connected sockets are closed");
                break;
            }
        }
    }

    return 0;
}

int PressureGenerator::addClients(struct epoll_event* events) {
    int errorTimes = ERROR_MAX;
    int connTimes  = CONN_SIZE;
    while (clients.size() < cliCount && connTimes > 0) {
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
                addOneClient(sockfd, -1);
            }
        }
        /* 直接连接建立 */
        else {
            addOneClient(sockfd, 0);
            logInfo(0, logfp, "PressureGenerator - client %d - new client (c:%zd u:%zd a:%zd)[1]", sockfd, connNum,
                    uncnNum, connNum + uncnNum);
        }
        connTimes--;
    }
    return 0;
}

void PressureGenerator::addOneClient(int sockfd, int state) {
    assert(state == 0 || state == -1);
    assert(clients.find(sockfd) == clients.end());
    ClientInfo client;
    client.connfd = sockfd;
    client.state  = state;
    if (state == 0) {
        client.buffer          = new ClientBuffer;
        client.buffer->sendPtr = this->payload;
        ++connNum;
    }
    else {
        ++uncnNum;
    }
    clients[sockfd] = client;
    addfd(epollfd, sockfd, 1, 0); /* 添加套接字到epoll事件表 */
}

void PressureGenerator::shutdownAll() {
    if (exitFlag && intFlag)
        intFlag = logInfo(0, logfp, "PressureGenerator - generator - received SIGINT signal");
    if (exitFlag && alrmFlag)
        alrmFlag = logInfo(0, logfp, "PressureGenerator - generator - received SIGALARM signal");
    if (shutFlag == 0) {
        logInfo(0, logfp, "PressureGenerator - generator - all clients send FIN to server");
    }
    shutFlag = 1;
    for (auto& cli : clients) {
        if (cli.second.state == 0) {
            shutdown(cli.first, SHUT_WR);
            cli.second.state = 1;
        }
    }
}

int PressureGenerator::handleEvents(struct epoll_event* events, const int& number) {
    for (int i = 0; i < number; ++i) {
        int continueFlag = 0;
        int sockfd       = events[i].data.fd;
        assert(clients.find(sockfd) != clients.end());
        /* 如果是未连接套接字 */
        if (clients[sockfd].state == -1) {
            int       error;
            socklen_t len = sizeof(error);
            if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) != 0) {
                errno = 0;
                logError(-1, logfp, "PressureGenerator - client %d - connect error: %s", sockfd, strerror(error));
                removeClient(sockfd);
                continue;
            }
            clients[sockfd].state           = 0;                /* 设置状态为已连接(等待接收头部) */
            clients[sockfd].buffer          = new ClientBuffer; /* 分配缓冲区 */
            clients[sockfd].buffer->sendPtr = this->payload;
            ++connNum;
            --uncnNum;
            logInfo(0, logfp, "PressureGenerator - client %d - new client (c:%zd u:%zd a:%zd)[2]", sockfd, connNum,
                    uncnNum, connNum + uncnNum);
            if (recordFlag == 0 && connNum >= cliCount) {
                recordFlag = 1;
                logInfo(0, logfp, "PressureGenerator - generator - %zd connected clients, start to send packets",
                        connNum);
            }
        }
        /* 初始检查与设置 */
        assert(clients[sockfd].state != -1);
        assert(clients[sockfd].buffer != nullptr);
        assert(clients[sockfd].buffer->sendPtr != nullptr);
        ClientBuffer* buffer = clients[sockfd].buffer;
        /* 如果可读，并且有空间容纳 */
        if (events[i].events & EPOLLIN) {
            /* 不断地读取，直到没有数据可读 */
            while (true) {
                buffer->recved = 0;
                ssize_t n      = recv(sockfd, buffer->usrBuf, BUFFER_SIZE, 0);
                if (n > 0) {
                    while (true) {
                        /* 报头或载荷接收完毕 */
                        if ((size_t)n >= buffer->unrecv) {
                            // 处理报头
                            if (buffer->recvFlag == 0) {
                                memcpy((char*)&buffer->recvHeader + (sizeof(Header) - buffer->unrecv),
                                       buffer->usrBuf + buffer->recved, buffer->unrecv);
                                size_t msgLen = (size_t)handleHeader(&buffer->recvHeader, sockfd);
                                recvPacketNum++; /* 报文数加1 */
                                n                = n - buffer->unrecv;
                                buffer->recved   = buffer->recved + buffer->unrecv;
                                buffer->recvFlag = 1;
                                buffer->unrecv   = msgLen;
                            }
                            // 处理载荷
                            else {
                                n                = n - buffer->unrecv;
                                buffer->recved   = buffer->recved + buffer->unrecv;
                                buffer->recvFlag = 0;
                                buffer->unrecv   = sizeof(Header);
                            }
                        }
                        /* 只接受了一部分 */
                        else {
                            if (buffer->recvFlag == 0) {
                                memcpy((char*)&buffer->recvHeader + (sizeof(Header) - buffer->unrecv),
                                       buffer->usrBuf + buffer->recved, n);
                            }
                            buffer->unrecv = buffer->unrecv - n;
                            buffer->recved = buffer->recved + n;
                            break;
                        }
                    }
                }
                else if (n == 0) {
                    logInfo(0, logfp, "PressureGenerator - client %d - receive FIN from server", sockfd);
                    if (clients[sockfd].state == 0) { /* 之前未关闭连接，则直接关闭写 */
                        shutdown(sockfd, SHUT_WR);
                    }
                    else {
                        shutdown(sockfd, SHUT_RD); /* 之前关闭了写，则把读关闭 */
                    }
                    /* 直接关闭写的一端，不再写了，因为数据可能源源不断地来，我们不知道还得写多少 */
                    removeClient(sockfd);
                    continueFlag = 1;
                    break;
                }
                else {
                    if (errno != EWOULDBLOCK) { /* 连接已经结束，直接close套接字 */
                        logError(-1, logfp, "PressureGenerator - client %d - recv error", sockfd);
                        removeClient(sockfd);
                        continueFlag = 1;
                    }
                    break; /* 读到没有数据了，退出 */
                }
            }
            if (continueFlag) {
                continue; /* continue最外层的for */
            }
        }
        /* 有空间可以发送数据，并且要开始记录才能发送数据，并且不能是关闭了写的一端 */
        if ((events[i].events & EPOLLOUT) && recordFlag == 1 && clients[sockfd].state != 1) {
            while (true) {
                ssize_t n = 0;
                if (buffer->sended < sizeof(Header)) {
                    if (buffer->sended == 0) {
                        getHeader(payloadSize, sockfd, &buffer->sendHeader);
                    }
                    n = send(sockfd, (char*)&buffer->sendHeader + buffer->sended, sizeof(Header) - buffer->sended, 0);
                }
                else {
                    n = send(sockfd, buffer->sendPtr + buffer->sended - sizeof(Header),
                             payloadSize - (buffer->sended - sizeof(Header)), 0);
                }
                if (n >= 0) {
                    buffer->sended += n;
                    if (buffer->sended == sizeof(Header) + payloadSize) {
                        buffer->sended = 0;
                    }
                }
                else { /* 遇到错误 */
                    if (errno != EWOULDBLOCK) {
                        logError(-1, logfp, "PressureGenerator - client %d - send error", sockfd);
                        removeClient(sockfd);
                        continueFlag = 1;
                    }
                    break; /* 写到不能写了，退出 */
                }
            }
            if (continueFlag) {
                continue; /* continue最外层的for */
            }
        }
    }
    return 0;
}

int PressureGenerator::removeClient(const int& sockfd) {
    assert(clients.find(sockfd) != clients.end());
    if (clients[sockfd].state == -1) {
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
    logInfo(0, logfp, "PressureGenerator - client %d - client left (c:%zd u:%zd a:%zd)", sockfd, connNum, uncnNum,
            connNum + uncnNum);
    return 0;
}

void PressureGenerator::sigIntHandler(int signum) {
    exitFlag = 1;
    intFlag  = 1;
}

void PressureGenerator::sigAlrmHandler(int signum) {
    exitFlag = 1;
    alrmFlag = 1;
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

uint16_t PressureGenerator::handleHeader(struct Header* header, const int& sockfd) {
    uint16_t        msgLen = ntohs(header->length);
    struct timespec timestamp;
    timestamp.tv_sec  = ntoh64(header->sec);
    timestamp.tv_nsec = ntoh64(header->nsec);
    addDelay(&timestamp);
    // logInfo(0, logfp, "PressureGenerator - client %d - recv header: <length: %hd, id: %d, time: %s>", sockfd, msgLen,
    //         ntohl(header->id), strftTime(&timestamp).c_str());
    return msgLen;
}

void PressureGenerator::addDelay(struct timespec* timestamp) {
    struct timespec timeNow;
    clock_gettime(CLOCK_REALTIME, &timeNow);
    if (timestamp->tv_nsec >= NANO_SEC) {
        return;
    }
    if (timeNow.tv_sec < timestamp->tv_sec) {
        return;
    }
    else if (timeNow.tv_sec == timestamp->tv_sec) {
        if (timeNow.tv_nsec < timestamp->tv_nsec) {
            return;
        }
        else {
            totalDelay.tv_nsec += (timeNow.tv_nsec - timestamp->tv_nsec);
            if (totalDelay.tv_nsec > NANO_SEC) {
                totalDelay.tv_sec += totalDelay.tv_nsec / NANO_SEC;
                totalDelay.tv_nsec %= NANO_SEC;
            }
        }
    }
    else {
        totalDelay.tv_sec += (timeNow.tv_sec - timestamp->tv_sec);
        if (timeNow.tv_nsec < timestamp->tv_nsec) {
            if (totalDelay.tv_nsec > (timestamp->tv_nsec - timeNow.tv_nsec)) {
                totalDelay.tv_nsec -= (timestamp->tv_nsec - timeNow.tv_nsec);
            }
            else {
                totalDelay.tv_nsec = NANO_SEC - ((timestamp->tv_nsec - timeNow.tv_nsec) - totalDelay.tv_nsec);
                totalDelay.tv_sec--;
            }
        }
        else {
            totalDelay.tv_nsec += (timeNow.tv_nsec - timestamp->tv_nsec);
            if (totalDelay.tv_nsec > NANO_SEC) {
                totalDelay.tv_sec += totalDelay.tv_nsec / NANO_SEC;
                totalDelay.tv_nsec %= NANO_SEC;
            }
        }
    }
}

void PressureGenerator::prepareExit() {
    if (payload != nullptr) {
        delete[] payload;
        payload = nullptr;
    }
}