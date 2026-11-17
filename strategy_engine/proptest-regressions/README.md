# proptest regression seeds

When a property test fails, proptest appends one `cc <seed> # shrinks to ...`
line to a seed file:

| Failing test in     | Seed file                             |
|---------------------|---------------------------------------|
| `src/lib.rs`        | `proptest-regressions/lib.txt` (here) |
| `tests/ffi_decide.rs` | `tests/ffi_decide.proptest-regressions` |

(`tests/ffi_threshold.rs` keeps no seeds: its runner sets
`failure_persistence: None` because the set-once threshold makes a replayed
case order-dependent.)

**Commit seed files.** Replay is automatic: on every later `cargo test`,
proptest reads the file next to the test's source and re-runs each `cc` entry
before generating new random cases, so a found bug cannot regress silently.
Nothing needs to be configured or referenced - the file just has to stay where
proptest wrote it.

Keep the entries after fixing the bug: they are the cheapest regression tests
available. Only prune a line when the test or its strategies changed so much
that the seed no longer reproduces anything meaningful.

This README exists so the directory stays tracked and the behaviour is written
down; delete nothing else here.
