// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 SASANO Takayoshi <uaa@uaa.org.uk>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
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
static int dit_total = 0, dah_total = 0;
static bool verbose = false;

#define DITDAH_LEN 0x8000
#define DITDAH_POS 0x2000

#define CALIB_LEN 0x2000
#define CALIB_POS 0x0800

#define STEP 4
#define RESULTS 8

//#define DEBUG
#ifdef DEBUG
#define DEBUG_PRINT(x) printf x
#else
#define DEBUG_PRINT(x) /* */
#endif


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

	DEBUG_PRINT(("#"));
	for (i = 0; i < size; i++) DEBUG_PRINT((" %d", out[i]));
	DEBUG_PRINT(("\n"));

	return j;
}

static char detect_element(int v)
{
#define RANGE 10
#define DIFF(x, t) ((((x) - t) * 100) / (t))
	
	if (v < 0) return ' ';
	else if (DIFF(v, dit_total / 2) >= -10 &&
		 DIFF(v, dit_total / 2) <= 10) return '.';
	else return '-';
}

static char *check_squeeze(int fd, unsigned char sig0, int sig0_on_delay, int sig0_off_delay, unsigned char sig1, int sig1_on_delay, int sig1_off_delay, int offset, int width, char *result_str, int results)
{
	int n, t0, t1, m, *u;
	struct event *ev, *ev0;

	set_maxpos(fd, DITDAH_LEN);

	ev = add_event_entry(ev0 = eventbuf, 0, 0, EVT_SET);
	ev = add_event_entry(ev, DITDAH_POS, sig0, EVT_SET);
	ev = add_event_entry(ev, 0, OUT_BIT, EVT_CHGSTS);
	ev = add_event_entry(ev, 1, sig0 | sig1, EVT_SET);

	t0 = offset + width - sig0_off_delay;
	t1 = offset + width - sig1_off_delay;
	if (!sig1 || t0 == t1) {
		// no sig1, or sig0 and sig1 same timing
		m = 0;
	} else if (t0 < t1) {
		// sig0 first
		m = sig1;
	} else {
		// sig1 first
		m = sig0;
		n = t1;
		t1 = t0;
		t0 = n;
	}
	ev = add_event_entry(ev, t0, m, EVT_SET);
	if (m)
		ev = add_event_entry(ev, t1, 0, EVT_SET);

	send_event_and_get_log(fd, ev - ev0);

	ev = &unpacked_log[0];
	n = DITDAH_LEN;
	u = alloca(sizeof(int) * results);
	parse_event(ev, n, u, results);

	for (n = 0; n < results / 2; n++)
		result_str[n] = detect_element(u[n * 2]);
	result_str[n] = '\0';

	return result_str;
}

static void do_squeeze(int fd)
{
#define SQ_RESULTS 10

	int n;
	double i, width, offset, step, total;
	char result_str1[SQ_RESULTS / 2 + 1], result_str2[SQ_RESULTS / 2 + 1];

	offset = dit_total / (STEP * 4);
	width = dit_total / (STEP * 2);
	step = dit_total / STEP;
	total = dit_total + dah_total;

	printf("* squeeze\n");

	memset(result_str1, 0, sizeof(result_str1));
	for (n = 1, i = offset; i < total * 2; n++, i += step) {
		check_squeeze(fd,
			      DIT_BIT, calib_on_1, calib_off_1,
			      DAH_BIT, calib_on_2, calib_off_2,
			      i, width, result_str2, SQ_RESULTS);
		if (!verbose && !strcmp(result_str1, result_str2)) continue;
		strcpy(result_str1, result_str2);
		printf("dit + dah %2d/%2d\t%s\n",
		       n, (int)(total / step), result_str1);
	}

	memset(result_str1, 0, sizeof(result_str1));
	for (n = 1, i = offset; i < total * 2; n++, i += step) {
		check_squeeze(fd,
			      DAH_BIT, calib_on_2, calib_off_2,
			      DIT_BIT, calib_on_1, calib_off_1,
			      i, width, result_str2, SQ_RESULTS);
		if (!verbose && !strcmp(result_str1, result_str2)) continue;
		strcpy(result_str1, result_str2);
		printf("dah + dit %2d/%2d\t%s\n",
		       n, (int)(total / step), result_str1);
	}
}

