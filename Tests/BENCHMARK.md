# Conflict workflow benchmark - 2026-10-30

UE 5.8.3, Windows, disposable GXPValidation host. The final build linked all three modules. All 34 tests in `GraphXPort.` and `UnrealMCP.Blueprint.Authoring.` passed, including cache GC retention, external-variable ownership/retry, compact responses, authored comments and native compile rollback.

| Measurement | Previous call pattern | Session workflow |
|---|---:|---:|
| Review tool calls | 7 | 1 |
| Review response bytes | 10,822 | 3,319 |
| Preflight response bytes | 6,489 (full) | 2,497 (summary) |

Review responses were 69.3% smaller; preflight responses were 61.5% smaller. Open/Preview/Apply completed the fixture in three principal calls with 8,531 response bytes and about 1.48 seconds of local tool execution. Guard tests and diagnostic comparisons are additional benchmark calls, excluded from that three-call count.

The script also verified stale-stage rejection, dirty-open and dirty-after-preview rejection, refusal without disk/index changes, explicit Save/Stage gating, successful compile/save/stage, retained current/incoming node positions, actual comment properties and idempotent retry receipts.

This is not a controlled old-binary/new-binary latency comparison: the previous call pattern also used the updated reader, and ran first with colder caches. Editor startup, C++ build time, MCP schema discovery and model reasoning/token usage are excluded. Bytes are measured, not model tokens. Do not extrapolate the fixture's execution time to an eight-minute real Blueprint task.

Reproduce using `setup_fixture.py` and `benchmark_workflow.py` in this directory; the latter verifies the disposable server/repository before any writes. The fixture changes only a temporary Blueprint and its synthetic Git stages. No remaining DSM conflict was resolved for this benchmark.
