# CBR conversion failure isolation

Automatic recording saves process both player slots and all queued identity/character
groups. A conversion error no longer disables automatic saving for the session.

Each rejected recording is written to a unique directory under
`CBRsave/FailedRecordings/`, containing `capture.cbrfailed`. Only after that write
succeeds is the recording removed from the normal save queue. Valid recordings
before and after it still enter their usual SteamID-keyed `.cbr` files. Failed
recordings never enter the AI dataset and are not automatically retried every frame.

Each gzip/Boost diagnostic archive contains the original annotated recording,
SteamID, display name, characters, player slot, build stamp, and conversion report.
Game frame counters and other metadata omitted by the historical `.rev` serializer
are included separately. `ReadCbrFailedCapture` restores those fields, allowing a
developer to reproduce conversion and rollback cleanup from the original samples.
Existing `.cbr` and `.rev` formats are unchanged.

`DEBUG.txt` identifies each file with `[CBR-QUARANTINE]`. The existing
`CBRsave/CbrErrorReport.txt` also collects conversion reports for the session.
The individual failed-capture files remain available across game launches, even
when the text report is subsequently replaced. To send several failures together,
zip `CBRsave/FailedRecordings` and include that session's `DEBUG.txt` and
`CbrErrorReport.txt`. These captures include player names/SteamIDs and gameplay data.
They are kept locally and are not uploaded automatically.

A disk/archive-write failure still stops automatic retries and retains uncommitted
recordings in memory. Fix the storage problem and use the existing save hotkey to
retry before closing the game. Already committed groups and archived failures are
removed independently, preventing duplication on retry. This does not introduce a
shutdown save or persistence for every pending recording.

The conversion harness exercises the production queue and archive helpers with real
converter results: multiple failures mixed with valid recordings, both slots,
multiple identities, later batches, raw-capture round-trip with rollback, failed
diagnostic writes, and database-write failure followed by retry.
