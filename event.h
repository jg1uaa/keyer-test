// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 SASANO Takayoshi <uaa@uaa.org.uk>

#ifndef EVENT_H
#define EVENT_H

#include "keyer-test-arduino.h"

void unpack_events(struct event *, int, struct event *, int);
struct event *find_event_entry(struct event *, int *, unsigned char, unsigned char);
struct event *add_event_entry(struct event *, int, int);

#endif
