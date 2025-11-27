Queue Flush And Forced-Rate Instructions
========================================

The rtl88x2eu driver exposes several procfs helpers under
`/proc/net/rtl88x2eu/<iface>/`. The sections below walk through the two
custom features that were added in this fork.

Lightweight RSSI/SNR/PUBQ sampling
----------------------------------

Use these minimal procfs nodes when you need high-rate telemetry without
the heavy `trx_info`/`trx_info_debug` dumps:

- `rssi_a`, `rssi_b`: current per-path RSSI reported by ODM.
- `snr_a`, `snr_b`: latest per-path OFDM SNR samples (dB).
- `pubq_free_page`: available public TX FIFO pages.

Each file prints a single integer, so userland loops like `watch -n0.1
cat /proc/net/rtl88x2eu/wlan0/rssi_a` avoid extra parsing and keep CPU
overhead low.

SNR sampling is gated behind the driver’s raw RX capture flag. Enable it
when you want non-zero `snr_a`/`snr_b` values and disable it afterward to
avoid unnecessary book-keeping:

```
# turn on per-packet SNR capture
echo 1 > /proc/net/rtl88x2eu/wlan0/rx_info_msg

# turn it back off when you are done
echo 0 > /proc/net/rtl88x2eu/wlan0/rx_info_msg
```

Only OFDM packets update the SNR cache. If you force a CCK-only rate or
there is no traffic, the `snr_*` entries will remain at 0 until the
device receives OFDM frames with the capture flag enabled.

Queue Flush Control (`flush_tx`)
--------------------------------

- Path: `/proc/net/rtl88x2eu/<iface>/flush_tx`
- Requires root access (`sudo -i` or use `sudo tee`).

Usage examples:

```
# Show available interfaces
ls /proc/net/rtl88x2eu

# Flush only the public queue (PUBQ) without disturbing the link
printf 'pub\n' > /proc/net/rtl88x2eu/wlan0/flush_tx

# Flush PUBQ and force-cancel outstanding URBs (legacy behaviour)
printf 'pub cancel\n' > /proc/net/rtl88x2eu/wlan0/flush_tx

# Flush specific data queues (multiple tokens are accepted)
printf 'vo vi\n' > /proc/net/rtl88x2eu/wlan0/flush_tx

# Flush every queue, pausing TX briefly
printf 'all\n' > /proc/net/rtl88x2eu/wlan0/flush_tx
```

Tokens you can pass (case-insensitive):

- `all` – flushes every hardware queue; also cancels USB transfers.
- `vo`, `vi`, `be`, `bk` – individual data queues.
- `mgmt`, `hiq`, `hi` – management/high priority queue.
- `pub` – alias for the public queue. This was the original source of
  build-up when PUBQ radio buffers needed to be cleared.
- `cancel` – optionally force a USB bulk-out cancel after the flush.
- `nocancel` – skip the USB cancel step even if you target data queues.
- Numeric queue IDs (`0`–`7`) are also accepted.

Mapping these tokens to the values reported by
`/proc/net/rtl88x2eu/<iface>/mac_qinfo` can be helpful when you are
watching the hardware FIFOs with `watch cat mac_qinfo`:

| proc token | queue id | mac_qinfo label | access category |
|------------|----------|-----------------|-----------------|
| `vo`       | 0        | `Q0`            | Voice           |
| `vi`       | 1        | `Q1`            | Video           |
| `be`       | 2        | `Q2`            | Best effort     |
| `bk`       | 3        | `Q3`            | Background      |
| `mgmt`     | 6/7      | `MG`/`HI`       | Management / HI |

If you see `pkt_num` incrementing on `Q0`, for example, flushing with
`printf 'vo\n' > …/flush_tx` will drain that queue without touching the
others.

Notes:

- Flushing non-data queues (`pub`, `mgmt`, `hiq`) leaves the transport
  path enabled, so the interface stays connected. Add the `cancel` token
  if you explicitly want to tear down outstanding URBs afterwards.
- When data queues are flushed (`vo`, `vi`, `be`, `bk`, `all`), the
  driver now pauses only the access categories you selected rather than
  blanketing every FIFO. That keeps beacons and management exchanges
  flowing even if you purge `vo`/`vi` repeatedly. The USB bulk-out
  cancel still runs by default for data queues; add `nocancel` to skip
  that step when you want to clear a queue quickly without disturbing
  associated stations.
- After the data queues are purged, the AP refreshes each associated
  station's inactivity and keep-alive counters. That prevents
  `expire_timeout_chk()` from expelling a client immediately after a
  flush just because its null-data probes were momentarily paused.
