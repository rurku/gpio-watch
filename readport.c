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

// resources
// http://linux-sunxi.org/GPIO
// https://www.kernel.org/doc/Documentation/gpio/sysfs.txt
// https://stackoverflow.com/questions/27411013/poll-returns-both-pollpri-pollerr

void export(int port)
{
	FILE* f;
	char buf[10];
	f = fopen("/sys/class/gpio/export", "w");
	snprintf(buf, 10, "%d", port);
	fputs("21", f);
	fclose(f);
}

void unexport(int port)
{
	FILE* f;
	char buf[10];
	f = fopen("/sys/class/gpio/unexport", "w");
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
	fputs(direction, f);
	fclose(f);
}

void set_edge(int port, char* edge)
{
	char buf[100];
	FILE* f;
	snprintf(buf, 100, "/sys/class/gpio/gpio%d/edge", port);
	f = fopen(buf, "w");
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
	char buf[100];
	snprintf(buf, 100, "/sys/class/gpio/gpio%d/value", port);
	return open(buf, O_RDONLY);
}

long time_diff(struct timespec* from, struct timespec* to)
{
	return ((*to).tv_sec - (*from).tv_sec) * 1000000000 + (*to).tv_nsec - (*from).tv_nsec;
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
	char buffer[40];
};

void print_temp(struct analysis_context* context)
{
	int whole = 0;
	int i;
	if (context->bitcount == 36)
	{
		for (i = 12; i <= 23; i++)
		{
			whole <<= 1;
			whole += context->buffer[i];
		}
		if (context->buffer[12] == 1)
		{
			// negative value
			whole |= 0xfffff000;
		}

		printf("%f\n", -50 + (double)whole/10);
	}
}

bool is_within_margin(long a, long b)
{
	return abs(a - b) / (double)a < 0.1;
}

void analyze(struct analysis_context* context, char value, long width)
{
	if (context->preamble_count < 8)
	{
		if (context->preamble_count % 2 == value - '0' && (context->preamble_count == 0 || is_within_margin(context->last_width, width)))
		{
			context->preamble_count ++;
			context->preamble_width += width;
			if (context->preamble_count == 8)
			{
				context->bit_width = context->preamble_width / 8;
				printf("%ld ", time(NULL));
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
		//printf("%c%d %ld\n", value == '0' ? 'H' : 'L', context->preamble_count, width);
	}
	else if (value == '1') // if it's after preamble then we analyze bits at the rising edge
	{
		//printf("bitwidth: %ld, last: %ld, width: %ld\n", context->bit_width, context->last_width, width);
		if (is_within_margin(context->bit_width, context->last_width + width))
		{
			putchar(context->last_width > width ? '1' : '0');
			if (context->bitcount < 40)
			{
				context->buffer[context->bitcount] = context->last_width > width ? 1 : 0;
				context->bitcount++;
//				if (context->bitcount == 10 || context->bitcount == 12 || context->bitcount == 18)
//					putchar(' ');
			}
		}
		else
		{
			context->preamble_count = 0;
			context->preamble_width = 0;
			putchar('\n');
			print_temp(context);
			context->bitcount = 0;
		}
	}

	context->last_width = width;
}



int main(void)
{
	int f;
	char value;
	struct timespec timestamp;
	struct timespec old_timestamp;
	bool started = false;
	long diff;
	int last_wait_ret = 0;
	struct analysis_context analysis_context;

	struct sigaction action;

	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = term;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);

	memset(&analysis_context, 0, sizeof(struct analysis_context));

        export(21);
	set_direction(21, "in");
	set_edge(21, "both");
	f = open_port(21);
	read(f, &value, 1);
	while (terminated == 0 && (last_wait_ret = wait_for_edge(f, 100, &value, &timestamp)) >= 0)
	{
		if (last_wait_ret == 0) continue;

		if (started)
		{
			diff = time_diff(&old_timestamp, &timestamp);
			analyze(&analysis_context, value, diff);
                        //printf("%c %ld sec:%ld nsec:%ld\n", value, diff, timestamp.tv_sec, timestamp.tv_nsec);
		}
		old_timestamp = timestamp;
		started = true;
	}
	
	if (last_wait_ret < 0)
		fprintf(stderr, "poll returned negativ value %d\n", last_wait_ret);

	close(f);
	unexport(21);
	printf("cleaned up");
}

