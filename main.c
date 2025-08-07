// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 SASANO Takayoshi <uaa@uaa.org.uk>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <curses.h>
#include "serial.h"
#include "event.h"
#include "keyer-test-arduino.h"

#define CONFIG_FILE "keyer-test.cfg"
#define DIT_BIT 0x01
#define DAH_BIT 0x02
#define OUT_BIT 0x01

static struct event unpacked_log[MAX_POS];
static struct event packed_log[MAX_ENTRY];
static int packed_log_entry;
static struct event eventbuf[MAX_ENTRY];
static int calib_on_1 = 0, calib_off_1 = 0 , calib_on_2 = 0, calib_off_2 = 0;
static int dit_on = 0, dit_off = 0, dah_on = 0, dah_off = 0;

#define DITDAH_LEN 0x8000
#define DITDAH_POS 0x0800

#define CALIB_LEN 0x2000
#define CALIB_POS 0x0800

#define RESULTS 8


static void send_event_and_get_log(int fd, int event_entry)
{
	write_event(fd, eventbuf, event_entry);
	start_log(fd);
	packed_log_entry = read_log(fd, packed_log, MAX_POS);
	unpack_events(unpacked_log, MAX_POS, packed_log, packed_log_entry);
}


static int parse_event(struct event *ev, int entry, int *out, int size)
{
	int i, j, n, t0, t1;

	/*
	 *    t[0]     t[1]     t[2]     t[3]     t[4]     t[5]     t[6]
	 *     :        :        :        :        :        :        :
	 *     +--------+        +--------+        +--------+        +---- 
	 *     |        |        |        |        |        |        |
	 * ----+        +--------+        +--------+        +--------+
	 *     :        :        :        :        :        :        :
	 *     |<-u[0]->|<-u[1]->|<-u[2]->|<-u[3]->|<-u[4]->|<-u[5]->|
	 */

	n = entry;
	for (i = j = 0; i < size + 1; i++) {
		if ((ev = find_event_entry(ev, &n, OUT_BIT,
					   (i & 1) ? 0 : OUT_BIT)) == NULL)
			break;

		t1 = ev->pos;
		if (i) out[j++] = t1 - t0;
		t0 = t1;

		ev++;
		n--;
	}
	for (i = j; i < size; i++) out[i] = -1;

	printf("#");
	for (i = 0; i < size; i++) printf(" %d", out[i]);
	printf("\n");

	return j;
}

static int get_ditdah_length(int fd, unsigned char mask, int *on_length, int *off_length)
{
	int i, n, u[RESULTS];
	struct event *ev;

	set_maxpos(fd, DITDAH_LEN);

	ev = add_event_entry(eventbuf, 0, 0, EVT_SET);
	ev = add_event_entry(ev, DITDAH_POS, mask, EVT_SET);
	ev = add_event_entry(ev, 0, OUT_BIT, EVT_CHGSTS);
	ev = add_event_entry(ev, DITDAH_LEN - 1, 0, EVT_SET);
	send_event_and_get_log(fd, 4);

	ev = &unpacked_log[0];
	n = DITDAH_LEN;
	if (parse_event(ev, n, u, RESULTS) < RESULTS)
		return -1;

	*on_length = *off_length = 0;
	for (i = 0; i < RESULTS; i += 2) {
		*on_length += u[i];
		*off_length += u[i + 1];
	}
	*on_length /= (RESULTS / 2);
	*off_length /= (RESULTS / 2);

	return 0;
}

static void do_ditdah_length(int fd)
{
	int dit_total, dah_total;


	if (get_ditdah_length(fd, DIT_BIT, &dit_on, &dit_off) < 0) {
		printf("dit too long\n");
		return;
	}

	if (get_ditdah_length(fd, DAH_BIT, &dah_on, &dah_off) < 0) {
		printf("dah too long\n");
		return;
	}

	printf("* dit: on=%d, off=%d, on/off=%.3f\n",
	       dit_on, dit_off, (double)dit_on / dit_off);
	printf("* dah: on=%d, off=%d, on/off=%.3f\n",
	       dah_on, dah_off, (double)dah_on / dah_off);
	printf("* dah/dit: %.3f\n", (double)dah_on / dit_on);

	dit_total = dit_on + dit_off;
	dah_total = dah_on + dah_off;

	printf("* dit: total=%d, total/on=%.3f, total/off=%.3f\n",  dit_total,
	       (double)dit_total / dit_on, (double)dit_total / dit_off);
	printf("* dah: total=%d, total/on=%.3f, total/off=%.3f\n", dah_total,
	       (double)dah_total / dah_on, (double)dah_total / dah_off);
	printf("* dah total/dit total=%.3f\n", (double)dah_total / dit_total);
}

