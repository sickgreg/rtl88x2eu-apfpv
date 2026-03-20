# AGENTS Handoff

This file is the continuation handoff for the `rtl88x2eu` / OpenIPC `ssc338q` AP-mode work after compaction and target reflash.

## Goal

Resume from a clean target reflash and determine why the latest rebuilt driver loses AP capability, while preserving the useful event-pipeline work that was added for real rate-change notifications.

## Hard Requirement

This requirement is now explicit and non-negotiable for the next phase:

- the driver must remain the owner of MCS/rate control
- when the link needs to shift downward, the driver must notify userspace **before** the lower MCS is applied
- userspace must lower the encoder bitrate first
- the driver must wait a short period for that encoder reduction to take effect
- only after that wait may the driver actually apply the lower MCS

In other words, the correct sequencing is:

1. detect impending degradation before committing the lower rate
2. emit a pre-drop event to userspace
3. defer or gate the actual MCS drop briefly
4. after the delay or acknowledgment, allow/apply the lower MCS

Anything that instead does:

- autonomous driver RA drop first
- userspace reacts afterward

does **not** satisfy the requirement, even if the event pipeline and bitrate controller are otherwise working.

Important current status:

- the current implementation does **not** satisfy this requirement
- the current implementation only reports rate changes after they already happened
- `br_setter` currently reacts post-change
- the optional `--sync-driver-rate` mode is not the intended architecture and should not be treated as the solution

The next plan/work must therefore focus on a proper driver-side pre-drop coordination model, not more post-facto userspace tuning.

## Local Repo State

Repo root: `C:\88x2eu_PR`

Current modified files:

- `Makefile`
- `README.md`
- `core/rtw_debug.c`
- `hal/phydm/phydm_rainfo.c`
- `include/rtw_debug.h`
- `os_dep/linux/rtw_proc.c`
- `userspace/br_setter.c`

Current untracked paths:

- `artifacts/`
- `gregs_testing_rules.md`

## Implemented Local Changes

### 1. Kernel 4.9 proc poll compatibility fix

File: `os_dep/linux/rtw_proc.c`

Purpose:

- Build against the OpenIPC `4.9.84` kernel
- Use `__poll_t` only on newer kernels
- Fall back to `unsigned int` on old kernels

This fix was required for the earlier successful build against the OpenIPC toolchain and should probably be kept.

### 2. Real RA event plumbing in the driver

Files:

- `core/rtw_debug.c`
- `include/rtw_debug.h`
- `hal/phydm/phydm_rainfo.c`

Purpose:

- export `rtw_rate_ctl_event_notify_with_rssi()`
- keep wrapper `rtw_rate_ctl_event_notify()`
- hook real firmware/PHYDM RA reports in `phydm_c2h_ra_report_handler()`
- emit `RATE_DROP` / `RATE_RISE` into the proc-backed event queue
- ignore SGI-only flips
- stay quiet when fixed-rate mode is active
- suppress automatic AP/MESH event emission when station count suggests more than one peer

Important note:

- This was the main behavioral change after the previously working build.
- It is still unproven whether this code is the direct cause of AP mode failing.

### 3. `br_setter` behavior change

File: `userspace/br_setter.c`

Purpose:

- default behavior is now encoder bitrate only
- no driver `rate_ctl` writes by default
- old behavior is opt-in via `--sync-driver-rate`
- ignore non-rate events like `RATE_INIT` and `USER_EVENT`

### 4. README updates

File: `README.md`

Purpose:

- describe the real RA-driven event path
- document BusyBox event monitoring
- document `--sync-driver-rate`

## Important File/Function References

These were the main edit points when the event path was added:

- `core/rtw_debug.c`
  - `rtw_rate_ctl_event_notify_with_rssi()` around line 2900
- `include/rtw_debug.h`
  - prototypes around lines 365-370
- `hal/phydm/phydm_rainfo.c`
  - reason helper around line 32
  - notify helper around line 61
  - `phydm_c2h_ra_report_handler()` around line 592
  - event call around line 653
- `userspace/br_setter.c`
  - config field for `sync_driver_rate` around line 52
  - encoder-only path around line 195
  - `--sync-driver-rate` parsing around line 234
- `README.md`
  - event-driven controller section starts around line 125

## Build Context

Earlier successful build target:

- OpenIPC target: `ssc338q apfpv`
- kernel: `4.9.84`
- toolchain was built under WSL in `~/builder/openipc`

Known earlier toolchain path:

