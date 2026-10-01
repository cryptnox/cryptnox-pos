# Clock hardening — test procedure

Verifies the three defences that make TLS certificate dates actually count:

| # | Defence | Where |
|---|---------|-------|
| 1 | `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` — enforce `notBefore`/`notAfter` | `sdkconfig.defaults` |
| 2 | Reject a synced clock earlier than the firmware build stamp | `main/net.cpp` — `build_time_floor()` |
| 3 | Corroborate the clock against the authenticated HTTP `Date` header | `main/eth_rpc.cpp` — `clock_corroborated()` |

Shared calendar arithmetic lives in `main/civil_time.{h,cpp}` — a pure unit with
no IDF dependencies, so it builds and self-checks on the host.

**Why all three:** `HAVE_TIME_DATE` alone bases a trust decision on a clock
supplied over unauthenticated NTP. Items 2 and 3 constrain that clock, and they
catch *different directions* of error — see the note under test 4.

---

## Prerequisites

```powershell
cd C:\Cryptnox\cryptnox-pos
. C:\esp\v5.5.4\esp-idf\export.ps1     # puts idf.py on PATH
```

Builds go through `scripts\idf-build-flash.bat COM3` (`idf.py` will not run
under Git Bash). Device assumed on COM3.

---

## Test 0 — Date arithmetic (host, no device)

The cheap gate. Run this on any change to `civil_time.cpp`.

```powershell
cd fuzz; g++ -std=c++14 -o tct test_civil_time.cpp; .\tct.exe; cd ..
```

Expect `civil_time: all checks passed`. Covers leap years (2024 / 2100 / 2000),
month boundaries across a decade, both parsers, and malformed-input rejection.
Reference epochs are cross-checked against Python's `calendar.timegm`.

Also available as a CMake target: `test_civil_time` in `fuzz/CMakeLists.txt`.

## Test 0b — Confirm date checks are compiled in

```powershell
Select-String MBEDTLS_HAVE_TIME_DATE build\config\sdkconfig.h
```

Must print `#define CONFIG_MBEDTLS_HAVE_TIME_DATE 1`.

> **`sdkconfig` is gitignored and an existing value there beats
> `sdkconfig.defaults`.** Anyone who pulls without deleting their `sdkconfig`
> gets a build where item 1 silently does nothing, and items 2–3 guard a check
> that was compiled out. If this prints nothing, delete `sdkconfig` and rebuild.

---

## Test 1 — Normal boot → unchanged

No code changes.

```powershell
.\scripts\idf-build-flash.bat COM3
idf.py -p COM3 monitor        # Ctrl+] to exit
```

Expect:

```
I net: System time synced via SNTP (epoch 17854...)
I eth_rpc: Nonce: 786
I cryptnox_pos: Ready
```

Then run one real payment end-to-end.

The `Date` corroboration logs at DEBUG on success, so a silent pass is the
expected result — a `refusing` or `no Date header` line here is a bug.

*Leaf renewals still accepted* is implied rather than observed: `RPC_CA_CERT_PEM`
pins the GTS WE1 intermediate, so server-side leaf rotation is transparent.
**Re-pin before its `notAfter`, Feb 20 14:00:00 2029 GMT.**

---

## Test 2 — Deliberately wrong clock → TLS refused, boot reports it

Temporary scaffold. In `main/net.cpp` add `#include <sys/time.h>` at the top,
and insert immediately **before** the `/* SNTP is unauthenticated` comment:

```c
{ struct timeval tv; tv.tv_sec = 1893456000; tv.tv_usec = 0;   /* 2030-01-01 */
  (void)settimeofday(&tv, NULL); ESP_LOGW(TAG, "TEMP: clock forced to 2030"); }
```

2030 is past the pinned intermediate's `notAfter`, and is *forward* of the build
stamp so it deliberately passes the item-2 floor — this isolates item 1.

Flash and monitor. Expect:

```
E esp-tls-mbedtls: mbedtls_ssl_handshake returned -0x2700      (x3 retries)
E esp-tls: Failed to open new connection
E eth_rpc: HTTP open: ESP_ERR_HTTP_CONNECT
E cryptnox_pos: RPC unreachable or clock rejected at boot
```

`-0x2700` is `MBEDTLS_ERR_X509_CERT_VERIFY_FAILED`.

### Control: prove item 1 is load-bearing

