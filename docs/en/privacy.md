# TGLES — Privacy Policy

TGLES (TGL) is an OpenGL ES 3.2 → Metal translation layer for iOS and macOS. It is MIT licensed.

## Data we collect

**None.** TGLES is a graphics translation library. It does not collect, store, or transmit any personal data. No analytics, no telemetry, no advertising, no user profiling.

## Local-only operation

TGLES runs entirely on the host device:

- Validates OpenGL ES 3.2 state machine locally
- Translates GLSL ES shaders to Metal Shading Language locally
- Presents frames through CAMetalLayer or headless readback

No data leaves the device unless your application explicitly sends it.

## Logs

Diagnostic logs may be written for debugging. They are local only and never uploaded by the library.

## Children

TGLES collects no data from anyone, including children.

## Your rights

Because no data is collected, there is nothing to access, export or delete on our side. All rendering state lives in your application's process.

## Contact

Open an issue on the [TGLES GitHub repository](https://github.com/Witch-Launcher/TGLES).
