# fileio

Reading a file whole into memory, which several features each did with their own loop.

- `computation/readfile_compute.h`, tested:
  - `ComputeReadWhole` reads an open file from where it stands to its end, in chunks, with an optional size cap.
  - `ComputeReadWholeFile` does the same for a path.
  - `ComputeFileText` gives a file's bytes, or none when it cannot be read.

## Users

- `addon-catalogue/zx_catalogue.cpp`: a catalogue entry, capped at 256 KB.
- `federated-server-registry/zx_serverregistrylist.cpp` and `server-browser/zx_customstore.cpp`: their stored lists.
- `frame/backendrenderer.cpp`: the save a later save picture goes into, and that picture's PNG.

It has no in-engine hooks of its own.
