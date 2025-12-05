#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pigpio.h>
#include <sys/select.h>

#define TS_PACKET_SIZE 188
#define PID_TO_WATCH   0x0404
#define GPIO_OUT       17
#define TIMEOUT_SEC    20.0   // timeout per measurement

static volatile int keep_running = 1;

static inline uint16_t get_pid(const uint8_t *pkt)
{
    return ((pkt[1] & 0x1F) << 8) | pkt[2];
}

void intHandler(int dummy)
{
    (void)dummy;
    keep_running = 0;
}

// sleep for given seconds using CLOCK_REALTIME
static void wait_seconds(double sec)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)sec;
    ts.tv_nsec = (long)((sec - (double)ts.tv_sec) * 1e9);
    if (ts.tv_nsec < 0) {
        ts.tv_nsec = 0;
    }
    clock_nanosleep(CLOCK_REALTIME, 0, &ts, NULL);
}

// compute end - start in seconds as double
static double timespec_diff_sec(const struct timespec *start,
                                const struct timespec *end)
{
    long sec  = end->tv_sec  - start->tv_sec;
    long nsec = end->tv_nsec - start->tv_nsec;
    if (nsec < 0) {
        nsec += 1000000000L;
        sec  -= 1;
    }
    return (double)sec + (double)nsec / 1e9;
}

int main(void)
{
    if (gpioInitialise() == PI_INIT_FAILED) {
        fprintf(stderr, "GPIO init failed\n");
        return 1;
    }

    gpioSetMode(GPIO_OUT, PI_OUTPUT);
    gpioWrite(GPIO_OUT, PI_OFF);

    signal(SIGINT, intHandler);

    // Stream buffer for stdin
    uint8_t buffer[64 * TS_PACKET_SIZE];
    size_t buf_len = 0;

    unsigned long measurement_index = 0;

    printf("Starting single-thread arm/measure with %.1f s timeout "
           "for PID 0x%04X on GPIO %d\n",
           TIMEOUT_SEC, PID_TO_WATCH, GPIO_OUT);
    printf("Press Ctrl+C to stop.\n");
    printf("Wait 10sec to sync DVB-T2...\n");
    wait_seconds(10.0);

    while (keep_running) {
        // =========================
        // ARM NEW MEASUREMENT
        // =========================
        measurement_index++;

        struct timespec arm_time, now, hit_time;
        int hit_found = 0;
        int timed_out = 0;

        clock_gettime(CLOCK_MONOTONIC_RAW, &arm_time);
        gpioWrite(GPIO_OUT, PI_ON);   // rising edge: start of measurement

        // =========================
        // WAIT FOR PID 0x0404 OR TIMEOUT
        // =========================
        while (keep_running && !hit_found && !timed_out) {
            // compute remaining timeout based on arm_time
            clock_gettime(CLOCK_MONOTONIC_RAW, &now);
            double elapsed = timespec_diff_sec(&arm_time, &now);
            double remaining = TIMEOUT_SEC - elapsed;
            if (remaining <= 0.0) {
                timed_out = 1;
                break;
            }

            // convert remaining to struct timeval for select()
            struct timeval tv;
            tv.tv_sec = (time_t)remaining;
            tv.tv_usec = (suseconds_t)((remaining - (double)tv.tv_sec) * 1e6);
            if (tv.tv_usec < 0) tv.tv_usec = 0;

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(STDIN_FILENO, &rfds);

            int ret = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
            if (ret < 0) {
                perror("select");
                keep_running = 0;
                break;
            }
            if (ret == 0) {
                // timeout in select: no data became ready in "remaining"
                timed_out = 1;
                break;
            }

            // stdin is readable; read as much as we can into buffer
            if (buf_len == sizeof(buffer)) {
                // buffer full: drop oldest packet-sized chunk
                memmove(buffer, buffer + TS_PACKET_SIZE, buf_len - TS_PACKET_SIZE);
                buf_len -= TS_PACKET_SIZE;
            }

            ssize_t r = read(STDIN_FILENO, buffer + buf_len,
                             sizeof(buffer) - buf_len);
            if (r < 0) {
                perror("read");
                keep_running = 0;
                break;
            }
            if (r == 0) {
                fprintf(stderr, "EOF on stdin\n");
                keep_running = 0;
                break;
            }

            buf_len += (size_t)r;

            // Process any complete packets in buffer
            size_t i = 0;
            while (buf_len - i >= TS_PACKET_SIZE && !hit_found) {
                // sync byte search
                if (buffer[i] != 0x47) {
                    i++;
                    continue;
                }

                const uint8_t *pkt = buffer + i;
                uint16_t pid = get_pid(pkt);

                if (pid == PID_TO_WATCH) {
                    clock_gettime(CLOCK_MONOTONIC_RAW, &hit_time);
                    hit_found = 1;
                    break;
                }

                i += TS_PACKET_SIZE;
            }

            // remove consumed bytes up to i
            if (i > 0) {
                if (i < buf_len) {
                    memmove(buffer, buffer + i, buf_len - i);
                    buf_len -= i;
                } else {
                    buf_len = 0;
                }
            }
        }

        // Measurement window ended: lower GPIO as soon as possible
        gpioWrite(GPIO_OUT, PI_OFF);

        if (!keep_running) {
            break;
        }

        // =========================
        // REPORT RESULT
        // =========================
        if (hit_found) {
            double latency = timespec_diff_sec(&arm_time, &hit_time);
            printf("Measurement %lu: PID 0x%04X detected\n",
                   measurement_index, PID_TO_WATCH);
            printf("  arm_time: %ld.%09ld (MONOTONIC_RAW)\n",
                   arm_time.tv_sec, arm_time.tv_nsec);
            printf("  hit_time: %ld.%09ld (MONOTONIC_RAW)\n",
                   hit_time.tv_sec, hit_time.tv_nsec);
            printf("  latency : %.9f s\n", latency);
        } else if (timed_out) {
            printf("Measurement %lu: TIMEOUT after %.1f s (no PID 0x%04X)\n",
                   measurement_index, TIMEOUT_SEC, PID_TO_WATCH);
        }

        // Loop re-arms immediately (no extra sleep) for next measurement
        // If you want a pause between windows, you can add a sleep here.
        wait_seconds(1.0);
    }

    gpioWrite(GPIO_OUT, PI_OFF);
    gpioTerminate();
    return 0;
}
