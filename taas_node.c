/*
 * TaaS Node - Userspace PTP Daemon
 * SPDX-License-Identifier: GPL-2.0
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PTP_PORT 1588
#define TIMER_DEVICE "/dev/taas_timer"
#define MAP_SIZE 4096

static int timer_fd = -1;
static void *map_base = NULL;

/* Async-signal-safe cleanup handler */
void shutdown_node(int sig)
{
    if (map_base != MAP_FAILED && map_base != NULL) 
        munmap(map_base, MAP_SIZE);
    
    if (timer_fd >= 0) 
        close(timer_fd);
        
    const char msg[] = "\n[taas] stopping daemon\n";
    write(STDOUT_FILENO, msg, sizeof(msg)-1);
    _exit(EXIT_SUCCESS);
}

int main(void)
{
    struct sched_param sp = { .sched_priority = 99 };
    struct sockaddr_in servaddr, cliaddr;
    volatile uint32_t *st_low, *st_high;
    int sockfd;

    /* 1. Setup Signals */
    signal(SIGINT, shutdown_node);
    signal(SIGTERM, shutdown_node);

    /* 2. Real-Time & Memory Locking */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0)
        perror("taas: warning: mlockall failed");

    if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
        perror("taas: warning: sched_setscheduler failed");

    /* 3. Hardware Access (MMIO) */
    timer_fd = open(TIMER_DEVICE, O_RDWR | O_SYNC);
    if (timer_fd < 0) {
        perror("taas: open timer device");
        return EXIT_FAILURE;
    }

    map_base = mmap(NULL, MAP_SIZE, PROT_READ, MAP_SHARED, timer_fd, 0);
    if (map_base == MAP_FAILED) {
        perror("taas: mmap failed");
        close(timer_fd);
        return EXIT_FAILURE;
    }

    /* BCM2837 System Timer Offsets: Low (0x04), High (0x08) */
    st_low  = (volatile uint32_t *)((char *)map_base + 0x04);
    st_high = (volatile uint32_t *)((char *)map_base + 0x08);

    /* 4. Network Setup */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("taas: socket creation");
        return EXIT_FAILURE;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PTP_PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("taas: bind failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    /* 5. Event Loop (Zero-Copy Read) */
    uint32_t trigger;
    uint32_t h1, h2, l;
    uint64_t ts;
    socklen_t len = sizeof(cliaddr);

    while (1) {
        if (recvfrom(sockfd, &trigger, sizeof(trigger), 0, 
                    (struct sockaddr *)&cliaddr, &len) > 0) {
            
            /* Atomic 64-bit read loop */
            do {
                h1 = *st_high;
                l  = *st_low;
                h2 = *st_high;
            } while (h1 != h2);

            ts = ((uint64_t)h1 << 32) | l;
            
            sendto(sockfd, &ts, sizeof(ts), 0, (struct sockaddr *)&cliaddr, len);
        }
    }

    return 0;
}
