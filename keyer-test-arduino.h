// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2025 SASANO Takayoshi <uaa@uaa.org.uk>

#ifndef KEYER_TEST_ARDUINO_H
#define KEYER_TEST_ARDUINO_H

struct event {
	unsigned short pos;
	unsigned char val;
	unsigned char evt;
} __attribute__((packed));

#define MAX_POS 0xf000U
#define MAX_ENTRY 128

#define CMD_READY 0x00
#define CMD_RESET 0x01
#define CMD_EVENT 0x02
#define CMD_LOG 0x03
#define CMD_RESULT 0x04
#define CMD_MAXPOS 0x05

#define EVT_SET 0x00
#define EVT_CHGSTS 0x01

#define RESP_NAK 0x55
#define RESP_ACK 0xaa

#endif