- `~/builder/openipc/output/host/bin/arm-openipc-linux-gnueabihf-`

Known earlier kernel tree:

- `~/builder/openipc/output/build/linux-custom`

## Current Local Binary Facts

Current repo-root module:

- file: `C:\88x2eu_PR\8812eu.ko`
- SHA256: `4ab6cc3a0ca4e3f33b7d44f77a892134e764b7cb954883186741131c36d62167`

Current artifact copies:

- `C:\88x2eu_PR\artifacts\ssc338q_apfpv\8812eu-ssc338q-apfpv.ko`
  - SHA256: `eba0470dbb12c2d500d26fc85787dc75da740f8489ca736e5d424eea26504474`
- `C:\88x2eu_PR\artifacts\ssc338q_apfpv\8812eu.ko`
  - same size/timestamp bucket as the stale artifact copy above
- `C:\88x2eu_PR\artifacts\ssc338q_apfpv\br_setter`
  - SHA256: `0502a54ae541b7533c1b69ffa3bb4cae8299aaf38f3e8fee748cd494fb402ea0`

Critical warning:

- The repo-root `8812eu.ko` and the named artifact `8812eu-ssc338q-apfpv.ko` do **not** match.
- Earlier deployment used the stale artifact hash `eba047...`.
- Before testing again after reflash, refresh artifacts from the actual intended build output and verify hashes.

## Target Access Rules

Source of truth for session rules: `gregs_testing_rules.md`

Important items remembered from that file:

- Forwarded target:
  - `root@192.168.8.156:2222`
  - password `12345`
- Preferred safer access path used later:
  - direct debug ethernet `root@192.168.8.105:22`
- Use Windows Paramiko when needed
- Use `scp -O` if using `scp`
- forwarded HTTP port `8088` maps to camera port `80`
- userspace binary belongs in `/usr/bin`
- kernel module to replace lives in `/lib/modules/4.9.84/extra/8812eu.ko`
- target space is tight, so keep backups locally, not on target

## What Happened On Target Before Reflash

The later deployed module on target matched the stale artifact hash, not the current repo-root build:

- on-target `/lib/modules/4.9.84/extra/8812eu.ko`
- on-target `/overlay/root/lib/modules/4.9.84/extra/8812eu.ko`
- both matched SHA256 `eba0470dbb12c2d500d26fc85787dc75da740f8489ca736e5d424eea26504474`

Observed target state over debug ethernet:

- SSH to `192.168.8.105:22` worked
- overlay was writable again during the later stable checks
- `8812eu` was loaded
- `majestic` was running
- `wlan0` existed and still had `192.168.0.1/24`
- but `wlan0` was **not** in AP mode

Specific findings:

- `iw dev wlan0 info` showed `type managed`
- `iw station dump` was empty
- `/proc/net/rtl88x2eu/wlan0/mlmext_state` was `0x0`
- `ap_info` showed no current network MAC

Manual AP mode test failed:

- `ip link set wlan0 down`
- `iw dev wlan0 set type __ap`
- result: `Operation not supported (-95)`

`adapter start` also failed:

- `hostapd` log showed:
  - `nl80211: Could not configure driver mode`
  - `nl80211 driver initialization failed.`
  - `wlan0: interface state UNINITIALIZED->DISABLED`
  - `hostapd_free_hapd_data: Interface wlan0 wasn't started`

`iw phy0 info` showed only:

- `managed`
- `monitor`

No `AP` support was advertised at runtime.

## Boot / AP Startup Path

This was inspected on target and is expected to still be true after reflash unless the firmware image changed:

File:

- `/etc/network/interfaces.d/wlan0`

Relevant content:

- `iface wlan0 inet static`
- `address 192.168.0.1`
- `pre-up adapter setup`
- `post-up adapter start`
- `post-down adapter stop`

Implication:

- AP should normally come up at boot via `ifup` and `/usr/bin/adapter`
- if AP does not come up after reflash, that is a useful baseline signal

`/usr/bin/adapter` behavior that matters:

- `setup` loads the driver via `modprobe`
- `start` runs `hostapd /tmp/hostapd.conf -B`
- then waits for `iw dev wlan0 info` to show `type AP`

## Runtime Clue From Kernel Logs

There was a repeated trace in the receive/C2H path around:

- `__mutex_lock_interruptible_slowpath`
- `_halmac_mutex_lock`
- `get_efuse_data_8822e`
- `get_c2h_info_88xx`
- `rtw_halmac_c2h_handle`
- `process_c2h_event`
- `rtl8822e_c2h_handler_no_io`
- `recvbuf2recvframe`
- `usb_recv_tasklet`

