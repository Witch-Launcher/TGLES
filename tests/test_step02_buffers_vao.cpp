// Step 2 tests: buffer object model (spec ch.6) and VAO/attribute state
// (spec 10.3-10.4), including the ELEMENT_ARRAY_BUFFER-is-VAO-state rule.

#include "test_framework.h"

#include <cstring>
#include <vector>

#include "tgles/buffer.h"
#include "tgles/vertex_array.h"

// --- Buffer object lifecycle (spec 2.6.1 + 6.1) ---

TEST(Step02, BufferGenBindIsDelete) {
  tgles::BufferManager mgr;
  tgles::GLuint names[2] = {0, 0};
  mgr.GenBuffers(2, names);
  EXPECT_NE(names[0], 0u);
  EXPECT_NE(names[1], names[0]);
  // Generated but never bound: not yet a buffer object.
  EXPECT_EQ(mgr.IsBuffer(names[0]), tgles::kGlFalse);
  mgr.BindBuffer(tgles::kGlArrayBuffer, names[0]);
  EXPECT_EQ(mgr.IsBuffer(names[0]), tgles::kGlTrue);
  EXPECT_EQ(mgr.BoundBuffer(tgles::kGlArrayBuffer), names[0]);
  mgr.DeleteBuffers(1, &names[0]);
  EXPECT_EQ(mgr.IsBuffer(names[0]), tgles::kGlFalse);
  EXPECT_EQ(mgr.BoundBuffer(tgles::kGlArrayBuffer), 0u);  // Auto-unbound.
  EXPECT_EQ(mgr.GetError(), tgles::kGlNoError);
}

TEST(Step02, InvalidBufferTargetIsEnumError) {
  tgles::BufferManager mgr;
  mgr.BindBuffer(0xDEADBEEFu, 1);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidEnum);
  tgles::GLuint zero = 0;
  mgr.DeleteBuffers(-1, &zero);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidValue);
}

// --- Data store (spec 6.2) ---

TEST(Step02, BufferDataUploadAndQuery) {
  tgles::BufferManager mgr;
  tgles::GLuint name = 0;
  mgr.GenBuffers(1, &name);
  mgr.BindBuffer(tgles::kGlArrayBuffer, name);
  const float data[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  mgr.BufferData(tgles::kGlArrayBuffer, sizeof(data), data,
                 tgles::kGlStaticDraw);
  EXPECT_EQ(mgr.BufferSize(name), static_cast<tgles::GLsizeiptr>(sizeof(data)));
  tgles::GLint size = 0, usage = 0;
  mgr.GetBufferParameteriv(tgles::kGlArrayBuffer, tgles::kGlBufferSize, &size);
  mgr.GetBufferParameteriv(tgles::kGlArrayBuffer, tgles::kGlBufferUsage,
                           &usage);
  EXPECT_EQ(size, static_cast<tgles::GLint>(sizeof(data)));
  EXPECT_EQ(usage, static_cast<tgles::GLint>(tgles::kGlStaticDraw));
  EXPECT_EQ(mgr.GetError(), tgles::kGlNoError);
}

TEST(Step02, BufferDataValidation) {
  tgles::BufferManager mgr;
  mgr.BufferData(tgles::kGlArrayBuffer, 16, nullptr, tgles::kGlStaticDraw);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidOperation);  // Nothing bound.
  tgles::GLuint name = 0;
  mgr.GenBuffers(1, &name);
  mgr.BindBuffer(tgles::kGlArrayBuffer, name);
  mgr.BufferData(tgles::kGlArrayBuffer, -4, nullptr, tgles::kGlStaticDraw);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidValue);
  mgr.BufferData(tgles::kGlArrayBuffer, 16, nullptr, 0x1234u);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidEnum);  // Bad usage.
}

