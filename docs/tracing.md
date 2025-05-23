# Gfxstream Tracing

The host renderer has optional support for Perfetto tracing which can be enabled
by defining `GFXSTREAM_BUILD_WITH_TRACING` (enabled by default on Android
builds).

The `perfetto` and `traced` tools from Perfetto should be installed. Please see
the [Perfetto Quickstart](https://perfetto.dev/docs/quickstart/linux-tracing) or
follow these short form instructions:

```
cd <your Android repo>/external/perfetto

./tools/install-build-deps

./tools/gn gen --args='is_debug=false' out/linux

./tools/ninja -C out/linux traced perfetto
```

To capture a trace on Linux, start the Perfetto daemon:

```
./out/linux/traced
```

Then, run Gfxstream with
[Cuttlefish](https://source.android.com/docs/devices/cuttlefish):

```
cvd start --gpu_mode=gfxstream_guest_angle_host_swiftshader
```

Next, start a trace capture with:

```
./out/linux/perfetto --txt -c gfxstream_trace.cfg -o gfxstream_trace.perfetto
```

with `gfxstream_trace.cfg` containing the following or similar:

```
buffers {
  size_kb: 4096
}
data_sources {
  config {
    name: "track_event"
    track_event_config {
    }
  }
}
```

Next, end the trace capture with Ctrl + C.

Finally, open https://ui.perfetto.dev/ in your webbrowser and use "Open trace
file" to view the trace.