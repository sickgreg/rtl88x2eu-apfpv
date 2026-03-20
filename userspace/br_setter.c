/*
 * br_setter.c
 *
 * Event-driven bitrate/driver-rate coordinator for FPV streaming.
 *
 * It reads driver rate events (one line per event), computes a conservative
 * encoder bitrate budget, and applies updates in a safe order:
 *   - rate drop:    encoder first, then driver
 *   - rate increase: driver first, then encoder
 *
 * Expected event line format:
 *   <seq> <ts_ms> <event> <ifname> <from_rate_id> <to_rate_id> <rssi> <reason>
 *
 * Example:
 *   1842 1711034123456 RATE_DROP wlan0 0x97 0x82 -74 RETRY_HIGH
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -o br_setter userspace/br_setter.c
 *
 * Cross-compile hint for SSC338Q (ARM hard-float):
 *   arm-linux-gnueabihf-gcc -O2 -Wall -Wextra -o br_setter userspace/br_setter.c
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_EVENT_PATH "/proc/net/rtl88x2eu/wlan0/rate_ctl_event"
#define DEFAULT_RATE_CTL_PATH "/proc/net/rtl88x2eu/wlan0/rate_ctl"
#define DEFAULT_HTTP_HOST "127.0.0.1"
#define DEFAULT_HTTP_PORT 80
#define DEFAULT_HTTP_PATH_FMT "/api/v1/set?video0.bitrate=%u"
#define DEFAULT_HEADROOM_PCT 55U
#define DROP_DELAY_US 50000U

struct cfg {
	const char *event_path;
	const char *rate_ctl_path;
	const char *http_host;
	uint16_t http_port;
	unsigned headroom_pct;
};

static bool parse_rate_id(const char *s, uint8_t *out)
{
	char *end = NULL;
	unsigned long v;

	if (!s || !out)
		return false;
	errno = 0;
	v = strtoul(s, &end, 0);
	if (errno || end == s || v > 0xFF)
		return false;
	*out = (uint8_t)v;
	return true;
}

/* Very conservative Mbps estimate by hw rate-id (approximate). */
static unsigned estimate_phy_mbps(uint8_t rid)
{
	/* HT MCS0..MCS31 in descriptor space starts at 0x80. */
	if (rid >= 0x80 && rid <= 0x9f) {
		static const unsigned ht1ss[8] = {7, 14, 21, 29, 43, 58, 65, 72};
		unsigned mcs = (rid - 0x80) & 0x7;
		unsigned nss = ((rid - 0x80) / 8) + 1;
		if (nss > 4)
			nss = 1;
		return ht1ss[mcs] * nss;
	}

	/* VHT1SS MCS0..MCS9 starts at 0xA0 in many Realtek mappings. */
	if (rid >= 0xA0 && rid <= 0xA9) {
		static const unsigned vht1ss[10] = {7, 14, 22, 29, 43, 58, 65, 72, 87, 96};
		return vht1ss[rid - 0xA0];
	}

	/* Legacy OFDM rough fallback (6..54). */
	if (rid >= 0x04 && rid <= 0x0b) {
		static const unsigned ofdm[8] = {6, 9, 12, 18, 24, 36, 48, 54};
		return ofdm[rid - 0x04];
	}

	return 12; /* Unknown -> safe baseline */
}

static unsigned encoder_bitrate_from_rate(uint8_t rid, unsigned headroom_pct)
{
	unsigned phy_mbps = estimate_phy_mbps(rid);
	unsigned kbps = (phy_mbps * 1000U * headroom_pct) / 100U;
	if (kbps < 500)
		kbps = 500;
	return kbps * 1000U; /* return bps for API compatibility */
}

static int http_set_bitrate(const struct cfg *c, unsigned bitrate_bps)
{
	char req[256];
	char path[128];
	char portbuf[16];
	struct addrinfo hints, *res = NULL, *rp;
	int fd = -1, rc = -1;

	snprintf(path, sizeof(path), DEFAULT_HTTP_PATH_FMT, bitrate_bps);
	snprintf(req, sizeof(req),
		 "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
		 path, c->http_host);
	snprintf(portbuf, sizeof(portbuf), "%u", c->http_port);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(c->http_host, portbuf, &hints, &res) != 0)
		return -1;

	for (rp = res; rp; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
			ssize_t n = send(fd, req, strlen(req), 0);
			if (n > 0) {
				char buf[256];
				(void)recv(fd, buf, sizeof(buf), 0);
				rc = 0;
			}
			close(fd);
			break;
		}
		close(fd);
	}

	freeaddrinfo(res);
	return rc;
}

static int driver_set_rate(const struct cfg *c, uint8_t rid)
{
	int fd;
	char out[16];
	ssize_t n;

	fd = open(c->rate_ctl_path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	n = snprintf(out, sizeof(out), "0x%02x 0\n", rid);
	if (n <= 0 || write(fd, out, (size_t)n) != n) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int handle_event_line(const struct cfg *c, const char *line)
{
	unsigned long long seq = 0, ts_ms = 0;
	char ev[32], ifname[32], from_s[16], to_s[16], reason[32];
	int rssi = 0;
	uint8_t from_id, to_id;
	unsigned bitrate_bps;
	bool is_drop;

	if (sscanf(line, "%llu %llu %31s %31s %15s %15s %d %31s",
		   &seq, &ts_ms, ev, ifname, from_s, to_s, &rssi, reason) != 8)
		return -1;
	(void)seq;
	(void)ts_ms;
	(void)ifname;
	(void)rssi;
	(void)reason;

	if (!parse_rate_id(from_s, &from_id) || !parse_rate_id(to_s, &to_id))
		return -1;

	bitrate_bps = encoder_bitrate_from_rate(to_id, c->headroom_pct);
	is_drop = to_id < from_id || strcmp(ev, "RATE_DROP") == 0 || strcmp(ev, "LINK_DEGRADED") == 0;

	if (is_drop) {
		/* Safety: only drop driver after encoder was successfully reduced. */
		if (http_set_bitrate(c, bitrate_bps) != 0)
			return -1;
		usleep(DROP_DELAY_US);
		if (driver_set_rate(c, to_id) != 0)
			return -1;
	} else {
		/* Safety: if encoder increase fails, roll driver back to previous rate. */
		if (driver_set_rate(c, to_id) != 0)
			return -1;
		if (http_set_bitrate(c, bitrate_bps) != 0) {
			(void)driver_set_rate(c, from_id);
			return -1;
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct cfg c = {
		.event_path = DEFAULT_EVENT_PATH,
		.rate_ctl_path = DEFAULT_RATE_CTL_PATH,
		.http_host = DEFAULT_HTTP_HOST,
		.http_port = DEFAULT_HTTP_PORT,
		.headroom_pct = DEFAULT_HEADROOM_PCT,
	};
	int fd;
	FILE *fp;
	char line[256];
	struct pollfd pfd;

	if (argc > 1)
		c.event_path = argv[1];
	if (argc > 2)
		c.rate_ctl_path = argv[2];

	fd = open(c.event_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open(%s) failed: %s\n", c.event_path, strerror(errno));
		return 1;
	}

	fp = fdopen(fd, "r");
	if (!fp) {
		fprintf(stderr, "fdopen failed: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	pfd.fd = fd;
	pfd.events = POLLIN;
	while (1) {
		int pr = poll(&pfd, 1, -1);
		if (pr <= 0)
			continue;
		if (fgets(line, sizeof(line), fp))
			(void)handle_event_line(&c, line);
		else
			clearerr(fp);
	}

	fclose(fp);
	return 0;
}
