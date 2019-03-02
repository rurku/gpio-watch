# This repo is obsolete
If you somehow stumbled upon this repo, don't use it.
After I wrote this I learned that I was doing it wrong. The sysfs GPIO interface is deprecated and the new way is using gpio character devices.

There is a library and a set of tools that make interacting with GPIO ports easy: `libgpiod` https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about/

What my tool does can be achieved with `gpiomon` and it will work better.


# gpio-watch

Linux command line program that watches a GPIO port and outputs timestamp and value when the state changes.

The idea is that the output can be piped for further processing.

I've tested this with Orange PI+ board running Armbian Ubuntu 18.04.2 with kernel 4.19.20-sunxi.
It must be run as root (at least in my setup) and the GPIO port must support external interupts.

## Usage
Compile
```
$ make
```
Run

In this example it will watch port 13. See http://linux-sunxi.org/GPIO on how to determine the port number in boards based on Allwinner processors
```
$ sudo bin/gpio-watch -p 13
```

Output example
```
    +---------------------->  Unix timestamp
    |
    |          +----------->  Number of seconds and nanoseconds starting from an unspecified point in time.
    |          |              This is based on system call clock_gettime with CLOCK_MONOTONIC_RAW.
    |          |              See man pages for explanation: http://man7.org/linux/man-pages/man2/clock_gettime.2.html
    |          |
    |          |          +>  New value of the GPIO port
    +          +          +
1551216208 3237.564950459 1
1551216208 3237.565285251 0
1551216208 3237.565700084 1
1551216208 3237.565915876 0
1551216208 3237.566428293 1
1551216208 3237.566919293 0
```

If there is no activity on the port then it will output an empty line every second. This can be used by a consumer program to detect when a transmission has stopped.

## Limitations
* It runs in userspace, so it's at scheduler's mercy. Because of that, the reported time of edge occurrence may not be accurate, especially when the system is under load.
This can make it unsuitable for decoding high frequency signals.


## Learn more
* http://linux-sunxi.org/GPIO
* https://www.kernel.org/doc/Documentation/gpio/sysfs.txt