static int get_calibration_value(int fd, unsigned char mask, bool state)
{
	int n;
	struct event *ev;
	
	set_maxpos(fd, CALIB_LEN);

	ev = add_event_entry(eventbuf, 0, state ? 0 : mask, EVT_SET);
	ev = add_event_entry(ev, CALIB_POS, state ? mask : 0, EVT_SET);
	send_event_and_get_log(fd, 2);

	ev = &unpacked_log[CALIB_POS];
	n = CALIB_LEN - CALIB_POS;
	if ((ev = find_event_entry(ev, &n, mask, state ? mask : 0)) != NULL)
		return ev->pos - CALIB_POS;

	return -1;
}

static int load_config(void)
{
	FILE *fp;

	if ((fp = fopen(CONFIG_FILE, "r")) == NULL) {
		printf(CONFIG_FILE " not found\n");
		return -1;
	}

	fscanf(fp, "%d %d %d %d",
	       &calib_on_1, &calib_off_1, &calib_on_2, &calib_off_2);
	fclose(fp);

	return 0;
}

static int save_config(void)
{
	FILE *fp;

	if ((fp = fopen(CONFIG_FILE, "w")) == NULL) {
		printf("cannot write " CONFIG_FILE "\n");
		return -1;
	}

	fprintf(fp, "%d %d %d %d\n",
		calib_on_1, calib_off_1, calib_on_2, calib_off_2);
	fclose(fp);

	return 0;
}

static void disp_config(void)
{
	printf("# relay_1: on=%d, off=%d\n", calib_on_1, calib_off_1);
	printf("# relay_2: on=%d, off=%d\n", calib_on_2, calib_off_2);
}

static void do_calibration(int fd)
{
#define CALIBRATION_TRY 4

	int i;

	calib_on_1 = calib_off_1 = calib_on_2 = calib_off_2 = 0;

	for (i = 0; i < CALIBRATION_TRY; i++) {
		calib_on_1 += get_calibration_value(fd, DIT_BIT, true);
		calib_off_1 += get_calibration_value(fd, DIT_BIT, false);
		calib_on_2 += get_calibration_value(fd, DAH_BIT, true);
		calib_off_2 += get_calibration_value(fd, DAH_BIT, false);
	}

	calib_on_1 /= CALIBRATION_TRY;
	calib_off_1 /= CALIBRATION_TRY;
	calib_on_2 /= CALIBRATION_TRY;
	calib_off_2 /= CALIBRATION_TRY;
}

static int do_main(int fd)
{
	char buf[256];

	load_config();
	disp_config();

menu:
	printf("\n");
	printf("menu:\n");
	printf("0) check dit/dah length\n");
	printf("c) calibration\n");
	printf("x) exit\n");

	printf("-> ");

	fgets(buf, sizeof(buf), stdin);
	switch (*buf) {
	case 'x':
	case 'X':
		return 0;
	case 'c':
	case 'C':
		do_calibration(fd);
		disp_config();
		save_config();
		printf(CONFIG_FILE " saved\n");
		break;
	case '0':
		do_ditdah_length(fd);
		break;
	default:
		break;
	}
	
	goto menu;
}

int	main(int argc, char *argv[])
{
	int	fd;

	if (argc < 2) {
		printf("%s [device]\n", argv[0]);
		goto fin0;
	}

	fd = open_serial(argv[1]);
	if (fd < 0) {
		printf("device open error\n");
		goto fin0;
	}

	printf("wait for device...\n");

	if (wait_for_device(fd)) {
		printf("device not ready\n");
		goto fin1;
	}

	printf("device ready\n");
	do_main(fd);

fin1:
	close(fd);
fin0:
	return 0;
}
