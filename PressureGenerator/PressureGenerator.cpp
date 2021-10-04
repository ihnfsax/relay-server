#include "PressureGenerator.hpp"

int PressureGenerator::timeoutFlag = 0;
int PressureGenerator::exitFlag    = 0;

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

    while (!timeoutFlag && !exitFlag) {
        if (sizeof(clients) < sessCount) {
            if (addClients(events) < 0) {
                closeGenerator();
                return 0;
            }
        }
        int ready = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ready < 0) {
            logError(-1, logfp, "[GENERATOR] epoll_wait error");
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
    int errorTimes = 10;
    while (sizeof(clients) < sessCount) {
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
                ClientInfo client;
                client.connfd   = sockfd;
                client.status   = -1; /* 设置状态为未连接 */
                clients[sockfd] = client;
                addfd(epollfd, sockfd, 1, 0); /* 添加套接字到epoll事件表 */
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
            logInfo(0, logfp, "[CLIENT] (fd:%d) New connection established (%zd clients in total)[1]", sockfd,
                    sizeof(clients));
        }
    }
    return 0;
}

void PressureGenerator::closeGenerator() {
    if (timeoutFlag)
        logInfo(0, logfp, "[GENERATOR] SIGALARM signal received");
    if (exitFlag)
        logInfo(0, logfp, "[GENERATOR] SIGINT signal received");
}

int PressureGenerator::handle_events(struct epoll_event* events, const int& number) {
    for (int i = 0; i < number; ++i) {
        int sockfd = events[i].data.fd;
        assert(clients.find(sockfd) != clients.end());
        /* 如果是未连接套接字 */
        if (clients[sockfd].status == -1) {
            socklen_t len;
            int       error;
            if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) != 0) {
                logInfo(-1, logfp, "[CLIENT] (fd:%d) getsockopt error: %s", sockfd, strerror(error));
                removeClient(sockfd);
                continue;
            }
            clients[sockfd].status = 0;                /* 设置状态为已连接(等待接收头部) */
            clients[sockfd].buffer = new ClientBuffer; /* 分配缓冲区 */
            logInfo(0, logfp, "[CLIENT] (fd:%d) New connection established (%zd clients in total)[2]", sockfd,
                    sizeof(clients));
            // if (recvfromServer(sockfd) < 0) {
            //     removeClient(sockfd);
            //     continue;
            // }
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
                        logInfo(-1, logfp, "[CLIENT] (fd:%d) Message length is larger than the receive buffer size",
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
                    logInfo(-1, logfp, "[SRC_CLIENT %d] Client sended FIN", srcID);
                    removeClient(sockfd);
                    continue;
                }
                else { /* 遇到错误 */
                    if (errno != EWOULDBLOCK) {
                        logError(-1, logfp, "[SRC_CLIENT %d] Unexpected error on recv", srcID);
                        removeClient(sockfd);
                        continue;
                    }
                }
            }
            // if (recvfromServer(sockfd) < 0) {
            //     removeClient(sockfd);
            //     continue;
            // }
        }
        /* 如果可写 */
        if (events[i].events & EPOLLOUT) {
            assert(clients[sockfd].status != -1);
            assert(clients[sockfd].buffer != nullptr);
            // Header header;
            // int    msgLen = strlen(str1) + 1;
            // header.length = htons((uint16_t)msgLen);
            // char buf[BUFFER_SIZE];
            // buf[BUFFER_SIZE - 1] = '\0';
            // memcpy(buf, &header, hSize);
            // memcpy(buf + hSize, str1, msgLen);
        }
    }
}

// int PressureGenerator::recvfromServer(const int& sockfd) {
//     if (ET_MODE) {
//         return etRecv(sockfd);
//     }
//     else {
//         return ltRecv(sockfd);
//     }
// }

