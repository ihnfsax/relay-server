#include "RelayServer.hpp"
#include <vector>

int RelayServer::exitFlag = 0;
int RelayServer::logFlag  = 0;

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
    logInfo(0, logfp, "RelayServer - server - server shutdowns");
    printf("Server shutdowns\n");
    printStatistics();
    if (logfp != nullptr) {
        fclose(logfp);
        logfp = nullptr;
    }
    status = 0;
    return r;
}

void RelayServer::printStatistics() {
    logInfo(0, logfp, "RelayServer - server - Statistics:");
    logInfo(0, logfp, "RelayServer - server - usrBufferSize: %d", BUFFER_SIZE);
    logInfo(0, logfp, "RelayServer - server - recvBytes: %lu", s_recvBytes);
    logInfo(0, logfp, "RelayServer - server - recvPackets: %lu", s_recvPackets);
    logInfo(0, logfp, "RelayServer - server - recvFINs: %lu", s_recvFINs);
    logInfo(0, logfp, "RelayServer - server - recvNoSpace: %lu", s_recvNoSpace);
    logInfo(0, logfp, "RelayServer - server - recvSuccess: %lu", s_recvSuccess);
    logInfo(0, logfp, "RelayServer - server - recvEAGAIN: %lu", s_recvEAGAIN);
    logInfo(0, logfp, "RelayServer - server - recvError: %lu", s_recvError);
    logInfo(0, logfp, "RelayServer - server - sendBytes: %lu", s_sendBytes);
    logInfo(0, logfp, "RelayServer - server - sendNoData: %lu", s_sendNoData);
    logInfo(0, logfp, "RelayServer - server - sendSuccess: %lu", s_sendSuccess);
    logInfo(0, logfp, "RelayServer - server - sendEAGAIN: %lu", s_sendEAGAIN);
    logInfo(0, logfp, "RelayServer - server - sendError: %lu", s_sendError);
    printf("Server statistics:\n\n");
    printf("usrBufferSize: %d\n\n", BUFFER_SIZE);
    printf("recvBytes: %lu\n", s_recvBytes);
    printf("recvPackets: %lu\n", s_recvPackets);
    printf("recvFINs: %lu\n", s_recvFINs);
    printf("recvNoSpace: %lu\n", s_recvNoSpace);
    printf("recvSuccess: %lu\n", s_recvSuccess);
    printf("recvEAGAIN: %lu\n", s_recvEAGAIN);
    printf("recvError: %lu\n\n", s_recvError);
    printf("sendBytes: %lu\n", s_sendBytes);
    printf("sendNoData: %lu\n", s_sendNoData);
    printf("sendSuccess: %lu\n", s_sendSuccess);
    printf("sendEAGAIN: %lu\n", s_sendEAGAIN);
    printf("sendError: %lu\n", s_sendError);
}

/* 返回值：-1表示出现错误终止，0表示被SIGINT信号终止 */
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
    logInfo(0, logfp, "RelayServer - server - bind to %s:%s", ip, port);

    /* 创建epoll事件表描述符 */
    struct epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(1);
    assert(epollfd >= 0);

    /* 开始监听 */
    if (toListen(listenfd, BACKLOG, logfp) < 0) {
        close(listenfd);
        return -1;
    }
    logInfo(0, logfp, "RelayServer - server - begin to listen", ip, port);

    /* 添加监听套接字到epoll事件表 */
    addfd(epollfd, listenfd, 0, 0);
    setnonblocking(listenfd);

    while (true) {
        /* 等待事件 */
        int ready = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ready < 0) {
            logError(0, logfp, "RelayServer - server - epoll_wait error");
            shutdownAll();
        }
        /* 处理事件 */
        if (handleEvents(events, ready) < 0) {
            shutdownAll();
        }
        if (exitFlag || shutFlag) {
            shutdownAll();
            if (clientFDs.size() == 0) {
                logInfo(0, logfp, "RelayServer - server - all connected sockets are closed");
                prepareExit();
                break;
            }
        }
    }

    return 0;
}

