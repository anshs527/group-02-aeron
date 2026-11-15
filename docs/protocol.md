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
>
> **Merge blockers.** The version 1 wire layout has no joint sign-off yet, and
> three wire decisions are unresolved. See
> [section 12](#12-merge-blockers-and-review-record). This PR must not merge until
> section 12 records each decision.

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

Handwritten enums use exactly the numeric codes of the generated enums, and
`static_assert` checks each one. Generated setters are not business validation.
The adapter enforces ranges and
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
exchange `orderId` instead is open (Q-9) and is merge blocker B-2.

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
| clientId | 0 | Identifier32 | clientId bound to the sending session, never the received value |
| clientOrderId | 4 | Identifier64 | echoed from the rejected command |
| symbolId | 12 | Identifier32 | echoed as received; null allowed (see below) |
| requestType | 16 | RequestType | which command was rejected |
| rejectReason | 17 | RejectReason | section 7 |

Draft - pending review: `symbolId` is optional here and carries null when the
received value was itself out of range. `clientId`, `clientOrderId` and
`requestType` are required, so the gateway can send this message only for a
correlatable command (section 5.1). Other frames are handled by D-26 and D-27.
Whether `clientOrderId` should become optional instead is merge blocker B-3.

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
cancel time; previously filled quantity is not included. `cancelClientOrderId`
is required, so this layout cannot report an exchange-initiated cancel, which
has no CancelOrder behind it. Whether such cancels are in scope is open (Q-13)
and is merge blocker B-4.

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
- **D-4.** Each correlatable command (section 5.1) receives exactly one
  terminal response for the command itself: NewOrder gets OrderAccepted or
  OrderRejected; CancelOrder gets OrderCancelled or OrderRejected. Fills are
  additional events. A frame that is not correlatable gets no order message,
  because OrderRejected cannot encode it; D-26 and D-27 define what happens
  instead.
- **D-5.** The gateway verifies the received `clientId` against the clientId
  bound to the session. A mismatch, zero or null is rejected with UnknownClient,
  and the reject carries the bound clientId, not the received value. A client
  cannot act on another client's orders.

### 5.1 Commands the gateway cannot answer

**Draft - pending review.** OrderRejected requires a valid `clientId`,
`clientOrderId` and `requestType`. The gateway can therefore reject a command
only when it is **correlatable**, meaning all of the following hold:

1. The session is bound to a clientId (Q-11).
2. The frame has a full 8-byte header with schema ID 421 and template ID 3
   (NewOrder) or 4 (CancelOrder). The template gives `requestType`.
3. The frame is at least 20 bytes long, so `clientOrderId` at body offset 4 is
   present. Both command templates place it there in every accepted version.
4. `clientOrderId` is in 1..2^64−2.

Any other defect of a correlatable command, such as a bad version, a length
that disagrees with `blockLength`, a wrong `clientId` field or an out-of-range
`symbolId`, side, price or quantity, is answered with OrderRejected and the
reason from section 7.

- **D-26. Unusable command ID on a bound session.** Conditions 2 to 4 fail. The
  gateway forwards nothing to the engine and sends no order message. It
  increments a per-session counter of uncorrelatable frames, records the event
  on the asynchronous diagnostic path, and closes the session. The client sees
  the closed session and must treat its outstanding commands as having an
  unknown outcome. The frame is never dropped without that counter and the
  session close.
- **D-27. Unbound session.** Condition 1 fails, so no valid `clientId` exists to
  put in OrderRejected. The gateway forwards nothing to the engine, sends no
  order message, increments the same counter, and closes the session. Until a
  logon protocol exists, the binding comes from gateway configuration (Q-11).

D-26 and D-27 fit the current wire layout. The alternative of a reject with a
null `clientOrderId` would need a wire change and is merge blocker B-3.

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
| 1 | MalformedMessage | both | gateway | header, length or encoding invalid in a correlatable command (section 5.1) |
| 2 | UnsupportedVersion | both | gateway | schema version outside the accepted range (section 10) |
| 3 | UnknownClient | both | gateway | received clientId is zero, null, or differs from the clientId bound to the session; the reject carries the bound clientId. An unbound session gets no reject (D-27) |
| 4 | UnknownSymbol | both | gateway | symbolId null or not in the symbol catalog |
| 5 | InvalidSide | NewOrder | gateway | side not Buy or Sell |
| 6 | InvalidPrice | NewOrder | gateway | price null, ≤ 0, above limit, or not a valid tick (Q-2, Q-4) |
| 7 | InvalidQuantity | NewOrder | gateway; engine for D-28 | quantity null, zero, above cap, or not a lot multiple (Q-2, Q-3); or resting it would overflow an aggregate (D-28) |
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
- Checked arithmetic for every aggregate quantity (D-28).

- **D-10.** The engine re-checks nothing the gateway owns except as defensive
  assertions, which fail loudly rather than emit a reject. The engine takes
  validated domain structs, not SBE frames, so matching stays independent of
  transport and encoding.
- **D-11.** The adapter throws `std::invalid_argument` on any invalid field.
  Producing a specific RejectReason requires the gateway to know which check
  failed. That mapping is **not implemented** in Phase 2 and belongs with the
  gateway work; adding it does not change existing Ping/Pong callers.
- **D-28. Aggregate quantities use checked arithmetic.** A per-order cap does
  not bound a sum over orders: two resting orders of 2^63 units already
  overflow uint64. Per-order sums are safe without checks, because the filled
  quantity never exceeds the accepted quantity, which is at most 2^64−2. Every
  sum over several orders is computed with checked addition before any state
  changes. That covers resting quantity at a price level and per side, and any
  cumulative traded volume the engine keeps. A NewOrder whose resting quantity
  would overflow an aggregate is rejected with InvalidQuantity and leaves the
  book unchanged. An overflow in a statistic that no command can reject is a
  defensive assertion under D-10.
- **D-29. Replacing checks with a static bound.** Checked addition on a resting
  aggregate may be dropped only if Q-3 documents and enforces both a maximum
  order quantity Qmax and a maximum count of live orders Nmax, with
  Qmax × Nmax ≤ 2^64−2. For example, Qmax = Nmax = 2^32−1 gives
  2^64 − 2^33 + 1, which fits. Such a bound never covers cumulative traded
  volume, which grows with order count over time and keeps its checks.

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
- **D-18.** No duplicate is ever dropped silently. Every correlatable command
  gets exactly one response per D-4. A frame that is not correlatable is
  counted and closes its session under D-26 or D-27.

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
  A new enum value or template is compatible for the schema only. A sender must
  not emit it until every receiver of that message accepts it, because D-25 and
  the adapter reject unknown enum values and templates. Receivers are upgraded
  first, then senders.
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
  rejects the frame; it never maps it to a default. Sending a new enum value
  or template therefore requires receiver support first, as D-21 says.

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

The tested compatibility direction is the version 1 decoder reading version 0
NewOrder frames. No version 0 decoder exists in this repository, so a version 0
receiver reading version 1 frames is not tested. The newer-version cases below
are hand-built headers that simulate a future sender; no real version 2 schema
exists.

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
  reaches 2^64−2. Until a cap is chosen, D-28 makes aggregates safe with checked
  arithmetic. A cap removes those checks only together with a live order limit
  that satisfies D-29.
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
- **Q-9. Cancel target key. Merge blocker B-2.** Should CancelOrder target by
  `origClientOrderId` (current draft), by exchange `orderId`, or accept either?
- **Q-10. Self-trade.** When a client's aggressor would match its own resting
  order, does the trade occur, or is one side cancelled or rejected?
- **Q-11. Client identity.** How are `clientId` values allocated and bound to
  sessions? Does the gateway overwrite `clientId` or only verify it?
- **Q-12. Uncorrelatable commands. Merge blocker B-3.** Section 5.1 drafts
  count-and-close (D-26, D-27), which fits the current wire. The alternative is
  a reject with a null `clientOrderId`, which needs that field to be optional.
- **Q-13. Exchange-initiated cancels. Merge blocker B-4.** Are unsolicited
  cancels (end of session, self-trade prevention) in scope? If so,
  `cancelClientOrderId` must be optional.
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

## 12. Merge blockers and review record

Issue #4 says shared semantics are finalized only after joint review with the
Matching Rules Specification (issue #5). On 2026-11-13 the following were
checked for recorded decisions:

- The repository has no matching rules specification. `docs/` and
  `docs/decisions/` contain no matching rules document.
- Issue #5 is open and has no comments.
- Issues #4 and #5 and PR #14 contain no record of a joint review decision.

No decision on the cancel target, uncorrelatable rejects or exchange-initiated
cancels is confirmed. This PR therefore changes no wire layout for them, and
each item below blocks the merge until this section records its outcome.

| ID | Decision needed | Current wire in version 1 | Wire change if decided otherwise |
| --- | --- | --- | --- |
| B-1 | Joint sign-off on the version 1 layout: templates 4–8, offsets, enum and RejectReason codes | as in sections 3, 4 and 7 | depends on the change |
| B-2 | Cancel target key (Q-9) | CancelOrder requires `origClientOrderId` | targeting by `orderId` needs a new CancelOrder field; accepting either makes both optional |
| B-3 | Uncorrelatable rejects (Q-12) | OrderRejected requires `clientId` and `clientOrderId`; section 5.1 drafts count-and-close | a reject with a null ID makes `clientOrderId`, and for unbound sessions `clientId`, optional |
| B-4 | Exchange-initiated cancels (Q-13) | OrderCancelled requires `cancelClientOrderId` | in scope makes it optional and likely adds a cancel reason field |

Nothing has been deployed, so any of these changes can still be made inside
version 1 before merge. Record each outcome here with the date and the people
who agreed, then update `schemas/trading.xml`, the adapters and the tests.
