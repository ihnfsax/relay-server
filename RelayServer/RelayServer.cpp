#include "RelayServer.hpp"
#include <vector>

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
    logInfo(0, logfp, "[SERVER] Server shutdown");
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
        logError(-1, logfp, "setsockopt error");
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
    addfd(epollfd, listenfd, 0, 0);
    setnonblocking(listenfd);

    while (1) {
        int ready = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ready < 0) {
            logError(-1, logfp, "[SERVER] epoll_wait error");
            closeServer();
            return close(listenfd);
        }
        /* 处理事件 */
        if (handle_events(events, ready) < 0) {
            closeServer();
            return close(listenfd);
        }
    }

    return close(listenfd);
}

int RelayServer::handle_events(struct epoll_event* events, const int& number) {
    for (int i = 0; i < number; ++i) {
        int sockfd = events[i].data.fd;
        /* 监听套接字 */
        if (sockfd == listenfd) {
            struct sockaddr_in cliAddr;
            socklen_t          cliAddrLen;
            int                connfd = accept(listenfd, (struct sockaddr*)&cliAddr, &cliAddrLen);
            if (connfd < 0) {
                if (errno == EWOULDBLOCK || errno == ECONNABORTED || errno == EPROTO || errno == EINTR)
                    continue;
                else
                    return logError(-1, logfp, "[SERVER] Unexpected error on accept");
            }
            ClientInfo* client = new ClientInfo;
            client->connfd     = connfd;
            addClient(client);
        }
        /* 已连接套接字 */
        else {
            /* 有数据可读 */
            if (events[i].events & EPOLLIN) {
                assert(clientFDs.find(sockfd) != clientFDs.end());
                int         srcID     = clientFDs[sockfd]->cliID;
                ClientInfo* srcClient = clientIDs[srcID];
                assert(clientIDs.find(srcID) != clientIDs.end());
                int desID = counterPart(srcID);
                /* 接收头部 */
                if (clientFDs[sockfd]->recvStatus == 0) {
                    assert(srcClient->unrecv != 0);
                    ssize_t n = recv(sockfd, srcClient->recvPtr, srcClient->unrecv, 0);
                    if (n == srcClient->unrecv) { /* 接收完毕 */
                        Header header;
                        memcpy(&header, srcClient->recvBuf, sizeof(Header));
                        size_t msgLen = (size_t)ntohs(header.length);
                        if (msgLen <= RECVBUF_MAX && msgLen > 0) {
                            srcClient->recvPtr    = srcClient->recvBuf;
                            srcClient->unrecv     = msgLen;
                            srcClient->recvStatus = 1;
                        }
                        else if (msgLen == 0) {
                            srcClient->recvPtr    = srcClient->recvBuf;
                            srcClient->unrecv     = sizeof(Header);
                            srcClient->recvStatus = 0;
                        }
                        else {
                            logInfo(-1, logfp, "[SRC_CLIENT%d] Message length is larger than the receive buffer size",
                                    srcID);
                            removeClient(sockfd);
                            continue;
                        }
                    }
                    else if (n > 0 && n < srcClient->unrecv) { /* 接收了一部分 */
                        srcClient->recvPtr += n;
                        srcClient->unrecv -= n;
                    }
                    else if (n == 0) { /* 遇到FIN */
                        logInfo(-1, logfp, "[SRC_CLIENT%d] Client sended FIN", srcID);
                        removeClient(sockfd);
                        continue;
                    }
                    else { /* 遇到错误 */
                        if (errno != EWOULDBLOCK) {
                            logError(-1, logfp, "[SRC_CLIENT%d] Unexpected error on recv", srcID);
                            removeClient(sockfd);
                            continue;
                        }
                    }
                } /* 接收载荷 */
                if (clientFDs[sockfd]->recvStatus != 0) {
                    assert(srcClient->unrecv != 0);
                    ssize_t n = recv(sockfd, srcClient->recvPtr, srcClient->unrecv, 0);
                    if (n == srcClient->unrecv) { /* 接收完毕 */
                        size_t msgLen = (srcClient->recvPtr - srcClient->recvBuf) + n;
                        /* 如果另一端存在，则复制数据 */
                        if (clientIDs.find(desID) != clientIDs.end()) {
                            ClientInfo* desClient = clientIDs[desID];
                            if (SENDBUF_MAX - (desClient->sendPtr - desClient->sendBuf) > sizeof(Header) + msgLen) {
                                Header header;
                                header.length = htons((uint16_t)msgLen);
                                memcpy(desClient->sendPtr, &header, sizeof(Header));
                                desClient->sendPtr += sizeof(Header);
                                memcpy(desClient->sendPtr, srcClient->recvBuf, msgLen);
                                desClient->sendPtr += msgLen;
                            }
                            else {
                                logInfo(-1, logfp, "[DES_CLIENT%d] Insufficient available send buffer space");
                                /* 丢弃报文 */
                            }
                        }
                        /* 否则将数据保存在文件 */
                        else {
                            writeMsgToFile(desID, srcClient->recvBuf, msgLen - 1);
                        }
                        /* 设置状态以接收新的报文 */
                        srcClient->recvPtr    = srcClient->recvBuf;
                        srcClient->unrecv     = sizeof(Header);
                        srcClient->recvStatus = 0;
                    }
                    else if (n > 0 && n < srcClient->unrecv) { /* 接收了一部分 */
                        srcClient->recvPtr += n;
                        srcClient->unrecv -= n;
                    }
                    else if (n == 0) { /* 遇到FIN */
                        logInfo(-1, logfp, "[SRC_CLIENT%d] Client sended FIN", srcID);
                        removeClient(sockfd);
                        continue;
                    }
                    else { /* 遇到错误 */
                        if (errno != EWOULDBLOCK) {
                            logError(-1, logfp, "[SRC_CLIENT%d] Unexpected error on recv", srcID);
                            removeClient(sockfd);
                            continue;
                        }
                    }
                }
            }
            /* 有空间可以发送数据 */
            if (events[i].events & EPOLLOUT) {
                assert(clientFDs.find(sockfd) != clientFDs.end());
                ClientInfo* desClient = clientFDs[sockfd];
                /* 转存保存了的数据 */
                if (copySavedMsg(desClient->cliID) == -1) {
                    logInfo(-1, logfp, "[DES_CLIENT%d] Failed to copy saved message to client's send buffer",
                            desClient->cliID);
                    continue;
                }
                desClient->unsend = desClient->sendPtr - desClient->sendBuf;
                if (desClient->unsend > 0) {
                    ssize_t n = send(sockfd, desClient->sendBuf, desClient->unsend, 0);
                    if (n > 0) {
                        desClient->unsend -= n;
                        char* unsendPtr = desClient->sendPtr - desClient->unsend;
                        memcpy(desClient->sendBuf, unsendPtr, desClient->unsend);
                        desClient->sendPtr = desClient->sendBuf + desClient->unsend;
                    }
                    else if (n == 0) { /* 空间不足 */
                        continue;
                    }
                    else { /* 遇到错误 */
                        if (errno != EWOULDBLOCK) {
                            logError(-1, logfp, "[DES_CLIENT%d] Unexpected error on recv", desClient->cliID);
                            removeClient(sockfd);
                            continue;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

void RelayServer::closeServer() {
    std::vector<int> connfds;
    for (auto const& cli : clientFDs)
        connfds.push_back(cli.first);
    for (auto const& connfd : connfds)
        removeClient(connfd);
    logInfo(0, logfp, "[SERVER] All connected sockets have been closed");
}

int RelayServer::addClient(ClientInfo* client) {
    client->cliID = nextID;
    assert(clientIDs.find(client->cliID) == clientIDs.end() && clientFDs.find(client->connfd) == clientFDs.end());
    clientIDs[client->cliID]  = client;
    clientFDs[client->connfd] = client;
    updateNextID();
    addfd(epollfd, client->connfd, 1, 0); /* 使用EPOLLIN | EPOLLOUT，启用LT模式 */
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

int RelayServer::writeMsgToFile(const int& id, const void* buf, const size_t& size) {
    printf("size: %zd\n", size);
    char filename[NAME_MAX];
    sprintf(filename, "MESSAGE_TO_%d.txt", id);
    FILE* fp = nullptr;
    if (msgUnsend.find(id) != msgUnsend.end()) {
        fp = msgUnsend[id].savedFile;
    }
    else {
        fp = fopen(filename, "a");
        SavedMsg savedMsg;
        savedMsg.cliID     = id;
        savedMsg.savedFile = fp;
        savedMsg.offset    = 0;
        msgUnsend[id]      = savedMsg;
    }
    fwrite(buf, size, 1, fp);
    fwrite("\n", 1, 1, fp);
    fflush(fp);
    return 0;
}

int RelayServer::copySavedMsg(const int& id) {
    // if (msgUnsend.find(id) != msgUnsend.end()) {
    //     if (msgUnsend[id] != nullptr) {
    //         fclose(msgUnsend[id]);
    //     }
    //     msgUnsend.erase(id);
    // }
    char filename[NAME_MAX];
    sprintf(filename, "MESSAGE_TO_%d.txt", id);
    ClientInfo* desClient = clientIDs[id];
    if (access(filename, F_OK) == 0) {
        size_t totalSize = 0;
        char   buf[SENDBUF_MAX + 1];
        buf[SENDBUF_MAX] = '\0';
        FILE* fp         = fopen(filename, "r");
        while (fgets(buf + sizeof(Header), SENDBUF_MAX - sizeof(Header) + 1, fp) != nullptr) {
            size_t msgLen = strlen(buf + sizeof(Header));
            if (feof(fp) && buf[sizeof(Header)] == '\n') {
                break;
            }
            if (buf[sizeof(Header) + msgLen - 1] == '\n') {
                buf[sizeof(Header) + msgLen - 1] = '\0';
            }
            Header header;
            header.length = htons(msgLen);
            memcpy(buf, &header, sizeof(Header));
            if (msgLen + sizeof(Header) > SENDBUF_MAX - (desClient->sendPtr - desClient->sendBuf)) {
                break;
            }
            else {
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