TEST(Step02, BufferSubDataBoundsChecked) {
  tgles::BufferManager mgr;
  tgles::GLuint name = 0;
  mgr.GenBuffers(1, &name);
  mgr.BindBuffer(tgles::kGlArrayBuffer, name);
  mgr.BufferData(tgles::kGlArrayBuffer, 16, nullptr, tgles::kGlDynamicDraw);
  const char patch[4] = {'a', 'b', 'c', 'd'};
  mgr.BufferSubData(tgles::kGlArrayBuffer, 4, 4, patch);
  EXPECT_EQ(mgr.GetError(), tgles::kGlNoError);
  mgr.BufferSubData(tgles::kGlArrayBuffer, 14, 4, patch);  // Overruns.
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidValue);
  mgr.BufferSubData(tgles::kGlArrayBuffer, -1, 4, patch);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidValue);
}

// --- Mapping (spec 6.3) ---

TEST(Step02, MapUnmapRoundTrip) {
  tgles::BufferManager mgr;
  tgles::GLuint name = 0;
  mgr.GenBuffers(1, &name);
  mgr.BindBuffer(tgles::kGlArrayBuffer, name);
  mgr.BufferData(tgles::kGlArrayBuffer, 16, nullptr, tgles::kGlDynamicDraw);
  void* p = mgr.MapBufferRange(tgles::kGlArrayBuffer, 0, 16,
                               tgles::kGlMapWriteBit);
  EXPECT_NOT_NULL(p);
  EXPECT_TRUE(mgr.IsMapped(name));
  std::memset(p, 0xAB, 16);
  EXPECT_EQ(mgr.UnmapBuffer(tgles::kGlArrayBuffer), tgles::kGlTrue);
  EXPECT_FALSE(mgr.IsMapped(name));
  EXPECT_EQ(mgr.GetError(), tgles::kGlNoError);
}

TEST(Step02, MapValidation) {
  tgles::BufferManager mgr;
  tgles::GLuint name = 0;
  mgr.GenBuffers(1, &name);
  mgr.BindBuffer(tgles::kGlArrayBuffer, name);
  mgr.BufferData(tgles::kGlArrayBuffer, 16, nullptr, tgles::kGlDynamicDraw);
  EXPECT_TRUE(mgr.MapBufferRange(tgles::kGlArrayBuffer, 0, 16, 0u) == nullptr);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidOperation);  // No R/W bit.
  EXPECT_TRUE(mgr.MapBufferRange(tgles::kGlArrayBuffer, 0, 16, 0xFFFFu) ==
              nullptr);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidValue);  // Reserved bits.
  EXPECT_TRUE(mgr.MapBufferRange(tgles::kGlArrayBuffer, 12, 8,
                                 tgles::kGlMapReadBit) == nullptr);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidValue);  // Overruns store.
  mgr.MapBufferRange(tgles::kGlArrayBuffer, 0, 16, tgles::kGlMapReadBit);
  EXPECT_TRUE(mgr.MapBufferRange(tgles::kGlArrayBuffer, 0, 16,
                                 tgles::kGlMapReadBit) == nullptr);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidOperation);  // Already mapped.
  EXPECT_EQ(mgr.UnmapBuffer(tgles::kGlArrayBuffer), tgles::kGlTrue);
  EXPECT_EQ(mgr.UnmapBuffer(tgles::kGlArrayBuffer), tgles::kGlFalse);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidOperation);  // Not mapped.
}

// --- Copy (spec 6.5) ---

