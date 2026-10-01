# Fuzzing — cryptnox-pos parsers

libFuzzer + AddressSanitizer harnesses for the three byte-parsing surfaces in
this firmware. Same pattern as the SDK's `cryptnox-sdk-cpp/fuzz` (single-TU
harness, ASan, corpus dir).

| Harness                  | Target                     | Source under test            |
|--------------------------|----------------------------|------------------------------|
| `fuzz_eth_rlp`           | `eth_rlp_encode_unsigned` / `_signed` | `main/eth_rlp.cpp` |
| `fuzz_parse_address`     | `eth_addr_parse`           | `main/eth_addr.cpp` |
| `fuzz_eth_rpc_json`      | `eth_json_result_string` + `eth_json_receipt_status` | `main/eth_json.cpp` (+ cJSON, `CW_Utils`) |

Each harness `#include`s the production `.cpp` under test directly — **no
copies**, so the fuzzers can never drift from the firmware. The two parsers
that used to be `static` inside ESP-IDF-welded translation units now live in
their own pure units (`eth_addr.cpp`, `eth_json.cpp`) precisely so both the
firmware and the fuzzers share one source.

The `eth_json` parsers are the only ones fed straight from the network (HTTP
response bodies), so `fuzz_eth_rpc_json` is the highest-value target — it runs
both JSON entry points (`result` extraction and receipt classification) over
each input.

## Build

Linux / macOS, or WSL on Windows. Requires `clang` (libFuzzer) and, for the
JSON target only, `IDF_PATH` exported so cJSON can be found.

```sh
cd fuzz
mkdir build && cd build
cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
make                       # builds all available targets
```

If `IDF_PATH` is unset the JSON target is skipped (a warning prints) and the
other two still build. To point at cJSON manually:
`cmake .. -DCJSON_DIR=/path/to/cJSON`.

## Run

```sh
./fuzz_eth_rlp        ../corpus/eth_rlp        -max_len=4096 -jobs=4
./fuzz_parse_address  ../corpus/parse_address  -max_len=64   -jobs=4
./fuzz_eth_rpc_json   ../corpus/eth_rpc_json   -max_len=4096 -jobs=4
```

A crash drops a `crash-<hash>` reproducer in the working directory; replay with
`./fuzz_<target> crash-<hash>`.

## Corpus

Seed files live under `corpus/<target>/`. They are starting points — libFuzzer
mutates and grows them.

- `eth_rpc_json/*_mainnet.json` are live Ethereum mainnet responses captured
  2026-09-30 from a public node: a USDC `Transfer` receipt, a balance, a fee
  history, the USDC `decimals()` call, an unknown-hash `null`, and a node
  refusing a malformed raw transaction. `error_nonce_too_low_geth.json` is
  geth's wording, hand-written.
- `eth_rlp/*.bin` follow the harness layout (six big-endian u64 scalars, the
  20-byte `to`, one parity byte, calldata): USDC `transfer` on mainnet and
  Polygon as the terminal builds them, a native ETH transfer, and all-ones
  scalars.
- `parse_address/` covers EIP-55 mixed case, a single flipped-case checksum
  failure, all-lowercase, one nibble short, and surrounding whitespace.

## Replaying the corpus without clang

`replay_main.cpp` stands in for libFuzzer's `main` and feeds each file to the
harness once, so the corpora double as a regression test with plain g++
(Windows included). `scripts/checks.sh` runs all three this way:

```sh
g++ -std=c++14 -Icryptnox-sdk-esp32/cryptnox-sdk-cpp     fuzz/fuzz_eth_rlp.cpp fuzz/replay_main.cpp -o r && ./r fuzz/corpus/eth_rlp/*
```

CI (`.github/workflows/tests.yml`) runs each harness for 60 s under clang +
ASan and keeps any `crash-*` input as a build artifact.
