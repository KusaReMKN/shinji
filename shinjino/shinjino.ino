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

#include <cstdint>
#include <deque>

#include <TimerTC3.h>

#define PIN_AUX	(D1)

#define LRWAIT	7500
#define PCWAIT	75000
#define TIC	100
#define DT	57

static volatile uint64_t sysClock;

void
tcHandler(void)
{
	sysClock += DT;
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

	TimerTc3.initialize(DT);
	TimerTc3.attachInterrupt(tcHandler);

	while (digitalRead(PIN_AUX) == LOW)
		delayMicroseconds(TIC);
}

void
loop(void)
{
	static std::deque<std::deque<char>> queue;

	static std::deque<char> buf;
	static uint64_t lastClock, lastLoRa;

	/* LoRa から受信した文字は PC へすぐに送る */
	if (Serial1.available() > 0)
		Serial.write(Serial1.read());

	/* PC から受信した文字は一旦バッファする */
	if (Serial.available() > 0) {
		buf.push_back(Serial.read());
		lastClock = sysClock;
	}

	/* PC から受信しなくなって久しければ送信キューへ入れる */
	if (lastClock > 0 && sysClock - lastClock > PCWAIT) {
		queue.push_back(buf);
		buf.clear();
		lastClock = 0;
	}

	/* 送信キューが空でなく、送信可能であれば LoRa へ送る */
	if (!queue.empty() && sysClock - lastLoRa > LRWAIT
			&& digitalRead(PIN_AUX) == HIGH) {
		for (const auto c: queue[0])
			Serial1.write(c);
		queue.pop_front();
		lastLoRa = sysClock;
	}
}