This appeared in logs around `Mar 20 07:21:10`.

It may be relevant because the real rate-event hook was added in a C2H handling path, but causality was not proven.

## Important Investigation Result

One early theory was that AP support vanished because `CONFIG_AP_MODE` was not being compiled in.

Evidence checked:

- `Makefile` visibly had `CONFIG_AP_MODE = n`
- AP support in cfg80211 code is gated on `CONFIG_AP_MODE`

But:

- `make -pn` showed effective `EXTRA_CFLAGS` already included `-DCONFIG_AP_MODE`

Conclusion:

- the runtime loss of AP support was **not** explained by the simple top-level `CONFIG_AP_MODE = n` line alone
- the problem is more likely due to some other build/input mismatch or runtime corruption

## Most Likely Failure Candidates

Current ordered suspicion list:

1. The real RA event hook in `hal/phydm/phydm_rainfo.c` changed runtime behavior enough to break driver state or registration.
2. The deployed module was not the intended latest repo-root build because artifacts were stale and hashes diverged.
3. A rebuild mismatch versus the previously working OpenIPC build path produced a binary that loads but no longer exposes AP capability.
4. Filesystem corruption on the target may have contributed, but the target is now being reflashed so that variable should be cleared.

## Required Reset Strategy After Fresh Reflash

Do **not** trust any previous on-target state. Start clean.

### Phase 1: Capture clean baseline from fresh stock system

Use debug ethernet first if possible.

Collect:

- `sha256sum /lib/modules/4.9.84/extra/8812eu.ko`
- `iw phy0 info | sed -n '/Supported interface modes:/,/software interface modes/p'`
- `iw dev wlan0 info`
- `ps | grep majestic`
- `adapter stop; adapter setup; adapter start`
- result of `iw dev wlan0 set type __ap`

Goal:

- prove stock image still has working AP mode before deploying anything

### Phase 2: Refresh local deploy artifacts

Before copying anything to target:

- rebuild if needed
- decide which exact local binary is the deploy candidate
- copy the intended module into a clearly named artifact file
- verify SHA256 for both module and userspace binary
- record those hashes in the session notes

### Phase 3: Deploy over debug ethernet, not over the Wi-Fi operational path

Rules:

- userspace binary goes in `/usr/bin`
- module goes in `/lib/modules/4.9.84/extra/8812eu.ko`
- keep backups local, not on target

Immediately after deployment, check:

- `sha256sum /lib/modules/4.9.84/extra/8812eu.ko`
- `iw phy0 info`
- `iw dev wlan0 set type __ap`
- `adapter start`

Do **not** move on to rate-event testing until AP capability is confirmed.

### Phase 4: If AP breaks again, bisect locally by feature area

Fastest likely isolation path:

- keep `os_dep/linux/rtw_proc.c` 4.9 poll fix
- temporarily remove only the real-event hook changes:
  - `core/rtw_debug.c`
  - `include/rtw_debug.h`
  - `hal/phydm/phydm_rainfo.c`
- rebuild
- redeploy
- compare AP capability again

This should quickly answer whether the new C2H/RA event integration is the cause.

## Suggested First Commands After Reflash

Baseline capture:

```sh
sha256sum /lib/modules/4.9.84/extra/8812eu.ko
iw phy0 info | sed -n '/Supported interface modes:/,/software interface modes/p'
iw dev wlan0 info
adapter stop; adapter setup; adapter start
```

Minimal AP capability probe:

```sh
ip link set wlan0 down
iw dev wlan0 set type __ap
echo $?
ip link set wlan0 up
```

If stock passes and a deployed test build fails, the deploy build is the problem.

## Resume Objective

After reflash, the next session should:

1. verify stock AP mode works
2. refresh artifacts so deploy hashes are unambiguous
3. redeploy one known module/userspace pair over debug ethernet
4. confirm AP mode still works
5. only then test `rate_ctl_event` and `br_setter`

## Short Version

- Fresh reflash should eliminate target filesystem damage as a confounder.
- The last deployed module on target matched stale artifact hash `eba047...`.
- The current repo-root module hash is different: `4ab6cc...`.
- AP failure symptom was real: `iw dev wlan0 set type __ap` returned `-95` and `iw phy0 info` lacked `AP`.
- The next session must re-establish a stock baseline first, then redeploy using hash-verified binaries only.
