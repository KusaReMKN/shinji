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

#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/if_tun.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define ALIGN16(x)	((x + (16-1)) & ~(16-1))

#define IFNAME	"lora%d"
#define TUNPATH	"/dev/net/tun"
#define LORAMTU	195

static void init_interface(const char *ifname, int mtu, const char *cidr);
static void init_lora(int fd);
static void receive_packet(int lorafd, int tunfd);
static void transmit_packet(int tunfd, int lorafd);
static void usage(void);

static int tun_alloc(char *ifname);

int
main(int argc, char *argv[])
{
	fd_set rfds;
	int lorafd, nfds, tunfd;
	char ifname[IFNAMSIZ] = IFNAME;

	if (argc != 3)
		usage();

	/* LoRa デバイスの準備をする */
	lorafd = open(argv[1], O_RDWR | O_NOCTTY);
	if (lorafd == -1)
		err(EXIT_FAILURE, "%s", argv[1]);
	init_lora(lorafd);

	/* ネットワークインタフェイスを準備する */
	tunfd = tun_alloc(ifname);
	if (tunfd == -1)
		err(EXIT_FAILURE, "tun_alloc");
	init_interface(ifname, LORAMTU, argv[2]);

	for (;;) {
		/* LoRa と TUN とを同時に監視する */
		FD_ZERO(&rfds);
		FD_SET(lorafd, &rfds);
		FD_SET(tunfd, &rfds);
		nfds = MAX(lorafd, tunfd) + 1;
		if (select(nfds, &rfds, NULL, NULL, NULL) == -1)
			err(EXIT_FAILURE, "select");

		/* 云いたい相手に応じて相手をする */
		if (FD_ISSET(lorafd, &rfds)) {
			receive_packet(lorafd, tunfd);
		} else {
			transmit_packet(tunfd, lorafd);
		}
	}
	/* NOTREACHED */

	return EXIT_FAILURE;
}

/**
 * ネットワークインタフェイスを初期化を代行する。
 * そのインタフェイスの MTU を設定し、IPv4 アドレスを設定し、
 * ネットマスクを設定し、ブロードキャストアドレスを設定し、そして UP する。
 * 途中で失敗した場合は err(3) する。
 *
 * ifname: インタフェイス名
 * mtu: そのインタフェイスの MTU
 * cidr: そのインタフェイスに割り当てる IPv4 アドレスの CIDR 表記
 */
