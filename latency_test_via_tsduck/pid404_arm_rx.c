// pid404_arm_measure.c
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <pigpio.h>

#define TS_PACKET_SIZE 188
#define PID_TO_WATCH   0x0404
#define GPIO_OUT       17

static volatile int keep_running = 1;

// Shared state between threads
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond = PTHREAD_COND_INITIALIZER;

static int measurement_active = 0;   // 1 = armed, waiting for PID
static int hit_ready = 0;            // 1 = reader thread has recorded hit_time
static unsigned long measurement_index = 0;

static struct timespec arm_time;     // when we set GPIO high
static struct timespec hit_time;     // when PID 0x0404 was seen

static inline uint16_t get_pid(const uint8_t *pkt)
{
    return ((pkt[1] & 0x1F) << 8) | pkt[2];
}

static void intHandler(int sig)
{
    (void)sig;
    keep_running = 0;

    // Wake up main thread if it's waiting
    pthread_mutex_lock(&lock);
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

// Compute hit_time - arm_time in seconds as double
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

void *reader_thread_func(void *arg)
{
    (void)arg;
    uint8_t buf[TS_PACKET_SIZE];

    while (keep_running) {
        ssize_t r = read(STDIN_FILENO, buf, TS_PACKET_SIZE);
        if (r < 0) {
            perror("read");
            break;
        }
        if (r == 0) {
            // EOF
            fprintf(stderr, "End of input on stdin\n");
            break;
        }
        if (r != TS_PACKET_SIZE) {
            // Unexpected partial read; just skip
            continue;
        }

        if (buf[0] != 0x47) {
            // Not a TS sync byte; skip
            continue;
        }

        uint16_t pid = get_pid(buf);

        pthread_mutex_lock(&lock);
        if (keep_running &&
            measurement_active &&
            !hit_ready &&
            pid == PID_TO_WATCH)
        {
            // Record detection time
            clock_gettime(CLOCK_MONOTONIC_RAW, &hit_time);
            hit_ready = 1;
            measurement_active = 0;
            pthread_cond_signal(&cond);
        }
        pthread_mutex_unlock(&lock);
    }

    // If we exit due to EOF/error, notify main so it doesn't wait forever
    pthread_mutex_lock(&lock);
    keep_running = 0;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);

    return NULL;
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

    pthread_t reader_thread;
    if (pthread_create(&reader_thread, NULL, reader_thread_func, NULL) != 0) {
        perror("pthread_create");
        gpioTerminate();
        return 1;
    }

    printf("Starting arm/measure loop for PID 0x%04X on GPIO %d\n",
           PID_TO_WATCH, GPIO_OUT);
    printf("Press Ctrl+C to stop.\n");

    while (keep_running) {
        // ARM: prepare for a new measurement
        pthread_mutex_lock(&lock);
        hit_ready = 0;
        measurement_active = 1;
        measurement_index++;

        // Start time and GPIO high
        clock_gettime(CLOCK_MONOTONIC_RAW, &arm_time);
        gpioWrite(GPIO_OUT, PI_ON);
        pthread_mutex_unlock(&lock);

        // Wait until reader thread signals a hit or we stop
        pthread_mutex_lock(&lock);
        while (keep_running && !hit_ready) {
            pthread_cond_wait(&cond, &lock);
        }

        // If we were interrupted or input ended, break
        if (!keep_running || !hit_ready) {
            pthread_mutex_unlock(&lock);
            break;
        }

        struct timespec arm_copy = arm_time;
        struct timespec hit_copy = hit_time;
        pthread_mutex_unlock(&lock);

        // End of measurement: GPIO low as soon as possible
        gpioWrite(GPIO_OUT, PI_OFF);

        // Compute and print delta
        double delta = timespec_diff_sec(&arm_copy, &hit_copy);

        printf("Measurement %lu: PID 0x%04X detected\n",
               measurement_index, PID_TO_WATCH);
        printf("  arm_time: %ld.%09ld (MONOTONIC_RAW)\n",
               arm_copy.tv_sec, arm_copy.tv_nsec);
        printf("  hit_time: %ld.%09ld (MONOTONIC_RAW)\n",
               hit_copy.tv_sec, hit_copy.tv_nsec);
        printf("  latency : %.9f s\n", delta);

        // Optional: short pause between measurements or immediate re-arm.
        // usleep(100000); // 100 ms, if you want a gap
    }

    pthread_join(reader_thread, NULL);
    gpioWrite(GPIO_OUT, PI_OFF);
    gpioTerminate();

    return 0;
}
