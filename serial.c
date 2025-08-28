// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 SASANO Takayoshi <uaa@uaa.org.uk>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include "serial.h"

static int read_serial(int fd, void *p, int len)
{
	int n, remain;

	for (remain = len; remain > 0; remain -= n) {
		if ((n = read(fd, p, remain)) < 0) return -1;
		p += n;
	}

	return 0;
}

static int write_serial(int fd, void *p, int len)
{
	int n, remain;

	for (remain = len; remain > 0; remain -= n) {
		if ((n = write(fd, p, remain)) < 0) return -1;
		p += n;
	}

	return 0;
}

static int wait_for_ack(int fd)
{
	unsigned char c;

	do {
		read_serial(fd, &c, sizeof(c));
	} while (c != RESP_ACK);

	return 0;
}

int set_maxpos(int fd, int maxpos)
{
	unsigned char c;

	c = CMD_MAXPOS;
	write_serial(fd, &c, sizeof(c));

	c = (maxpos >> 8) - 1;
	write_serial(fd, &c, sizeof(c));

	wait_for_ack(fd);

	return 0;
}

int start_log(int fd)
{
	unsigned char c;

	c = CMD_LOG;
	write_serial(fd, &c, sizeof(c));

	wait_for_ack(fd);

	return 0;
}

int read_log(int fd, struct event *out, int size)
{
	unsigned char c, n;
	int entry;

	c = CMD_RESULT;
	write_serial(fd, &c, sizeof(c));
	
	if (read_serial(fd, &n, sizeof(n)) < 0)
		entry = 0;
	else
		entry = n + 1;

	if (entry)
		read_serial(fd, out, entry * sizeof(struct event));
	
	wait_for_ack(fd);

	return entry;
}

int write_event(int fd, struct event *ev, int entry)
{
	unsigned char c, n;

	c = CMD_EVENT;
	write_serial(fd, &c, sizeof(c));

	n = entry - 1;
	write_serial(fd, &n, sizeof(n));
	write_serial(fd, ev, sizeof(struct event) * entry);

	wait_for_ack(fd);

	return 0;
}

static bool set_nonblock(int d, bool nonblock)
{
	int flags;

	return ((flags = fcntl(d, F_GETFL)) < 0 ||
		fcntl(d, F_SETFL, nonblock ?
		      (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) < 0);
}

int open_serial(char *serdev)
{
	int fd;
	struct termios t;

	if ((fd = open(serdev,
		       O_RDWR | O_NOCTTY | O_EXCL | O_NONBLOCK)) < 0)
		goto fin0;

	memset(&t, 0, sizeof(t));
	cfsetospeed(&t, B38400);
	cfsetispeed(&t, B38400);

	t.c_cflag |= CREAD | CLOCAL | CS8;
	t.c_iflag = INPCK;
	t.c_oflag = 0;
	t.c_lflag = 0;
	t.c_cc[VTIME] = 0;
	t.c_cc[VMIN] = 1;

	tcflush(fd, TCIOFLUSH);
	tcsetattr(fd, TCSANOW, &t);

fin0:
	return fd;
}

int wait_for_device(int fd)
{
	unsigned char c;
	int i;

	/* use read() and write(), not read_serial() and write_serial() */
	for (i = 0; i < 10; i++) {
		c = CMD_READY;
		write(fd, &c, sizeof(c));
		sleep(1);

		if (read(fd, &c, sizeof(c)) >= 1) {
			if (c == RESP_ACK) break;
			else return -1;
		}
		    
		sleep(1);
	}
	if (i >= 10)
		return -1;

	if (set_nonblock(fd, false)) {
		printf("non-block mode set failed\n");
		return -1;
	}

	return 0;
}
