#include "PressureGenerator.hpp"

int PressureGenerator::start(const char* ip, const char* port, int sessCount, int time, int logFlag) {
    if (sessCount <= 0) {
        printf("The number of sessions must be positive\n");
        return -1;
    }
    if (sessCount * 2 > MAX_EVENT_NUMBER) {
        printf("The number of sessions must be less than %d\n", MAX_EVENT_NUMBER / 2);
        return -1;
    }
    this->sessCount = (size_t)sessCount;
    if (time <= 0) {
        printf("The test time must be positive\n");
    }
    this->time = time;
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
    int r = doit(ip, port);
    logInfo(0, logfp, "[SERVER] Server shutdown");
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

    while (!timeoutFlag) {
        if (sizeof(clientFDs) < sessCount) {
            if (addClients(events) < 0) {
                closeGenerator();
                return logInfo(-1, logfp, "[GENERATOR] Too many errors on socket function");
            }
        }
        int ready = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ready < 0) {
            logError(-1, logfp, "[SERVER] epoll_wait error");
            closeGenerator();
            return 0;
        }
        handle_events(events, ready);
    }
}

int PressureGenerator::addClients(struct epoll_event* events) {
    int errorTimes = 10;
    while (sizeof(clientFDs) < sessCount) {
        int sockfd;
        if ((sockfd = createSocket(AF_INET, SOCK_STREAM, 0, logfp)) < 0) {
            errorTimes--;
            if (errorTimes < 0) {
                return logInfo(-1, logfp, "[GENERATOR] Too many errors on socket function");
            }
            continue;
        }
        setnonblocking(sockfd); /* 非阻塞 */
        errno   = 0;
        int ret = 0;
        if ((ret = connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr))) < 0) {
            if (errno != EINPROGRESS) {
                close(sockfd);
                errorTimes--;
                if (errorTimes < 0) {
                    return logInfo(-1, logfp, "[GENERATOR] Too many errors on connect function");
                }
                continue;
            }
            else {
                clientFDs[sockfd] = 0; /* 设置状态为未连接 */
                /* 添加套接字到epoll事件表 */
                addfd(epollfd, sockfd, 1, ET_MODE);
            }
        }
        /* 直接连接建立 */
        else {
            setTimeout(sockfd); /* 设置超时 */
            if (ET_MODE == 0)
                setblocking(sockfd); /*设置为阻塞*/
            clientFDs[sockfd] = 1;   /* 设置状态为已连接 */
            /* 添加套接字到epoll事件表 */
            addfd(epollfd, sockfd, 1, ET_MODE);
            logInfo(0, logfp, "[CLIENT] (fd:%d) New connection established (%zd clients in total)[1]", sockfd,
                    sizeof(clientFDs));
        }
    }
    return 0;
}

int PressureGenerator::setTimeout(const int& sockfd) {
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        logError(-1, logfp, "[GENERATOR] setsockopt for recv timeout error");
        return -1;
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        logError(-1, logfp, "[GENERATOR] setsockopt for send timeout error");
        return -1;
    }
    return 0;
}

void PressureGenerator::closeGenerator() {}

void PressureGenerator::handle_events(struct epoll_event* events, const int& number) {
    for (int i = 0; i < number; ++i) {
        int sockfd = events[i].data.fd;
        assert(clientFDs.find(sockfd) != clientFDs.end());
        /* 如果是未连接套接字 */
        if (clientFDs[sockfd] == 0) {
            socklen_t len;
            int       error;
            if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) != 0) {
                logInfo(-1, logfp, "[CLIENT] (fd:%d) getsockopt error: %s", sockfd, strerror(error));
                removeClient(sockfd);
                continue;
            }
            setTimeout(sockfd); /* 设置超时 */
            if (ET_MODE == 0)
                setblocking(sockfd); /*设置为阻塞*/
            clientFDs[sockfd] = 1;
            logInfo(0, logfp, "[CLIENT] (fd:%d) New connection established (%zd clients in total)[2]", sockfd,
                    sizeof(clientFDs));
            if (recvfromServer(sockfd) < 0) {
                removeClient(sockfd);
                continue;
            }
        }
        if (events[i].events & EPOLLIN) {
            assert(clientFDs.find(sockfd) != clientFDs.end());
            if (recvfromServer(sockfd) < 0) {
                removeClient(sockfd);
                continue;
            }
        }
        if (events[i].events & EPOLLOUT) {
            assert(clientFDs.find(sockfd) != clientFDs.end());
            Header header;
            int    msgLen = strlen(str1) + 1;
            header.length = htons((uint16_t)msgLen);
            char buf[BUFFER_SIZE];
            buf[BUFFER_SIZE - 1] = '\0';
            memcpy(buf, &header, hSize);
            memcpy(buf + hSize, str1, msgLen);
        }
    }
}

