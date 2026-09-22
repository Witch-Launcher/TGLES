#ifndef TGLES_VERSION_H
#define TGLES_VERSION_H

// Project version. Keep in sync with CMake project() version.
#define TGLES_VERSION_MAJOR 0
#define TGLES_VERSION_MINOR 2
#define TGLES_VERSION_PATCH 0

namespace tgles {

// Human-readable library identity for tglesAbiVersion(). Must mention both
// the project and the GLES profile so launchers can show it verbatim.
inline constexpr const char* kVersionString = "TGL OpenGL ES 3.2 (0.2.0)";

}  // namespace tgles

#endif // TGLES_VERSION_H