TEST(Step02, CopyBufferSubData) {
  tgles::BufferManager mgr;
  tgles::GLuint names[2] = {0, 0};
  mgr.GenBuffers(2, names);
  mgr.BindBuffer(tgles::kGlCopyReadBuffer, names[0]);
  mgr.BindBuffer(tgles::kGlCopyWriteBuffer, names[1]);
  const std::uint8_t src[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  mgr.BufferData(tgles::kGlCopyReadBuffer, 8, src, tgles::kGlStaticDraw);
  mgr.BufferData(tgles::kGlCopyWriteBuffer, 8, nullptr, tgles::kGlStaticDraw);
  mgr.CopyBufferSubData(tgles::kGlCopyReadBuffer, tgles::kGlCopyWriteBuffer, 2,
                        4, 4);
  EXPECT_EQ(mgr.GetError(), tgles::kGlNoError);
  EXPECT_EQ(mgr.BufferSize(names[1]), 8);
  mgr.CopyBufferSubData(tgles::kGlCopyReadBuffer, tgles::kGlCopyWriteBuffer, 6,
                        0, 4);  // Read overrun.
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidValue);
}

// --- Indexed bindings (spec 6.1.1) ---

TEST(Step02, UniformBindingLimitIs72) {
  EXPECT_EQ(tgles::kMaxUniformBufferBindings, 72);  // PDF-confirmed minimum.
  tgles::BufferManager mgr;
  tgles::GLuint name = 0;
  mgr.GenBuffers(1, &name);
  mgr.BindBuffer(tgles::kGlUniformBuffer, name);
  mgr.BufferData(tgles::kGlUniformBuffer, 256, nullptr, tgles::kGlStaticDraw);
  mgr.BindBufferBase(tgles::kGlUniformBuffer, 71, name);
  EXPECT_EQ(mgr.GetError(), tgles::kGlNoError);
  mgr.BindBufferBase(tgles::kGlUniformBuffer, 72, name);
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidValue);
  mgr.BindBufferBase(tgles::kGlArrayBuffer, 0, name);  // Not indexable.
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidEnum);
}

TEST(Step02, BindBufferRangeAlignmentAndBounds) {
  tgles::BufferManager mgr;
  tgles::GLuint name = 0;
  mgr.GenBuffers(1, &name);
  mgr.BindBuffer(tgles::kGlUniformBuffer, name);
  mgr.BufferData(tgles::kGlUniformBuffer, 256, nullptr, tgles::kGlStaticDraw);
  mgr.BindBufferRange(tgles::kGlUniformBuffer, 0, name, 16, 64);
  EXPECT_EQ(mgr.GetError(), tgles::kGlNoError);
  tgles::IndexedBinding slot =
      mgr.IndexedBindingState(tgles::kGlUniformBuffer, 0);
  EXPECT_EQ(slot.buffer, name);
  EXPECT_EQ(slot.offset, 16);
  EXPECT_EQ(slot.size, 64);
  mgr.BindBufferRange(tgles::kGlUniformBuffer, 1, name, 7, 64);  // Misaligned.
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidValue);
  mgr.BindBufferRange(tgles::kGlUniformBuffer, 1, name, 200, 100);  // Overrun.
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidValue);
}

// --- VAO and attributes (spec 10.3-10.4) ---

TEST(Step02, VaoLifecycle) {
  tgles::VertexArrayManager vao;
  EXPECT_EQ(vao.BoundArray(), 0u);  // Default VAO bound at start.
  EXPECT_EQ(vao.IsVertexArray(0), tgles::kGlFalse);  // 0 is never an object.
  tgles::GLuint name = 0;
  vao.GenVertexArrays(1, &name);
  EXPECT_NE(name, 0u);
  vao.BindVertexArray(name);
  EXPECT_EQ(vao.BoundArray(), name);
  vao.DeleteVertexArrays(1, &name);
  EXPECT_EQ(vao.BoundArray(), 0u);  // Reverts to default.
  EXPECT_EQ(vao.GetError(), tgles::kGlNoError);
}

