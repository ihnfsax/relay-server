#include "common.hpp"

int logInfo(int returnValue, FILE* fp, const char* fmt, ...) {
    if (fp == nullptr) {
        return returnValue;
    }
    char    buf[LINE_MAX + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, LINE_MAX, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    fprintf(fp, "%s", buf);
    fflush(fp);
    return returnValue;
}

int logError(int returnValue, FILE* fp, const char* fmt, ...) {
    if (fp == nullptr) {
        return returnValue;
    }
    char    buf[LINE_MAX + 1];
    int     errno_save = errno;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, LINE_MAX, fmt, ap);
    va_end(ap);
    int n = strlen(buf);
    snprintf(buf + n, LINE_MAX - n, ": %s", strerror(errno_save));
    strcat(buf, "\n");
    fprintf(fp, "%s", buf);
    fflush(fp);
    return returnValue;
}

int createSocket(int family, int type, int protocol, FILE* fp) {
    int n;
    if ((n = socket(family, type, protocol)) < 0)
        return logError(-1, fp, "listen error");
    return n;
}

int toBind(int fd, const struct sockaddr* sa, socklen_t salen, FILE* fp) {
    if (bind(fd, sa, salen) < 0)
        return logError(-1, fp, "bind error");
    return 0;
}

int toListen(int fd, int backlog, FILE* fp) {
    if (listen(fd, backlog) < 0)
        return logError(-1, fp, "listen error");
    return 0;
}

int inetPton(int family, const char* strptr, void* addrptr, FILE* fp) {
    int n;
    if ((n = inet_pton(family, strptr, addrptr)) < 0)
        return logError(-1, fp, "inet_pton error"); /* errno set */
    else if (n == 0)
        return logInfo(-1, fp, "inet_pton error"); /* errno not set */
    return 0;
}

int setPort(const char* strptr, in_port_t* addrptr, FILE* fp) {
    int port;
    if (strptr[0] == '0' && strlen(strptr) == 1)
        (*addrptr) = htons(0);
    else {
        port = (int)strtol(strptr, NULL, 10);
        if (port <= 0 || port > 65535)
            return logInfo(-1, fp, "Invalid port number");
        else
            (*addrptr) = htons(port);
    }
    return 0;
}

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, int enable_et) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events  = EPOLLIN;
    if (enable_et) {
        event.events |= EPOLLET;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // setnonblocking(fd);
}

void delfd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
}