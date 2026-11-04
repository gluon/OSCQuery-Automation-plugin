# src/core

The plugin's core: slots, the device registry, the state chunk, scaling, the send and record
paths. Everything that matters lives here; the format shims call into it and the editor is a
client of it. No typed OSC address, ever: addresses come from the device.

Empty until devplan **Phase 3**. The process-wide registry is built here as a shared core library
loaded by every plugin binary, after WFS-DIY's Bridge.
