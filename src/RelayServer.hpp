#include "common.hpp"
#include <set>
#include <string>

class RelayServer {
private:
    std::set<uint16_t> clientIDs;             /* 已连接客户端ID集合 */
    int                status = 0;            /* 服务器状态 */
    FILE*              logfp  = nullptr;      /* log文件指针 */
    pid_t              pid;                   /* 进程ID */
    char               logFilename[NAME_MAX]; /* log文件名 */
    int                doit(const char* ip, const char* port);

public:
    RelayServer() {
        pid = getpid();
        snprintf(logFilename, NAME_MAX - 1, "SERVER_%d.log", pid);
    }

    ~RelayServer() {
        if (logfp != nullptr) {
            fclose(logfp);
        }
    }

    int start(const char* ip, const char* port, int logFlag = 0);
    int start(std::string ip, std::string port, int logFlag = 0);
};