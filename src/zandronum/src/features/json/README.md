# json

JSON strings for every feature that writes JSON (the MCP bridge's answers, the sampler's report, the web build's crash context, the server browser's saved presets), so each escapes a string the same way.

- `computation/json_compute.h`: `ComputeJsonEscape` (a string's inside, with quotes, backslashes and control characters escaped) and `ComputeJsonQuoted` (the same in quotes), tested. `JsonControls::Drop` drops control characters other than newline, carriage return and tab, for files the addon catalogue reads back, since its reader refuses `\u` escapes.

## In-engine hooks

None; it is called from the features above.
