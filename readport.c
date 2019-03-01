#include <stdio.h>
#include <poll.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>


// resources
// http://linux-sunxi.org/GPIO
// https://www.kernel.org/doc/Documentation/gpio/sysfs.txt
// https://stackoverflow.com/questions/27411013/poll-returns-both-pollpri-pollerr

void export(int port)
{
    FILE* f;
    char buf[10];
    f = fopen("/sys/class/gpio/export", "w");
    if (f == NULL)
    {
        fprintf(stderr, "Failed to open export file\n");
        exit(1);
    }
    snprintf(buf, 10, "%d", port);
    fputs(buf, f);
    fclose(f);
}

void unexport(int port)
{
    FILE* f;
    char buf[10];
    f = fopen("/sys/class/gpio/unexport", "w");
    if (f == NULL)
    {
        fprintf(stderr, "Failed to open unexport file\n");
        exit(1);
    }
    snprintf(buf, 10, "%d", port);
    fputs(buf, f);
    fclose(f);
}

void set_direction(int port, char* direction)
{
    char buf[100];
    FILE* f;
    snprintf(buf, 100, "/sys/class/gpio/gpio%d/direction", port);
    f = fopen(buf, "w");
    if (f == NULL)
    {
        fprintf(stderr, "Failed to open direction file");
        exit(1);
    }
    fputs(direction, f);
    fclose(f);
}

void set_edge(int port, char* edge)
{
    char buf[100];
    FILE* f;
    snprintf(buf, 100, "/sys/class/gpio/gpio%d/edge", port);
    f = fopen(buf, "w");
    if (f == NULL)
    {
        fprintf(stderr, "Failed to open edge file\n");
        exit(1);
    }
    fputs(edge, f);
    fclose(f);
}

int wait_for_edge(int file, int timeout_ms, char* value, struct timespec* timestamp)
{
    struct pollfd fd;
    int ret;
    fd.fd = file;
    fd.events = POLLPRI;
    ret = poll(&fd, 1, timeout_ms);
    if (ret > 0)
    {
        clock_gettime(CLOCK_MONOTONIC_RAW, timestamp);
        lseek(file, 0, SEEK_SET);
        read(file, value, 1);
    }
    return ret;
}

int open_port(int port)
{
    int ret;
    char buf[100];
    snprintf(buf, 100, "/sys/class/gpio/gpio%d/value", port);
    ret = open(buf, O_RDONLY);
    if (ret == -1)
    {
        fprintf(stderr, "Failed to open value file\n");
        exit(1);
    }
    return ret;
}

volatile sig_atomic_t terminated = 0;

void term (int signum)
{
    terminated = 1;
}

void write_edges(int gpio_port, FILE* output_stream)
{
    int f;
    char value;
    struct timespec timestamp, start_timestamp;
    int wait_result;

    clock_gettime(CLOCK_MONOTONIC_RAW, &start_timestamp);

    export(gpio_port);
    set_direction(gpio_port, "in");
    set_edge(gpio_port, "both");
    f = open_port(gpio_port);
    read(f, &value, 1);

    while (terminated == 0 && (wait_result = wait_for_edge(f, 1000, &value, &timestamp)) >= 0)
    {
        if (wait_result > 0)
        {
            fprintf(output_stream, "%ld %ld.%09ld %c\n", time(NULL), timestamp.tv_sec, timestamp.tv_nsec, value);
            fflush(output_stream);
        }
        else
        {
            fputs(output_stream, "\n");
        }
    }

    close(f);
    unexport(gpio_port);
}

void print_usage(char *program_name)
{
    fprintf(stderr, "Usage: %s -p port [-o output_file]\n", program_name);

}

int main(int argc, char** argv)
{
    struct sigaction action;
    int port = 0, opt = 0;
    char *output_file = NULL;
    FILE *f = NULL, *output_stream;

    while ((opt = getopt(argc, argv, "o:p:")) != -1)
    {
        switch (opt)
        {
            case 'o':
                output_file = optarg;
                break;
            case 'p':
                if (sscanf(optarg, "%d", &port) != 1)
                {
                    fprintf(stderr, "Invalid port number\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case '?':
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
                break;
        }
    }

    if (port == 0)
    {
        fprintf(stderr, "Port number is required\n");
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }


    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = term;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    if (output_file != NULL)
    {
        if ((f = fopen(output_file, "w")) == NULL)
        {
            fprintf(stderr, "Failed to open output file\n");
            exit(EXIT_FAILURE);
        }
        output_stream = f;
    }
    else
    {
        output_stream = stdout;
    }

    write_edges(port, output_stream);

    if (f != NULL)
    {
        fclose(f);
    }
}

