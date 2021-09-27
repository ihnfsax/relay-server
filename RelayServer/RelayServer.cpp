#include "RelayServer.hpp"

int RelayServer::start(std::string ip, std::string port, int logFlag) {
    return start(ip.c_str(), port.c_str(), logFlag);
}

int RelayServer::start(const char* ip, const char* port, int logFlag) {
    if (status != 0) {
        printf("This Server has been started.\n");
        return -1;
    }
    else {
        status = 1;
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
    if (logfp != nullptr) {
        fclose(logfp);
        logfp = nullptr;
    }
    status = 0;
    return r;
}

int RelayServer::doit(const char* ip, const char* port) {
    /* 初始化地址结构 */
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    if (inetPton(AF_INET, ip, &servaddr.sin_addr, logfp) < 0)
        return -1;
    if (setPort(port, &servaddr.sin_port, logfp) < 0)
        return -1;

    /* 创建套接字 */
    if ((listenfd = createSocket(AF_INET, SOCK_STREAM, 0, logfp)) < 0)
        return -1;

    /* 设置套接字选项 */
    int reuse = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, (const void*)&reuse, sizeof(int)) < 0) {
        logError(-1, logfp, "[LOG] setsockopt error");
        close(listenfd);
        return -1;
    }

    /* 绑定地址和套接字 */
    if (toBind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr), logfp) < 0) {
        close(listenfd);
        return -1;
    }
    logInfo(0, logfp, "[SERVER] Relay server has binded to %s:%s", ip, port);

    /* 创建epoll事件表描述符 */
    struct epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(1);
    assert(epollfd >= 0);

    /* 开始监听 */
    if (toListen(listenfd, 128, logfp) < 0) {
        close(listenfd);
        return -1;
    }
    logInfo(0, logfp, "[SERVER] Relay server begin to listen", ip, port);

    /* 添加监听套接字到epoll事件表 */
    addfd(epollfd, listenfd, 0);

    while (1) {
        int ready = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ready < 0) {
            // TODO
            return logError(-1, logfp, "[SERVER] epoll_wait error");
        }
        handle_events(events, ready);
    }

    return close(listenfd);
}

void RelayServer::handle_events(struct epoll_event* events, const int& number) {
    char buf[BUFFER_SIZE];
    buf[BUFFER_SIZE - 1] = '\0';
    for (int i = 0; i < number; ++i) {
        int sockfd = events[i].data.fd;
        /* 监听套接字 */
        if (sockfd == listenfd) {
            struct sockaddr_in cliAddr;
            socklen_t          cliAddrLen;
            clientInfo*        client = new clientInfo;
            client->connfd            = accept(listenfd, (struct sockaddr*)&cliAddr, &cliAddrLen);
            if (client->connfd == -1 && errno == EINTR) {
                delete client;
                continue;
            }
            if (setTimeout(sockfd) < 0) {
                delete client;
                continue;
            }
            addClient(client);
            if (sendSavedMsgToClient(client->connfd, client->cliID) == -1) {
                logInfo(-1, logfp, "[CLIENT] Failed to send saved message to client %d", recvID);
                removeClient(sockfd);
            }
        }
        /* 已连接套接字 */
        else if (events[i].events & EPOLLIN) {
            assert(clientFDs.find(sockfd) != clientFDs.end());
            recvID = clientFDs[sockfd]->cliID;
            assert(clientIDs.find(recvID) != clientIDs.end());
            sendID        = counterPart(recvID);
            size_t bufLen = 0;
            Header header;
            if (recvFromClient(sockfd, &header, hSize) < 0) {
                removeClient(sockfd);
                continue;
            }
            else {
                int msgLen = ntohs(header.length);
                if (msgLen > BUFFER_SIZE - hSize || msgLen <= 0) {
                    logInfo(-1, logfp, "[CLIENT] Client %d send message exceeding the limit size", recvID);
                    removeClient(sockfd);
                    continue;
                }
                else {
                    if (recvFromClient(sockfd, buf + hSize, msgLen) < 0) {
                        removeClient(sockfd);
                        continue;
                    }
                    else {
                        if (clientIDs.find(sendID) != clientIDs.end()) {
                            int sendfd = clientIDs[sendID]->connfd;
                            memcpy(buf, &header, hSize);
                            if (sendToClient(sendfd, buf, msgLen + hSize) < 0) {
                                removeClient(sendfd);
                                continue;
                            }
                        }
                        else {
                            writeMsgToFile(sendID, buf + hSize, msgLen - 1);
                        }
                    }
                }
            }
        }
    }
}

void RelayServer::closeServer() {}

int RelayServer::addClient(clientInfo* client) {
    client->cliID = nextID;
    assert(clientIDs.find(client->cliID) == clientIDs.end() && clientFDs.find(client->connfd) == clientFDs.end());
    clientIDs[client->cliID]  = client;
    clientFDs[client->connfd] = client;
    updateNextID();
    addfd(epollfd, client->connfd, 0);
    logInfo(0, logfp, "[CLIENT] Client %d has joined", client->cliID);
    return 0;
}

