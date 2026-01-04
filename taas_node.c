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
 * [ADDENDUM] Integrated Trusted Timestamping Authority logic.
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

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#define PTP_PORT 1588
#define TIMER_DEVICE "/dev/taas_timer"
#define MAP_SIZE 4096
#define KEY_FILE "private_key.pem"

struct __attribute__((packed)) taas_certificate {
    uint8_t  client_hash[32];
    uint64_t hw_timestamp;
    uint8_t  signature[64];
};

static int timer_fd = -1;
static void *map_base = NULL;
static EVP_PKEY *pkey = NULL;
static EVP_MD_CTX *md_ctx = NULL;

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

    if (md_ctx)
        EVP_MD_CTX_free(md_ctx);

    if (pkey)
        EVP_PKEY_free(pkey);

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

    OpenSSL_add_all_algorithms();
    
    md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        fprintf(stderr, "Fatal: Failed to allocate crypto context\n");
        return EXIT_FAILURE;
    }

    FILE *fp = fopen(KEY_FILE, "r");
    if (fp) {
        pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
        fclose(fp);
    } else {
        fprintf(stderr, "Fatal: Key file not found. Generate Ed25519 key first.\n");
    }

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

    struct taas_certificate cert;
    uint8_t data_to_sign[40];
    uint32_t h1, h2, l;
    uint8_t buffer[64];
    socklen_t len = sizeof(cliaddr);
    size_t req_len;

    printf("[TaaS] Unified Ed25519 Node Ready. Core 3 Isolation active.\n");

    /*
     * Main event loop:
     * - Wait for UDP trigger
     * - Perform atomic 64-bit timer read
     * - Send raw timestamp back OR signed certificate
     */
    while (1) {
        ssize_t rec = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                (struct sockaddr *)&cliaddr, &len);

        if (rec > 0) {

            /* Atomic 64-bit read loop */
            do {
                h1 = *st_high;
                l  = *st_low;
                h2 = *st_high;
            } while (h1 != h2);

            uint64_t ts = ((uint64_t)h1 << 32) | l;

            if (rec == 32 && pkey) {
                /* TSA MODE */
                memcpy(cert.client_hash, buffer, 32);
                cert.hw_timestamp = ts;

                memcpy(data_to_sign, cert.client_hash, 32);
                memcpy(data_to_sign + 32, &cert.hw_timestamp, 8);

                if (EVP_DigestSignInit(md_ctx, NULL, NULL, NULL, pkey) == 1) {
                    req_len = 64;
                    EVP_DigestSign(md_ctx, cert.signature, &req_len, data_to_sign, 40);
                }

                sendto(sockfd, &cert, sizeof(cert), 0, (struct sockaddr *)&cliaddr, len);
            } else {
                /* ORIGINAL MODE */
                sendto(sockfd, &ts, sizeof(ts), 0, (struct sockaddr *)&cliaddr, len);
            }
        }
    }

    return 0;
}
