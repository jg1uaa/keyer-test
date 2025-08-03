// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 SASANO Takayoshi <uaa@uaa.org.uk>

#ifndef SERIAL_H
#define SERIAL_H

#include "keyer-test-arduino.h"
#include "serial.h"

int set_maxpos(int, int);
int start_log(int);
int read_log(int, struct event *, int);
int write_event(int, struct event *, int);
int open_serial(char *);
int wait_for_device(int);

#endif
