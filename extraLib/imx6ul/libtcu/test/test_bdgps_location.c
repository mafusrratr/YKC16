// BY ZF
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "libtcu.h"

static double convert_coord(char direction, unsigned int raw)
{
    // BY ZF
    double value = raw / 10000.0;
    if (direction == 'W' || direction == 'S') {
        value = -value;
    }
    return value;
}

static void print_time(unsigned int utc_seconds)
{
    // BY ZF
    time_t t = (time_t)utc_seconds;
    struct tm *tm_utc = gmtime(&t);
    if (tm_utc == NULL) {
        printf("utc_time=%u\n", utc_seconds);
        return;
    }

    printf("utc_time=%u (%04d-%02d-%02d %02d:%02d:%02d UTC)\n",
           utc_seconds,
           tm_utc->tm_year + 1900,
           tm_utc->tm_mon + 1,
           tm_utc->tm_mday,
           tm_utc->tm_hour,
           tm_utc->tm_min,
           tm_utc->tm_sec);
}

static int read_bdgps_data(int fd)
{
    // BY ZF
    char locate_state = 0;
    char we = 0;
    char ns = 0;
    unsigned int longitude = 0;
    unsigned int latitude = 0;
    unsigned int altitude = 0;
    unsigned int utc_seconds = 0;

    int ret = GetBDGPSData(fd,
                           &locate_state,
                           &we,
                           &longitude,
                           &ns,
                           &latitude,
                           &altitude,
                           &utc_seconds);
    if (ret != ERROR_OK) {
        printf("GetBDGPSData failed, ret=%d\n", ret);
        return ret;
    }

    printf("locate_state=%c(%d)\n", locate_state ? locate_state : '-', (int)locate_state);
    printf("longitude_raw=%u, direction=%c, longitude=%.6f\n",
           longitude,
           we ? we : '-',
           convert_coord(we, longitude));
    printf("latitude_raw=%u, direction=%c, latitude=%.6f\n",
           latitude,
           ns ? ns : '-',
           convert_coord(ns, latitude));
    printf("altitude_raw=%u, altitude_m=%.4f\n", altitude, altitude / 10000.0);
    print_time(utc_seconds);

    return ERROR_OK;
}

static void print_fd_target(int fd)
{
    // BY ZF
    char fd_path[64];
    char target[256];
    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);

    ssize_t len = readlink(fd_path, target, sizeof(target) - 1);
    if (len < 0) {
        perror("readlink fd target failed");
        return;
    }

    target[len] = '\0';
    printf("bdgps_fd_target=%s\n", target);
}

int main(int argc, char *argv[])
{
    // BY ZF
    int attempts = 30;
    int interval_seconds = 1;
    if (argc > 1) {
        attempts = atoi(argv[1]);
        if (attempts <= 0) {
            attempts = 30;
        }
    }
    if (argc > 2) {
        interval_seconds = atoi(argv[2]);
        if (interval_seconds <= 0) {
            interval_seconds = 1;
        }
    }

    int fd = -1;
    int ret = InitBDGPS(&fd);
    printf("InitBDGPS ret=%d, fd=%d\n", ret, fd);
    if (ret != ERROR_OK) {
        return EXIT_FAILURE;
    }
    print_fd_target(fd);

    printf("attempts=%d, interval_seconds=%d\n", attempts, interval_seconds);
    for (int i = 1; i <= attempts; ++i) {
        int state = -1;
        ret = GetBDGPSState(fd, &state);
        printf("attempt=%d/%d, GetBDGPSState ret=%d, state=%d", i, attempts, ret, state);
        if (state >= 32 && state <= 126) {
            printf("('%c')", state);
        }
        printf("\n");

        ret = read_bdgps_data(fd);
        if (ret == ERROR_OK) {
            return EXIT_SUCCESS;
        }
        if (i < attempts) {
            sleep(interval_seconds);
        }
    }

    return ret == ERROR_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