Set `# CONFIG_MBEDTLS_HAVE_TIME_DATE is not set` in `sdkconfig`, rebuild, and
re-run. **The handshake succeeds on the expired certificate** — this is the bug
the whole change exists to fix. Restore the flag afterwards and confirm test 0b.

---

## Tests 3 + 4 — one rig, both items

Test 4's back-dated case *is* test 3, exercised over the real SNTP wire path, so
run them together rather than forcing the clock for test 3.

Remove the test-2 scaffold first. Then two temporary edits:

**`main/net.cpp`** — point SNTP at your machine (use your own Wi-Fi IPv4, from
`ipconfig`; the device must be on the same subnet):

```c
esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(1,
    ESP_SNTP_SERVER_LIST("192.168.1.39"));
```

**`CMakeLists.txt`** — before `project(cryptnox_pos)`:

```cmake
idf_build_set_property(COMPILE_DEFINITIONS "SNTP_PORT=11123" APPEND)
```

The port override is what lets this run **without Administrator**: `w32time`
holds UDP/123 exclusively, and freeing it needs elevation. lwIP guards
`SNTP_PORT` with `#if !defined`, so a global define moves both the client's
local bind and its destination. Nothing else about the SNTP path changes.

Flash, then run the hostile server — ordinary shell, one case per board reset:

```powershell
python -u tools\spoof_ntp.py --days -200  --bind 192.168.1.39 --port 11123   # Test 3
python -u tools\spoof_ntp.py --minutes 30 --bind 192.168.1.39 --port 11123   # Test 4
```

### Test 3 — clock forced before the build date → sync refused

```
E net: SNTP time 1768125504 precedes firmware build floor 1785344045 - refusing (spoofed NTP?)
E net: SNTP time 1768125508 precedes firmware build floor 1785344045 - refusing (spoofed NTP?)
E net: SNTP time 1768125508 precedes firmware build floor 1785344045 - refusing (spoofed NTP?)
E cryptnox_pos: SNTP time sync failed
```

The floor is the build stamp minus 86400 s. `__DATE__`/`__TIME__` are the build
machine's *local* time read back as UTC, so a day of slack absorbs any TZ
offset; it costs an attacker nothing, since reviving an expired certificate
needs weeks of back-dating.

### Test 4 — simulated NTP spoof → detected

```
I net: System time synced via SNTP (epoch 1785407390)
E eth_rpc: clock off by 1800 s vs server (local 1785407393, server 1785405593) - refusing (spoofed NTP?)
E cryptnox_pos: RPC unreachable or clock rejected at boot
```

The server prints `lied to 192.168.1.40: ...` when the device polls it. If it
stays silent the device is not reaching you — check Windows Firewall for
`python.exe` on Private networks, and that the bind IP is still current.

> **Run both cases.** They exercise different defences: back-dating trips the
> item-2 floor at sync time and never reaches TLS; forward skew passes the floor
> and is caught later by the item-3 `Date` header. Either one alone leaves half
> the feature untested.

---

## Cleanup

Revert the server list, the `SNTP_PORT` line, and any clock override, then:

```powershell
Select-String -Path main\*.cpp,CMakeLists.txt -Pattern "TEMP|192\.168|SNTP_PORT|settimeofday"
```

Should return nothing. Rebuild, flash, re-run test 1.

---

## Notes

- **Boot now requires a reachable RPC endpoint.** `app_main` makes one
  `eth_rpc_get_nonce()` call after the sync loop: it proves reachability and is
  the first request whose authenticated `Date` can contradict SNTP. Three
  retries, then the terminal refuses to start. Without it a forward-dated clock
  reaches `Ready` and only fails mid-payment. Costs ~4 s of boot.
- **A missing or unparseable `Date` header skips the check**, it does not fail
  the request. The response already passed pinned-CA TLS, so an absent header
  means the provider omitted it; failing payments over that would be a
  self-inflicted outage. An attacker cannot induce this without breaking TLS.
- **On mismatch the firmware refuses rather than adopting the header's time**,
  so one wrong provider clock can never silently redefine "now".
- `esp_http_client_get_header()` reads *request* headers and can never return
  the server's `Date`. Response headers are only reachable via
  `HTTP_EVENT_ON_HEADER` — see `http_event_cb()` in `main/eth_rpc.cpp`.
