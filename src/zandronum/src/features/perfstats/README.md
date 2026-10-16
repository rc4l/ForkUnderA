# perfstats

Frame-time statistics: the mean, the extremes, the percentiles and the 1% low of a run of frame times, which the MCP bridge's performance answers and the backend's frame report both give.

- `computation/perfstats_compute.h`: `PerfSummary` and `ComputeFrameTimeSummary`, tested.

## In-engine hooks

None; it is called from `features/mcp-bridge` and `features/hwrender/diligent/dgframe.cpp`.
