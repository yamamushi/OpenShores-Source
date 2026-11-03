# Walking frame-time diagnostics

Set `OPENSHORES_FRAME_TIMING=1` before starting the client. This works on
Windows, Linux and macOS; normal launches leave the instrumentation disabled.
For example, on macOS:

```sh
OPENSHORES_FRAME_TIMING=1 "$HOME/Applications/OpenShores/OpenShores.app/Contents/MacOS/OpenShores" -anightowl.cc
```

Each view writes `frame-timing-<UTC timestamp>-<PID>-<view>.csv` in the game cache
(the same directory as `soh.txt`). With the default OpenShores cache location on macOS that is
`~/OpenShores/Temporary`. Files remain local and are not uploaded.
Walk through the affected area, then close the client normally to flush the tail.

```sh
python3 tools/performance/frame-timing.py /path/to/frame-timing-file.csv
```

The JSON report gives p50/p95/p99/max presentation intervals, counts above 50
and 100 ms, and the worst intervals with matching paint work and resource calls.
Compare walking with standing still in the same location and quality settings.

- `frame` rows measure every `paintGL` invocation, start-to-start intervals, draw
  counts, triangle counts, and CPU wall times for instrumented work.
- `swap` rows identify the most recent painted frame and Qt's `frameSwapped`
  callback. They include the interval between such callbacks and paint-start to
  swap-callback time. Composition-only swaps are excluded; multiple paints before
  one swap remain separate frame records. This is not physical scanout timing.
- Categories include terrain, vegetation, rocks, atmosphere, effects, controls,
  scene synchronization, animation, preparation (including scene locking), finish,
  mesh construction, flora construction, texture upload/mipmaps, selected dynamic
  buffer updates, and shader checks. Resource calls on worker threads or outside
  the active paint are deliberately excluded; these are instrumented sites, not
  an exhaustive inventory of all uploads.
- Category times overlap when nested: do not add them to estimate frame time.
  GL API calls can include driver waits, but these are not GPU pass durations.
- No GPU waits or queries are added. Leave `OPENSHORES_GPU_PROFILE` unset for this
  test; its separate synchronized measurements deliberately introduce pauses.
- Records buffer in memory and flush every two seconds outside `paintGL`. Memory
  is capped at 2,048 rows per view; `dropped_records` reports overflow. The next
  frame's `logger_io_ms` identifies the timing logger's own formatting/write cost.
  Forced termination can lose the final buffered rows. Minimized/hidden windows,
  debugger pauses and resizing can legitimately produce long frame intervals.

This instrumentation diagnoses stalls; it does not itself optimize rendering.
