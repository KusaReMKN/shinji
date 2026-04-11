#
/*-
 * SPDX-License-Identifier: BSD 2-Clause License
 *
 * Copyright (c) 2026, KusaReMKN
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <deque>
#include <string>
#include <utility>

#include <TimerTC3.h>

#define PIN_AUX	(D1)

#define TIC	100
#define RDELIM	1000
#define TDELIM	6000

static std::deque<std::pair<uint64_t, std::string>> queue;
static uint64_t lastSend;
static volatile uint64_t sysClock;

void
tcHandler(void)
{
	sysClock += TIC;
}

void
setup(void)
{
	pinMode(PIN_AUX, INPUT);

	while (!Serial)
		delayMicroseconds(TIC);
	Serial.begin(115200);

	while (!Serial1)
		delayMicroseconds(TIC);
	Serial1.begin(9600);

	TimerTc3.initialize(TIC);
	TimerTc3.attachInterrupt(tcHandler);

	while (digitalRead(PIN_AUX) == LOW)
		delayMicroseconds(TIC);
}

void
loop(void)
{
	while (Serial1.available() > 0)
		Serial.write(Serial1.read());

	while (Serial.available() > 0) {
		if (queue.empty() || sysClock - queue.back().first > RDELIM)
			queue.push_back({ sysClock, "" });
		queue.back().second.push_back(Serial.read());
		delayMicroseconds(TIC);
	}

	if (!queue.empty() && sysClock - lastSend > TDELIM
			&& digitalRead(PIN_AUX) == HIGH) {
		Serial1.write(queue[0].second.c_str(), queue[0].second.size());
		queue.pop_front();
		lastSend = sysClock;
	}
}
