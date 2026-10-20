# Pipeline compiles and the waits behind them

A pipeline first seen during play is compiled on the compile threads (`BB_ASYNC_PIPELINES`,
default on). With a cold NVIDIA cache a compile takes 100-300 ms; entering an area brings
20-90 at once. Pipelines known from the shader database are created at start (preload) and
come out of the driver cache (`<serial>.vkcache`) quickly.

## What a draw does with a pipeline still compiling

Before (0.9.10 beta 1) the draw recording thread (stage B of the draw pipe) waited for it when
it recorded the bind: every new pipeline stalled the frame by its compile time (user logs, RTX
5060: `2 compiles 594 ms` -> a 607 ms frame, `18 compiles 1110 ms` -> 1665 ms), and the GPU
command thread and the guest threads that waited for stage B (drains, see below) stalled with
it. Now (`Rasterizer::DrawRecord`, `HandleIfReady`):

- **Scene geometry is left out** while its pipeline compiles (`SkipUncompiledDraw`): draws with
  the scene depth (the G-buffer pass's, which was also the scene depth the frame before) that
  are not full-screen passes (more than 6 indices or instanced) - the draws the upscaler
  jitters. G-buffer, light volumes, forward/transparent draws and effects drawn with the scene
  depth. The frame draws these targets again from a clear, so the object is missing for the
  frames the compile takes (pop-in), nothing more. Everything else of the draw runs as before
  (targets, barriers, render pass, resources): only `vkCmdDraw` is not recorded. Never left
  out: draws the game measures (occlusion queries: zero samples would hide their predicated
  draws), indirect draws, compute dispatches, shadow maps, half-resolution effects,
  post-processing and UI (what they draw may be kept over frames or read as a whole), and
  draws during a frame capture. A pipeline's draws are left out for at most
  `BB_SKIP_UNCOMPILED_MS` (1000) from its first left-out draw; then they wait for it. "A few
  frames" at 120 FPS is 25-40 ms, below every compile measured, so the bound is in time.
  `BB_SKIP_UNCOMPILED=0`: never left out.
- **Everything else binds the pipeline on a recording thread** (`BindWhenCompiled`): the bind
  becomes a command of its own, and the recording thread that records it into the command
  buffer waits for the compile there. Stage B goes on with the next draws, so drains of the
  GPU command thread (and the guest threads behind them) do not wait for the compile. The
  frame itself still waits for it (its submission needs the command). `BB_LATE_PIPELINE_BIND=0`
  (or commands recorded on the calling thread: `BB_VK_RECORD_THREAD=0`, direct segments):
  stage B waits, as before.
- **A thread that must wait compiles the pipeline itself** when no compile thread has started
  it (`Pipeline::WaitReady`, `TakePipelineJob`): in a burst it was queued behind the other
  pipelines first. `BB_PIPELINE_TAKE=0`: waits for the compile threads.

An already compiled pipeline costs what it did: `HandleIfReady` is the load and branch
`Handle()` was. The draw command gained a branch on the recording thread (bind or not).

## Why not VK_EXT_graphics_pipeline_library

DXVK links pipelines from libraries compiled when the game creates its shaders, long before the
draws. Here (measured on the user's shader database, 855 graphics pipelines):

- Shaders reach the port at the draw that needs them: the guest binary is translated there.
  Libraries would be compiled at that draw too; only linking is fast.
- All stages of a pipeline share descriptor set 0 (one push descriptor set; the vertex stage's
  bindings follow the fragment stage's, and the vertex module is specialized on that offset).
  Libraries need identically defined layouts, so they are shared only between pipelines with
  the same stage modules: 780 distinct stage sets among the 855 pipelines (75, 9%, would reuse
  libraries). Separate sets per stage would end push descriptors for one of them: a descriptor
  set write per draw on the thread that limits the frame rate.
- NVIDIA compiles each library stage about as fully as in a pipeline: a draw would wait for
  the slower of the vertex and fragment libraries instead of both (perhaps 30-40% less), at
  about twice the compile work (libraries and the optimized pipeline after them).

The start line reports whether the device offers it (`graphics pipeline library offered (fast
linking)`) for later work, e.g. if shader translation could run ahead of the draws.

## Waits behind the draw recording thread (write faults, drains)

`Stall: 335.8 ms frame; ... write faults 324.6 ms; GPU thread on CPU 328.1 ms (kernel 203.1
ms)`, and from a boss fight `image create 575 ms`, `image refresh 608.6 ms` with `write faults
605.1 ms`. Two different waits:

- **Guest threads** handle their write faults with locks (`OnWriteFault`, `assume_locks`
  false); they never drain. `TextureCache::InvalidateMemory` takes the texture cache's lock,
  which stage B holds through `FindImage` (image creation, page protection) and `UpdateImage`
  (`RefreshImage`: the upload): 0.5-0.6 s with memory short. Most faults are on buffer pages
  (the game rewriting constants and vertices) that no image watches. The texture cache now
  counts, per 4 KiB page, the tracked images that protect it (`WatchedPages`, kept with the
  page manager's watchers, readable without the lock); a fault on a page no image watches
  updates the buffer tracking only. Images learn of CPU writes through their own page
  protection alone, so this is what the lock gave them: an untracked image is already dirty,
  or new (dirty), or hash-checked (MaybeCpuDirty). `BB_FAULT_SKIP_TEXTURES=0`: the lock for
  every fault. A fault on a page an image watches still waits for the lock (unchanged), and
  a guest thread's fault on GPU-written data still needs the GPU thread for the readback.
- **The GPU command thread** (stage A) drains before it handles its own faults (stream order:
  the queued draws read the old bytes first; a readback records commands) and at many other
  points. It waited with `SwitchToThread` in a loop for as long as stage B took: a core burnt
  in the kernel while stage B (or a compile thread) needed it. Its waits (`Drain`,
  `WaitForPacket`, a full ring) now spin, yield for `BB_DRAIN_SPIN_US` (500), then sleep until
  stage B has run the packet they need (stage B checks after each packet whether stage A
  sleeps for it: a load and a branch). `BB_DRAIN_SLEEP=0`: spin and yield as before. The wait
  itself is as long as stage B's work: with compiles off stage B that is now mostly image
  creation and uploads (memory pressure).

## Statistics and log lines

At start:

    Pipelines: new ones compiled on 4 threads; draws of one still compiling: scene geometry left out for up to 1000 ms, other draws bind it on the recording threads (they wait for it) (BB_SKIP_UNCOMPILED, BB_LATE_PIPELINE_BIND); graphics pipeline library offered (fast linking), not used
    Waits: GPU command thread drains sleep after a spin (stage B wakes them); write faults on pages no image watches leave the texture cache alone (BB_DRAIN_SLEEP, BB_FAULT_SKIP_TEXTURES)

`BB_FRAME_STATS`, every 5 s when anything happened:

    Pipelines: 18 compiled in the background (2 by the thread that needed them); 340 draws of 12 pipelines left out while compiling; 6 draws/dispatches bound on the recording threads; 3 waits, 210.4 ms, longest 120.3 ms

and `(N/s slept)` in the `Draw pipe:` line. The `shader/pipeline compiles, ms` figure of
`Frame stats` still adds the waits (now on recording threads too) to the translation time.

## Risks

- Left-out scene draws: an object (or light volume, decal, particle) appears a few frames
  late the first time its pipeline is needed. A transparent layer whose pipeline is ready
  over an opaque one still compiling shows over the background for those frames.
- A recording thread waiting for a compile delays its segment, the submission and the guest's
  fences after it, as stage B's wait did; ordered tasks (guest copies, fence signals) queue
  behind it.
- `ReplaceShader` (shader patching from the debug UI) destroys pipelines at once; a late bind
  still queued for one would use it after that.
