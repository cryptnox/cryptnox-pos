#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (c) 2026 Cryptnox SA
"""Known-answer vectors for the money code, from implementations that are not ours.

The host tests in tests/units/ check the firmware's transaction bytes, calldata
and amount arithmetic against literals. Those literals must NOT come from the
code under test — a test that compares an encoder with its own earlier output
proves only that it has not changed. They come from here instead:

  * eth-account  — signs each EIP-1559 transaction and produces the raw
                   (signed) bytes and the signing hash, as a wallet would.
  * rlp          — encodes the unsigned payload, independently of eth-account;
                   the script asserts keccak(unsigned) equals eth-account's hash,
                   so the two libraries have to agree before anything is printed.
  * eth-abi      — the USDC transfer(address,uint256) calldata.
  * pycryptodome — keccak-256 for the admin-code digest.
  * Python ints / Decimal — the unit, wei and fee-ceiling arithmetic, with no
                   64-bit wrap to hide behind.

Run (any venv with the four packages):

    python -m venv kat && kat/Scripts/python -m pip install eth-account eth-abi rlp pycryptodome
    kat/Scripts/python tools/gen_kat_vectors.py

and paste the output into tests/units/test_eth_rlp.cpp, test_money.cpp and
test_settings_rules.cpp. The output is deterministic: RFC 6979 signatures, fixed
keys, fixed salts.
"""

from decimal import Decimal, ROUND_CEILING

import rlp
from Crypto.Hash import keccak
from eth_abi import encode as abi_encode
from eth_account import Account
from eth_account.typed_transactions import TypedTransaction

# A throwaway key from the eth-account documentation. Never funded.
KEY = "0x4c0883a69102937d6231471b5dbb6204fe5129617082792ae468d01a3f362318"
PAYEE = "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed"   # the EIP-55 spec vector
USDC_SEPOLIA = "0x1c7D4B196Cb0C7B01d743Fbc6116a902379C7238"

GWEI = 10 ** 9
WEI_PER_UNIT = 10 ** 12          # 6-decimal keypad units -> 18-decimal wei
UNITS_MAX_NATIVE = 18446744      # POS_AMOUNT_UNITS_MAX_NATIVE
U64 = 2 ** 64


def k256(b):
    return keccak.new(digest_bits=256, data=b).digest()


def c_bytes(name, b, indent="    "):
    out = [f"static const uint8_t {name}[{len(b)}] = {{"]
    for i in range(0, len(b), 12):
        out.append(indent + ", ".join(f"0x{x:02x}" for x in b[i:i + 12]) + ",")
    out.append("};")
    return "\n".join(out)


def usdc_calldata(to, amount):
    return bytes.fromhex("a9059cbb") + abi_encode(["address", "uint256"], [to, amount])


def tx_vector(name, chain_id, nonce, prio, max_fee, gas, to, value, data):
    d = {
        "type": 2, "chainId": chain_id, "nonce": nonce,
        "maxPriorityFeePerGas": prio, "maxFeePerGas": max_fee, "gas": gas,
        "to": to, "value": value, "data": data, "accessList": [],
    }
    signed = Account.sign_transaction(d, KEY)
    unsigned = b"\x02" + rlp.encode([chain_id, nonce, prio, max_fee, gas,
                                     bytes.fromhex(to[2:]), value, data, []])
    sighash = TypedTransaction.from_dict(d).hash()
    assert k256(unsigned) == sighash, "rlp and eth-account disagree on the payload"
    raw = bytes(signed.raw_transaction)
    r = signed.r.to_bytes(32, "big")
    s = signed.s.to_bytes(32, "big")
    print(f"/* {name}: chain {chain_id}, nonce {nonce}, tip {prio}, cap {max_fee},"
          f" gas {gas}, value {value}, v {signed.v} */")
    print(c_bytes(f"{name}_UNSIGNED", unsigned))
    print(c_bytes(f"{name}_SIGHASH", sighash))
    print(c_bytes(f"{name}_R", r))
    print(c_bytes(f"{name}_S", s))
    print(f"static const uint8_t {name}_V = {signed.v};")
    print(c_bytes(f"{name}_SIGNED", raw))
    print()
    return signed


