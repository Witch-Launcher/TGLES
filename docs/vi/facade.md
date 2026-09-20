# Facade — GlesContext

`include/tgles/gles.h` gom toàn bộ 9 modules thành một context duy nhất,
đúng cách app OpenGL ES nhìn thấy driver.

## Ba mối nối liên module (dễ sai nhất)

1. `BindBuffer(ELEMENT_ARRAY_BUFFER)` → đồng bộ vào VAO đang bound
   (spec 10.4: EAB binding là VAO state).
2. `DrawElementsIndirect` → tự đồng bộ `DRAW_INDIRECT_BUFFER` bound vào
   `DrawValidator` trước khi validate.
3. `DispatchCompute` → tự kiểm tra program đang `UseProgram` có stage
   compute không (qua `ProgramHasStage`).

## Lỗi thống nhất

`GetError()` poll từng manager theo thứ tự pipeline cố định — đúng mô hình
"multiple flag-code pairs" của spec §2.3.1 cho distributed implementations:
foundation → buffers → VAO → shaders → programs → textures → samplers →
renderbuffers → FBO → tessellation → compute → sync → query → debug →
draw → raster → command plan.

## Conformance checklist

`ConformanceChecklist()` trả về mảng rỗng khi mọi gate đạt: 358 entry
points, 13 caps, 4 query targets, 6 stages, toàn bộ minimums (UBO 72,
attribs 16, units 32, MRT 4/4, samples 4, patch 32, tess level 64,
geometry 256, compute 128/16384, texture limits...), ma trận Metal
(BC = Apple9+), và ví dụ MSL đã sửa (varyings struct).
