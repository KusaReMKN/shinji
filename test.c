#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <arpa/inet.h>

#include <linux/if_tun.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TUNPATH	"/dev/net/tun"
#define IFNAME	"lora%d"

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

static int
set_mtu(const char *ifname, int mtu)
{
	struct ifreq ifr;
	int error, sock;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
		return -1;

	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	ifr.ifr_mtu = mtu;
	if (ioctl(sock, SIOCSIFMTU, &ifr) == -1) {
		error = errno;
		(void)close(sock);
		errno = error;
		return -1;
	}

	if (close(sock) == -1)
		return -1;

	return 0;
}

static int
set_in_addr(const char *ifname, const struct sockaddr_in *sin)
{
	struct ifreq ifr;
	int error, sock;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
		return -1;

	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	(void)memcpy(&ifr.ifr_addr, sin, sizeof(*sin));
	if (ioctl(sock, SIOCSIFADDR, &ifr) == -1) {
		error = errno;
		(void)close(sock);
		errno = error;
		return -1;
	}

	if (close(sock) == -1)
		return -1;

	return 0;
}

static int
set_in_netmask(const char *ifname, const struct sockaddr_in *sin)
{
	struct ifreq ifr;
	int error, sock;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
		return -1;

	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	(void)memcpy(&ifr.ifr_addr, sin, sizeof(*sin));
	if (ioctl(sock, SIOCSIFNETMASK, &ifr) == -1) {
		error = errno;
		(void)close(sock);
		errno = error;
		return -1;
	}

	if (close(sock) == -1)
		return -1;

	return 0;
}

static int
set_link(const char *ifname, int up)
{
	struct ifreq ifr;
	int error, sock;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
		return -1;

	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	if (ioctl(sock, SIOCGIFFLAGS, &ifr) == -1) {
		error = errno;
		(void)close(sock);
		errno = error;
		return -1;
	}

	if (up)
		ifr.ifr_flags |= IFF_UP;
	else
		ifr.ifr_flags &= ~IFF_UP;

	if (ioctl(sock, SIOCSIFFLAGS, &ifr) == -1) {
		error = errno;
		(void)close(sock);
		errno = error;
		return -1;
	}

	if (close(sock) == -1)
		return -1;

	return 0;
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in sin;
	ssize_t nread;
	int tunfd;
	char buf[256], ifname[IFNAMSIZ] = IFNAME;

	// create new interface
	tunfd = tun_alloc(ifname);
	if (tunfd == -1)
		err(1, "tun_alloc");

	(void)fprintf(stderr, "ifname: %s\n", ifname);

	if (set_mtu(ifname, 195) == -1)
		err(1, "set_mtu");

	(void)memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	(void)inet_pton(AF_INET, "10.5.2.10", &sin.sin_addr);
	if (set_in_addr(ifname, &sin) == -1)
		err(1, "set_in_addr");

	(void)memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	(void)inet_pton(AF_INET, "255.255.255.0", &sin.sin_addr);
	if (set_in_netmask(ifname, &sin) == -1)
		err(1, "set_in_netmask");

	if (set_link(ifname, 1) == -1)
		err(1, "set_link");


	for (;;) {
		nread = read(tunfd, buf, sizeof(buf));
		if (nread == -1)
			err(1, "read");

		for (ssize_t i = 0; i < nread; i++) {
			if ((i & 0x0F) == 0)
				fprintf(stderr, "\n%#04zx:\t", (size_t)i);
			fprintf(stderr, "%02x ", buf[i] & 0xFF);
		}
		fprintf(stderr, "\n%1$#04zx (%1$zd)\n", (size_t)nread);
	}
	pause();

	return 0;
}
