/*
 * TaaS PTP Node
 * -------------
 * Real-time user-space daemon for deterministic timestamp serving
 *
 * Requires:
 *  - taas_driver loaded
 *  - /dev/taas_timer present
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdint.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>

#define PTP_PORT 1588
#define TIMER_DEVICE "/dev/taas_timer"

int timer_fd = -1;
void *map_base = NULL;

void shutdown_node(int sig) {
    if (map_base != MAP_FAILED && map_base != NULL) munmap(map_base, 4096);
    if (timer_fd >= 0) close(timer_fd);
    printf("\n[TaaS] Node shutdown complete.\n");
    exit(0);
}

int main() {
    signal(SIGINT, shutdown_node);
    signal(SIGTERM, shutdown_node);

    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) {
        perror("[!] Warning: mlockall failed (check LimitMEMLOCK)");
    }

    struct sched_param sp = { .sched_priority = 99 };
    if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0) {
        perror("[!] Warning: Could not set RT priority");
    }

    timer_fd = open(TIMER_DEVICE, O_RDWR | O_SYNC);
    if (timer_fd < 0) {
        perror("[!] Error: Driver access failed");
        return -1;
    }

    map_base = mmap(NULL, 4096, PROT_READ, MAP_SHARED, timer_fd, 0);
    if (map_base == MAP_FAILED) {
        perror("[!] Error: MMAP failed");
        close(timer_fd);
        return -1;
    }

    volatile uint32_t *st_low  = (volatile uint32_t *)((char *)map_base + 0x04);
    volatile uint32_t *st_high = (volatile uint32_t *)((char *)map_base + 0x08);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr, cliaddr;
    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PTP_PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Bind error");
        return -1;
    }

    printf("=========================================\n");
    printf("   TaaS MASTER NODE - 64-BIT MODE        \n");
    printf("=========================================\n");

    uint32_t trigger;
    uint32_t h1, h2, l;
    uint64_t timestamp;
    socklen_t len = sizeof(cliaddr);

    while(1) {
        if (recvfrom(sockfd, &trigger, sizeof(trigger), 0, (struct sockaddr *)&cliaddr, &len) > 0) {
            do {
                h1 = *st_high;
                l  = *st_low;
                h2 = *st_high;
            } while (h1 != h2);

            timestamp = ((uint64_t)h1 << 32) | l;
            sendto(sockfd, &timestamp, sizeof(uint64_t), 0, (struct sockaddr *)&cliaddr, len);
        }
    }

    return 0;
}
