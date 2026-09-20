# Interactive test window (Intel Mac)

A standalone Cocoa app (`apps/tgl_viewer.mm`, target `tgl_viewer`): a
street of vertical blocks drawn with OpenGL ES 3.2 API calls, pushed
through `GlesContext::RenderFrame`, translated to MSL and presented to a
real window-backed `CAMetalLayer`.

```sh
cmake --build build --target tgl_viewer
./build/tgl_viewer
```

## What it proves

Every pixel travels the real TGL path — no hand-written Metal drawing:

- GLES managers own the scene: two `ARRAY_BUFFER`s (xyz + rgba), VAO
  attribs 0/1, a GLSL ES 3.20 program with the `u_modelViewProj` uniform,
  and a draw FBO whose `COLOR_ATTACHMENT0` sizes the target.
- A directional diffuse light is baked on the CPU into the color stream
  (`TriNormal`/`ShadeFace` in `apps/cube_render.h`).
- Real GPU depth: a `DEPTH_COMPONENT24` renderbuffer + `DEPTH_TEST`
  (LESS, write on, clear 1) → the bridge builds a Depth32Float target, a
  depth-stencil state and a dedicated PSO. Occlusion resolves per pixel
  (this fixed the old painter-sort artifact where the ground slab covered
  blocks).
- Present blits the offscreen target into the window drawable.

## Scene

Seven vertical bars (1.6–5.0 tall) cycling the pure RGB primaries
(Red/Green/Blue), one spinning RGB cube, one gray ground quad. The CPU
painter sort stays as a stable order underneath the depth test.

## HUD panel (bilingual VI/EN)

- Follows the system locale by default; flip with the segmented control
  at the panel top or the `L` key. All-text UI, no icons or images.
- FPS (EMA), frame ms (EMA) + worst, 180-frame bar graph
  (green <17.5ms, yellow <33.3ms, red = drop) with 16.7/33.3ms guides.
- Vert/tris counts, draws, bridge serials (frame/done/swap), camera
  state, spin angle, light direction, FBO size, version report, errors.
- Help pane: frame path + original GLSL + the bridge MSL twin.

## Responsive layout

The Metal view takes all free space; the 360pt HUD panel stays pinned
right. Enlarging the window shares the extra height between the stats
and help scroll panes proportionally; shrinking is safe via scrolling +
minimum heights + a 900×560 window min size, so text never overlaps.
FBO/depth targets track the view's backing pixels every frame.

## Camera controls

- Left-drag: orbit (yaw/pitch) • Right-drag / Shift+drag: pan target
- Wheel: zoom (3.5–34) • Space: toggle auto-spin
- `R`: reset camera • `L`: flip VI/EN

## Metal NDC constraint (important)

Metal only accepts clip-space z in **[0, 1]** (GL uses [-1, 1]);
out-of-range geometry is clipped away before the depth test runs
(probed on Intel, see `RealDepthResolvesOverlap`). The viewer's
perspective matrix yields positive z at normal distances; extreme
close-ups (object closer than ~0.2 to the camera) may clip — a known
limit, not yet remapped.

## Troubleshooting

- Weird camera angle (e.g. dragged under the ground): press `R`.
- Tiny window: rendering pauses below 8px, resumes on enlarge.
- GPU-less host (VM/remote): an alert says Metal cannot initialize.
- Overlapping text: cannot happen with the current layout; report with
  a screenshot if you ever see it.
