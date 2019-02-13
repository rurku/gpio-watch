#include <stdio.h>
#include <poll.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <curl/curl.h>

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
	fputs("21", f);
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
	fputs("21", f);
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

// Max 1 second. If more then returns -1;
long time_diff(struct timespec* from, struct timespec* to)
{
    long secDiff, totalDiff;
    secDiff = (*to).tv_sec - (*from).tv_sec;
    if (secDiff > 1)
        return -1;
    totalDiff = ((*to).tv_sec - (*from).tv_sec) * 1000000000 + (*to).tv_nsec - (*from).tv_nsec;
    if (totalDiff > 1000000000)
        return -1;
    return totalDiff;
}

volatile sig_atomic_t terminated = 0;

void term (int signum)
{
	terminated = 1;
}

struct analysis_context
{
	long last_width;
	int preamble_count;
	long preamble_width;
	long bit_width;
	int bitcount;
};

// void print_temp(struct analysis_context* context)
// {
// 	int whole = 0;
// 	int i;
// 	if (context->bitcount == 36)
// 	{
// 		for (i = 12; i <= 23; i++)
// 		{
// 			whole <<= 1;
// 			whole += context->buffer[i];
// 		}
// 		if (context->buffer[12] == 1)
// 		{
// 			// negative value
// 			whole |= 0xfffff000;
// 		}

// 		printf("%f\n", -50 + (double)whole/10);
// 	}
// }

bool is_within_margin(long a, long b)
{
    // if shorter than 200 then return false because it's too short to reliably measure
    if (a < 1000 || b < 1000)
        return false;
	return abs(a - b) / (double)a < 0.1;
}

bool analyze(struct analysis_context* context, char value, long width, char* buffer, int buffer_size)
{
    bool ret = false;

	if (context->preamble_count < 8)
	{
		if (context->preamble_count % 2 == value - '0' && (context->preamble_count == 0 || is_within_margin(context->last_width, width)))
		{
			context->preamble_count ++;
			context->preamble_width += width;
			if (context->preamble_count == 8)
			{
				context->bit_width = context->preamble_width / 8;
                context->bitcount = 0;
			}
		}
		else if (value == '0')
		{
			context->preamble_count = 1;
			context->preamble_width = width;
		}
		else
		{
			context->preamble_count = 0;
			context->preamble_width = 0;
		}
	}
	else if (value == '1') // if it's after preamble then we analyze bits at the rising edge
	{
		if (is_within_margin(context->bit_width, context->last_width + width))
		{
			if (context->bitcount < buffer_size)
			{
				buffer[context->bitcount] = context->last_width > width ? 1 : 0;
				context->bitcount++;
			}
            else
            {
                // we've filled the buffer and there's more being transmitted. Discard and reset for next transmission.
                context->preamble_count = 0;
                context->preamble_width = 0;
            }
		}
		else
		{
            // Transmission ended. If we captured at least 1 bit then indicate that it's ready
			context->preamble_count = 0;
			context->preamble_width = 0;
            if (context->bitcount > 0)
                ret = true;
		}
	}

	context->last_width = width;

    return ret;
}

// if return value = true then *temp is temperature in tenth's of degrees. e.g. 21.5 degrees = 215.
bool await_transmission(int gpio_port, char* buffer, int buffer_size, int* payload_size)
{
    int f;
	char value;
	struct timespec timestamp;
	struct timespec old_timestamp;
	bool started = false;
	long diff;
	int last_wait_ret = 0;
	struct analysis_context analysis_context;
    bool ret = false;
    int i;

	memset(&analysis_context, 0, sizeof(struct analysis_context));

    export(gpio_port);
	set_direction(gpio_port, "in");
	set_edge(gpio_port, "both");
	f = open_port(gpio_port);
	read(f, &value, 1);
	while (terminated == 0 && (last_wait_ret = wait_for_edge(f, 100, &value, &timestamp)) >= 0)
	{
		if (last_wait_ret == 0)
        {
            memset(&analysis_context, 0, sizeof(struct analysis_context));
            continue;
        }

		if (started)
		{
			diff = time_diff(&old_timestamp, &timestamp);
            
			if (analyze(&analysis_context, value, diff, buffer, buffer_size))
            {
                ret = true;
                *payload_size = analysis_context.bitcount;
                break;
                // we have a completed transmission.
                // if (analysis_context.bitcount == 36)
                // {
                //     if (channel == 2*analysis_context.buffer[10] + analysis_context.buffer[11])
                //     {
                //         *temp = 0;
                //         for (i = 12; i <= 23; i++)
                //         {
                //             *temp <<= 1;
                //             *temp += analysis_context.buffer[i];
                //         }
                //         if (analysis_context.buffer[12] == 1)
                //         {
                //             // negative value
                //             *temp |= 0xfffff000;
                //         }
                //         *temp -= 500; // offset from -50 degrees celcius
                //         ret = true;
                //         break;
                //     }
                // }

            }
		}
		old_timestamp = timestamp;
		started = true;
	}
	
	if (last_wait_ret < 0)
		fprintf(stderr, "poll returned negative value %d\n", last_wait_ret);

	close(f);
	unexport(gpio_port);

    return ret;
}

int main(int argc, char** argv)
{
	struct sigaction action;
    char buffer[100];
    int payload_size;
    int port;
    int i;

    if (argc < 2 || sscanf(argv[1], "%d", &port) != 1)
    {
        fprintf(stderr, "Usage: readport portnumber\n");
        exit(-1);
    }

	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = term;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);

    while (terminated == 0)
    {
        if (await_transmission(port, buffer, sizeof(buffer), &payload_size))
        {
            printf("%ld ", time(NULL));
            for (i = 0; i < payload_size; i++)
                putchar(buffer[i] + '0');
            putchar('\n');
            //sleep(1);
        }
    }
}

