/*
 * br_setter.c
 *
 * Event-driven bitrate controller for FPV streaming.
 *
 * It reads driver rate events (one line per event), computes a conservative
 * encoder bitrate budget, and updates the encoder. Manual driver-rate writes
 * are optional and disabled by default because the normal path is for the
 * driver to keep ownership of rate control.
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
#define DEFAULT_MAJESTIC_CFG "/etc/majestic.yaml"
#define DEFAULT_IW_INFO_CMD "iw dev wlan0 info 2>/dev/null"
#define DEFAULT_HEADROOM_PCT 20U
#define MAX_BITRATE_20MHZ_KBPS 25000U
#define MAX_BITRATE_40MHZ_KBPS 50000U
#define DROP_DELAY_US 50000U

struct cfg {
	const char *event_path;
	const char *rate_ctl_path;
	const char *http_host;
	const char *majestic_cfg_path;
	uint16_t http_port;
	unsigned headroom_pct;
	bool sync_driver_rate;
	unsigned max_bitrate_kbps;
	unsigned last_bitrate_kbps;
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

/*
 * Realtek descriptor-space rate IDs:
 * - bit7 carries SGI
 * - base ID lives in the low 7 bits
 * - HT MCS0..31 starts at 0x0c
 * - VHT1SS MCS0 starts at 0x2c
 *
 * Return bitrate in 100kbps units, mirroring rtw_desc_rate_to_bitrate().
 */
static unsigned estimate_phy_rate_100kbps(uint8_t rid)
{
	static const unsigned legacy_100kbps[12] = {
		10, 20, 55, 110, 60, 90, 120, 180, 240, 360, 480, 540
	};
	static const unsigned ht_lgi_100kbps[8] = {
		65, 130, 195, 260, 390, 520, 585, 650
	};
	static const unsigned ht_sgi_100kbps[8] = {
		72, 144, 217, 289, 433, 578, 650, 722
	};
	static const unsigned vht_lgi_100kbps[10] = {
		65, 130, 195, 260, 390, 520, 585, 650, 780, 867
	};
	static const unsigned vht_sgi_100kbps[10] = {
		72, 144, 217, 289, 433, 578, 650, 722, 867, 963
	};
	unsigned base = rid & 0x7f;
	unsigned sgi = (rid & 0x80) ? 1U : 0U;

	if (base <= 0x0b)
		return legacy_100kbps[base];

	if (base >= 0x0c && base <= 0x2b) {
		unsigned mcs = (base - 0x0c) % 8U;
		unsigned nss = ((base - 0x0c) / 8U) + 1U;
		return (sgi ? ht_sgi_100kbps[mcs] : ht_lgi_100kbps[mcs]) * nss;
	}

	if (base >= 0x2c && base <= 0x53) {
		unsigned mcs = (base - 0x2c) % 10U;
		unsigned nss = ((base - 0x2c) / 10U) + 1U;
		return (sgi ? vht_sgi_100kbps[mcs] : vht_lgi_100kbps[mcs]) * nss;
	}

	return 120; /* 12 Mbps fallback */
}

static bool load_majestic_bitrate_value(const char *path, unsigned *out_kbps)
{
	FILE *fp;
	char line[256];

	if (!path || !out_kbps)
		return false;

	fp = fopen(path, "r");
	if (!fp)
		return false;

	while (fgets(line, sizeof(line), fp)) {
		char *p = line;
		unsigned v;

		while (*p && isspace((unsigned char)*p))
			p++;
		if (strncmp(p, "bitrate:", 8) != 0)
			continue;
		p += 8;
		while (*p && isspace((unsigned char)*p))
			p++;
		if (sscanf(p, "%u", &v) == 1 && v > 0) {
			fclose(fp);
			*out_kbps = v;
			return true;
		}
	}

	fclose(fp);
	return false;
}

static unsigned detect_channel_cap_kbps(void)
{
	FILE *fp;
	char line[256];
	unsigned width_mhz = 20;

	fp = popen(DEFAULT_IW_INFO_CMD, "r");
	if (!fp)
		return MAX_BITRATE_20MHZ_KBPS;

	while (fgets(line, sizeof(line), fp)) {
		char *p = strstr(line, "width:");
		if (!p)
			continue;
		p += 6;
		while (*p && isspace((unsigned char)*p))
			p++;
		if (sscanf(p, "%u", &width_mhz) == 1)
			break;
	}

	pclose(fp);

	if (width_mhz >= 40)
		return MAX_BITRATE_40MHZ_KBPS;
	return MAX_BITRATE_20MHZ_KBPS;
}

static unsigned encoder_bitrate_kbps_from_rate(const struct cfg *c, uint8_t rid)
{
	unsigned phy_100kbps = estimate_phy_rate_100kbps(rid);
	unsigned kbps = (phy_100kbps * 100U * c->headroom_pct) / 100U;

	if (c->max_bitrate_kbps && kbps > c->max_bitrate_kbps)
		kbps = c->max_bitrate_kbps;
	if (kbps < 500)
		kbps = 500;
	return kbps;
}

