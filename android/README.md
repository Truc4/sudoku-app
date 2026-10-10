# Android build

A minimal NDK/CMake wrapper around `ui/backend_sokol.c` + the engine sources — no Java/Kotlin code.
`sokol_app.h` implements `ANativeActivity_onCreate` itself, so the manifest just points a stock
`android.app.NativeActivity` at `libsudoku.so` (see `app/src/main/AndroidManifest.xml`'s
`android.app.lib_name` meta-data) and that's the whole app.

No Gradle wrapper is committed (keeps a binary jar out of the repo) — CI provisions Gradle directly
via `gradle/actions/setup-gradle` (see `.github/workflows/android.yml`). To build locally:

- **Android Studio**: File > Open > this `android/` directory. It'll supply its own Gradle/SDK/NDK.
- **Command line**: install Gradle, the Android SDK (`platforms;android-34`, `build-tools;34.0.0`),
  and NDK `26.3.11579264` + CMake `3.22.1` yourself, then `gradle -p android assembleDebug`. Output:
  `android/app/build/outputs/apk/debug/app-debug.apk`.

Builds `arm64-v8a` only (covers the vast majority of phones from the last several years) — add more
ABIs to `app/build.gradle`'s `ndk.abiFilters` if you need 32-bit or x86 device/emulator support.

**Status: scaffolded, not yet built anywhere** (this sandbox has no Android SDK/NDK — multi-GB
install, out of scope for this pass). First real build/validation happens in CI; iterate from the
Actions logs if `assembleDebug` fails.