int RelayServer::handleEvents(struct epoll_event* events, const int& number) {
    for (int i = 0; i < number; ++i) {
        int sockfd = events[i].data.fd;
        /* 监听套接字 */
        if (sockfd == listenfd) {
            while (true) {
                int connfd = accept(listenfd, NULL, NULL);
                if (connfd < 0) {
                    if (errno == EWOULDBLOCK || errno == ECONNABORTED || errno == EPROTO || errno == EINTR)
                        break;
                    else
                        return logError(-1, logfp, "RelayServer - server - accept error");
                }
                ClientInfo* client = new ClientInfo;
                client->connfd     = connfd;
                setnonblocking(connfd);
                addClient(client);
            }
        }
        /* 已连接套接字 */
        else {
            /* 初始检查与设置 */
            assert(clientFDs.find(sockfd) != clientFDs.end());
            int         selfID = clientFDs[sockfd]->cliID;
            ClientInfo* selfC  = clientIDs[selfID];
            assert(clientIDs.find(selfID) != clientIDs.end());
            int         peerID = counterPart(selfID);
            ClientInfo* peerC  = nullptr;
            if (clientIDs.find(peerID) != clientIDs.end()) {
                peerC = clientIDs[peerID];
                assert(clientFDs.find(peerC->connfd) != clientFDs.end());
            }
            if (peerC == nullptr) {
                selfC->recved = 0;
                if (BETTER_EPOLL && selfC->epollIn == 0) {
                    modfd(epollfd, selfC->connfd, 1, selfC->epollOut);
                    selfC->epollIn = 1;
                }
            }
            /* 有数据可读，并且有空间可存 */
            if (events[i].events & EPOLLIN) {
                // 如果没有空间接收数据
                if (BUFFER_SIZE == selfC->recved) {
                    s_recvNoSpace++;
                }
                // 如果有空间可接收数据
                else {
                    ssize_t n = recv(sockfd, selfC->usrBuf + selfC->recved, BUFFER_SIZE - selfC->recved, 0);
                    if (n > 0) {
                        s_recvSuccess++;
                        s_recvBytes += n;
                        while (true) {
                            /* 报头或载荷接收完毕 */
                            if ((size_t)n >= selfC->unrecv) {
                                // 处理报头
                                if (selfC->recvFlag == 0) {
                                    memcpy((char*)&selfC->header + (sizeof(Header) - selfC->unrecv),
                                           selfC->usrBuf + selfC->recved, selfC->unrecv);
                                    size_t msgLen = (size_t)handleHeader(&selfC->header, selfID);
                                    s_recvPackets++;
                                    n               = n - selfC->unrecv;
                                    selfC->recved   = selfC->recved + selfC->unrecv;
                                    selfC->recvFlag = 1;
                                    selfC->unrecv   = msgLen;
                                }
                                // 处理载荷
                                else {
                                    if (peerC == nullptr && SAVE_FILE) {
                                        writeMsgToFile(peerID, selfC->usrBuf + selfC->recved,
                                                       selfC->unrecv - 1); /* 不能把/0写进文件 */
                                        writeMsgToFile(peerID, "\n", 1);
                                    }
                                    n               = n - selfC->unrecv;
                                    selfC->recved   = selfC->recved + selfC->unrecv;
                                    selfC->recvFlag = 0;
                                    selfC->unrecv   = sizeof(Header);
                                }
                            }
                            /* 只接受了一部分 */
                            else {
                                if (selfC->recvFlag == 0) {
                                    memcpy((char*)&selfC->header + (sizeof(Header) - selfC->unrecv),
                                           selfC->usrBuf + selfC->recved, n);
                                }
                                else if (selfC->recvFlag == 1 && peerC == nullptr && SAVE_FILE) {
                                    writeMsgToFile(peerID, selfC->usrBuf + selfC->recved, n);
                                }
                                selfC->unrecv = selfC->unrecv - n;
                                selfC->recved = selfC->recved + n;
                                break;
                            }
                        }
                    }
                    else if (n == 0) {
                        s_recvFINs++;
                        logInfo(0, logfp, "RelayServer - client %d - receive FIN from client (id:%u)", selfID,
                                selfC->id);
                        if (selfC->state == 0) { /* 之前未关闭连接，则直接关闭写 */
                            shutdown(sockfd, SHUT_WR);
                        }
                        else {
                            shutdown(sockfd, SHUT_RD); /* 之前关闭了写，则把读关闭 */
                        }
                        /* 直接关闭写的一端，不再写了，因为数据可能源源不断地来，我们不知道还得写多少
                         */
                        removeClient(sockfd);
                        continue; /* continue最外层的for */
                    }
                    else {
                        if (errno != EWOULDBLOCK) { /* 连接已经结束，直接close套接字 */
                            s_recvError++;
                            logError(-1, logfp, "RelayServer - client %d - recv error (id:%u)", selfID, selfC->id);
                            removeClient(sockfd);
                            continue; /* continue最外层的for */
                        }
                        else {
                            s_recvEAGAIN++;
                        }
                    }
                    if (peerC == nullptr) {
                        selfC->recved = 0;
                    }
                }
                // TEST
                if (BETTER_EPOLL) {
                    if (selfC->recved == BUFFER_SIZE) {
                        modfd(epollfd, selfC->connfd, 0, selfC->epollOut);
                        selfC->epollIn = 0;
                    }
                    if (peerC != nullptr && peerC->epollOut == 0 && selfC->recved > 0) {
                        modfd(epollfd, peerC->connfd, peerC->epollIn, 1);
                        peerC->epollOut = 1;
                    }
                }
            }
            int isExist = 0;
            if (SAVE_FILE) {
                isExist = copySavedMsg(selfC);
                if (selfC->fakePeer != nullptr) {
                    peerC = selfC->fakePeer;
                    if (selfC->epollOut == 0 && peerC->recved > 0) {
                        modfd(epollfd, selfC->connfd, selfC->epollIn, 1);
                        selfC->epollOut = 1;
                    }
                }
            }
            /* 有数据需要发送，并且能够发送，并且未关闭写 */
            if ((events[i].events & EPOLLOUT) && selfC->state != 1) {
                if (peerC != nullptr) {
                    // 无数据可发送
                    if (peerC->recved == 0) {
                        s_sendNoData++;
                    }
                    // 有数据可发送
                    else {
                        ssize_t n = send(sockfd, peerC->usrBuf, peerC->recved, 0);
                        if (n >= 0) {
                            s_sendSuccess++;
                            s_sendBytes += n;
                            memcpy(peerC->usrBuf, peerC->usrBuf + n, peerC->recved - n);
                            peerC->recved = peerC->recved - n;
                        }
                        else {
                            if (errno != EWOULDBLOCK) { /* 连接已经结束，直接close套接字 */
                                s_sendError++;
                                logError(-1, logfp, "RelayServer - client %d - send error (id:%u)", selfID, selfC->id);
                                removeClient(sockfd);
                                continue; /* continue最外层的for */
                            }
                            else {
                                s_sendEAGAIN++;
                            }
                        }
                    }
                }
                if (SAVE_FILE) {
                    // 既没有匹配客户端的数据可发，也没有文件的数据可发
                    if (selfC->fakePeer != nullptr && selfC->fakePeer->recved == 0 && !isExist) {
                        delete selfC->fakePeer;
                        selfC->fakePeer = nullptr;
                        peerC           = nullptr;
                    }
                }
                if (BETTER_EPOLL) {
                    // 如果有匹配的客户端
                    if (selfC->fakePeer == nullptr && peerC != nullptr) {
                        // 可以接收新数据
                        if (peerC->recved < BUFFER_SIZE && peerC->epollIn == 0) {
                            modfd(epollfd, peerC->connfd, 1, peerC->epollOut);
                            peerC->epollIn = 1;
                        }
                        // 没有数据可发
                        if (peerC->recved == 0 && selfC->epollOut == 1) {
                            modfd(epollfd, selfC->connfd, selfC->epollIn, 0);
                            selfC->epollOut = 0;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

void RelayServer::shutdownAll() {
    if (exitFlag && logFlag) {
        logInfo(0, logfp, "RelayServer - server - received SIGINT signal");
        logFlag = 0;
    }
    if (shutFlag == 0) {
        close(listenfd);
        delfd(epollfd, listenfd);
        logInfo(0, logfp, "RelayServer - server - send FIN to all clients and stop listening");
    }
    shutFlag = 1;
    for (auto const& cli : clientFDs) {
        if (cli.second->state == 0) {
            shutdown(cli.first, SHUT_WR);
            cli.second->state = 1;
        }
    }
}

int RelayServer::addClient(ClientInfo* client) {
    client->cliID = nextID;
    assert(clientIDs.find(client->cliID) == clientIDs.end() && clientFDs.find(client->connfd) == clientFDs.end());
    clientIDs[client->cliID]  = client;
    clientFDs[client->connfd] = client;
    updateNextID();
    if (BETTER_EPOLL) {
        addfd(epollfd, client->connfd, 0, 0);
    }
    else {
        addfd(epollfd, client->connfd, 1, 0); /* 使用EPOLLIN | EPOLLOUT，启用LT模式 */
    }
    logInfo(0, logfp, "RelayServer - client %d - new client (%zd in total)", client->cliID, clientFDs.size());
    return 0;
}

int RelayServer::removeClient(const int& connfd) {
    assert(clientFDs.find(connfd) != clientFDs.end());
    int      cliID = clientFDs[connfd]->cliID;
    uint32_t id    = clientFDs[connfd]->id;
    delete clientFDs[connfd];
    clientIDs.erase(cliID);
    clientFDs.erase(connfd);
    if (cliID < nextID) {
        nextID = cliID;
    }
    if (close(connfd) < 0) {
        logError(-1, logfp, "RelayServer - client %d - close error", cliID);
    }
    delfd(epollfd, connfd);
    logInfo(0, logfp, "RelayServer - client %d - client left (id:%u) (%zd in total)", cliID, id, clientFDs.size());
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
    char filename[NAME_MAX];
    sprintf(filename, "MESSAGE_TO_%d.txt", id);
    FILE* fp = nullptr;
    if (msgAppend.find(id) != msgAppend.end()) {
        fp = msgAppend[id].fp;
    }
    else {
        fp = fopen(filename, "a");
        File file;
        file.fp = fp;
        strcpy(file.filename, filename);
        msgAppend[id] = file;
    }
    fwrite(buf, size, 1, fp);
    fflush(fp);
    return 0;
}

int RelayServer::copySavedMsg(ClientInfo* selfC) {
    FILE* fp = nullptr;
    char  filename[NAME_MAX];
    sprintf(filename, "MESSAGE_TO_%d.txt", selfC->cliID);
    if (msgRead.find(selfC->cliID) != msgRead.end()) {
        assert(msgRead[selfC->cliID].fp != nullptr);
        fp = msgRead[selfC->cliID].fp;
    }
    else {
        if (access(filename, F_OK) == 0) {
            fp = fopen(filename, "r");
            File file;
            file.fp = fp;
            strcpy(file.filename, filename);
            msgRead[selfC->cliID] = file;
        }
        else { /* 没有信息文件 */
            return 0;
        }
    }
    assert(fp != nullptr);
    if (selfC->fakePeer == nullptr) {
        selfC->fakePeer         = new ClientInfo;
        selfC->fakePeer->recved = 0;
    }
    ClientInfo* peerC = selfC->fakePeer;
    while (true) {
        ssize_t msgWindow = BUFFER_SIZE - (ssize_t)peerC->recved - (ssize_t)sizeof(Header);
        if (msgWindow > 0) {
            char* msgPtr = peerC->usrBuf + peerC->recved + sizeof(Header);
            if (fgets(msgPtr, msgWindow, fp) != nullptr) {
                if (!(feof(fp) && *msgPtr == '\n')) { /* 没有到文件末尾 */
                    size_t msgLen = strlen(msgPtr);
                    if (*(msgPtr + msgLen - 1) == '\n') {
                        *(msgPtr + msgLen - 1) = '\0';
                    }
                    else {
                        msgLen = msgLen + 1; /* 将\0包含进去 */
                    }
                    Header          header;
                    struct timespec timestamp = getHeader(msgLen, 0, &header);
                    memcpy(peerC->usrBuf + peerC->recved, &header, sizeof(Header));
                    peerC->recved = peerC->recved + sizeof(Header) + msgLen;
                    logInfo(0, logfp,
                            "RelayServer - client %d - packet from file %s: <length: "
                            "%hd, id: %d, time: %s>",
                            selfC->cliID, filename, msgLen, 0, strftTime(&timestamp).c_str());
                }
            }
            if (feof(fp)) {
                fclose(fp);
                msgRead.erase(selfC->cliID);
                if (msgAppend.find(selfC->cliID) != msgAppend.end()) {
                    fclose(msgAppend[selfC->cliID].fp);
                    msgAppend.erase(selfC->cliID);
                }
                if (remove(filename) < 0) {
                    return logError(0, logfp, "RelayServer - client %d - fail to remove file %s", selfC->cliID,
                                    filename);
                }
                else {
                    return logInfo(0, logfp, "RelayServer - client %d - remove file %s", selfC->cliID, filename);
                }
            }
        }
        else {
            break;
        }
    }
    return 1;
}

void RelayServer::sigIntHandler(int signum) {
    exitFlag = 1;
    logFlag  = 1;
}

void RelayServer::sigPipeHandler(int signum) {
    // do nothing
}

sigfunc* RelayServer::signal(int signo, sigfunc* func) {
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

uint16_t RelayServer::handleHeader(struct Header* header, const uint16_t& cliID) {
    uint16_t msgLen      = ntohs(header->length);
    clientIDs[cliID]->id = ntohl(header->id);
    if (logfp == nullptr)
        return msgLen;
    // struct timespec timestamp;
    // timestamp.tv_sec  = ntoh64(header->sec);
    // timestamp.tv_nsec = ntoh64(header->nsec);
    // logInfo(0, logfp, "RelayServer - client %d - recv header: <length: %hd, id:
    // %d, time: %s>", cliID, msgLen,
    //         ntohl(header->id), strftTime(&timestamp).c_str());
    return msgLen;
}

void RelayServer::prepareExit() {
    for (auto file : msgRead) {
        fclose(file.second.fp);
        if (msgAppend.find(file.first) != msgAppend.end()) {
            fclose(msgAppend[file.first].fp);
        }
        remove(file.second.filename);
    }
    logInfo(0, logfp, "RelayServer - server - all read files are deleted");
}