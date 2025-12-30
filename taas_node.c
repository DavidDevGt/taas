/*
 * TaaS Node - Userspace PTP Daemon
 * SPDX-License-Identifier: GPL-2.0
 *
 * TaaS User-Space Time Node
 *
 * This process maps the BCM2837 system timer directly and serves
 * raw hardware timestamps over UDP.
 *
 * Design constraints:
 * - No dynamic allocation after init
 * - No libc time functions
 * - Deterministic execution path
 *
 * This is a validation and deployment node, not a full PTP stack.
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
#include <sched.h>

#define PTP_PORT 1588
#define TIMER_DEVICE "/dev/taas_timer"
#define MAP_SIZE 4096

static int timer_fd = -1;
static void *map_base = NULL;

/*
 * shutdown_node - async-signal-safe cleanup handler
 *
 * Only async-signal-safe functions are used here.
 * No heap allocation or stdio calls are performed.
 */
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
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(3, &cpuset);
	if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) < 0) {
		perror("taas warning: isolcpus is active?");
	}
	
    struct sched_param sp = { .sched_priority = 99 };
    struct sockaddr_in servaddr, cliaddr;
    volatile uint32_t *st_low, *st_high;
    int sockfd;

    signal(SIGINT, shutdown_node);
    signal(SIGTERM, shutdown_node);

    /*
     * Lock all current and future pages to avoid page faults
     * during real-time execution.
     */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0)
        perror("taas: warning: mlockall failed");

    /*
     * Elevate process to real-time FIFO scheduling.
     * Failure is logged but not fatal.
     */
    if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
        perror("taas: warning: sched_setscheduler failed");

    /*
     * Open the timer device for MMIO mapping.
     */
    timer_fd = open(TIMER_DEVICE, O_RDWR | O_SYNC);
    if (timer_fd < 0) {
        perror("taas: open timer device");
        return EXIT_FAILURE;
    }

    /*
     * Map the timer registers into user space.
     * MAP_SHARED ensures visibility of hardware updates.
     */
    map_base = mmap(NULL, MAP_SIZE, PROT_READ, MAP_SHARED, timer_fd, 0);
    if (map_base == MAP_FAILED) {
        perror("taas: mmap failed");
        close(timer_fd);
        return EXIT_FAILURE;
    }

    /* BCM2837 System Timer Offsets: Low (0x04), High (0x08) */
    st_low  = (volatile uint32_t *)((char *)map_base + 0x04);
    st_high = (volatile uint32_t *)((char *)map_base + 0x08);

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

    uint32_t trigger;
    uint32_t h1, h2, l;
    uint64_t ts;
    socklen_t len = sizeof(cliaddr);

    /*
     * Main event loop:
     * - Wait for UDP trigger
     * - Perform atomic 64-bit timer read
     * - Send raw timestamp back
     */
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
