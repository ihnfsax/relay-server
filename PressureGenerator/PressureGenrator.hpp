#include "common.hpp"
#include <map>
#include <string>

typedef struct clientInfo {
    uint16_t cliID;
    int      connfd;
} clientInfo;

class PressureGenrator {
private:
    std::map<uint16_t, clientInfo*> clientIDs; /* 已连接客户端集合1 */
    std::map<int, clientInfo*>      clientFDs; /* 已连接客户端集合2 */
};