TEST(Step02, AttribPointerState) {
  tgles::VertexArrayManager vao;
  vao.EnableVertexAttribArray(0);
  vao.VertexAttribPointer(0, 3, tgles::kGlFloat, tgles::kGlFalse, 12, 0, 7);
  tgles::GenericAttrib a = vao.AttribState(0);
  EXPECT_TRUE(a.enabled);
  EXPECT_EQ(a.size, 3);
  EXPECT_EQ(a.type, tgles::kGlFloat);
  EXPECT_EQ(a.stride, 12);
  EXPECT_EQ(a.buffer, 7u);
  EXPECT_FALSE(a.pure_integer);
  vao.VertexAttribIPointer(1, 2, tgles::kGlUnsignedInt, 0, 0, 7);
  EXPECT_TRUE(vao.AttribState(1).pure_integer);
  vao.VertexAttribDivisor(0, 1);  // Instanced.
  EXPECT_EQ(vao.AttribState(0).divisor, 1u);
  EXPECT_EQ(vao.GetError(), tgles::kGlNoError);
}

TEST(Step02, AttribIndexBounds) {
  tgles::VertexArrayManager vao;
  EXPECT_EQ(tgles::kMaxVertexAttribs, 16);  // Spec minimum.
  vao.EnableVertexAttribArray(16);
  EXPECT_EQ(vao.GetError(), tgles::kGlInvalidValue);
  vao.VertexAttribPointer(16, 4, tgles::kGlFloat, tgles::kGlFalse, 0, 0, 0);
  EXPECT_EQ(vao.GetError(), tgles::kGlInvalidValue);
  vao.VertexAttribPointer(0, 5, tgles::kGlFloat, tgles::kGlFalse, 0, 0, 0);
  EXPECT_EQ(vao.GetError(), tgles::kGlInvalidValue);  // Bad size.
  vao.VertexAttribPointer(0, 4, 0x9999u, tgles::kGlFalse, 0, 0, 0);
  EXPECT_EQ(vao.GetError(), tgles::kGlInvalidEnum);  // Bad type.
  vao.VertexAttribPointer(0, 4, tgles::kGlFloat, tgles::kGlFalse, -4, 0, 0);
  EXPECT_EQ(vao.GetError(), tgles::kGlInvalidValue);  // Negative stride.
}

TEST(Step02, ElementArrayBindingIsVaoState) {
  tgles::VertexArrayManager vao;
  tgles::GLuint name = 0;
  vao.GenVertexArrays(1, &name);
  vao.SetElementArrayBuffer(11);  // Stored in default VAO.
  vao.BindVertexArray(name);
  EXPECT_EQ(vao.ElementArrayBuffer(), 0u);  // Fresh VAO sees nothing.
  vao.SetElementArrayBuffer(22);
  vao.BindVertexArray(0);
  EXPECT_EQ(vao.ElementArrayBuffer(), 11u);  // Default VAO kept its own.
  EXPECT_EQ(vao.GetError(), tgles::kGlNoError);
}

TEST(Step02, RestartIndexValues) {
  EXPECT_EQ(tgles::VertexArrayManager::RestartIndexForType(
                tgles::kGlUnsignedByte),
            0xFFu);
  EXPECT_EQ(tgles::VertexArrayManager::RestartIndexForType(
                tgles::kGlUnsignedShort),
            0xFFFFu);
  EXPECT_EQ(tgles::VertexArrayManager::RestartIndexForType(
                tgles::kGlUnsignedInt),
            0xFFFFFFFFu);
  EXPECT_EQ(
      tgles::VertexArrayManager::RestartIndexForType(tgles::kGlFloat), 0u);
}

TEST(Step02, DefaultAttribValues) {
  tgles::VertexArrayManager vao;
  tgles::GenericAttribValue v = vao.AttribValue(3);
  EXPECT_EQ(v.f[0], 0.0f);
  EXPECT_EQ(v.f[3], 1.0f);  // Spec default (0,0,0,1).
  vao.VertexAttrib4f(3, 1.0f, 2.0f, 3.0f, 4.0f);
  EXPECT_EQ(vao.AttribValue(3).f[2], 3.0f);
  vao.VertexAttrib4f(16, 0, 0, 0, 0);
  EXPECT_EQ(vao.GetError(), tgles::kGlInvalidValue);
}