int RelayServer::removeClient(const int& connfd) {
    assert(clientFDs.find(connfd) != clientFDs.end());

    int id = clientFDs[connfd]->cliID;
    delete clientFDs[connfd];
    clientIDs.erase(id);
    clientFDs.erase(connfd);
    if (id < nextID) {
        nextID = id;
    }
    if (close(connfd) < 0) {
        logError(-1, logfp, "[SERVER] Client %d close error", id);
    }
    delfd(epollfd, connfd);
    logInfo(0, logfp, "[CLIENT] Client %d has left", id);
    return 0;
}

void RelayServer::updateNextID() {
    for (int id = nextID;; ++id) {
        if (clientIDs.find(id) == clientIDs.end()) {
            nextID = id;
            break;
        }
    }
}

int RelayServer::setTimeout(const int& connfd) {
    if (setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        logError(-1, logfp, "[SERVER] setsockopt for recv timeout error");
        return -1;
    }
    if (setsockopt(connfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        logError(-1, logfp, "[SERVER] setsockopt for send timeout error");
        return -1;
    }
    return 0;
}

int RelayServer::recvFromClient(const int& connfd, void* buf, const size_t& size) {
    ssize_t ret = recv(connfd, buf, size, MSG_WAITALL);
    if (ret < size) {
        if (ret < 0) {
            if (errno == EWOULDBLOCK)
                logError(-1, logfp, "[CLIENT] Client %d recv timeout", recvID);
            else
                logError(-1, logfp, "[CLIENT] Client %d recv error", recvID);
        }
        else {
            logInfo(-1, logfp, "[CLIENT] Client %d send FIN", recvID);
        }
        return -1;
    }
    return ret;
}

int RelayServer::sendToClient(const int& connfd, const void* buf, const size_t& size) {
    size_t      nleft;
    ssize_t     nsended;
    const char* ptr;
    ptr   = (const char*)buf;
    nleft = size;
    while (nleft > 0) {
        if ((nsended = send(connfd, ptr, nleft, 0)) <= 0) {
            if (nsended < 0 && errno == EINTR) {
                continue;
            }
            else if (nsended < 0 && errno == EWOULDBLOCK)
                logError(-1, logfp, "[CLIENT] Client %d send timeout", sendID);
            else if (nsended < 0)
                logError(-1, logfp, "[CLIENT] Client %d send error", sendID);
            else if (nsended == 0)
                logError(-1, logfp, "[CLIENT] Client %d has terminated", sendID);
            return -1;
        }
        nleft -= nsended;
        ptr += nsended;
    }
    if (size - nleft < 0) {
        logError(-1, logfp, "[CLIENT] Client %d send error", sendID);
        return -1;
    }
    return size;
}

int RelayServer::writeMsgToFile(const int& id, const void* buf, const size_t& size) {
    printf("size: %zd\n", size);
    char filename[NAME_MAX];
    sprintf(filename, "MESSAGE_TO_%d.txt", id);
    FILE* fp = nullptr;
    if (msgUnsend.find(id) != msgUnsend.end()) {
        fp = msgUnsend[id];
    }
    else {
        fp            = fopen(filename, "a");
        msgUnsend[id] = fp;
    }
    fwrite(buf, size, 1, fp);
    fwrite("\n", 1, 1, fp);
    fflush(fp);
    return 0;
}

int RelayServer::sendSavedMsgToClient(const int& connfd, const int& id) {
    if (msgUnsend.find(id) != msgUnsend.end()) {
        if (msgUnsend[id] != nullptr) {
            fclose(msgUnsend[id]);
        }
        msgUnsend.erase(id);
    }
    char filename[NAME_MAX];
    sprintf(filename, "MESSAGE_TO_%d.txt", id);
    if (access(filename, F_OK) == 0) {
        size_t totalSize = 0;
        char   buf[BUFFER_SIZE + 1];
        buf[BUFFER_SIZE] = '\0';
        FILE* fp         = fopen(filename, "r");
        while (fgets(buf + hSize, BUFFER_SIZE - hSize + 1, fp) != nullptr) {
            size_t msgLen = strlen(buf + hSize);
            if (feof(fp) && buf[hSize] == '\n') {
                break;
            }
            if (buf[hSize + msgLen - 1] == '\n') {
                buf[hSize + msgLen - 1] = '\0';
            }
            Header header;
            header.length = htons(msgLen);
            memcpy(buf, &header, hSize);
            if (sendToClient(connfd, buf, msgLen + hSize) < 0) {
                fclose(fp);
                return -1;
            }
            totalSize += msgLen;
            if (feof(fp)) {
                break;
            }
        }
        fclose(fp);
        if (remove(filename) < 0) {
            return logError(1, logfp, "[SERVER] remove error for file %s", filename);
        }
        return logInfo(0, logfp, "[CLIENT] Finish sending %zd bytes of text to client %d", totalSize, id);
    }
    return 0;
}