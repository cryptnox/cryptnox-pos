# Unit tests — cryptnox-pos

Host-side unit tests. Each is a single translation unit that `#include`s the
production sources directly (same pattern as [`fuzz/`](../../fuzz)), so a test
can never drift from the firmware and no build system is required.

| Test               | Under test                                     |
|--------------------|------------------------------------------------|
| `test_eth_addr`    | `main/eth_addr.cpp` — hex parsing + EIP-55, both ways |
| `test_hardening`   | `main/hardening.h` — decision-integrity gate   |
| `test_tron_tx`     | `main/tron_tx.cpp` — Tron varints, TransferContract + TRC-20 checks, envelope |
| `test_prov_form`   | `main/form_parse.h` — config-portal urlencoded field extraction |
| `test_addr_check`  | `main/addr_check.h` — structural Tron address check |
| `test_json_out`    | `main/json_out.h` — escaping an SSID into a JSON response |
| `test_ota_version` | `main/ota_version.h` — update vs downgrade ordering |
| `test_card_status` | `main/card_status.h` — is the tapped card initialised and seeded |
| `test_chain`       | `main/settings.h` — which chain selections are Tron, which Polygon |
| `test_rpc_error`   | `main/rpc_error.h` — a node's refusal turned into an operator instruction |
| `test_networks`    | `main/config.h` — every contract parses (EIP-55) and mainnet/testnet chain ids differ |
| `test_assets`      | `main/assets.h` — one complete descriptor row per asset |
| `test_touch_cal`   | `main/touch_cal.h` — calibration arithmetic and its refusals |
| `test_eth_hex`     | `main/eth_json.cpp` — JSON-RPC QUANTITY → balance, no silent wrap |
| `test_eth_receipt` | `main/eth_json.cpp` — a receipt counts only for our tx, contract, payee and amount (needs cJSON) |
| `test_eth_sig`     | `main/eth_sig.cpp` — the recovery bit, computed locally (needs mbedTLS) |
| `test_civil_time`  | `fuzz/test_civil_time.cpp` — date arithmetic for the clock checks |
| `test_eth_rlp`     | `main/eth_rlp.cpp` — EIP-1559 bytes, unsigned and signed, against eth-account |
| `test_money`       | `main/money.h` — keypad cents, units to wei, fees, funds check, USDC calldata |
| `test_settings_rules` | `main/settings_rules.h` — time zone, fee bounds, admin digest, address form |

`test_eth_receipt` and `test_eth_sig` link against ESP-IDF's own cJSON and
mbedTLS sources; `checks.sh` finds them through `IDF_PATH` and skips the two
tests when it is unset.

Run all of them, plus the config portal page checks, with:

```sh
bash scripts/checks.sh            # add --build for the firmware build too
```

Or one at a time, from the repo root (any C++14 compiler):

```sh
for t in test_eth_addr test_hardening test_tron_tx test_prov_form test_addr_check \
         test_json_out test_ota_version test_card_status test_chain \
         test_rpc_error test_networks test_assets test_touch_cal test_eth_hex \
         test_eth_rlp test_money test_settings_rules; do
  g++ -std=c++14 -Wall -Imain -Icryptnox-sdk-esp32/cryptnox-sdk-cpp \
      tests/units/$t.cpp -o $t && ./$t || exit 1
done
```

Assertions only — a test either prints `... OK` and exits 0, or aborts.

## The config portal page

The page is a pair of C string literals, so the compiler proves the C is valid and
nothing proves the HTML or the JavaScript is. Two checks in `tools/` cover it, and
`scripts/checks.sh` runs both:

| | |
|---|---|
| `check_portal_page.py` | extracts the literals, runs the script through `node --check`, and asserts every element id is referenced from both sides |
| `test_portal_render.js` | drives `render()` against each `(mode, step, authed, pending)` the device can report, asserting which sections are visible |

The second one matters more than it looks: which sections show is a hand-written
pile of booleans, it decides whether setup can be completed at all, and a mistake
there is invisible until somebody is standing in front of a blank phone.

