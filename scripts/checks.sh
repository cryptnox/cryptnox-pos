#!/usr/bin/env bash
# Every check that needs no hardware, in one command. Run from the repo root:
#
#   bash scripts/checks.sh          # host tests + page checks
#   bash scripts/checks.sh --build  # ...and the firmware build (slow)
#
#   SANITIZE="-fsanitize=address,undefined -fno-sanitize-recover=all" bash scripts/checks.sh
#                                   # every host test under ASan + UBSan (what CI runs)
#
# What is NOT here: anything needing the panel in your hands. Those are the numbered
# sections of docs/testing-provisioning.md and docs/ota-testing.md.
set -uo pipefail

SAN=${SANITIZE:-}

fail=0
step() { printf '\n\033[1m== %s ==\033[0m\n' "$1"; }
ok()   { printf '  ok    %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fail=1; }

step "host unit tests"
TESTS="test_eth_addr test_hardening test_tron_tx test_prov_form test_addr_check
       test_json_out test_ota_version test_card_status test_chain
       test_rpc_error test_networks test_assets test_touch_cal test_eth_hex
       test_eth_rlp test_money test_settings_rules"
out=$(mktemp -d)
for t in $TESTS; do
  if g++ -std=c++14 -Wall $SAN -Imain -Icryptnox-sdk-esp32/cryptnox-sdk-cpp \
         "tests/units/$t.cpp" -o "$out/$t" 2>"$out/$t.log" && "$out/$t" >/dev/null; then
    ok "$t"
  else
    bad "$t"; sed 's/^/        /' "$out/$t.log"
  fi
done

# Lives with the fuzz harnesses, but needs nothing they need.
if g++ -std=c++14 -Wall $SAN fuzz/test_civil_time.cpp -o "$out/test_civil_time"        2>"$out/test_civil_time.log" && "$out/test_civil_time" >/dev/null; then
  ok "test_civil_time"
else
  bad "test_civil_time"; sed 's/^/        /' "$out/test_civil_time.log"
fi

# The receipt check parses JSON, and cJSON ships with ESP-IDF rather than here.
CJSON="${IDF_PATH:-/c/esp/v5.5.4/esp-idf}/components/json/cJSON"
if [ -f "$CJSON/cJSON.c" ]; then
  if gcc $SAN -c "$CJSON/cJSON.c" -o "$out/cJSON.o" 2>"$out/cjson.log" &&
     g++ -std=c++14 -Wall $SAN -Imain -Icryptnox-sdk-esp32/cryptnox-sdk-cpp -I"$CJSON" \
         tests/units/test_eth_receipt.cpp "$out/cJSON.o" -o "$out/test_eth_receipt" \
         2>"$out/test_eth_receipt.log" && "$out/test_eth_receipt" >/dev/null; then
    ok "test_eth_receipt"
  else
    bad "test_eth_receipt"; sed 's/^/        /' "$out/test_eth_receipt.log" "$out/cjson.log"
  fi
else
  printf '  skip  test_eth_receipt (no cJSON: set IDF_PATH)\n'
fi

# The recovery bit is secp256k1 arithmetic on mbedTLS — ESP-IDF's own copy, built
# for the host with its default config (which has the curve and the ECP module).
MBT="${IDF_PATH:-/c/esp/v5.5.4/esp-idf}/components/mbedtls/mbedtls"
if [ -f "$MBT/library/ecp.c" ]; then
  mbt_ok=1
  for f in bignum bignum_core bignum_mod bignum_mod_raw constant_time ecp \
           ecp_curves ecp_curves_new platform_util platform; do
    gcc $SAN -O1 -c -I"$MBT/include" -I"$MBT/library" "$MBT/library/$f.c" \
        -o "$out/mbt_$f.o" 2>>"$out/mbedtls.log" || mbt_ok=0
  done
  if [ "$mbt_ok" = 1 ] &&
     g++ -std=c++14 -Wall $SAN -Imain -Icryptnox-sdk-esp32/cryptnox-sdk-cpp -I"$MBT/include" \
         tests/units/test_eth_sig.cpp "$out"/mbt_*.o -o "$out/test_eth_sig" \
         2>"$out/test_eth_sig.log" && "$out/test_eth_sig" >/dev/null; then
    ok "test_eth_sig"
  else
    bad "test_eth_sig"; sed 's/^/        /' "$out/test_eth_sig.log" "$out/mbedtls.log" 2>/dev/null
  fi
else
  printf '  skip  test_eth_sig (no mbedTLS: set IDF_PATH)\n'
fi

step "fuzz corpus replay"
# Each harness once over its seeds, through replay_main.cpp instead of libFuzzer,
# so the corpora are a regression test without clang. CI also fuzzes them.
SDK=cryptnox-sdk-esp32/cryptnox-sdk-cpp
for h in eth_rlp parse_address; do
  if g++ -std=c++14 -Wall $SAN -I$SDK "fuzz/fuzz_$h.cpp" fuzz/replay_main.cpp          -o "$out/replay_$h" 2>"$out/replay_$h.log" &&
     "$out/replay_$h" fuzz/corpus/$h/* >/dev/null 2>>"$out/replay_$h.log"; then
    ok "fuzz_$h"
  else
    bad "fuzz_$h"; sed 's/^/        /' "$out/replay_$h.log"
  fi
done
if [ -f "$out/cJSON.o" ]; then
  if g++ -std=c++14 -Wall $SAN -I$SDK -I"$CJSON" fuzz/fuzz_eth_rpc_json.cpp          fuzz/replay_main.cpp "$out/cJSON.o" -o "$out/replay_eth_rpc_json"          2>"$out/replay_eth_rpc_json.log" &&
     "$out/replay_eth_rpc_json" fuzz/corpus/eth_rpc_json/* >/dev/null 2>>"$out/replay_eth_rpc_json.log"; then
    ok "fuzz_eth_rpc_json"
  else
    bad "fuzz_eth_rpc_json"; sed 's/^/        /' "$out/replay_eth_rpc_json.log"
  fi
else
  printf '  skip  fuzz_eth_rpc_json (no cJSON: set IDF_PATH)
'
fi

step "config portal page"
# The extractor doubles as the id-wiring check, and emits the script for the
# render test so there is one extractor rather than two that could disagree.
mkdir -p build
# Not a bare `python`: Windows has it under three names and WSL has none of them,
# so the shell you happen to be in decided whether this check ran at all.
PY=$(command -v python || command -v python3 || command -v py || true)
if [ -z "$PY" ]; then
  bad "no python found (tried python, python3, py)"
elif "$PY" tools/check_portal_page.py --emit-js build/portal_page.js; then
  ok "parses, ids wired"
  if command -v node >/dev/null; then
    if node tools/test_portal_render.js build/portal_page.js; then
      ok "render logic"
    else
      bad "render logic"
    fi
  else
    printf '  skip  render logic (node not found)\n'
  fi
else
  bad "page extraction / parse"
fi

step "UI strings export"
# Only that the extractor still finds what it should and skips what it should:
# the sheet itself is reviewed by eye, and `--audit` says what was dropped.
if [ -z "$PY" ]; then
  bad "no python found"
elif "$PY" tools/export_strings.py --selftest >/dev/null; then
  ok "extractor"
else
  bad "extractor"; "$PY" tools/export_strings.py --selftest 2>&1 | sed 's/^/        /'
fi

if [ "${1:-}" = "--build" ]; then
  step "firmware build"
  # idf.py will not run in Git Bash; the .bat sets up the environment. Called
  # directly, not through `cmd //c` — that double slash is a Git Bash idiom and
  # was a silent no-op anywhere else. Warnings from TFT_eSPI about the reset and
  # touch pins are expected — see sdkconfig.defaults.
  if ./scripts/idf-build.bat 2>&1 |
       grep -E "error:|FAILED|binary size|Project build complete" |
       grep -v "TFT_config.h\|TFT_eSPI.h"; then
    grep -q . /dev/null   # keep the pipeline's exit status out of it
    ok "built"
  else
    bad "build"
  fi
fi

printf '\n'
[ "$fail" -eq 0 ] && echo "all checks passed" || echo "CHECKS FAILED"
exit "$fail"
