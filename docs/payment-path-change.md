# Any card, and a funding check

Why the EVM payment path now takes its sender from the tapped card, why a
balance check could not be added without that, and what each of them changed.

Written against the code in this repo — every claim below names the file that
implements it, so a reader can disagree with the code rather than with this
document.

Scope: the sale path on Ethereum, Polygon and Tron. Not the card applet, not the
setup flow, not the UI.

---

## 1. What the code did before

On Ethereum and Polygon, `sign_and_broadcast()` built the whole transaction
**before the customer tapped**:

```
eth_rpc_select()         from-address = "0x" ADDR_FROM, straight from config.h
eth_rpc_get_nonce()      the nonce of THAT account
build the EIP-1559 tx    chain id, that nonce, fees, gas, recipient, amount
RLP encode + keccak256   the hash that will be signed
── card_connect() ──     only now does the customer tap
verifyPin()
card_sign()              the card signs that hash
ecrecover_parity()       recover the signer, compare against ADDR_FROM
eth_rpc_send_raw_tx()    broadcast
```

The nonce is per-account, so by the time anyone presented a card the
transaction was already committed to one specific account: the one whose
address was compiled into `main/config.h` as `ADDR_FROM`.

Tron never worked this way. `sign_and_broadcast_tron()` connects to the card
first, derives the sender from its public key
(`getPublicKey` → `CW_Tron::addressBytesFromPublicKey`), and only then asks the
node to serialise a transfer — because Tron will not build one without knowing
who is sending.

---

## 2. The two defects that came out of that ordering

### 2.1 Only one card could pay on EVM

A card that was not `ADDR_FROM`'s signed perfectly well — the card has no idea
what address the terminal believes it holds — and was then rejected at
`eth_rpc_ecrecover_parity()`, which recovers the signing address from the
signature and compares it against the configured one. Neither parity matched,
so the sale was declined **after the customer had entered their PIN and
tapped**.

Nothing was broadcast and nothing was spent, but nothing could ever succeed
either. The behaviour was known and documented — the README's troubleshooting
section carried it as an expected error:

> `ecrecover did not match either parity` → `ADDR_FROM` in `config.h` does not
> correspond to the card's `m/44'/60'/0'/0/0` derived key.

What had not been decided was whether it was intended.

### 2.2 Nothing checked whether the payer could fund the sale

There was no balance query anywhere in the firmware, on either chain. An
underfunded card was discovered by the network, at the end of the flow:

| Shortfall | What happened before | Cost to the customer |
|---|---|---|
| Gas, on EVM | The node refused the broadcast; `rpc_error.h` turned its message into a panel line ("Fee too low", "insufficient funds for gas * price + value") | PIN, tap and signature spent on a sale that could not settle |
| Tokens, on EVM | Nothing refused it. The transaction is valid, so it broadcast, mined and reverted | The whole confirmation wait, **and the gas for the failure** |
| TRX, on Tron | `tron_rpc_create_transfer()` will not build a transfer from an account the chain cannot fund, so this failed before signing — by accident, not by design | Reported as `No TRX on <address>` |
| Tokens, on Tron | Nothing refused it. Creating a `TriggerSmartContract` is only serialisation; the node builds it whether or not the account holds the tokens | Signed, broadcast, failed on-chain |

The token cases are the worse half: a refusal is cheap, a mined revert is not.

---

## 3. Why the two could not be fixed separately

A funding check has to know whose funds to check.

On EVM that answer only existed early *because* the payer was hardcoded. A
check placed before the tap would have been asking about the integrator's own
account — not the customer's — and would have cemented the one-card limit
rather than exposing it. Moving the check later without changing the sender
would have been strictly worse than leaving it alone: a later refusal, and
still only one card able to pay.

Tron is what made the choice unavoidable. Any card already worked there and one
card worked on EVM, so the same terminal behaved differently depending on which
asset the operator had selected. Reconciling them is a product decision — a
demo bound to one card, or a terminal that accepts customers — not a code one.

---

## 4. What changed

### 4.1 The EVM sender comes from the card

`sign_and_broadcast()` now connects to the card and verifies the PIN before
anything that depends on the account, then derives the sender the same way the
setup flow already derives a payout address from a card: `keccak256` of the
uncompressed public key at `m/44'/60'/0'/0/0`, low 20 bytes, formatted by
`eth_addr_format()`.

