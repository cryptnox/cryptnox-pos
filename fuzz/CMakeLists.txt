cmake_minimum_required(VERSION 3.16)
project(cryptnox_pos_fuzz CXX C)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# All harnesses are single-TU: each #includes the production code under test.
# eth_addr is self-contained (only main/); eth_rlp and eth_rpc_json route their
# copies through CW_Utils::safe_memcpy, so they also need the SDK include path
# (and eth_rpc_json additionally needs cJSON).

set(SDK_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/../cryptnox-sdk-esp32/cryptnox-sdk-cpp")

set(FUZZ_FLAGS -fsanitize=fuzzer,address -g -O1 -Wall -Wno-unused-function)

# ── Target 1: eth_rlp encoder (needs CW_Utils for safe_memcpy) ────────
add_executable(fuzz_eth_rlp fuzz_eth_rlp.cpp)
target_include_directories(fuzz_eth_rlp PRIVATE "${SDK_DIR}")
target_compile_options(fuzz_eth_rlp PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_eth_rlp    PRIVATE -fsanitize=fuzzer,address)

# ── Target 2: hex address parser (now pulls in keccak256 for EIP-55) ──
# eth_addr.cpp verifies the EIP-55 checksum via keccak256, which routes its
# squeeze through CW_Utils::safe_memcpy — so, like the others, this harness
# #includes CW_Utils.cpp and needs the SDK include path.
add_executable(fuzz_parse_address fuzz_parse_address.cpp)
target_include_directories(fuzz_parse_address PRIVATE "${SDK_DIR}")
target_compile_options(fuzz_parse_address PRIVATE ${FUZZ_FLAGS})
target_link_options(fuzz_parse_address    PRIVATE -fsanitize=fuzzer,address)

# ── civil_time self-check (not a fuzzer: plain assert main()) ─────────
# Self-contained (stdint/stdio/string only), so it needs no include paths.
add_executable(test_civil_time test_civil_time.cpp)
target_compile_options(test_civil_time PRIVATE -fsanitize=address -g -O1 -Wall)
target_link_options(test_civil_time    PRIVATE -fsanitize=address)

# ── Target 3: JSON-RPC response parser (needs cJSON + CW_Utils) ───────
# cJSON ships with ESP-IDF; locate it via $IDF_PATH.
if(DEFINED ENV{IDF_PATH})
    set(CJSON_DIR "$ENV{IDF_PATH}/components/json/cJSON")
endif()

if(EXISTS "${CJSON_DIR}/cJSON.c")
    add_executable(fuzz_eth_rpc_json
        fuzz_eth_rpc_json.cpp
        "${CJSON_DIR}/cJSON.c")
    target_include_directories(fuzz_eth_rpc_json PRIVATE
        "${SDK_DIR}"        # CW_Utils.h / CW_Utils.cpp
        "${CJSON_DIR}")     # cJSON.h
    target_compile_options(fuzz_eth_rpc_json PRIVATE ${FUZZ_FLAGS})
    target_link_options(fuzz_eth_rpc_json    PRIVATE -fsanitize=fuzzer,address)
else()
    message(WARNING
        "cJSON not found (set IDF_PATH, or CJSON_DIR=<dir with cJSON.c>) — "
        "skipping fuzz_eth_rpc_json. The other two targets still build.")
endif()
