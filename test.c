#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
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
	struct sockaddr_in sin;
	int error, fd;

	fd = open(TUNPATH, O_RDWR);
	if (fd == -1)
		return -1;

	(void)memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN;
	if (ifname != NULL && *ifname != '\0')
		(void)strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

	if (ioctl(fd, TUNSETIFF, (void *)&ifr) == -1) {
		error = errno;
		(void)close(fd);
		errno = error;
		return -1;
	}
	(void)strcpy(ifname, ifr.ifr_name);

	return fd;
}

int
main(int argc, char *argv[])
{
	struct ifreq ifr;
	struct sockaddr_in sin;
	int sockfd, tunfd;
	char ifname[IFNAMSIZ] = IFNAME;

	// create new interface
	tunfd = tun_alloc(ifname);
	if (tunfd == -1)
		err(1, "tun_alloc");

	(void)fprintf(stderr, "ifname: %s\n", ifname);

	// socket for control
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd == -1)
		err(1, "socket");

	// set MTU
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	ifr.ifr_mtu = 195;
	if (ioctl(sockfd, SIOCSIFMTU, &ifr) == -1)
		err(1, "SIOCSIFMTU");

	// set IPv4 addr.
	(void)memset(&ifr, 0, sizeof(ifr));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr("10.5.2.10");
	(void)memcpy(&ifr.ifr_addr, &sin, sizeof(sin));
	(void)strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(sockfd, SIOCSIFADDR, &ifr) == -1)
		err(1, "SIOCSIFADDR");

	// set IPv4 netmask
	(void)memset(&ifr, 0, sizeof(ifr));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr("255.255.255.0");
	(void)memcpy(&ifr.ifr_netmask, &sin, sizeof(sin));
	(void)strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(sockfd, SIOCSIFNETMASK, &ifr) == -1)
		err(1, "SIOCSIFNETMASK");

	// link up
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) == -1)
		err(1, "SIOCGIFFLAGS");
	ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
	if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) == -1)
		err(1, "SIOCSIFFLAGS");

	pause();

	return 0;
}