That address is installed with `eth_rpc_set_from()` (`main/eth_rpc.cpp`), which
**copies** the string — unlike `eth_rpc_init()`, whose pointer contract suits a
`config.h` literal and not an address derived into a stack buffer during one
sale. A malformed address is refused rather than ignored, because leaving the
previous payer in force would fetch somebody else's nonce.

`ADDR_FROM` survives only as the address for the boot-time RPC reachability
probe. It is no longer a whitelist of one.

One consequence worth recording: `ETH_RPC_PARITY_MISMATCH` used to mean "wrong
card", because it compared against `config.h`. Both sides now come from the card
that just signed, so a mismatch is an internal inconsistency — the message is
"Signature check failed", and **the README's troubleshooting entry for
`ecrecover did not match either parity` is now out of date.**

### 4.2 Both chains refuse an unfundable sale before signing

| Function | Chain | Asks |
|---|---|---|
| `evm_balance_ok()` | Ethereum, Polygon | `eth_getBalance` for gas (+ the value on a native sale), `balanceOf` over `eth_call` for a token |
| `tron_balance_ok()` | Tron | `/wallet/getaccount` for TRX, `balanceOf` via `/wallet/triggerconstantcontract` for a TRC-20, `/wallet/getaccountresource` for energy |

Both run at the first moment the payer is known — after the tap, before the
signature, before the broadcast. Nothing is spent if the answer is no.

Two rules they share:

- **A read that fails is not a refusal.** These improve a message; they do not
  gate payments. A terminal that stopped selling because one RPC read timed out
  would be worse than the problem being fixed.
- **Refusals name the asset**: "Not enough USDC on the card", "Not enough ETH
  for the network fee".

On Tron the fee test is deliberately narrow — it refuses only when TRX *and*
available energy are both zero. A TRC-20 burn can be paid out of a stake, so an
account with no TRX can still transact; pricing the burn would mean tracking a
network parameter, and getting it wrong refuses sales that would have settled.

### 4.3 One parser, saturating

Balances are `uint256` and the firmware compares them as `uint64`.
`eth_json_hex_quantity()` (`main/eth_json.h`, header-only) saturates at
`UINT64_MAX` rather than wrapping: over-reporting a balance only lets a doomed
sale reach the node that would have refused it anyway, whereas a wrapped value
would refuse a sale that is funded. 20 ETH is past 2^64 wei, so this is the
ordinary case and not a corner. Host-tested in `tests/units/test_eth_hex.cpp`.

---

## 5. What did not change

The card remains the trust anchor. The terminal has never held a private key
and still does not; it never sees a seed. This changes which account the
terminal asks the *network* about — not what the card signs, and not the
security model in [security-analysis.md](security-analysis.md). A fully
compromised terminal still cannot move funds without a cardholder tapping.

Every early exit between `card_connect()` and `card_sign()` closes the card
session; `card_sign()` closes it on its own paths. Seven exits, checked.

---

## 6. What is not proven yet

**No sale has been paid with a card other than the previously compiled-in one.**
That is the single test that demonstrates the change, and it needs a second
Cryptnox card at the terminal. Until then the firmware builds, the host suite
and the portal checks pass, and the image boots on hardware under Secure Boot
v2 — none of which exercises the path this document is about.

The balance refusals are likewise unexercised on hardware.

---

## 7. The open question

If the terminal is meant to be tied to one merchant card rather than to accept
customer cards, §4.1 should be reverted — and the funding check would move back
before the tap with it, since the payer would be known in advance again.

That is a product decision. The code supports either; this document exists so
the decision is made on purpose rather than inherited from an ordering.