static char *check_ditdah_memory(int fd, unsigned char sig0, int sig0_on_delay, int sig0_off_delay, unsigned char sig1, int sig1_on_delay, int sig1_off_delay, int offset, int width, char *result_str, int results)
{
	int n, t0, t1, m, *u;
	struct event *ev, *ev0;

	set_maxpos(fd, DITDAH_LEN);

	ev = add_event_entry(ev0 = eventbuf, 0, 0, EVT_SET);
	ev = add_event_entry(ev, DITDAH_POS, sig0, EVT_SET);
	ev = add_event_entry(ev, 0, OUT_BIT, EVT_CHGSTS);
	ev = add_event_entry(ev, 1, sig0, EVT_SET);
	if (sig1) {
		ev = add_event_entry(ev, offset - sig1_on_delay,
				     sig0 | sig1, EVT_SET);
	}

	t0 = offset + width - sig0_off_delay;
	t1 = offset + width - sig1_off_delay;
	if (!sig1 || t0 == t1) {
		// no sig1, or sig0 and sig1 same timing
		m = 0;
	} else if (t0 < t1) {
		// sig0 first
		m = sig1;
	} else {
		// sig1 first
		m = sig0;
		n = t1;
		t1 = t0;
		t0 = n;
	}
	ev = add_event_entry(ev, t0, m, EVT_SET);
	if (m)
		ev = add_event_entry(ev, t1, 0, EVT_SET);

	send_event_and_get_log(fd, ev - ev0);

	ev = &unpacked_log[0];
	n = DITDAH_LEN;
	u = alloca(sizeof(int) * results);
	parse_event(ev, n, u, results);

	for (n = 0; n < results / 2; n++)
		result_str[n] = detect_element(u[n * 2]);
	result_str[n] = '\0';

	return result_str;
}

static void do_simple(int fd)
{
	int n;
	double i, width, offset, step;
	char result_str1[RESULTS / 2 + 1], result_str2[RESULTS / 2 + 1];

	offset = dit_total / (STEP * 4);
	width = dit_total / (STEP * 2);
	step = dit_total / STEP;

	printf("* simple\n");

	memset(result_str1, 0, sizeof(result_str1));
	for (n = 1, i = offset; i < dit_total * 2; n++, i += step) {
		check_ditdah_memory(fd,
				    DIT_BIT, calib_on_1, calib_off_1,
				    0, calib_on_2, calib_off_2,
				    i, width, result_str2, RESULTS);
		if (!verbose && !strcmp(result_str1, result_str2)) continue;
		strcpy(result_str1, result_str2);
		printf("dit 1-%2d/%2d\t%s\n",
		       n, (int)(dit_total / step), result_str1);
	}

	memset(result_str1, 0, sizeof(result_str1));
	for (n = 1, i = offset; i < dah_total * 2; n++, i += step) {
		check_ditdah_memory(fd,
				    DAH_BIT, calib_on_2, calib_off_2,
				    0, calib_on_1, calib_off_1,
				    i, width, result_str2, RESULTS);
		if (!verbose && !strcmp(result_str1, result_str2)) continue;
		strcpy(result_str1, result_str2);
		printf("dah 1-%2d/%2d\t%s\n",
		       n, (int)(dah_total / step), result_str1);
	}
}

