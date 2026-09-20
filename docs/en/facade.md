# Facade — GlesContext

`include/tgles/gles.h` merges all 9 modules into a single context, the way
an OpenGL ES app sees a driver.

## The three cross-module wirings (easiest to get wrong)

1. `BindBuffer(ELEMENT_ARRAY_BUFFER)` → synced into the bound VAO
   (spec 10.4: the EAB binding is VAO state).
2. `DrawElementsIndirect` → syncs the bound `DRAW_INDIRECT_BUFFER` into
   the `DrawValidator` before validating.
3. `DispatchCompute` → checks that the `UseProgram` program actually has a
   compute stage (via `ProgramHasStage`).

## Unified errors

`GetError()` polls each manager in a fixed pipeline order — the
"multiple flag-code pairs" model of spec §2.3.1 for distributed
implementations: foundation → buffers → VAO → shaders → programs →
textures → samplers → renderbuffers → FBO → tessellation → compute →
sync → query → debug → draw → raster → command plan.

## Conformance checklist

`ConformanceChecklist()` returns an empty array when every gate passes:
358 entry points, 13 caps, 4 query targets, 6 stages, all minimums (UBO 72,
16 attribs, 32 units, MRT 4/4, 4 samples, 32 patch verts, tess level 64,
256 geometry verts, compute 128/16384, texture limits...), the Metal matrix
(BC = Apple9+), and the fixed MSL example (varyings struct).
