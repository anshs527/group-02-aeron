# Protocol contract, schema 421 version 0

`schemas/trading.xml` is the source of truth. SBE 1.35.0 generates C++ codecs during
the build; handwritten adapters validate before decoding. These are fixed-length,
little-endian messages without variable-length fields or repeating groups. No
compiler struct packing, host byte order, native pointer, or floating-point
representation is placed on the wire.

Every message starts with an 8-byte SBE header: `blockLength`, `templateId`,
`schemaId`, `version`, each an unsigned 16-bit integer in that order. Schema ID
is 421, schema version is 0. The body starts at byte 8.

| Template | ID | Body bytes | Total bytes | Fields |
| --- | --- | --- | --- | --- |
| Ping | 1 | 8 | 16 | correlationId: uint64 |
| Pong | 2 | 8 | 16 | correlationId: uint64 |
| NewOrder | 3 | 33 | 41 | fields below, tightly encoded |

Ping and Pong are fully integrated into the Aeron IPC test. Correlation IDs run
from 1 through warm-up + measured count, and Pong echoes each ID. Ping requires
the exact outstanding ID; Pong requires the next ID. Correlation IDs are not
wall-clock timestamps. NewOrder is a codec example only, with no exchange or
order submission endpoint implemented.

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

Generated setters are not business validation. The adapter enforces ranges and
recognized enums on encode and decode, rejects every short/oversized fixed frame,
and validates all header fields before invoking message decoders. The decoder API
accepts mutable spans because SBE flyweights require `char*`; decoding does not
mutate the buffer. No SBE bounds checks are disabled in project code.

The current contract intentionally accepts **only exactly version 0**, the known
template, and its exact block length. Future append-only fields use SBE
`sinceVersion` and an increased schema version; backward/forward compatibility
must be specified and tested before relaxing this policy. Never reuse template
or field IDs. Incompatible semantic changes need new templates or schema IDs.
Do not equate SBE wire compatibility with application semantic compatibility.

Tests cover both side values, minimum/maximum valid integer values, actual
generated accessor values, golden little-endian Ping bytes, every truncation,
header corruption, unknown enum values, nulls, bad prices, and correlations.
Pending protocol work includes cancel/replace, rejects, acknowledgments, fills,
market data, sequencing, duplicate handling and recovery (roadmap tasks 2–5).
