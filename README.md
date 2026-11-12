[![Version](https://img.shields.io/github/v/tag/laobamac/itlwm?style=flat&label=Downloads)](https://github.com/laobamac/itlwm/releases)
# itlwm

**An Intel Wi-Fi Adapter Kernel Extension for macOS, based on the OpenBSD Project.**

## Documentation

We highly recommend exploring our documentation before using this Kernel Extension:

- [Intro](https://OpenIntelWireless.github.io/itlwm)
- [Compatibility](https://openintelwireless.github.io/itlwm/Compat)
- [FAQ](https://openintelwireless.github.io/itlwm/FAQ)

## AirportItlwm on macOS Sequoia

The `AirportItlwm-Sequoia` target supports macOS Sequoia 15.2 and later on x86_64
through the native Wi-Fi interface. It has been tested on macOS 15.8 (24H23).
Sequoia and Tahoe use separate ABI headers and build products.

With the same MacKernelSDK checkout described below:

```sh
xcodebuild -project itlwm.xcodeproj -scheme AirportItlwm-Sequoia \
  -configuration Release ARCHS=x86_64 CODE_SIGNING_ALLOWED=NO build
```

Use this kext only on Darwin 24.2.0–24.99.99. Keep the Tahoe kext restricted to
Darwin 25.x when both systems share an OpenCore configuration. HE remains opt-in
with `itlwm_he=1`, and the Tahoe feature limitations below also apply to Sequoia.

## AirportItlwm on macOS 26




This fork supports macOS Tahoe 26.x (x86_64) through the native Wi-Fi interface,
including scanning, connecting, network switching, and private Wi-Fi addresses.

- **Wi-Fi 6 (802.11ax / HE) is disabled by default.**
  Add `itlwm_he=1` to enable it on supported adapters.
- WPA2-Personal and WPA2/WPA3 transition networks with optional PMF are supported.
  WPA3-only, mandatory PMF, and MLO are not supported.
- **AWDL / AirDrop is supported** through a host-side AWDL stack (see below).
- Device support follows the upstream hardware table; Tahoe compatibility still
  requires validation on each adapter.

Download Sequoia or Tahoe **Release** and **Debug** builds from [this fork's Releases](https://github.com/laobamac/itlwm/releases).
CI builds both systems and updates the alpha release after all builds pass on `main`.

To build locally, install Xcode and Python 3, and check out MacKernelSDK commit
`3f750085caa17ec3a7880f11c11bf4f48cd6a164` in `MacKernelSDK/`:

```sh
xcodebuild -project itlwm.xcodeproj -scheme AirportItlwm-Tahoe \
  -configuration Release ARCHS=x86_64 CODE_SIGNING_ALLOWED=NO build
```

## AWDL / AirDrop on macOS 26 (this fork's work)

macOS 26 moved the AWDL stack into `IO80211Family`, so the driver must provide
its own `IO80211AWDLProtocol` subclass (`awdl0`) and do in the host what the
Broadcom firmware used to do: availability windows, AWDL action frames, P2P
device address, and channel sequencing. On top of that stack, this fork adds
three layers measured against real AirDrop traffic:

**1. Bidirectional dwell hold** — while a data transfer is running, channel
dwell requests are deferred (`EBUSY`) instead of yanking the radio off the
station channel mid-transfer. The busy test counts bytes on *both* directions:
a receive-heavy flow (iPhone → Mac) is mostly ACKs on the TX side, so a
TX-only test would keep "discovering" the radio as idle and starve the
receiver. Fairness cap: at most 2 s of consecutive holds, then one dwell is
forced so the peer never sees the Mac vanish.

**2. Idle presence dwell** — with no live peer the home-branch slots used to
be skipped entirely, leaving only the 2.4 GHz social-channel slots: the Mac
announced itself on channel 6 while an idle iPhone sits on the station's
channel, and neither hears the other. That is the "first send sits on
Waiting" failure. Now, when the peer is idle or absent, a low-rate (500 ms)
presence dwell keeps sync/MI frames going out on the station channel — no
channel switch, no band change, data gate stays closed.

**3. Cross-band channel clamp** — each AWDL sequence still contains one
2.4 GHz social slot; on a 5 GHz station this costs a PHY REMOVE + ADD round
trip (measured 23 ms deaf, 53-58 ms when it overlaps traffic, twice per
sequence). The AirDrop control exchange runs below the hold threshold, so it
hit those windows unprotected: captures show 2 of 3 SYNs lost, a 4.3 kB SACK
hole, SYN-ACKs retransmitted 4 times, and the station link dropping outright.
Cross-band slots are now folded into the station channel (same-band switches
like 149 ↔ 44 are kept); the announced channel-sequence TLV is clamped the
same way, so peers hear what we actually do.

Everything above is runtime-adaptive: the clamp target is the station's
*current* channel (`ic_bss`), so it follows the router — nothing is hardwired
to 149.

**Observed results** (AX200, macOS 26.7.2): discovery becomes instant, the
first iPhone → Mac send no longer stalls on "Waiting", control-plane QUIC
handshake answers in ~5 ms, and Wi-Fi no longer drops when AirDrop is active.
Measured throughput: Mac → iPhone 26-29 MB/s, iPhone → Mac 10-17 MB/s
(previously 6).

**Diagnostics** (`ioreg -l | grep AWDL`): `AWDLTxHoldSkips`,
`AWDLTxHoldForced`, `AWDLStaTxBytes`, `AWDLRocBandChanges`,
`AWDLPresenceDwells`, `AWDLChanClamps`, `AWDLChanSeq`, `AWDLPeerChanSeq`.

**Boot-args** (all switches are off-switches; features are on by default):

| Boot-arg | Effect |
|---|---|
| `itlwm_he=1` | Enable Wi-Fi 6 (HE) |
| `-itlawdlnostahold` | Disable the bidirectional dwell hold (A/B) |
| `-itlawdnopresence` | Disable the idle presence dwell (A/B) |
| `-itlawdnochanclamp` | Disable the cross-band channel clamp (A/B) |

## Questions and Issues

Check out our [FAQ Page](https://openintelwireless.github.io/itlwm/FAQ) for more info.

If you have other questions or feedback, feel free to [![Join the chat at https://gitter.im/OpenIntelWireless/itlwm](https://badges.gitter.im/OpenIntelWireless/itlwm.svg)](https://gitter.im/OpenIntelWireless/itlwm?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge).

We only accept bug reports in GitHub Issues, before opening an issue, you're recommended to reconfirm it with us on [Gitter](https://gitter.im/OpenIntelWireless/itlwm); once it's confirmed, please use the provided issue template.

## Credits

- [Acidanthera](https://github.com/acidanthera) for [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK)
- [Apple](https://www.apple.com) for [macOS](https://www.apple.com/macos)
- [AppleIntelWiFi](https://github.com/AppleIntelWiFi) for [Black80211-Catalina](https://github.com/AppleIntelWiFi/Black80211-Catalina)
- [ErrorErrorError](https://github.com/ErrorErrorError) for UserClient bug fixes
- [Intel](https://www.intel.com) for [Wireless Adapter Firmwares](https://www.intel.com/content/www/us/en/support/articles/000005511/network-and-io/wireless.html) and [iwlwifi](https://wireless.wiki.kernel.org/en/users/drivers/iwlwifi)
- [Linux](https://www.kernel.org) for [iwlwifi](https://wireless.wiki.kernel.org/en/users/drivers/iwlwifi)
- [mercurysquad](https://github.com/mercurysquad) for [Voodoo80211](https://github.com/mercurysquad/Voodoo80211)
- [OpenBSD](https://openbsd.org) for [net80211, iwn, iwm, and iwx](https://github.com/openbsd/src)
- [pigworlds](https://github.com/OpenIntelWireless/itlwm/commits?author=pigworlds) for DVM devices support, MIRA bug fixes, and Tx aggregation for MVM Gen 1 devices
- [rpeshkov](https://github.com/rpeshkov) for [black80211](https://github.com/rpeshkov/black80211)
- [usr-sse2](https://github.com/usr-sse2) for implementing the usage of Apple RSN Supplicant and bug fixes
- [zxystd](https://github.com/zxystd) for developing [itlwm](https://github.com/OpenIntelWireless/itlwm)

## Acknowledgements

- [@penghubingzhou](https://github.com/startpenghubingzhou)
- [@Bat.bat](https://github.com/williambj1)
- [@iStarForever](https://github.com/XStar-Dev)
- [@stevezhengshiqi](https://github.com/stevezhengshiqi)
- [@DogAndPot](https://github.com/DogAndPot) for providing resources and help for system configuration
- [@Daliansky](https://github.com/Daliansky) for providing Wi-Fi cards
