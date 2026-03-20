# rtl88x2eu-20230815
Linux Driver for WiFi Adapters that are based on the RTL8812EU and RTL8822EU Chipsets, based on driver ```v5.15.0.1-249```  
Original driver tar: [rtl88x2EU_rtl88x2CU-VE_WiFi_linux_v5.15.0.1-249-g9245f8bd9.20241218_COEX20240913-390e.tar.gz](https://github.com/user-attachments/files/20632328/rtl88x2EU_rtl88x2CU-VE_WiFi_linux_v5.15.0.1-249-g9245f8bd9.20241218_COEX20240913-390e.tar.gz)  

This branch is mainly focused on FPV. PRs welcome.

## Hardware 
BL-M8812EU2 datasheet: [BL-M8812EU2_datasheet_V1.0.1.1_240511.pdf](https://github.com/user-attachments/files/16627775/BL-M8812EU2_datasheet_V1.0.1.1_240511.pdf)  
Or any adaptor based on RTL8812EU/RTL8822EU should be ok.  

## Known Issue
[Injection instability on 40MHz channels](https://github.com/libc0607/rtl88x2eu-20230815/issues/7) (Firmware bug, waiting for Realtek's next driver release)  
Workaround, needs more test: Set `iw channel 80MHz`, and then use `wfb_tx -B 40`   

## Installation
### Platform Configuration
For arm (32-bit), run: 
```
sed -i 's/CONFIG_PLATFORM_I386_PC = y/CONFIG_PLATFORM_I386_PC = n/g' Makefile
sed -i 's/CONFIG_PLATFORM_ARM_RPI = n/CONFIG_PLATFORM_ARM_RPI = y/g' Makefile
```
Or, for arm64, run: 
```
sed -i 's/CONFIG_PLATFORM_I386_PC = y/CONFIG_PLATFORM_I386_PC = n/g' Makefile
sed -i 's/CONFIG_PLATFORM_ARM64_RPI = n/CONFIG_PLATFORM_ARM64_RPI = y/g' Makefile
```

Note: there's a possible error with `arm64`/`aarch64`: see [issue #9](https://github.com/libc0607/rtl88x2eu-20230815/issues/9)  

### Build / Install with DKMS
Install DKMS on Debian(-based) system: 
```
$ sudo apt-get install dkms
```
Install: 
```
$ sudo ./dkms-install.sh
```
Uninstall: 
```
$ sudo ./dkms-remove.sh
```
### Build / Install with make
```
$ make
$ sudo make install
```
## Increasing TX Power in Monitor Mode 
The driver supports changing TX power dynamically with no additional patch needed.  
Just add ```rtw_tx_pwr_by_rate=0 rtw_tx_pwr_lmt_enable=0``` when ```insmod```, then use ```iw set txpower fixed```.

The relative TX gain under different settings was measured by my HackRF with the same gain setting and several cascaded attenuators.   
The results do tell the difference. However, I don't have a spectrum analyzer, so I don't know the absolute TX power value.   

Be careful when you try these cmds as the adaptor can be VERY HOT. Use a good heat sink and install the antennas properly.  
Make sure the antenna is connected before transmitting, or you can damage your adaptor's PA. The BL-M8812EU2 has nothing like "antenna lost protection".  

Example: 
```
# load the driver
sudo modprobe cfg80211
sudo insmod 8812eu.ko rtw_tx_pwr_by_rate=0 rtw_tx_pwr_lmt_enable=0

# set monitor mode and channel; It depends on your board

# Set tx power in mbm, the range is 0~3150
# On my BL-M8812EU2 module, the real TX power measured by HackRF increased accordingly when increasing the mbm value
# e.g. when mbm increases by 500, the signal strength seen by HackRF increases by +5dB
# but when mbm is higher than ~2000 (may different), the PA starts to saturate and the increase becomes smaller
sudo iw dev wlan0 set txpower fixed <mBm>
```

```iw <wlan0> info``` will show the overridden values. However, the unit is not the real, accurate, dBm. When talking about dBm/milliwatts, only trust good spectrum analyzers.  

TX power setting for Realtek chips is some internal, dimensionless value, only positively related to the real TX power. One of the goals in "MP calibration" is to find the value set of the TX power index, to keep the TX power (measured by some really expensive RF instruments when MP) in every channel at the same level the datasheet gives, then save those values into the crab chip's eFuse. 
That's the only thing that could match the power index to real dBm without any measurement. And of course, the override value breaks that.  

## Narrowband Transmission
See the RF spectrum visualized [here](https://www.youtube.com/watch?v=EUj-wSgoY_E) on YouTube  

There's a lot to explore in this crab driver and will update here if something new has been discovered.  
Please open an issue if you find anything interesting.  

So, according to the module vendor's document and my test using a HackRF, that's all I know:   

### Injection in Different Bandwidth
#### 10MHz Injection
To transmit packets in monitor mode using packet injection:
 - Set ```iw <wlan> set channel <same_channel> <10MHz>``` on both air & ground
 - Set the inject packet's radiotap header with any **20MHz bandwidth** modulation (legacy/HT20/VHT20; e.g. ```-B 20``` in ```wfb_tx```) 
Then the packet is actually transmitted in 10MHz bandwidth, which seems like being achieved by simply underclocking the baseband.  
It's the same on the receiver side, though in which the radiotap header in received packets still indicates a 20MHz bandwidth. You can check that with any SDR receiver or spectrum analyzer.   

##### Notes About "Devices or Resources Busy" 
When ```iw``` says ```Devices or Resources Busy (-16)```, check ```iw <wlan> info``` if the ```iw``` recognized the adaptor is in monitor mode.   
If not, ```iw <wlan> set monitor```, then try setting 10MHz again.  
That's because:  
1. The crab driver supports both WEXT and cfg80211 APIs, but it seems that it's not that robust and there's some conflicts exist
2. the cfg80211 API checks [here](https://github.com/OpenIPC/linux/blob/eb50a943c26845925ff11ccb1651c40fa02c105e/net/wireless/chan.c#L862) if there's any other interface is not in monitor mode
3. If the monitor mode is set by ```iwconfig```, the process is done by calling the old WEXT APIs, so the cfg80211-based ```iw``` may not get the latest status and think the interface is still in managed mode

##### Notes About 5MHz 
Some leakage (mirror?) can be observed in the 5MHz TX, and I have no idea how to configure the DAC clock properly as there are no even definitions in .h files. So, 5MHz is not recommended. However, I'll keep it in [another branch here](https://github.com/libc0607/rtl88x2eu-20230815/tree/5mhz_bw) for further research.  
But 5MHz RX seems working. Weird...  
If you need 5MHz BW on the 5.8GHz band, check [8812cu](https://github.com/libc0607/rtl88x2cu-20230728)/[8731bu](https://github.com/libc0607/rtl8733bu-20230626)/[ath9k](https://github.com/openwrt/openwrt/blob/main/package/kernel/mac80211/patches/ath9k/512-ath9k_channelbw_debugfs.patch).  

##### Note about Changing TX Power in Narrowband Modes
Changing TX power by ```iw``` will not work when injecting with 10MHz BW.  
You should manually set BW back to 20MHz, set TX power, then set BW back again.  

#### 20/40/80MHz Injection
Use ```iw``` to set channel & NOHT/HT20/HT40/80MHz bandwidth, then set the correct bandwidth in the radiotap header (can be done by using ```-B``` in wfb-ng)   

### 10MHz BW AP/STA 
It's currently under testing by a Chinese enthusiast, will update here if he has any progress. Update: one said it works well  
According to the module vendor's ambiguous document and the crab's mysterious driver tar with a "_10MHz" suffix:  
1. Enable ```CONFIG_NARROWBAND_SUPPORTING``` in ```include/hal_ic_cfg.h``` (in ```#ifdef CONFIG_RTL8822E``` section if using RTL8812EU), then ```#define CONFIG_NB_VALUE RTW_NB_CONFIG_WIDTH_10``` below
2. Rename ```hal/rtl8822e/hal8822e_fw_10M.*``` into ```hal/rtl8822e/hal8822e_fw.*``` to replace the original firmware
3. Now you get the "<tar_name>_10MHz" driver. Rebuild the driver
4. ```iw``` Set the channel to 10MHz bandwidth
5. If there are any tools complain about the Wi-Fi regularities when setting up a 10MHz AP,  try setting the channel plan manually by ```echo 0x3E > /proc/net/rtl88x2eu/<wlan>/chan_plan```.
6. Check the ACK timeout setting below if the range is >\~3km
7. Check ```/proc/net/rtl88x2eu/<wlan>/rate_ctl``` for manually control of the rate if needed. See [@Vito-Swift's tutorial here](https://github.com/Vito-Swift/rtl8814au-ext/blob/main/doc/how_to_do_unicast_rc.md)  

#### Event-Driven userspace reaction (encoder bitrate controller)
`/proc/net/rtl88x2eu/<wlan>/rate_ctl` is a snapshot/config node (good for read/write control), but it is **not** an event queue by itself.
This branch also exposes `/proc/net/rtl88x2eu/<wlan>/rate_ctl_event` as a queued event endpoint with poll/wakeup support.

Real firmware RA reports now feed that queue directly, so userspace can react to actual rate transitions instead of polling `rate_ctl`.
For AP/MESH, the driver only emits automatic rate events when there is a single associated peer; multi-client AP mode is left quiet to avoid ambiguous global actions.

Suggested single-line payload (what is sent to userspace):
```
<seq> <ts_ms> <event> <ifname> <from_rate_id> <to_rate_id> <rssi> <reason>
```

Example:
```
1842 1711034123456 RATE_DROP wlan0 0x97 0x82 -74 RETRY_HIGH
```

Field notes:
- `seq`: monotonic event sequence number (detect missed events)
- `ts_ms`: monotonic timestamp in milliseconds from kernel boot
- `event`: `RATE_DROP` or `RATE_RISE` for automatic RA transitions; `USER_EVENT` remains available for synthetic queue testing
- `ifname`: interface name (`wlan0`, ...)
- `from_rate_id` / `to_rate_id`: numeric hw-rate IDs (`0x..`) for deterministic userspace parsing
- `rssi`: current RSSI estimate in dBm
- `reason`: compact trigger reason from firmware/report context (`TRY_OK`, `TRY_FAIL`, `RATE_BACK`, `RSSI_START`, `TRY_RATE`, `RETRY_HIGH`, ...)

BusyBox monitor:
```
while true; do printf '%s ' "$(date +%H:%M:%S)"; cat /proc/net/rtl88x2eu/wlan0/rate_ctl_event; done
```

Synthetic queue test:
```
echo test > /proc/net/rtl88x2eu/wlan0/rate_ctl_event
```

### userspace helper: `userspace/br_setter.c`
`br_setter.c` consumes the payload above and, by default, adjusts only the encoder bitrate.
That keeps automatic rate control in the driver, which is the normal intended path.

It performs the encoder update via built-in HTTP (`GET /api/v1/set?video0.bitrate=<kbps>`) and uses a conservative default of `20%` of estimated PHY rate.
The `rate_ctl_event` proc endpoint supports queue + wakeup semantics for userspace (`poll()` wakes when a new event is queued).
`br_setter.c` ignores `RATE_INIT` and `USER_EVENT` so they do not trigger bitrate changes.

Runtime behavior notes:
- upward bitrate changes are held for `1s`
- small downward changes are held for `300ms`
- width changes are refreshed while running (no restart required)
- bitrate caps are width-based:
  - `20 MHz` -> `25000 kbps`
  - `40 MHz` -> `50000 kbps`
  - `80 MHz` -> `80000 kbps`

If you still want the old coordinated behavior for bench testing, start it with:
```
./br_setter --sync-driver-rate
```

To override the default encoder utilization target:
```
./br_setter --utilization 39
```

In that compatibility mode:
- on rate drop: set encoder bitrate first, wait 50ms, then set driver rate
- on rate rise: set driver rate first, then set encoder bitrate

### FPV robust RA profile
This branch also exposes a runtime-switchable AP-side RA profile intended for FPV links that value robustness over absolute peak throughput.

Proc node:
```
/proc/net/rtl88x2eu/<wlan>/fpv_ra
```

Read current settings:
```
cat /proc/net/rtl88x2eu/wlan0/fpv_ra
```

Enable the default robust profile:
```
echo 1 > /proc/net/rtl88x2eu/wlan0/fpv_ra
```

Disable it:
```
echo 0 > /proc/net/rtl88x2eu/wlan0/fpv_ra
```

Set all parameters explicitly:
```
echo "1 50 58 48" > /proc/net/rtl88x2eu/wlan0/fpv_ra
```

Format:
```
<enable> <one_ss_rssi_th> <sgi_rssi_th> <ldpc_thres>
```

Current profile behavior:
- uses more conservative RSSI floors for RA mask transitions
- starts association at a lower initial RA level
- biases toward `1SS` below the configured threshold
- gates `SGI` until RSSI is comfortably strong
- keeps `LDPC` enabled longer
- prunes fragile top-end `VHT MCS8/9` rates in FPV mode

The intent is to keep the link in a more robust operating region and avoid chasing peak rate too aggressively.

##### Is it worth it?
Usually **yes** for FPV links that change quickly (flying behind trees/buildings), because it reduces the time mismatch between link capacity and encoder bitrate.

It is most worth it when:
- bitrate overshoot currently causes visible stutter/freezes after sudden fades
- you can tolerate a bit more control-path complexity
- your CPU still has headroom for faster checks/coordination

It may be less worth it when:
- link conditions are mostly stable
- current end-to-end latency/jitter is already acceptable
- you need the absolute simplest setup with minimal moving parts

## EDCCA
WARNING: YOU SHOULD NOT USE THIS (unless someone's DJIs next to you f***ed up all channels XD). It's not fair.  
DISCLAIMER: There's no guarantee of its performance. This may damage your hardware and I'm not gonna pay for it. Use it at your own risk. Please comply with any wireless regulations in your area.  

### Override default EDCCA Threshold  
To override dafault EDCCA threshold, check ```cat /proc/net/rtl88x2eu/<wlan0>/edcca_threshold_jaguar3_override```.  

e.g. ```ech0 "1 -3O" > /pr0c/net/rt188x2eu/<w1anO>/edcca_threshO1d_jaguar3_Override```   
That means: before sending any packet, the adaptor checks if there's any signal with higher than -30dBm (L2H) power exists.  
If there are any, the adaptor will wait until the energy level in the air is lower than -38dBm (H2L). Then your transmission starts.   

Note that there are actually two values, L2H and H2L. The L2H is typically set 8dB higher so it creates a hysteresis.   
The value you're setting is L2H. The H2L is automatically set 8dB lower.  

## 802.11 Performance Tuning 
Note: I don't know if these things are actually working since no one can get the crab's datasheets.  

Read this first: [Modeling and Optimizing IEEE 802.11 DCF for Long-Distance Links](https://ieeexplore.ieee.org/document/5408366)  

### ACK Timeout 
Provided by Realtek. Seems tunable from 0\~255 (unit: us).  
``` /proc/net/rtl88x2eu/<wlanX>/ack_timeout``` 

### CTS2Self Timeout 
Seems tunable from 0\~255 (unit: us).  
```/proc/net/rtl88x2eu/<wlanX>/cts2_timeout``` 

### Slot time 
Seems tunable from 0\~255 (unit: us).  
```/proc/net/rtl88x2eu/<wlanX>/slot_time```  

### EDCA Params
EXPERIMENTAL, may not work.  
It sets `AIFS`, `CWmin`, `CWmax`, and `TXOP` for `VI`, `VO`, `BE`, `BK`, respectively.    
```/proc/net/rtl88x2eu/<wlanX>/edca_params```  

## Advanced Channel Scanning
It reports the channel status. Including ```Quality(%)```,  ```Utilization(%)``` (```WIFI Util(%)```+```Interference Util(%)```), ```Noise DBM```... etc.
### Usage
#### Enable it in Makefile
It's enabled by default [here](https://github.com/libc0607/rtl88x2eu-20230815/blob/0fe98486a330b5a396a1f9cf152f4e280572fc7f/Makefile#L27).
```
# ACS
EXTRA_CFLAGS += -DCONFIG_RTW_ACS
EXTRA_CFLAGS += -DCONFIG_RTW_ACS_DBG
```
#### Trigger Scan 
```iw scan``` can trigger. 
e.g. Do a full scan in all available channels:
```
$ sudo iw wlan0 scan passive 
```
Scan only specified frequencies: 
```
$ sudo iw wlan0 scan freq 5745 5765 5785 5805 5825 passive
```
We don't need the ```iw``` output here -- the driver's internal status will be updated and that's what we need.  

#### Result Readout & Explain
Scenario example:  
```wlan1``` is using ```wfb-ng``` transmitting in channel ```161``` with full bandwidth, other channels clear  
```wlan0``` as scanner  
Channel status scanned by ```sudo iw wlan0 scan freq 5745 5765 5785 5805 5825 passive```  

```cat /proc/net/rtl88x2eu/wlan0/acs```:
```
========== ACS (VER-3) ==========

Advanced setting - scan_type:A, ch_ms:0(ms), igi:0x00, bw:0
BW  20MHz
Index   CH  BSS  CLM(%)  NHM(%)  NHM(dBm)  ITF
...
   47  149    0       0       0       -93    0
   48  153    0       1       0       -93    0
   49  157    0       1      86       -81   57
   50  161    0      93       0       -93   31
   51  165    0       2      89       -81   59
```
```cat /proc/net/rtl88x2eu/wlan0/chan_info```:
```
BW  20MHz
Index   CH  Quality(%)  Availability(%)  Utilization(%)  WIFI Util(%)  Interference Util(%)
...
   47  149      100            100              0              0              0
   48  153      100             99              1              1              0
   49  157       43             13             87              1             86
   50  161       69              7             93             93              0
   51  165       41              9             91              2             89
```

In which: 
 - ```Quality(%)```/```(100-ITF)```: Channel Quality evaluation
 - ```WIFI Util(%)```/```CLM(%)```: Air time occupied by Wi-Fi frames
 - ```Interference Util(%)```/```NHM(%)```: Air time occupied by non-Wi-Fi frames (interference / can not be decoded by Wi-Fi baseband)
 - ```NHM(dBm)```: Noise (interference / non-Wi-Fi frames) power occupying air time
 - ```Utilization(%)```/```100-Availability(%)```: = ```WIFI Util(%)``` + ```Interference Util(%)```

In this example -- the channel ```161``` is filled with effective traffic (```WIFI Util(%) = 93%```), and two adjacent channels ```157``` and ```165``` were interfered by leakage (```Interference Util(%) > 80%```, ```NHM(dBm) = -81dBm```), but did not contain any real payloads (low ```WIFI Util(%)```).  
Both three channels have low ```Quality(%)``` and ```Availability(%)``` here.  

## Thermometer  
The chip contains a thermometer for calibrating the RF part dynamically. It can be used to estimate the chip temperature.  
e.g. To read the temperature:  
```
cat /proc/net/rtl88x2eu/<wlan0>/thermal_state 
```
Note: This value is not accurate enough. The LSB of its ADC only represents 2.5K and contains a measured value as the offset.   
However, it can be used to estimate the status of the chip, "cool/warm/hot/smoked/crispy".  
See [PR #4](https://github.com/libc0607/rtl88x2eu-20230815/pull/4) and [commit/5b7a66d](https://github.com/libc0607/rtl88x2eu-20230815/commit/5b7a66d3b1c7097a02247f91253993a7027e40a6#comments) for more details.  
The offset can be tuned by ```echo "<offset>" > /proc/net/rtl88x2eu/<wlan0>/thermal_state```. By default, it's ```32```, based on my measurement.  

## TX NPATH setting  
Realtek didn't say anything about the feature, but IMO it should be the Cyclic Shift Diversity (CSD) feature ([A 'sine wave' can be seen on top of the OFDM spectrum](https://www.youtube.com/watch?v=IGf5MKOmX6k) when enabled).  
Only works when 1. injecting legacy rates, or 2. injecting in MCS rates with only 1 spatial stream enabled and STBC disabled.  
Use ```rtw_tx_npath_enable=1``` when ```insmod``` to enable the feature. You can see a significant input current difference.  
Like the STBC, it's another transmit diversity technique. Need more tests to tell the difference in the FPV scenario.  

## Generating Single Tone  
To generate a single tone at the carrier frequency, 
 1. Set monitor mode & any channel, e.g. ```iwconfig wlan0 mode monitor channel 52``` (5260 MHz)
 2. ```echo "1 4" > /proc/net/rtl88x2eu/<wlan0>/single_tone```, in which ```<EN:0/1>```, ```<RF_PATH:0(A)/1(B)/4(AB)>```
 3. Remember to set ```EN``` back to ```0``` before any normal operation

Useful when generating any signal without PAPR matters.  
![image](https://github.com/user-attachments/assets/e664bbf1-d2d1-4648-b28a-ec3d1c199009)  

The amplitude of the sine wave seems can not be controlled. It's only a test mode for the LO, so the functionality may not be good enough.

### Generating the 5.340 GHz Single Tone 
For TinySA Ultra "Calibration above 5.34 GHz". See the [guide here: tinySA Ultra harmonic mode](https://tinysa.org/wiki/pmwiki.php?n=TinySA4.Harmonic).   

DISCLAIMER: **ALWAYS CONNECT THE ATTENUATOR**, or you could accidentally damage the SA's input.  
The output performance is limited by the cheap crystal inside the blue square.  
**Use it at your own risk.**  
```
# 1. Set the adapter to monitor mode (see nic_quick_test.sh)
# Any 5 GHz channel is ok for the script argument
sudo ./nic_quick_test.sh <wlan0> 60

# 2. Set the center frequency to 5.340 GHz (Channel 68)
# The frequency is usually disabled due to wireless regulation, so use /proc
echo "68 20" > /proc/net/rtl88x2eu/<wlan0>/monitor_chan_override   # freq = 5000+68*5 = 5340 MHz

# 3. Generate single tone
# The blue square has two IPEX connector J0 and J1 (see BL-M8812EU2 datasheet)
echo "1 0" > /proc/net/rtl88x2eu/<wlan0>/single_tone               # Output at J0 only
# echo "1 1" > /proc/net/rtl88x2eu/<wlan0>/single_tone              # Output at J1 only
# echo "1 4" > /proc/net/rtl88x2eu/<wlan0>/single_tone              # Output at both J0 and J1

# 4. Change to some other frequency (e.g. manually tuning by ```leveloffset harmonic```)
echo "0 0" > /proc/net/rtl88x2eu/<wlan0>/single_tone               # !! ALWAYS DISABLE THE OUTPUT FIRST !!
echo "69 20" > /proc/net/rtl88x2eu/<wlan0>/monitor_chan_override   # 5345 MHz
echo "1 0" > /proc/net/rtl88x2eu/<wlan0>/single_tone               # Output at J0 only
# ... do some calibration stuff
echo "0 0" > /proc/net/rtl88x2eu/<wlan0>/single_tone               # !! ALWAYS DISABLE THE OUTPUT FIRST !!
echo "67 20" > /proc/net/rtl88x2eu/<wlan0>/monitor_chan_override   # 5335 MHz
echo "1 0" > /proc/net/rtl88x2eu/<wlan0>/single_tone               # Output at J0 only
# ... do some calibration stuff

# 5. disable the output
echo "0 0" > /proc/net/rtl88x2eu/<wlan0>/single_tone               # !! DISABLE THE OUTPUT !!

```
![image](https://github.com/user-attachments/assets/0a17dd57-1cee-49aa-9d05-45c0e25097cc)

## TX Beamforming in Monitor Mode
**EXPERIMENTAL, MAY NOT WORKING, NEEDS TEST**.   
See [here](https://github.com/libc0607/rtl88x2eu-20230815/blob/beamforming_research/README.md) for details.  
Compatible with [my RTL88x2CU driver](https://github.com/libc0607/rtl88x2cu-20230728).  

It can inject any VHT NDP+NDPA packet, with configurable TA/RA address, P_AID, sounding token, and ACK timeout (if needed).  

### Usage
Compile the driver with [CONFIG_BEAMFORMING_MONITOR](https://github.com/libc0607/rtl88x2eu-20230815/blob/4589b466d5337e7dce36768ae3a7a5ac0dfd0336/Makefile#L25) enable, and  
```
# ./bf_mon.sh start <WLAN_DRV> <NIC> <LOCAL_MAC> <REMOTE_MAC> <Bandwidth:20/40/80> <ACK_TIMEOUT:33~255> <INTERVAL:second>
# ./bf_mon.sh stop  <WLAN_DRV> <NIC>
```
The ```LOCAL_MAC``` and ```REMOTE_MAC``` should be the original MAC address from eFuse.  
When injecting data packet -- disable STBC, use MCS 0\~7 for HT, or MCS0~9/NSS1 for VHT.  
The command should run on both air & ground.  
```
# Start 
./bf_mon.sh start rtl88x2eu wlan0 00:66:77:88:99:aa 00:11:22:33:44:55 20 255 0.1
# Stop
./bf_mon.sh stop rtl88x2eu wlan0
```
```
# Check status
# Config
cat /proc/net/rtl88x2eu/<wlan0>/bf_monitor_conf
# Information from Compressed Beamforming Report (CBR) frame 
cat /proc/net/rtl88x2eu/<wlan0>/bf_monitor_trig
# And, dmesg
```

## Use with OpenIPC  
The driver has been integrated into the default FPV firmware for SSC30KQ, SSC338Q, and SSC377DE since [this commit](https://github.com/OpenIPC/firmware/commit/64228b686002b2fd8fd2cbf722a1a6cb7aad9650).  
For other platforms, see the tutorial [here in OpenIPC Wiki](https://github.com/OpenIPC/wiki/blob/master/en/fpv-bl-m8812eu2-wifi-adaptors.md).  