- Repeated VO/VI flushes can still make stations fall off the BSS when
  they are already running on a tight forced-rate mask. Consider the
  hostapd options below if you see long keep-alive gaps, and leave some
  time between flushes so null-data probes and BAR/ADDBA recovery frames
  can get through at the forced rate.

Hostapd knobs for aggressive queue purges
-----------------------------------------

If you are clearing the VO/VI queue on a fixed high MCS, extend the
station grace period in hostapd so temporary keep-alive failures do not
drop the client outright:

- `ap_max_inactivity=600` stretches hostapd’s inactivity window so it
  waits much longer before deciding a station is gone while the driver
  is still busy retrying null-data keep-alives at the forced rate.
- `skip_inactivity_poll=1` stops hostapd from probing with QoS null
  frames immediately before dropping a STA; the driver already runs its
  own DELBA/ADDBA recovery during `expire_timeout_chk()`.
- `disassoc_low_ack=0` keeps hostapd from forcefully disassociating a
  peer after a burst of failed retries while you are holding a strict
  mask.

Combine those hostapd overrides with a slightly longer delay between
flushes (1–2 s instead of a few hundred milliseconds) and, when
possible, enable fallback retries via `rate_ctl` so null frames and BAR
exchanges can step down if they hit a fade.

The example configuration you provided already includes those
hostapd values, so no further change is necessary on that side; the new
driver behaviour described above handles the remaining inactivity timer
resets automatically.

Why the driver still expels a STA after many flushes
----------------------------------------------------

- The AP-side watchdog in `expire_timeout_chk()` only gives a station a
  few two-second ticks to reply before it is reclaimed. Each failed
  check triggers DELBA/ADDBA recovery over the data queues, so if those
  frames never get ACKed at the forced MCS the station eventually hits
  the zero counter and is freed.
- `rtw_tx_flush_queue()` tears down every pending VO/VI frame and resets
  the per-AC transmit counters. From the peer’s perspective an entire
  AMPDU burst vanished, so it requests reordering state via BAR/DELBA
  exchanges that also have to traverse the forced-rate data path.
- When `rate_ctl` disables `data_fb`, the transmit descriptor clears the
  firmware’s fallback table. Keep-alive null data frames therefore retry
  at exactly the same (possibly too high) rate, compounding the recovery
  issues above.

Forced-Rate Telemetry (`rate_ctl`, `tx_stat`, `sta_tx_stat`)
------------------------------------------------------------

The driver supports forcing a transmit rate via `rate_ctl`. The
telemetry update that previously ran in the watchdog loop has been
disabled, so you now pull retry/failure counters manually when needed.

Set or clear a forced rate:

```
# Force MCS5 (0x15) without firmware fallback (default)
printf '0x15 0\n' > /proc/net/rtl88x2eu/wlan0/rate_ctl

# Force MCS5 (0x15) but allow the firmware to fall back on retries
printf '0x15 1\n' > /proc/net/rtl88x2eu/wlan0/rate_ctl

# Return to rate adaptation (RA) mode
printf '0xff\n' > /proc/net/rtl88x2eu/wlan0/rate_ctl
```

Collect retry statistics on demand:

```
# Dump retry/failure counters for every associated station
cat /proc/net/rtl88x2eu/wlan0/tx_stat

# Query a single station (replace MAC with the peer you care about)
printf 'aa:bb:cc:dd:ee:ff\n' > /proc/net/rtl88x2eu/wlan0/sta_tx_stat
cat /proc/net/rtl88x2eu/wlan0/sta_tx_stat
```

Tips:

- Run the `tx_stat` or `sta_tx_stat` commands immediately after forcing
  a rate to capture retries that occurred under the fixed mask.
- "Fallback" controls whether the firmware may walk its retry table and
  drop to lower data rates after the initial attempt at the forced rate
  fails. Leaving fallback enabled gives the hardware room to recover
  from momentary fades or interference without dropping the link;
  disabling fallback means every retry uses exactly the rate you forced.
- Fallback stays disabled unless you explicitly write `1` as the second
  argument. Keep it disabled if you are experimenting with pure fixed
  rates, but enable it when you want retries to walk down the firmware's
  rate table automatically.
- The default `pub` flush keeps USB transports running so even tight
  masks remain stable. If you need the legacy "drop everything" flush,
  add the `cancel` token to explicitly tear down the bulk-out pipes.
- If you revert to RA (`rate_ctl` set to `0xff`), the firmware resumes
  managing link-speed selection and the manual telemetry requests will
  still work whenever you need them.
