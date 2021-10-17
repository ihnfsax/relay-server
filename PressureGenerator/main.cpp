#include "PressureGenerator.hpp"

int main(int argc, char** argv) {
    if (argc != 6) {
        printf("usage: PressureGenerator <IP_Address> <Port> <Seession_Count> <Time> <Packet_Size>\n");
        return 0;
    }
    int               sessionCount = atoi(argv[3]);
    int               seconds      = atoi(argv[4]);
    int               packetSize   = atoi(argv[5]);
    PressureGenerator generator;
    generator.start(argv[1], argv[2], sessionCount, seconds, packetSize, 0);
    return 0;
}