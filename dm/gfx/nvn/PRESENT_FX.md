# D2R iPhone presentation effects

The presentation-only path is adapted from ns2mac's Castlevania implementation
and its `MAC_IOS_PORTING_PLAYBOOK.md` (2026-10-25), under the same MIT license.
Game logic, audio, server ticks and guest frame counts never advance for a
synthesized frame. An explicit 33.333 ms acquire/present minimum limits real frames while
frame generation is active, independently of display backpressure. Slower
game intervals remain intact; pure pacing tests cover uncapped, 60/120 Hz and
jittered producers. This also gives the effects path the correct 33.333 ms
source period instead of relying on the former 30 Hz display-link cap.

On supported iPhones `frame_gen` defaults on. It implies a 30 real FPS target
and requests 60 Hz display refresh. In smoothness mode the native 3D scale is
capped at 0.55 to reserve GPU room, with adaptive fallback down to 0.50
on phones under heat/load; UI remains native resolution. Quality-first
mode remains an explicit override and may miss the presentation target.
`fx_upscale` is a separate option, off by default; turning it on also processes
real frames at reduced resolution. Turning frame generation off restores the
existing native path unless standalone scaling or antialiasing is requested.
The existing power-saving switch can independently keep native 30 FPS.

For the 2736x1260 iPhone Air output, interpolation works on a 1563x720 image.
Bidirectional image optical flow is estimated on a quarter-size field. MetalFX
uses this estimate and planar depth, then perceptual colour/occlusion guards
reject uncertain pixels to the real current frame. Only the generated image
is spatially scaled by default. Real frames sample the original source crop;
static pixels and the bottom HUD use native pixels in generated frames as well.
The final display shader also rejects synthesized pixels more than 0.06 apart
in visible RGB distance from the full-resolution real source, including
upscaling errors that the reduced-resolution guard cannot detect. This limits
interpolation coverage at high-contrast moving edges.
This is an image-based approximation, not extracted game motion/depth/UI layers.
Fast animations, transparent effects and occlusion boundaries can fall back to
30 FPS content. Input responsiveness remains that of real 30 FPS rendering.

The Metal 4 producer signals a dedicated shared event to the Metal 3 effects
queue. Source tickets are retained through the last output GPU completion, with
two bounded inflight slots. Crop, dimensions, scene/panel changes, mode toggles
and gaps of at least 100 ms reset history; two warm-up frames produce no middle
frame. Requests are applied before creating the effects command buffer so a
resize/drain cannot wait behind its own unsubmitted work. Unsupported devices
or initialization failure retain the original presentation path.

`[present-fx]` logs independently count encoded real/middle frames and actual
real/generated presentation callbacks, plus recent display-interval p50/p95.
The effects-command-buffer GPU duration can include producer-event waiting;
it is not a measurement of pure interpolation cost. Host `perf.txt` describes
real frames; generated display rate is reported only by the effects counters.

## Verification

- `python3 dm/gfx/tests/test_present_fx.py`: actual intermediate-time image
  error versus copied/blended endpoints, independent moving objects, static
  HUD, gamma, dark occlusion, long-gap reset, cold/runtime toggles, native real
  output and final-display GPU pixel checks. Mac timings are not phone timings.
- `make -C dm/host/tests`: includes the poisoned hidden-inventory-tree regression.
- `make -C dm/ios/tests run`; `make -C dm/gfx/tests t_nvn` and run `t_nvn`.
- Regenerate embedded shaders with `python3 dm/gfx/tools/present_shaders.py`.

2026-10-27, iPhone Air / iOS 27.2: the initial full-resolution effects path
missed the target and was superseded. The reduced-work-image/native-real path
recorded 30 real + 30 generated presented FPS, with display p50/p95 16.67 ms,
in a 90-second town run at 0.55 native 3D scale, thermal state 0. A 150-second default-settings town run also held approximately 30 real + 30
generated, output p50/p95 16.67 ms, as thermal state changed from 0 to 1.
The dark-scene run reached the same cadence. A moving capture exposed local
texture artifacts and led to the additional final-output colour guard; its
GPU test explicitly rejects corrupt pixels while retaining valid moving ones.
Final-guard device acceptance is recorded separately in the private run directory.
These windows do not establish all-scene or long-term thermal guarantees.

Private evidence: `$DM_ROOT/run/ios/town-fg-20261027/`; production save snapshots
are separate from `Documents/save.test`, which is used for all automated runs.
Never use `devicectl copy to --remove-existing-content true` on the app container.