static void do_ditdah_memory(int fd)
{
	int n;
	double i, width, offset, step;
	char result_str1[RESULTS / 2 + 1], result_str2[RESULTS / 2 + 1];

	offset = dit_total / (STEP * 4);
	width = dit_total / (STEP * 2);
	step = dit_total / STEP;

	printf("* dit/dah memory\n");

	memset(result_str1, 0, sizeof(result_str1));
	for (n = 1, i = offset; i < dit_total * 2; n++, i += step) {
		check_ditdah_memory(fd,
				    DIT_BIT, calib_on_1, calib_off_1,
				    DAH_BIT, calib_on_2, calib_off_2,
				    i, width, result_str2, RESULTS);
		if (!verbose && !strcmp(result_str1, result_str2)) continue;
		strcpy(result_str1, result_str2);
		printf("dit on, dah %2d/%2d\t%s\n",
		       n, (int)(dit_total / step), result_str1);
	}

	memset(result_str1, 0, sizeof(result_str1));
	for (n = 1, i = offset; i < dah_total * 2; n++, i += step) {
		check_ditdah_memory(fd,
				    DAH_BIT, calib_on_2, calib_off_2,
				    DIT_BIT, calib_on_1, calib_off_1,
				    i, width, result_str2, RESULTS);
		if (!verbose && !strcmp(result_str1, result_str2)) continue;
		strcpy(result_str1, result_str2);
		printf("dah on, dit %2d/%2d\t%s\n",
		       n, (int)(dah_total / step), result_str1);
	}
}

static int get_ditdah_length(int fd, unsigned char mask, int *on_length, int *off_length)
{
	int i, n, u[RESULTS];
	struct event *ev, *ev0;

	set_maxpos(fd, DITDAH_LEN);

	ev = add_event_entry(ev0 = eventbuf, 0, 0, EVT_SET);
	ev = add_event_entry(ev, DITDAH_POS, mask, EVT_SET);
	ev = add_event_entry(ev, 0, OUT_BIT, EVT_CHGSTS);
	ev = add_event_entry(ev, DITDAH_LEN - 1, 0, EVT_SET);
	send_event_and_get_log(fd, ev - ev0);

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
	printf("* dit/dah length\n");

	if (get_ditdah_length(fd, DIT_BIT, &dit_on, &dit_off) < 0) {
		printf("dit too long\n");
		return;
	}

	if (get_ditdah_length(fd, DAH_BIT, &dah_on, &dah_off) < 0) {
		printf("dah too long\n");
		return;
	}

	printf("dit: on=%d, off=%d, on/off=%.3f\n",
	       dit_on, dit_off, (double)dit_on / dit_off);
	printf("dah: on=%d, off=%d, on/off=%.3f\n",
	       dah_on, dah_off, (double)dah_on / dah_off);
	printf("dah/dit: %.3f\n", (double)dah_on / dit_on);

	dit_total = dit_on + dit_off;
	dah_total = dah_on + dah_off;

	printf("dit: total=%d, total/on=%.3f, total/off=%.3f\n",  dit_total,
	       (double)dit_total / dit_on, (double)dit_total / dit_off);
	printf("dah: total=%d, total/on=%.3f, total/off=%.3f\n", dah_total,
	       (double)dah_total / dah_on, (double)dah_total / dah_off);
	printf("dah total/dit total=%.3f\n", (double)dah_total / dit_total);
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
	DEBUG_PRINT(("# relay_1: on=%d, off=%d\n", calib_on_1, calib_off_1));
	DEBUG_PRINT(("# relay_2: on=%d, off=%d\n", calib_on_2, calib_off_2));
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
	printf("a) do all tests\n");
	printf("0) check dit/dah length\n");
	printf("1) check simple timing\n");
	printf("2) check dit/dah memory\n");
	printf("3) check squeeze\n");
	printf("c) calibration\n");
	printf("v) verbose output %s\n", verbose ? "off" : "on");
	printf("x) exit\n");

	printf("-> ");

	fgets(buf, sizeof(buf), stdin);
	switch (*buf) {
	case 'x':
	case 'X':
		return 0;
	case 'v':
	case 'V':
		verbose = !verbose;
		break;
	case 'c':
	case 'C':
		do_calibration(fd);
		disp_config();
		save_config();
		printf(CONFIG_FILE " saved\n");
		break;
	case 'a':
	case 'A':
	case '0':
		do_ditdah_length(fd);
		if (*buf == '0') break;
		sleep(2);
	case '1':
		do_simple(fd);
		if (*buf == '1') break;
		sleep(2);
	case '2':
		do_ditdah_memory(fd);
		if (*buf == '2') break;
		sleep(2);
	case '3':
		do_squeeze(fd);
		if (*buf == '3') break;
		sleep(2);
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
