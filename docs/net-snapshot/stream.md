# stream: the .zsnap reader

Covers: src/zandronum/src/features/net-snapshot/stream.h, src/zandronum/src/features/net-snapshot/stream.cpp

## Responsibility

Reads a `.zsnap` server recording record by record: the file header, each record's framing, zstd
decompression, and a file that ends mid-record. It knows nothing of what a payload holds; the caller
hands each payload to the client's decoder. The format is `format.h`'s (`ZSNAP_VERSION`,
`ZSNAP_HEADER_SIZE`, `ZSNAP_HEADER_FRAME`, `ZSNAP_RECHDR_SIZE`, `ZSNAP_FLAG_ZSTD`) and the design
doc's [Appendix B](design.md#appendix-b--the-zsnap-fixture-format). The writer is the server's
`fua_snap_record`, in `capture.cpp`.

## Data structures

- `SnapStreamReader`: the open file, the header's version, tic rate, flags and map frame (narrow at
  origin 0 before v33, wide on an axis only from v34), and after each read the record's tic, wall
  time, span and payload. The payload points into the reader's own buffers, valid until the next
  read.
- `ESnapReadResult`: `SNAPREAD_OK`, `SNAPREAD_END` (clean end), `SNAPREAD_TRUNCATED` (the file ends
  mid-record, as a crash leaves it) and `SNAPREAD_ERROR` (corrupt; `error` says why).

## Public functions

- `ZsnapOpen`: opens the file and validates the header, including the version range
  (`ZSNAP_VERSION_MIN` to `ZSNAP_VERSION`), its size for that version (`ZsnapHeaderBytes`) and
  the frame's widths, refusing one with no encoding or a wide one before v34. On failure `error`
  explains.
- `ZsnapReadRecord`: reads the next record, decompressing when its stored size is below its
  raw size.
- `ZsnapClose`: closes the file and frees the buffers.

## Rules that must hold

- **Refuse before allocating.** A record claiming a raw size past `ZSNAP_MAX_RAWSIZE`, a stored size
  past that or past the bytes left in the file, or a zstd frame recording another size than the
  record is refused before any buffer is sized from it. The file is untrusted input: the fuzzer
  feeds this reader mutated files.
- **A truncated tail is not an error.** A recording cut short by a crash reads to its last whole
  record, then reports `SNAPREAD_TRUNCATED`, so the records before it are still usable. The server's
  recorder stops at a map change, at `fua_snap_stoprecord` and on a failed write, but not at quit,
  so a recording left running when the process ends always has this tail.
- Where the file's size is unknown (`ftell` failing past 2 GB on some platforms), the bytes-left
  test is skipped and only the size ceiling applies.

## Engine hooks

None. Its callers are `selftest.cpp` (`fua_snap_verify`), the dumper shared by the reference test
and the `snap_dump` command line (`tests/snapdump/snapdump.cpp`), and the standalone fuzzer
(`tests/snapfuzz/fuzz.cpp`, built as `snap_fuzz` with `SNAP_FUZZ_ZSNAP` where zstd is available).

## Tests

`fua_snap_verify` reads every record of a recording through it ([selftest.md](selftest.md)), and
the reference test decodes `ref.zsnap` through it ([tools.md](tools.md)). The fuzzer's file targets
mutate whole `.zsnap` files through it. There is no computation unit here: the reader needs a file.