static void
init_interface(const char *ifname, int mtu, const char *cidr)
{
	struct sockaddr_in sin;
	struct ifreq ifr;
	uint32_t mask;
	int masklen, sock;
	char *slash, tail;
	char addr[] = "XXX.XXX.XXX.XXX";

	/* 作業用ソケットを開く */
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
		err(EXIT_FAILURE, "socket");

	/* MTU を設定する */
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	ifr.ifr_mtu = mtu;
	if (ioctl(sock, SIOCSIFMTU, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCSIFMTU");

	/* CIDR 表記を IPv4 アドレスとネットマスクとに分解する */
	if (sscanf(cidr, "%15[^/]/%d%c", addr, &masklen, &tail) != 2
			|| masklen < 0 || masklen > 32)
		errx(EXIT_FAILURE, "%s: %s", cidr, strerror(EINVAL));

	/* アドレスを設定する */
	(void)memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	if (inet_aton(addr, &sin.sin_addr) == 0)
		errx(EXIT_FAILURE, "%s: %s", addr, strerror(EINVAL));
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	(void)memcpy(&ifr.ifr_addr, &sin, sizeof(ifr.ifr_addr));
	if (ioctl(sock, SIOCSIFADDR, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCSIFADDR");

	/* ネットマスクを設定する */
	(void)memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	mask = masklen > 0 ? ~0u << (32 - masklen) : 0;
	sin.sin_addr.s_addr = (in_addr_t)htonl(mask);
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	(void)memcpy(&ifr.ifr_addr, &sin, sizeof(ifr.ifr_addr));
	if (ioctl(sock, SIOCSIFNETMASK, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCSIFNETMASK");

	/* ブロードキャストアドレスを設定する */
	(void)memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	if (inet_aton(addr, &sin.sin_addr) == 0)
		errx(EXIT_FAILURE, "%s: %s", addr, strerror(EINVAL));
	mask = masklen > 0 ? ~0u << (32 - masklen) : 0;
	sin.sin_addr.s_addr |= (in_addr_t)htonl(~mask);
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	(void)memcpy(&ifr.ifr_addr, &sin, sizeof(ifr.ifr_addr));
	if (ioctl(sock, SIOCSIFBRDADDR, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCSIFBRDADDR");

	/* インタフェイスを UP する */
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	if (ioctl(sock,SIOCGIFFLAGS, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCGIFFLAGS");
	ifr.ifr_flags |= IFF_UP;
	if (ioctl(sock,SIOCSIFFLAGS, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCSIFFLAGS");

	if (close(sock) == -1)
		err(EXIT_FAILURE, "close");
}

/**
 * LoRa 通信モジュールとの通信を初期化する。
 * 実装を簡単にするため、かなりサボっている。
 *
 * fd: LoRa 通信モジュール
 */
static void
init_lora(int fd)
{
	struct termios term;

	if (tcgetattr(fd, &term) == -1)
		err(1, "tcgetattr");
	cfmakeraw(&term);
	if (tcsetattr(fd, TCSANOW, &term) == -1)
		err(1, "tcsetattr");
}

/**
 * パケットの受信を代行する。
 */
static void
receive_packet(int lorafd, int tunfd)
{
	ssize_t nbyte, tmp;
	char rbuf[ALIGN16(LORAMTU+9)];

	(void)fprintf(stderr, "\nfrom LoRa:");

	nbyte = 0;
	do {
		tmp = read(lorafd, rbuf + nbyte, 1);
		if (tmp == -1)
			err(1, "read");
		nbyte += tmp;
	} while (nbyte <= ((unsigned)rbuf[0] & 0xFF));

	for (ssize_t i = 0; i < nbyte; i++) {
		if ((i & 0x0F) == 0x00)
			(void)fprintf(stderr, "\n%#04zx:\t", (size_t)i);
		(void)fprintf(stderr, "%02x ", (unsigned)rbuf[i] & 0xFF);
	}
	(void)fprintf(stderr, "\n%04zx (%zd)\n", (size_t)nbyte, (size_t)nbyte);

	/* その場しのぎで良いのでとりあえず TUN に投げ付ける */
	rbuf[4] = rbuf[5] = 0;
	if (write(tunfd, rbuf+4, nbyte-5) == -1)
		err(1, "write");
}

/**
 * パケットの送信を代行する。
 *
 * tunfd: TUN デバイス
 * lorafd: LoRa デバイス
 */
static void
transmit_packet(int tunfd, int lorafd)
{
	struct tun_pi pi;
	size_t buflen;
	ssize_t nbyte;
	char rbuf[ALIGN16(LORAMTU+sizeof(pi))];
	char tbuf[ALIGN16(LORAMTU+9)];
	unsigned char chksum;

	(void)fprintf(stderr, "\nfrom TUN:");

	/* パケットを読み出す */
	nbyte = read(tunfd, rbuf, sizeof(rbuf));
	if (nbyte == -1)
		err(1, "read");

	for (ssize_t i = 0; i < nbyte; i++) {
		if ((i & 0x0F) == 0x00)
			(void)fprintf(stderr, "\n%#04zx:\t", (size_t)i);
		(void)fprintf(stderr, "%02x ", (unsigned)rbuf[i] & 0xFF);
	}
	(void)fprintf(stderr, "\n%04zx (%zd)\n", (size_t)nbyte, (size_t)nbyte);

	/* IPv4 でなければ黙って捨てる */
	(void)memcpy(&pi, rbuf, sizeof(pi));
	if (ntohs(pi.proto) != ETHERTYPE_IP) {
		(void)fprintf(stderr, "not IPv4 packet, discard\n");
		return ;
	}

	/* XXX: 本来であればここで ARP する */

	/* パケットを構成する */
	tbuf[0] = tbuf[1] = 0xFF;	/* ブロードキャスト */
	tbuf[2] = 0x07;			/* 7 ch */
	tbuf[3] = tbuf[4] = 0xFF;	/* 自分のアドレスも知らない */
	tbuf[5] = 0x07;			/* 7 ch */
	(void)memcpy(tbuf+6, &pi.proto, sizeof(pi.proto));
	for (ssize_t i = 0; i < nbyte - sizeof(pi); i++)
		tbuf[8+i] = rbuf[sizeof(pi)+i];
	buflen = nbyte - sizeof(pi) + 9;

	chksum = 0;
	for (size_t i = 0; i < buflen-1; i++)
		chksum ^= tbuf[i];
	tbuf[buflen-1] = chksum;

	(void)fprintf(stderr, "to LoRa:");
	for (ssize_t i = 0; i < buflen; i++) {
		if ((i & 0x0F) == 0x00)
			(void)fprintf(stderr, "\n%#04zx:\t", (size_t)i);
		(void)fprintf(stderr, "%02x ", (unsigned)tbuf[i] & 0xFF);
	}
	(void)fprintf(stderr, "\n%04zx (%zd)\n", (size_t)buflen, (size_t)buflen);

	if (write(lorafd, tbuf, buflen) == -1)
		err(1, "write");

	/* ちょっと待つ */
	(void)usleep(500000);
}

/**
 * TUN インタフェイスを作成する。
 *
 * ifname: インタフェイス名のテンプレート
 *
 * 成功するとファイルデスクリプタを返し、
 * ifname の指す先を作られたインタフェイス名で更新する。
 * 失敗すると -1 を返し、それらしい errno を設定する。
 */
static int
tun_alloc(char *ifname)
{
	struct ifreq ifr;
	int error, fd;

	fd = open(TUNPATH, O_RDWR);
	if (fd == -1)
		return -1;

	(void)memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN;
	if (ifname != NULL && *ifname != '\0')
		(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);

	if (ioctl(fd, TUNSETIFF, (void *)&ifr) == -1) {
		error = errno;
		(void)close(fd);
		errno = error;
		return -1;
	}
	(void)snprintf(ifname, IFNAMSIZ, "%s", ifr.ifr_name);

	return fd;
}

/**
 * 使い方を出力して自滅する。
 */
static void
usage(void)
{
	fprintf(stderr, "usage: shinjid <LoRa> <CIDR>\n");
	exit(EXIT_FAILURE);
}
