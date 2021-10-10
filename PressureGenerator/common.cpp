#include "common.hpp"

#define LOGGER_PRETTY_TIME_FORMAT "%Y-%m-%d %H:%M:%S"
#define LOGGER_PRETTY_MS_FORMAT ".%03d"
#define PACKET_TIME_FORMAT "%H:%M:%S "
#define PACKET_US_FORMAT ".%06ld"

std::string prettyTime() {
    auto        tp           = std::chrono::system_clock::now();
    std::time_t current_time = std::chrono::system_clock::to_time_t(tp);
    std::tm*    time_info    = std::localtime(&current_time);

    char buffer[128];
    int  string_size = strftime(buffer, sizeof(buffer), LOGGER_PRETTY_TIME_FORMAT, time_info);

    auto dur = tp.time_since_epoch();
    int  ms  = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()) % 1000;
    string_size += std::snprintf(buffer + string_size, sizeof(buffer) - string_size, LOGGER_PRETTY_MS_FORMAT, ms);

    return std::string(buffer, buffer + string_size);
}

std::string strftTime(struct timespec* timestamp) {
    struct tm time0;
    localtime_r(&(timestamp->tv_sec), &time0);
    char currentTime[17];
    strftime(currentTime, 10, PACKET_TIME_FORMAT, &time0);
    sprintf(currentTime + 9, PACKET_US_FORMAT, timestamp->tv_nsec / 1000);
    return std::string(currentTime);
}

uint64_t ntoh64(uint64_t net64) {
    if (IS_LITTLE)
        return bswap_64(net64);
    else
        return net64;
}

uint64_t hton64(uint64_t host64) {
    if (IS_LITTLE)
        return bswap_64(host64);
    else
        return host64;
}

struct timespec getHeader(uint16_t length, uint32_t id, Header* header) {
    header->length = htons(length);
    header->id     = htonl(id);
    struct timespec timestamp;
    clock_gettime(CLOCK_REALTIME, &timestamp);
    header->sec  = hton64(timestamp.tv_sec);
    header->nsec = hton64(timestamp.tv_nsec);
    return timestamp;
}

int logInfo(int returnValue, FILE* fp, const char* fmt, ...) {
    if (fp == nullptr) {
        return returnValue;
    }
    fprintf(fp, "%s - INFO - ", prettyTime().c_str());
    char    msgBuf[LINE_MAX + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgBuf, LINE_MAX, fmt, ap);
    va_end(ap);
    strcat(msgBuf, "\n");
    fprintf(fp, "%s", msgBuf);
    fflush(fp);
    return returnValue;
}

int logError(int returnValue, FILE* fp, const char* fmt, ...) {
    if (fp == nullptr) {
        return returnValue;
    }
    fprintf(fp, "%s - ERROR - ", prettyTime().c_str());
    int     errno_save = errno;
    char    msgBuf[LINE_MAX + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgBuf, LINE_MAX, fmt, ap);
    va_end(ap);
    int n = strlen(msgBuf);
    if (errno_save != 0)
        snprintf(msgBuf + n, LINE_MAX - n, ": %s", strerror(errno_save));
    strcat(msgBuf, "\n");
    fprintf(fp, "%s", msgBuf);
    fflush(fp);
    return returnValue;
}

int createSocket(int family, int type, int protocol, FILE* fp) {
    int n;
    if ((n = socket(family, type, protocol)) < 0)
        return logError(-1, fp, "socket error");
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

int setblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option & (~O_NONBLOCK);
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, int enable_out, int enable_et) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events  = EPOLLIN;
    if (enable_out) {
        event.events |= EPOLLOUT;
    }
    if (enable_et) {
        event.events |= EPOLLET;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}

void delfd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
}