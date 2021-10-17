#include "RelayServer.hpp"

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("usage: RelayServer <IP_Address> <Port>\n");
        return 0;
    }
    RelayServer server;
    server.start(argv[1], argv[2], 0);
    return 0;
}