static int http_set_bitrate(const struct cfg *c, unsigned bitrate_kbps)
{
	char req[256];
	char path[128];
	char portbuf[16];
	struct addrinfo hints, *res = NULL, *rp;
	int fd = -1, rc = -1;

	snprintf(path, sizeof(path), DEFAULT_HTTP_PATH_FMT, bitrate_kbps);
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

static int handle_event_line(struct cfg *c, const char *line)
{
	unsigned long long seq = 0, ts_ms = 0;
	char ev[32], ifname[32], from_s[16], to_s[16], reason[32];
	int rssi = 0;
	uint8_t from_id, to_id;
	unsigned bitrate_kbps;
	bool is_drop;

	if (sscanf(line, "%llu %llu %31s %31s %15s %15s %d %31s",
		   &seq, &ts_ms, ev, ifname, from_s, to_s, &rssi, reason) != 8)
		return -1;
	(void)seq;
	(void)ts_ms;
	(void)ifname;
	(void)rssi;
	(void)reason;

	if (strcmp(ev, "RATE_DROP") != 0 && strcmp(ev, "RATE_RISE") != 0 &&
	    strcmp(ev, "LINK_DEGRADED") != 0 && strcmp(ev, "LINK_RECOVERED") != 0)
		return 0;

	if (!parse_rate_id(from_s, &from_id) || !parse_rate_id(to_s, &to_id))
		return -1;

	bitrate_kbps = encoder_bitrate_kbps_from_rate(c, to_id);
	is_drop = to_id < from_id || strcmp(ev, "RATE_DROP") == 0 || strcmp(ev, "LINK_DEGRADED") == 0;

	if (bitrate_kbps == c->last_bitrate_kbps)
		return 0;

	if (!c->sync_driver_rate) {
		int rc = http_set_bitrate(c, bitrate_kbps);
		if (rc == 0) {
			c->last_bitrate_kbps = bitrate_kbps;
			fprintf(stderr, "%s => bitrate_kbps=%u\n", line, bitrate_kbps);
			fflush(stderr);
		}
		return rc;
	}

	if (is_drop) {
		/* Safety: only drop driver after encoder was successfully reduced. */
		if (http_set_bitrate(c, bitrate_kbps) != 0)
			return -1;
		c->last_bitrate_kbps = bitrate_kbps;
		usleep(DROP_DELAY_US);
		if (driver_set_rate(c, to_id) != 0)
			return -1;
	} else {
		/* Safety: if encoder increase fails, roll driver back to previous rate. */
		if (driver_set_rate(c, to_id) != 0)
			return -1;
		if (http_set_bitrate(c, bitrate_kbps) != 0) {
			(void)driver_set_rate(c, from_id);
			return -1;
		}
		c->last_bitrate_kbps = bitrate_kbps;
	}

	fprintf(stderr, "%s => bitrate_kbps=%u\n", line, bitrate_kbps);
	fflush(stderr);

	return 0;
}

int main(int argc, char **argv)
{
	struct cfg c = {
		.event_path = DEFAULT_EVENT_PATH,
		.rate_ctl_path = DEFAULT_RATE_CTL_PATH,
		.http_host = DEFAULT_HTTP_HOST,
		.majestic_cfg_path = DEFAULT_MAJESTIC_CFG,
		.http_port = DEFAULT_HTTP_PORT,
		.headroom_pct = DEFAULT_HEADROOM_PCT,
		.sync_driver_rate = false,
	};
	int fd;
	char line[256];
	struct pollfd pfd;
	int argi = 1;

	if (argi < argc && strcmp(argv[argi], "--sync-driver-rate") == 0) {
		c.sync_driver_rate = true;
		argi++;
	}
	if (argi < argc)
		c.event_path = argv[argi++];
	if (argi < argc)
		c.rate_ctl_path = argv[argi++];
	if (argi < argc) {
		fprintf(stderr, "usage: %s [--sync-driver-rate] [event_path] [rate_ctl_path]\n",
			argv[0]);
		return 1;
	}

	c.max_bitrate_kbps = detect_channel_cap_kbps();
	if (!load_majestic_bitrate_value(c.majestic_cfg_path, &c.last_bitrate_kbps))
		c.last_bitrate_kbps = c.max_bitrate_kbps;

	fd = open(c.event_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open(%s) failed: %s\n", c.event_path, strerror(errno));
		return 1;
	}

	pfd.fd = fd;
	pfd.events = POLLIN;
	while (1) {
		int pr = poll(&pfd, 1, -1);
		ssize_t n;

		if (pr <= 0)
			continue;

		if (lseek(fd, 0, SEEK_SET) < 0)
			continue;

		n = read(fd, line, sizeof(line) - 1);
		if (n <= 0)
			continue;
		line[n] = '\0';
		(void)handle_event_line(&c, line);
	}
}
