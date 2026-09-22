// Textures and color targets (spec Chapter 8 + FBO Chapter 9).
// ASTC is core in ES 3.2; BC/DXT on Apple GPUs is family-gated (Apple9+).

#include "test_framework.h"

#include <fstream>
#include <string>

TEST(TextureFormats, AstcIsCoreInSpec) {
  const char* dir = TGLES_REFERENCE_DIR;
  std::string core = std::string(dir) + "/gl32.h";
  std::ifstream probe(core);
  if (!probe) return;
  std::string content((std::istreambuf_iterator<char>(probe)),
                      std::istreambuf_iterator<char>());
  EXPECT_TRUE(content.find("ASTC") != std::string::npos);
}

TEST(TextureFormats, RequiredTextureTargets) {
  const char* dir = TGLES_REFERENCE_DIR;
  std::string core = std::string(dir) + "/gl32.h";
  std::ifstream probe(core);
  if (!probe) return;
  std::string content((std::istreambuf_iterator<char>(probe)),
                      std::istreambuf_iterator<char>());
  EXPECT_TRUE(content.find("GL_TEXTURE_3D") != std::string::npos);
  EXPECT_TRUE(content.find("GL_TEXTURE_2D_ARRAY") != std::string::npos);
  EXPECT_TRUE(content.find("GL_TEXTURE_CUBE_MAP_ARRAY") != std::string::npos);
  EXPECT_TRUE(content.find("GL_TEXTURE_BUFFER") != std::string::npos);
  EXPECT_TRUE(content.find("GL_TEXTURE_2D_MULTISAMPLE") != std::string::npos);
}

TEST(TextureFormats, MultipleRenderTargets) {
  const char* dir = TGLES_REFERENCE_DIR;
  std::string core = std::string(dir) + "/gl32.h";
  std::ifstream probe(core);
  if (!probe) return;
  std::string content((std::istreambuf_iterator<char>(probe)),
                      std::istreambuf_iterator<char>());
  EXPECT_TRUE(content.find("GL_COLOR_ATTACHMENT0") != std::string::npos);
  EXPECT_TRUE(content.find("glDrawBuffers") != std::string::npos);
  EXPECT_TRUE(content.find("glBlitFramebuffer") != std::string::npos);
}

TEST(TextureFormats, ImmutableStorageEntryPoints) {
  const char* dir = TGLES_REFERENCE_DIR;
  std::string core = std::string(dir) + "/gl32.h";
  std::ifstream probe(core);
  if (!probe) return;
  std::string content((std::istreambuf_iterator<char>(probe)),
                      std::istreambuf_iterator<char>());
  EXPECT_TRUE(content.find("glTexStorage2D") != std::string::npos);
  EXPECT_TRUE(content.find("glTexStorage3D") != std::string::npos);
}
