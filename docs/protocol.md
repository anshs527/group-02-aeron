# Protocol contract, schema 421

`schemas/trading.xml` is the source of truth for what is implemented. SBE 1.35.0
generates C++ codecs during the build; handwritten adapters validate before
decoding. These are fixed-length, little-endian messages without variable-length
fields or repeating groups. No compiler struct packing, host byte order, native
pointer, or floating-point representation is placed on the wire.

> **Document status.** Schema version 1 implements the wire layouts of sections
> 3 and 4 and the compatibility policy of section 10 (issue #4, Phase 2,
> 2026-11-12). Byte offsets and sizes below match the generated codecs. Semantic
> decisions written for the joint review with the Matching Rules Specification
> keep their **Draft - pending review** marking until that review signs them off.
> Matching-dependent semantics stay in
> [Open questions](#11-open-questions-for-joint-review). The adapter enforces only
> wire-level rules: ranges, enums, headers and versions. Duplicate handling,
> correlation and recipients are engine and gateway behavior not implemented yet.

## 1. Framing, Ping/Pong and NewOrder

Every message starts with an 8-byte SBE header: `blockLength`, `templateId`,
`schemaId`, `version`, each an unsigned 16-bit integer in that order. Schema ID
is 421, schema version is 1. The body starts at byte 8. Version 1 added
templates 4–8 and the enums of section 3.1; templates 1–3 are byte-identical to
version 0 apart from the header's version field.

| Template | ID | Body bytes | Total bytes | Fields |
| --- | --- | --- | --- | --- |
| Ping | 1 | 8 | 16 | correlationId: uint64 |
| Pong | 2 | 8 | 16 | correlationId: uint64 |
| NewOrder | 3 | 33 | 41 | fields below, tightly encoded |

Ping and Pong are fully integrated into the Aeron IPC test. Correlation IDs run
from 1 through warm-up + measured count, and Pong echoes each ID. Ping requires
the exact outstanding ID; Pong requires the next ID. Correlation IDs are not
wall-clock timestamps. NewOrder and the messages of section 4 are codecs only;
no gateway, matching engine or order submission endpoint is implemented.

| NewOrder field | Body offset | Representation | Units / valid values |
| --- | --- | --- | --- |
| clientId | 0 | uint32, 4 bytes | opaque ID, 1..2^32−2 |
| clientOrderId | 4 | uint64, 8 bytes | client-assigned ID, 1..2^64−2 |
| symbolId | 12 | uint32, 4 bytes | instrument ID, 1..2^32−2 |
| side | 16 | uint8, 1 byte | Buy=1, Sell=2 |
| priceTicks | 17 | int64, 8 bytes | positive integer ticks, 1..2^63−1 |
| quantity | 25 | uint64, 8 bytes | positive integral instrument units, 1..2^64−2 |

Ping/Pong correlation IDs use the same uint64 ID range. Zero is invalid for all
IDs and quantities. The unsigned maximum is reserved as the SBE null sentinel;
Side=255 and any value other than 1/2 are rejected. The signed int64 minimum is
the SBE null sentinel and is rejected; zero and negative prices are also rejected
by this initial cash-equity-like example. Tick size, lot rules, symbol catalog,
client identity allocation, and uniqueness scope require contracts before trading.
The representation deliberately reserves signed price space for later review;
negative prices are not silently accepted today.

## 2. Adapter behavior

Generated setters are not business validation. The adapter enforces ranges and
recognized enums on encode and decode, rejects every short/oversized fixed frame,
and validates all header fields before invoking message decoders. The decoder API
accepts mutable spans because SBE flyweights require `char*`; decoding does not
mutate the buffer. No SBE bounds checks are disabled in project code.

Ping and Pong keep the original strict policy: they accept **only exactly the
current version**, their template and their exact block length. Both smoke-test
peers come from one build, so accepting other versions would add nothing. Order
messages (templates 3–8) follow the version-range policy of section 10. Never
reuse template or field IDs. Incompatible semantic changes need new templates or
schema IDs. Do not equate SBE wire compatibility with application semantic
compatibility.

Every encoder validates its struct and every decoder validates the length,
header and each field; all failures throw `std::invalid_argument`. Enum fields
are decoded from their raw byte, so an unknown code or the 255 null never maps
to a default. `peek_template` reads a header for dispatch: it requires at least
8 bytes, schema 421 and a template from 1 to 8, and leaves version and length
checks to the message decoder.

`tests/protocol_test.cpp` covers Ping/Pong and NewOrder: both side values,
minimum/maximum valid integers, generated accessor values, golden little-endian
Ping bytes, every truncation, header corruption, unknown enum values, nulls, bad
prices and correlations. `tests/order_protocol_test.cpp` covers templates 3–8:
min/max round trips for every message and every enum code, a golden
little-endian OrderExecuted frame, every truncation and oversize, corruption of
each header field, all 256 values of every enum byte, zero/null/non-positive
values of every integer field, encoder-side validation, the compatibility cases
of section 10, and template dispatch.

## 3. Scope of the order protocol draft

**Draft - pending review.** The first order protocol covers one symbol, limit
orders, and cancellations only. Every price is an integer number of ticks and
every quantity an integer number of instrument units; no floating-point value
appears on the wire or in validation. Out of scope for this draft: replace/amend,
market orders, time-in-force variants, market data, session logon, sequencing
and recovery (roadmap tasks 4 and 5). The single-symbol scope still carries
`symbolId` in every message so multi-symbol support does not need a new template.

Commands flow client → gateway → matching engine. Events flow matching engine →
gateway → client. Gateway rejections flow gateway → client without reaching the
engine.

| Message | Template ID | Direction | Body bytes | Total bytes |
| --- | --- | --- | --- | --- |
| NewOrder | 3 (existing) | client → exchange | 33 | 41 |
| CancelOrder | 4 | client → exchange | 24 | 32 |
| OrderAccepted | 5 | exchange → client | 41 | 49 |
| OrderRejected | 6 | exchange → client | 18 | 26 |
| OrderCancelled | 7 | exchange → client | 40 | 48 |
| OrderExecuted | 8 | exchange → client | 58 | 66 |

**Draft - pending review:** template IDs 4–8 as listed. Template 3 keeps its
existing layout unchanged. All frames stay well below the Aeron IPC fragment
size, so no reassembly is needed. Implemented in schema version 1; sizes are
checked by `static_assert` against the generated codecs.

### 3.1 Shared types

Existing types are reused. New types are proposed only where a zero value or an
enumeration is needed.

| Type | Representation | Valid range | Null sentinel | Status |
| --- | --- | --- | --- | --- |
| Identifier32 | uint32 | 1..2^32−2 | 2^32−1 | existing |
| Identifier64 | uint64 | 1..2^64−2 | 2^64−1 | existing |
| PriceTicks | int64 | 1..2^63−1 | −2^63 | existing; upper bound open (Q-4) |
| Quantity | uint64 | 1..2^64−2 | 2^64−1 | existing; upper bound open (Q-3) |
| Side | uint8 enum | Buy=1, Sell=2 | 255 | existing |
| QuantityOrZero | uint64 | 0..2^64−2 | 2^64−1 | Draft - pending review |
| RequestType | uint8 enum | NewOrder=1, CancelOrder=2 | 255 | Draft - pending review |
| RejectReason | uint8 enum | section 7 | 255 | Draft - pending review |
| Liquidity | uint8 enum | Maker=1, Taker=2 | 255 | Draft - pending review |

`QuantityOrZero` exists because `leavesQuantity` is zero once an order is fully
filled. A null sentinel is never a valid value in any field unless a field
explicitly says it is optional. The only optional field is OrderRejected
`symbolId`, encoded with the schema type `OptionalIdentifier32` (uint32,
1..2^32−2, null 2^32−1 means absent; zero is still invalid).

## 4. Message definitions

All offsets are body offsets; add 8 for the frame offset. All fields are
required unless stated otherwise. Every field layout below is **Draft - pending
review** and is implemented in schema version 1 with exactly these offsets,
which were checked against the generated codecs.

### 4.1 NewOrder (template 3, existing)

**Purpose:** submit a limit order to rest or match. Layout and ranges are in
section 1 and are unchanged. `clientOrderId` names the new order and correlates
every response about it.

### 4.2 CancelOrder (template 4)

**Purpose:** request cancellation of the remaining open quantity of one of the
sender's own live orders.

| Field | Offset | Type | Units / valid values |
| --- | --- | --- | --- |
| clientId | 0 | Identifier32 | sending client, 1..2^32−2 |
| clientOrderId | 4 | Identifier64 | ID of this cancel request, 1..2^64−2 |
| origClientOrderId | 12 | Identifier64 | clientOrderId of the order to cancel, 1..2^64−2 |
| symbolId | 20 | Identifier32 | instrument of the target order, 1..2^32−2 |

Draft - pending review: the cancel carries its own `clientOrderId` so that its
response can be correlated independently of the target order. Targeting by
exchange `orderId` instead is open (Q-9).

### 4.3 OrderAccepted (template 5)

**Purpose:** the matching engine has validated the order and assigned it an
exchange `orderId`. It confirms acceptance by the engine, not by the transport.

| Field | Offset | Type | Units / valid values |
| --- | --- | --- | --- |
| clientId | 0 | Identifier32 | owning client |
| clientOrderId | 4 | Identifier64 | echoed from NewOrder |
| orderId | 12 | Identifier64 | exchange-assigned, 1..2^64−2 |
| symbolId | 20 | Identifier32 | echoed from NewOrder |
| side | 24 | Side | echoed from NewOrder |
| priceTicks | 25 | PriceTicks | echoed limit price in ticks |
| quantity | 33 | Quantity | accepted original quantity in units |

Draft - pending review: echoing the order terms lets a client verify the engine
recorded what it sent. Whether this message precedes fills for an order that
matches immediately is open (Q-6).

### 4.4 OrderRejected (template 6)

**Purpose:** a NewOrder or CancelOrder was refused. No book state changed as a
result of the rejected command.

| Field | Offset | Type | Units / valid values |
| --- | --- | --- | --- |
| clientId | 0 | Identifier32 | client bound to the session that sent the command |
| clientOrderId | 4 | Identifier64 | echoed from the rejected command |
| symbolId | 12 | Identifier32 | echoed as received; null allowed (see below) |
| requestType | 16 | RequestType | which command was rejected |
| rejectReason | 17 | RejectReason | section 7 |

Draft - pending review: `symbolId` is optional here and carries null when the
received value was itself out of range. How a command with an unusable
`clientOrderId` is reported is open (Q-12).

### 4.5 OrderCancelled (template 7)

**Purpose:** the remaining open quantity of an order was removed from the book.
The order is terminal afterwards.

| Field | Offset | Type | Units / valid values |
| --- | --- | --- | --- |
| clientId | 0 | Identifier32 | owning client |
| cancelClientOrderId | 4 | Identifier64 | clientOrderId of the CancelOrder that caused this |
| origClientOrderId | 12 | Identifier64 | clientOrderId of the cancelled order |
| orderId | 20 | Identifier64 | exchange orderId of the cancelled order |
| symbolId | 28 | Identifier32 | instrument |
| cancelledQuantity | 32 | Quantity | open quantity removed, units, ≥ 1 |

Draft - pending review: `cancelledQuantity` is only the quantity still open at
cancel time; previously filled quantity is not included. Exchange-initiated
cancels, which would have no `cancelClientOrderId`, are out of scope (Q-13).

### 4.6 OrderExecuted (template 8)

**Purpose:** report one fill of one order. Each trade produces one report per
participating order.

| Field | Offset | Type | Units / valid values |
| --- | --- | --- | --- |
| clientId | 0 | Identifier32 | owner of the order this report describes |
| clientOrderId | 4 | Identifier64 | that order's clientOrderId |
| orderId | 12 | Identifier64 | that order's exchange orderId |
| symbolId | 20 | Identifier32 | instrument |
| side | 24 | Side | side of the order this report describes |
| tradeId | 25 | Identifier64 | exchange-assigned, shared by both reports of a trade |
| fillPriceTicks | 33 | PriceTicks | execution price in ticks |
| fillQuantity | 41 | Quantity | units filled in this trade, ≥ 1 |
| leavesQuantity | 49 | QuantityOrZero | units still open after this fill; 0 means fully filled |
| liquidity | 57 | Liquidity | Maker for the resting order, Taker for the aggressor |

Draft - pending review: the counterparty's `clientId` and order IDs are never
included. The execution price rule belongs to the matching specification (Q-7).
Invariant for every order: sum of `fillQuantity` + `leavesQuantity` after the
latest fill equals the accepted quantity, and a later cancel removes exactly
`leavesQuantity`.

## 5. Identifiers and correlation

**Draft - pending review.**

| ID | Scope | Assigned by | Appears in |
| --- | --- | --- | --- |
| clientId | global | exchange configuration (Q-11) | every message |
| clientOrderId | per clientId | client | every message |
| origClientOrderId | per clientId | client | CancelOrder, OrderCancelled |
| orderId | global, per engine instance | matching engine | OrderAccepted, OrderCancelled, OrderExecuted |
| tradeId | global, per engine instance | matching engine | OrderExecuted |
| symbolId | global | exchange configuration (Q-2) | every order message |

- **D-1.** `clientOrderId` is unique only within one `clientId`. The pair
  (`clientId`, `clientOrderId`) identifies a command globally. NewOrder and
  CancelOrder share one namespace per client, so a cancel cannot reuse an order's
  ID. The uniqueness window is open (Q-5).
- **D-2.** `orderId` and `tradeId` are assigned by the engine, start at 1, and
  increase strictly within one engine run. Their behavior across restarts is
  task 4 work and is not promised here.
- **D-3.** Correlation: every response to a command echoes that command's
  `clientId` and `clientOrderId` (for OrderCancelled, as `cancelClientOrderId`).
  OrderExecuted is not a response to one command; a client correlates it to its
  order by `clientOrderId` or `orderId`.
- **D-4.** Each command receives exactly one terminal response for the command
  itself: NewOrder gets OrderAccepted or OrderRejected; CancelOrder gets
  OrderCancelled or OrderRejected. Fills are additional events.
- **D-5.** The gateway sets or verifies `clientId` from the session identity and
  rejects a command whose `clientId` does not match (UnknownClient). A client
  cannot act on another client's orders.

## 6. Event recipients

**Draft - pending review.**

| Event | Recipient |
| --- | --- |
| OrderAccepted | owner of the new order |
| OrderRejected | sender of the rejected command |
| OrderCancelled | owner of the cancelled order |
| OrderExecuted | owner of the order described; two reports per trade |

- **D-6.** Order events go only to the owning client. Public trade and book
  information is market data (roadmap task 5) and is not part of this protocol.
- **D-7.** For each trade the engine emits two OrderExecuted reports with the
  same `tradeId`, `fillPriceTicks` and `fillQuantity`. The resting order's owner
  receives one with `liquidity=Maker` and its own IDs. The aggressor's owner
  receives one with `liquidity=Taker` and its own IDs.
- **D-8.** An aggressor that matches several resting orders receives one report
  per trade, in match order, each with decreasing `leavesQuantity`. Each resting
  owner receives only reports for its own orders. A resting order partially
  filled by several aggressors receives one report per trade.
- **D-9.** Within one client, events are delivered in engine emission order.
  No ordering is promised between different clients' streams.

The order in which the engine emits the maker and taker reports of one trade,
and self-trade behavior, are open (Q-8, Q-10).

## 7. Rejection reasons

**Draft - pending review.** Codes are fixed once released and never reused.

| Code | Name | Applies to | Raised by | Meaning |
| --- | --- | --- | --- | --- |
| 1 | MalformedMessage | both | gateway | header, length or encoding invalid but correlation fields readable |
| 2 | UnsupportedVersion | both | gateway | schema version outside the accepted range (section 10) |
| 3 | UnknownClient | both | gateway | clientId missing, null, or not bound to this session |
| 4 | UnknownSymbol | both | gateway | symbolId null or not in the symbol catalog |
| 5 | InvalidSide | NewOrder | gateway | side not Buy or Sell |
| 6 | InvalidPrice | NewOrder | gateway | price null, ≤ 0, above limit, or not a valid tick (Q-2, Q-4) |
| 7 | InvalidQuantity | NewOrder | gateway | quantity null, zero, above cap, or not a lot multiple (Q-2, Q-3) |
| 8 | DuplicateClientOrderId | both | engine | clientOrderId already used by this client within the window |
| 9 | UnknownOrder | CancelOrder | engine | no order with origClientOrderId for this client and symbol |
| 10 | OrderAlreadyDone | CancelOrder | engine | target order is already fully filled or cancelled |
| 11 | Throttled | both | gateway | admission policy refused the command; not retried by the exchange |
| 12 | InternalError | both | either | exchange failed to process; outcome of the command is "not applied" |

Code 0 is invalid and 255 is the null sentinel. Whether `Throttled` is in scope
for the first release is part of Q-14.

## 8. Validation ownership

**Draft - pending review.** The split keeps the engine free of transport concerns
and keeps stateless checks out of the matching hot path.

**Gateway (stateless, per message):**
- Frame length equals header length plus `blockLength` of a known template.
- Header `schemaId` is 421; `version` is in the accepted range; `templateId` is
  a client → exchange template.
- `clientId` matches the session identity.
- Every field is in range and not null; enums are known values.
- `symbolId` exists in the catalog; price and quantity respect tick, lot, cap
  and band rules once those are defined (Q-2, Q-3, Q-4).
- Admission policy (Throttled), if adopted.

**Matching engine (stateful):**
- `clientOrderId` uniqueness for the client (DuplicateClientOrderId).
- Cancel target exists, belongs to the client, and is still live (UnknownOrder,
  OrderAlreadyDone).
- Arithmetic overflow of aggregated book quantities is impossible given the
  gateway's quantity cap; the engine still asserts it (Q-3).

- **D-10.** The engine re-checks nothing the gateway owns except as defensive
  assertions, which fail loudly rather than emit a reject. The engine takes
  validated domain structs, not SBE frames, so matching stays independent of
  transport and encoding.
- **D-11.** The adapter throws `std::invalid_argument` on any invalid field.
  Producing a specific RejectReason requires the gateway to know which check
  failed. That mapping is **not implemented** in Phase 2 and belongs with the
  gateway work; adding it does not change existing Ping/Pong callers.

## 9. Duplicate handling

**Draft - pending review**, subject to Q-5 and Q-15.

- **D-12. Duplicate NewOrder while the original is live:** rejected with
  DuplicateClientOrderId. The original order, its priority and its fills are
  unaffected. The reject echoes the duplicate's `clientOrderId`.
- **D-13. Duplicate NewOrder after the original is terminal:** behavior depends
  on the uniqueness window (Q-5). The proposed default is reject for the life of
  the engine run.
- **D-14. Second CancelOrder for an already cancelled order:** rejected with
  OrderAlreadyDone. No second OrderCancelled is sent.
- **D-15. CancelOrder for an order filled before the cancel is processed:**
  rejected with OrderAlreadyDone. The fills already reported stand.
- **D-16. CancelOrder for an order that never existed:** rejected with
  UnknownOrder.
- **D-17. CancelOrder that reuses a clientOrderId:** rejected with
  DuplicateClientOrderId, because commands share one namespace (D-1).
- **D-18.** No duplicate is ever dropped silently. Every command gets exactly one
  response per D-4.

Transport delivery alone does not provide exactly-once processing; see
architecture.md. Resubmitting after an ambiguous failure is safe only if the
retry semantics chosen in Q-15 say so.

## 10. Schema versioning and compatibility

**Draft - pending review.** Implemented for order messages (templates 3–8) in
schema version 1. Ping and Pong keep the exact-version policy (section 2).

- **D-19.** Schema ID 421 never changes for this protocol family. An
  incompatible change requires a new schema ID.
- **D-20.** Any change to the schema increments `version`. Template IDs, field
  IDs, enum codes and RejectReason codes are never reused or renumbered.
- **D-21.** Allowed compatible changes: append a field to the end of a message
  with `sinceVersion` set to the new version; add a new template; add a new enum
  value. Everything else is incompatible: removing, reordering, retyping or
  resizing a field, changing units or meaning, or narrowing a valid range.
- **D-22. Newer sender, older receiver:** the receiver uses the header
  `blockLength` to find the end of the body, reads only fields it knows, and
  ignores trailing bytes. A frame whose `blockLength` is shorter than the
  receiver's known minimum for that template is malformed.
- **D-23. Older sender, newer receiver:** fields with `sinceVersion` greater
  than the header version are treated as absent and read as null; the receiver
  must define a behavior for each absent field.
- **D-24.** Each component declares the minimum version it accepts. Versions
  below that minimum, or a frame length that disagrees with `blockLength`, are
  rejected (UnsupportedVersion or MalformedMessage) rather than guessed.
- **D-25.** A receiver that sees an unknown enum value or unknown template
  rejects the command; it never maps it to a default.

Implementation details of D-22 to D-24 in schema version 1:

| Template | Minimum accepted version | Reason |
| --- | --- | --- |
| NewOrder (3) | 0 | layout unchanged since version 0 |
| CancelOrder, OrderAccepted, OrderRejected, OrderCancelled, OrderExecuted (4–8) | 1 | introduced in version 1 |

- Frame length must always equal 8 + header `blockLength`.
- For a header version up to the current one, `blockLength` must equal the known
  block exactly, because no field has been appended since any accepted version.
  A shorter or longer block is rejected.
- For a newer header version, `blockLength` must be at least the known block.
  Unknown trailing bytes are ignored and known fields are decoded normally.
- No field yet has a `sinceVersion` later than its message, so D-23's absent
  field behavior has no case to exercise until a field is appended.

Tests cover: NewOrder at version 0 accepted; version 0 with a shorter block
rejected; versions 2 and 65535 with longer blocks and trailing bytes accepted
for every order template; newer versions with a shorter block or a length that
disagrees with `blockLength` rejected; current version with a longer block
rejected; wrong schema ID rejected; version 0 rejected for templates 4–8; and
unknown templates rejected by `peek_template`.

## 11. Open questions for joint review

These are not decided, except Q-1 as noted. None of them changes the wire
layout implemented in version 1, except where a question says a field would be
added or made optional. Each needs agreement with the matching rules owner.

- **Q-1. Version 0 or 1.** Adding templates 4–8 changes the schema. Option A
  keeps version 0, because no order message has been deployed and the existing
  NewOrder layout is unchanged. Option B bumps to version 1, which follows D-20
  strictly and exercises the compatibility rules from the start. Option B also
  changes the Ping/Pong golden bytes and the exact-version test.
  **Applied in Phase 2: Option B, version 1, following D-20.** Confirm at the
  joint review; reverting to version 0 would also need the minimum versions in
  section 10 changed.
- **Q-2. Tick and lot size.** What is the tick size and lot size for the symbol,
  and where does the symbol catalog live? Is the wire price already in ticks
  (current design) with no further tick check, and must quantity be a lot
  multiple?
- **Q-3. Quantity cap.** What is the maximum order quantity? The current range
  reaches 2^64−2, which lets aggregated book depth or cumulative fills overflow
  uint64. A cap such as 2^32 per order, with a bounded order count, would make
  overflow impossible by construction.
- **Q-4. Price bounds.** Is there an upper price limit or band below 2^63−1,
  and are zero or negative prices ever valid?
- **Q-5. clientOrderId uniqueness window.** Unique for the life of an engine
  run, per client session, or only among live orders? Must IDs increase
  monotonically per client?
- **Q-6. Accept before execute.** For an order that matches immediately, is
  OrderAccepted always sent before its first OrderExecuted? Is it sent at all
  for an order that fills completely on entry?
- **Q-7. Execution price.** Is the fill price always the resting order's price?
- **Q-8. Maker/taker report order.** Within one trade, is the maker's report
  emitted before the taker's, or is no order guaranteed?
- **Q-9. Cancel target key.** Should CancelOrder target by `origClientOrderId`
  (current draft), by exchange `orderId`, or accept either?
- **Q-10. Self-trade.** When a client's aggressor would match its own resting
  order, does the trade occur, or is one side cancelled or rejected?
- **Q-11. Client identity.** How are `clientId` values allocated and bound to
  sessions? Does the gateway overwrite `clientId` or only verify it?
- **Q-12. Uncorrelatable commands.** When a frame is too damaged to read
  `clientOrderId`, should the gateway send a reject with a null ID, or count the
  error and disconnect the session? Either way it must not be lost silently.
- **Q-13. Exchange-initiated cancels.** Are unsolicited cancels (end of session,
  self-trade prevention) in scope? If so, `cancelClientOrderId` must be optional.
- **Q-14. Admission control.** Is Throttled needed in the first release, and
  what policy triggers it?
- **Q-15. Duplicate NewOrder: reject or idempotent replay.** Section 9 proposes a
  reject. The alternative is to treat a byte-identical resend as a replay and
  return the original outcome, which supports safe client retry after an
  ambiguous timeout but requires the engine to store prior outcomes.
- **Q-16. Sequencing and timestamps.** Do events carry a per-client sequence
  number for gap detection and an engine timestamp now, or is that deferred to
  task 4? Adding them later is compatible under D-21 but changes every event.
- **Q-17. Partial-fill cancel quantity.** Confirm that OrderCancelled reports
  only the remaining open quantity, as drafted in 4.5.