// int PressureGenerator::etRecv(const int& sockfd) {
//     /* ET模式套接字是非阻塞模式 */
//     char buf[BUFFER_SIZE];
//     buf[BUFFER_SIZE - 1] = '\0';
//     size_t n             = 0;
//     while (1) {
//         ssize_t ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0); /* 反复覆盖数据 */
//         if (ret < 0) {
//             /*
//             对于非阻塞IO，下面的条件成立表示数据已全部读取完毕。此后，epoll就能再次出发sockfd上的EPOLLIN事件，以驱动下一次读操作
//              */
//             if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
//                 logInfo(-1, logfp, "[CLIENT] (fd:%d) Read %d bytes data [ET]", sockfd, n);
//                 return 0;
//             }
//             logError(-1, logfp, "[CLIENT] (fd:%d) recv error: %s", sockfd);
//             removeClient(sockfd);
//             return -1;
//         }
//         else if (ret == 0) {
//             logInfo(-1, logfp, "[CLIENT] (fd:%d) Server send FIN", sockfd);
//             removeClient(sockfd);
//             return -1;
//         }
//         else {
//             n += ret; /* 接收到了数据 */
//             /* handle data */
//         }
//     }
// }

// int PressureGenerator::ltRecv(const int& sockfd) {
//     char buf[BUFFER_SIZE];
//     buf[BUFFER_SIZE - 1] = '\0';
//     Header header;
//     /* 接收报文头部 */
//     if (blockingRecv(sockfd, &header, hSize) < 0) {
//         return -1;
//     }
//     else {
//         size_t msgLen = ntohs(header.length);
//         if (msgLen > BUFFER_SIZE - hSize) {
//             logInfo(-1, logfp, "[CLIENT] (fd:%d )Client send message exceeding the limit size", sockfd);
//             return -1;
//         }
//         else {
//             /* 接收报文载荷 */
//             if (blockingRecv(sockfd, buf + hSize, msgLen) < 0) {
//                 return -1;
//             }
//             else {
//                 /* 如果另一端存在，则发送数据 */
//                 logInfo(-1, logfp, "[CLIENT] (fd:%d) Read %d bytes data (%d bytes text) [LT]", sockfd, msgLen +
//                 hSize,
//                         msgLen);
//                 /* handle data */
//                 return 0;
//             }
//         }
//     }
// }

// int PressureGenerator::blockingRecv(const int& sockfd, void* buf, const size_t& size) {
//     ssize_t ret = recv(sockfd, buf, size, MSG_WAITALL);
//     if (ret < (ssize_t)size) {
//         if (ret < 0) {
//             if (errno == EWOULDBLOCK)
//                 logError(-1, logfp, "[CLIENT] (fd:%d) Client recv timeout", sockfd);
//             else
//                 logError(-1, logfp, "[CLIENT] (fd:%d) Client recv error", sockfd);
//         }
//         else {
//             logInfo(-1, logfp, "[CLIENT] (fd:%d) Server send FIN", sockfd);
//         }
//         return -1;
//     }
//     return ret;
// }

// int PressureGenerator::sendToServer(const int& sockfd, const void* buf, const size_t& size) {
//     size_t      nleft;
//     ssize_t     nsended;
//     const char* ptr;
//     ptr   = (const char*)buf;
//     nleft = size;
//     while (nleft > 0) {
//         if ((nsended = send(sockfd, ptr, nleft, 0)) <= 0) {
//             if (nsended < 0 && errno == EINTR) {
//                 continue;
//             }
//             else if (nsended < 0 && errno == EWOULDBLOCK)
//                 logError(-1, logfp, "[CLIENT] (fd:%d) Client send timeout", sockfd);
//             else if (nsended < 0)
//                 logError(-1, logfp, "[CLIENT] (fd:%d) Client send error", sockfd);
//             else if (nsended == 0)
//                 logError(-1, logfp, "[CLIENT] (fd:%d) Server has terminated", sockfd);
//             return -1;
//         }
//         nleft -= nsended;
//         ptr += nsended;
//     }
//     if (size - nleft < 0) {
//         logError(-1, logfp, "[CLIENT] (fd:%d) Client send error", sockfd);
//         return -1;
//     }
//     return size;
// }

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