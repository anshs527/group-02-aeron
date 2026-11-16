# Exchange

The exchange will hold the gateway, the matching engine and the market-data
service (roadmap tasks 2–5). Only the single-symbol order book exists so far.
There is no gateway, engine process, or market-data service yet.

## Order book (issue #9)

`hft::OrderBook` in `include/hft/order_book.hpp` is a transport-independent
limit order book for one symbol. It builds as the static library `hft_matching`,
which links only `hft_common` for the protocol structs. It has no Aeron, SBE
frame, clock, thread or I/O dependency.

```cpp
hft::OrderBook book(symbol_id);
std::vector<hft::OrderEvent> events;
book.submit(new_order, events);
book.cancel(cancel_order, events);
```

Commands are the `NewOrder` and `CancelOrder` structs from `hft/protocol.hpp`.
Each call appends its events to the caller's vector and never clears it. An
`OrderEvent` is one of `OrderAccepted`, `OrderRejected`, `OrderCancelled` or
`OrderExecuted`. Inspection functions return the best prices, per-level and
per-side resting quantity, resting orders in priority order, and a client's
live order.

### Matching behavior

- **Price-time priority.** The best price trades first: highest bid, lowest ask.
  At equal prices, orders trade in arrival order.
- **Partial fills keep priority.** A resting order that is partly filled stays
  at its place in the queue.
- **Crossing.** An incoming order trades while the best opposite price is at or
  inside its limit. Any remainder rests at the back of its price level.
- **IDs.** `orderId` and `tradeId` start at 1 and increase by one per accepted
  order and per trade, as protocol.md D-2 says. A rejected order gets no
  `orderId`.
- **Reports.** Each trade emits two `OrderExecuted` reports with the same
  `tradeId`, price and quantity, as protocol.md D-7 says.
- **Validation split.** The book makes only the stateful checks that protocol.md
  section 8 gives the engine. Those are duplicate IDs, unknown cancel targets,
  ownership, already-done orders and aggregate overflow, and each produces an
  `OrderRejected`. Anything the gateway owns, and a symbol other than the
  book's, throws `std::invalid_argument` without changing state, under D-10.
  The gateway must never forward such a command.

| Case | Outcome |
| --- | --- |
| NewOrder reusing a clientOrderId of the same client, live or done | `OrderRejected` DuplicateClientOrderId (D-12, D-13) |
| CancelOrder reusing any clientOrderId of the same client | `OrderRejected` DuplicateClientOrderId (D-17) |
| Cancel of a live order of the same client | `OrderCancelled` with the open quantity only |
| Cancel of a filled or already cancelled order | `OrderRejected` OrderAlreadyDone (D-14, D-15) |
| Cancel of an ID the client never used for an order | `OrderRejected` UnknownOrder (D-16) |
| Cancel of another client's order | `OrderRejected` UnknownOrder; the order is untouched |
| NewOrder whose resting remainder would overflow its side's total | `OrderRejected` InvalidQuantity; book unchanged (D-28) |

The same clientOrderId may be used by different clients. Ownership comes from
the (clientId, clientOrderId) key, so one client can never see or touch
another's order.

### Draft - pending matching rules spec

Issue #5 has not produced a matching rules specification. Every default below
is **Draft - pending matching rules spec**, and the book applies it until the
spec decides. Each one maps to an open question in
docs/protocol.md section 11.

| Question | Default implemented |
| --- | --- |
| Q-5 uniqueness window | IDs are unique for the life of the book |
| Q-6 accept before execute | `OrderAccepted` always comes first, even when the order fills completely on entry |
| Q-7 execution price | the resting order's price |
| Q-8 report order | maker report, then taker report |
| Q-9 cancel target | `origClientOrderId` of the same client |
| Q-10 self-trade | allowed; both reports go to the same client |
| Q-15 duplicate NewOrder | reject, no idempotent replay |

A command consumes its clientOrderId once it passes the duplicate check,
whatever its outcome. A rejected cancel or an overflow-rejected order therefore
still uses up its ID. A command that throws consumes nothing.

### Known limits

- Aggregates are checked per side. Each level total is at most its side's
  total, so checking the side covers every level. The book keeps no cumulative
  traded volume.
- Used IDs and finished orders are kept for the life of the book, so memory
  grows with the number of commands. A bounded window needs Q-5.
- Price levels and queues are standard containers that allocate per order. This
  is fine for functional testing. A pooled layout is needed before the book is
  on a measured hot path.
- There is no replace/amend, market order, time-in-force, or exchange-initiated
  cancel.

### Tests

`tests/order_book_test.cpp` builds as `hft_matching_tests` and runs as the
ctest test `matching`, labelled both `unit` and `matching`:

```bash
ctest --preset debug -L matching --output-on-failure
```

The tests check exact event sequences for each rule above. A seeded
2000-command random run checks the following after every command:
- the book never crosses
- no order overfills, and each fill's leaves quantity is exact
- every trade has one maker and one taker report

Side totals are compared with the sum of resting quantities every 100
commands and after the last one. Rebuilding the resting list after every
command made the asan run exceed the 20-second ctest limit with GCC 13 on
arm64, where ASan's stack-use-after-return detection is slow. For the same
reason the run has 2000 commands rather than 5000. At the end of that run, it
checks that filled plus cancelled plus resting
quantity equals each order's quantity, and that bought equals sold. It then
replays the same commands into a fresh book and requires identical events and
final state.
