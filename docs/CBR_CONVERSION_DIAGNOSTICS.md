# CBR conversion diagnostics

Use `BBCF_IM/DEBUG.txt` for the per-recording `[CBR-CONVERT]` summary and
`CBRsave/CbrErrorReport.txt` for failed conversions. Successful conversions also
include the timeline summary in their existing `CBRsave/Structure` report.

| Report category | What failed | What it does not establish |
| --- | --- | --- |
| `CAPTURE_INVALID` | Missing metadata, mismatched vector lengths, or too few captured samples | A character-command or rollback-cleanup defect |
| `TIMELINE_INVALID` | Cleanup left non-increasing frame counts, insufficient samples, or misaligned vectors | Whether the capture or cleanup caused it |
| `COMMAND_UNRESOLVED` | A move's expected command was not resolved within the existing counter limit | That the character table is necessarily wrong |
| `CASE_LAYOUT` / `CASE_VALIDATION` | Generated cases failed an existing consistency check | That rollback caused the inconsistency |

Removed samples, raw frame repeats/rewinds, and cleaned frame gaps are
observations. Rollback removal is expected; a monotonically increasing cleaned
timeline alone does not prove that every retained input or facing is correct.

Command errors include the actor, character, preceding action, expected and
remaining command alternatives, move-start sample/frame, and facing. Numeric
button values are rendered as A/B/C/D; direction digits use numpad notation.
`_` marks an already matched command element, and `release:` is a release check.
The counter is the existing resolver counter, not a timing measurement.

`CLEANED_TRACE` maps every retained sample to `source_sample` in `RAW_TRACE`.
Both include game frame, physical encoded input, direction relative to that
frame's facing, actions, and both players' hit/block flags. Removed raw samples
have `retained=0`. A command's resolver facing may differ from a frame's facing;
compare both when investigating side switches. `CASE_TRACE` lists case bounds.
For trimmed conversions, raw samples are numbered after trimming but before
rollback cleanup. The header also records the original captured sample count.

## Controlled comparisons

Run `tools/test_cbr_conversion.py` after applying the workflow patches. Tests
compare serialized conversion results for the same final timeline with and
without speculative frames, including wrong predicted inputs/actions/facing
around the command. Coverage includes both facings, Nu's neutral Luminous Slave,
Gravity Seed into Luminous Slave, Sickle Storm hitting into Luminous Slave, and
Ragna's `Shot`. Invalid commands are checked with and without rollback so they
remain command failures rather than being relabelled as timeline failures.

These are synthetic converter tests, not proof of in-game cancel legality or
timing. For game tests, record the same move/cancel offline and online on both
sides. A failure offline narrows the investigation beyond rollback; failure only
online still leaves capture, facing, timing, and rollback as possibilities.

Use [Dustloop's BBCF wiki](https://www.dustloop.com/w/BBCF) for player-facing move
names, inputs, numpad notation, and mechanics. Verify internal action-name
mapping separately. Frame data may be outdated; do not infer exact cancel timing
solely from those values.
