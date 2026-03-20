# Pre-Drop Encoder Sync Plan

This file captures the implementation plan for the required AP-mode rate-control behavior:

1. the driver remains the owner of MCS/rate control
2. when a downward shift is needed, the driver notifies userspace before applying the lower MCS
3. userspace lowers encoder bitrate first
4. the driver waits a short period
5. only then does the driver apply the lower MCS

Anything that drops MCS first and adjusts encoder bitrate afterward is not acceptable as the final design.

## Target Behavior

Required sequence for a downward transition:

1. detect impending degradation before committing the lower rate
2. emit a `PRE_DROP` event with the current rate, intended lower rate, and defer window
3. let `br_setter` reduce encoder bitrate immediately
4. after a short delay or acknowledgment, commit the lower MCS
5. if conditions recover before commit, cancel the pending drop

## First Implementation Strategy

The first version should be timeout-based, not acknowledgment-based.

Why:

- it is the smallest change that proves the sequencing model
- it avoids adding a userspace-to-kernel control dependency on the first pass
- it gives a bounded fallback when userspace is missing or slow

After the timeout-based version works on target, an explicit userspace acknowledgment path can be added if needed.

## Work Breakdown

### 1. Find the earliest safe pre-commit hook

Audit the RA path and determine where the driver first knows a downward shift is about to happen, but before the lower rate is committed.

Primary files to inspect:

- `hal/phydm/phydm_rainfo.c`
- `core/rtw_debug.c`

Goal:

- stop relying on post-facto notifications from already-applied rate changes
- identify a point where the driver can start a pending-drop state instead

### 2. Add per-STA pending-drop state

Introduce driver state for the active AP peer:

- pending target lower rate
- current source rate
- event sequence or token
- defer deadline
- pending/cancelled/committed state

Suggested locations:

- `include/drv_types.h`
- AP STA structures if a per-station placement is more appropriate

### 3. Extend the event protocol

Add explicit event types for the sequencing model:

- `PRE_DROP`
- `DROP_COMMIT`
- `DROP_CANCEL`

Keep existing `RATE_DROP` and `RATE_RISE` for compatibility and observability, but do not rely on them for the pre-drop contract.

Protocol expectations:

- event format should remain simple proc-line text
- include enough information for `br_setter` to calculate the new encoder target immediately

### 4. Gate the actual lower-MCS commit

When the driver decides the link needs to go down:

- queue `PRE_DROP`
- start a short defer timer
- prevent the lower MCS from being committed until the timer expires

If conditions improve before expiry:

- cancel the pending drop
- emit `DROP_CANCEL`

If conditions remain degraded at expiry:

- commit the lower MCS
- emit `DROP_COMMIT`

### 5. Update `br_setter`

Change userspace behavior so the main reaction is to `PRE_DROP`, not to a post-facto drop.

Required behavior:

- on `PRE_DROP`, calculate and apply reduced encoder bitrate immediately
- keep default mode driver-owned, without normal `rate_ctl` forcing
- optionally support a later acknowledgment mechanism if the kernel side adds it

### 6. Add fail-safe behavior

The driver must not stall rate adaptation if userspace is absent or broken.

Requirements:

- bounded timeout before committing the drop
- no permanent blocked state
- safe recovery if userspace never reacts

### 7. Testing sequence

Validate the exact ordering on target:

1. AP comes up normally
2. `rate_ctl` remains `RA`
3. station associates and traffic is flowing
4. link degradation is introduced
5. `PRE_DROP` appears before the lower MCS is visible on the station
6. encoder bitrate falls first
7. after the defer window, the lower MCS is committed
8. if the link recovers early enough, `DROP_CANCEL` occurs instead of the drop

### 8. Documentation

Update:

- `README.md`
- `AGENTS.md`

Document:

- event sequencing semantics
- timeout behavior
- any new proc/control nodes
- target test procedure

## Code Areas

Most likely files:

- `core/rtw_debug.c`
- `hal/phydm/phydm_rainfo.c`
- `include/rtw_debug.h`
- `include/drv_types.h`
- `include/cmn_info/rtw_sta_info.h`
- `os_dep/linux/rtw_proc.c`
- `userspace/br_setter.c`
- `README.md`
- `AGENTS.md`

## Non-Goal

Do not spend more time refining the current post-facto design as if it were the final answer. It is useful groundwork only.
