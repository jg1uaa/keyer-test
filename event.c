// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 SASANO Takayoshi <uaa@uaa.org.uk>

#include <stddef.h>
#include "event.h"

void unpack_events(struct event *out, int entry_out, struct event *in, int entry_in)
{
	int i;

	for (i = 0; i < entry_out; i++) {
		if (entry_in && i >= in->pos)
			break;

		out[i].pos = i;
		out[i].val = 0;
		out[i].evt = 0;
	}

	for (; i < entry_out; i++) {
		out[i].pos = i;

		if (entry_in && i == in->pos) {
			out[i].val = in->val;
			out[i].evt = in->evt;
			in++;
			entry_in--;
		} else {
			out[i].val = out[i - 1].val;
			out[i].evt = out[i - 1].evt;
		}			
	}
}

struct event *find_event_entry(struct event *ev, int *entry, unsigned char mask, unsigned char val)
{
	int i, n;

	for (i = 0, n = *entry; i < n; i++) {
		if ((ev->val & mask) == val)
			return ev;
		ev++;
		(*entry)--;
	}

	return NULL;
}

struct event *add_event_entry(struct event *ev, int pos, int val, int evt)
{
	ev->pos = pos;
	ev->val = val;
	ev->evt = evt;

	return ++ev;
}
