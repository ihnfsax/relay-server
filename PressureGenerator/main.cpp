#include "PressureGenerator.hpp"

int main(int argc, char** argv) {
    if (argc != 5) {
        printf("usage: PressureGenerator <IP_Address> <Port> <Seession_Count> <Time>\n");
        return 0;
    }
    int               sessionCount = atoi(argv[3]);
    int               seconds      = atoi(argv[4]);
    PressureGenerator generator;
    generator.start(argv[1], argv[2], sessionCount, seconds, 1);
    return 0;
}