int PressureGenerator::recvfromServer(const int& sockfd) {
    if (ET_MODE) {
        return etRecv(sockfd);
    }
    else {
        return ltRecv(sockfd);
    }
}

int PressureGenerator::etRecv(const int& sockfd) {
    /* ET模式套接字是非阻塞模式 */
    char buf[BUFFER_SIZE];
    buf[BUFFER_SIZE - 1] = '\0';
    size_t n             = 0;
    while (1) {
        ssize_t ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0); /* 反复覆盖数据 */
        if (ret < 0) {
            /* 对于非阻塞IO，下面的条件成立表示数据已全部读取完毕。此后，epoll就能再次出发sockfd上的EPOLLIN事件，以驱动下一次读操作
             */
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                logInfo(-1, logfp, "[CLIENT] (fd:%d) Read %d bytes data [ET]", sockfd, n);
                return 0;
            }
            logError(-1, logfp, "[CLIENT] (fd:%d) recv error: %s", sockfd);
            removeClient(sockfd);
            return -1;
        }
        else if (ret == 0) {
            logInfo(-1, logfp, "[CLIENT] (fd:%d) Server send FIN", sockfd);
            removeClient(sockfd);
            return -1;
        }
        else {
            n += ret; /* 接收到了数据 */
            /* handle data */
        }
    }
}

int PressureGenerator::ltRecv(const int& sockfd) {
    char buf[BUFFER_SIZE];
    buf[BUFFER_SIZE - 1] = '\0';
    Header header;
    /* 接收报文头部 */
    if (blockingRecv(sockfd, &header, hSize) < 0) {
        return -1;
    }
    else {
        size_t msgLen = ntohs(header.length);
        if (msgLen > BUFFER_SIZE - hSize) {
            logInfo(-1, logfp, "[CLIENT] (fd:%d )Client send message exceeding the limit size", sockfd);
            return -1;
        }
        else {
            /* 接收报文载荷 */
            if (blockingRecv(sockfd, buf + hSize, msgLen) < 0) {
                return -1;
            }
            else {
                /* 如果另一端存在，则发送数据 */
                logInfo(-1, logfp, "[CLIENT] (fd:%d) Read %d bytes data (%d bytes text) [LT]", sockfd, msgLen + hSize,
                        msgLen);
                /* handle data */
                return 0;
            }
        }
    }
}

int PressureGenerator::blockingRecv(const int& sockfd, void* buf, const size_t& size) {
    ssize_t ret = recv(sockfd, buf, size, MSG_WAITALL);
    if (ret < (ssize_t)size) {
        if (ret < 0) {
            if (errno == EWOULDBLOCK)
                logError(-1, logfp, "[CLIENT] (fd:%d) Client recv timeout", sockfd);
            else
                logError(-1, logfp, "[CLIENT] (fd:%d) Client recv error", sockfd);
        }
        else {
            logInfo(-1, logfp, "[CLIENT] (fd:%d) Server send FIN", sockfd);
        }
        return -1;
    }
    return ret;
}

int PressureGenerator::sendToServer(const int& sockfd, const void* buf, const size_t& size) {
    size_t      nleft;
    ssize_t     nsended;
    const char* ptr;
    ptr   = (const char*)buf;
    nleft = size;
    while (nleft > 0) {
        if ((nsended = send(sockfd, ptr, nleft, 0)) <= 0) {
            if (nsended < 0 && errno == EINTR) {
                continue;
            }
            else if (nsended < 0 && errno == EWOULDBLOCK)
                logError(-1, logfp, "[CLIENT] (fd:%d) Client send timeout", sockfd);
            else if (nsended < 0)
                logError(-1, logfp, "[CLIENT] (fd:%d) Client send error", sockfd);
            else if (nsended == 0)
                logError(-1, logfp, "[CLIENT] (fd:%d) Server has terminated", sockfd);
            return -1;
        }
        nleft -= nsended;
        ptr += nsended;
    }
    if (size - nleft < 0) {
        logError(-1, logfp, "[CLIENT] (fd:%d) Client send error", sockfd);
        return -1;
    }
    return size;
}

int PressureGenerator::removeClient(const int& sockfd) {
    assert(clientFDs.find(sockfd) != clientFDs.end());
    clientFDs.erase(sockfd);
    if (close(sockfd) < 0) {
        logError(-1, logfp, "[GENERATOR] (fd:%d) close error", sockfd);
    }
    delfd(epollfd, sockfd);
    logInfo(0, logfp, "[CLIENT] (fd:%d) Client has left (%zd clients in total)", sockfd, sizeof(clientFDs));
    return 0;
}