def first_nonce_with_short_r(start, **kw):
    """A nonce whose signature has r < 2^248, so its RLP drops a leading zero."""
    n = start
    while True:
        d = dict(kw, type=2, nonce=n, accessList=[])
        if Account.sign_transaction(d, KEY).r < 2 ** 248:
            return n
        n += 1


def main():
    print("/* ---- tests/units/test_eth_rlp.cpp ---- */")
    print(f"/* key {KEY}, sender {Account.from_key(KEY).address} */\n")

    # 1. 12.50 USDC on Sepolia: the ERC-20 path, as the terminal builds it.
    tx_vector("USDC", 11155111, 7, 20 * GWEI, 30 * GWEI, 100000, USDC_SEPOLIA, 0,
              usdc_calldata(PAYEE, 12_500_000))

    # 2. The largest native sale the keypad allows, on Polygon, at its tip floor;
    #    nonce 0 (RLP empty string).
    tx_vector("POLMAX", 137, 0, 30 * GWEI, 30 * GWEI, 21000, PAYEE,
              UNITS_MAX_NATIVE * WEI_PER_UNIT, b"")

    # 3. One unit of ETH on mainnet, nonce >= 128 (two-byte RLP), and a nonce
    #    chosen so r has a leading zero byte that the encoder must strip.
    kw = dict(chainId=1, maxPriorityFeePerGas=2 * GWEI, maxFeePerGas=500 * GWEI,
              gas=21000, to=PAYEE, value=WEI_PER_UNIT, data=b"")
    n = first_nonce_with_short_r(128, **kw)
    tx_vector("ETHSHORTR", 1, n, 2 * GWEI, 500 * GWEI, 21000, PAYEE, WEI_PER_UNIT, b"")

    print("/* ---- tests/units/test_money.cpp ---- */\n")
    for amt in (0, 1, 12_500_000, U64 - 1):
        print(c_bytes(f"CALLDATA_{amt:x}", usdc_calldata(PAYEE, amt)))
    print()

    print("/* units -> wei at the native ceiling (2^64 = %d) */" % U64)
    for u in (UNITS_MAX_NATIVE - 1, UNITS_MAX_NATIVE, UNITS_MAX_NATIVE + 1):
        w = u * WEI_PER_UNIT
        print(f"/* {u} units = {w} wei, fits uint64: {w < U64} */")
    print()

    print("/* native funds: (have, gas cost, units) -> can pay gas / gas + value */")
    gas = 21000 * 30 * GWEI
    for have, units in ((gas, 0), (gas - 1, 0), (gas + 10 ** 12, 1), (gas + 10 ** 12 - 1, 1),
                        (U64 - 1, UNITS_MAX_NATIVE), (U64 - 1, (U64 - 1 - gas) // WEI_PER_UNIT),
                        (U64 - 1, (U64 - 1 - gas) // WEI_PER_UNIT + 1)):
        need = gas + units * WEI_PER_UNIT
        print(f"/* have {have}, gas {gas}, units {units}: gas {have >= gas},"
              f" all {have >= need} */")
    print()

    print("/* fee ceilings, rounded up to 6 places: (base units, decimals) -> text */")
    cases = [(21000 * 30 * GWEI, 18), (100000 * 500 * GWEI, 18), (21000 * 1 * GWEI, 18),
             (1, 18), (10 ** 12, 18), (10 ** 12 + 1, 18), (100_000_000, 6),
             (0, 18), (U64 - 1, 18)]
    for v, dec in cases:
        q = (Decimal(v) / (Decimal(10) ** dec)).quantize(Decimal("0.000001"),
                                                        rounding=ROUND_CEILING)
        txt = format(q.normalize(), "f") if q != 0 else "0"
        print(f'    {{ {v}ULL, {dec}U, "{txt}" }},')
    print()

    print("/* ---- tests/units/test_settings_rules.cpp ---- */\n")
    salt = bytes(range(16))
    for code in ("123456", "0000", "987654321", "x" * 40):
        # admin_derive keeps at most 32 bytes of the code.
        dig = k256(salt + code.encode()[:32])
        print(f'/* keccak256(00..0f || "{code[:32]}") */')
        print(c_bytes(f"ADMIN_{code[:12]}".replace("x" * 12, "LONG"), dig))


if __name__ == "__main__":
    main()
