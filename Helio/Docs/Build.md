# Build

## Prerequisites

1. **Windows 10 or 11**
2. **Visual Studio 2022** (Community or higher). Install the *Desktop development with C++* workload (gives you MSVC, the Windows SDK, CMake, Ninja).
3. **CMake 3.27+** — comes with VS 2022. Verify with `cmake --version`.
4. **Vulkan SDK 1.4+** — download and install from <https://vulkan.lunarg.com/>. The installer sets `VULKAN_SDK` automatically. This gives you Vulkan headers, the validation layer, `slangc`, `dxc`, and RenderDoc compatibility.
5. **vcpkg** — clone and bootstrap once:

   ```powershell
   git clone https://github.com/microsoft/vcpkg.git C:/Users/<you>/vcpkg
   C:/Users/<you>/vcpkg/bootstrap-vcpkg.bat -disableMetrics
   ```

   Then set `VCPKG_ROOT` in your shell (or persistently via System Properties → Environment Variables):

   ```powershell
   $env:VCPKG_ROOT = "C:/Users/<you>/vcpkg"
   ```

## Configure

From the repo root:

```powershell
cmake --preset windows-msvc-debug
```

The first configure downloads and builds all vcpkg dependencies (volk, VMA, SDL3, spdlog, hlslpp, Tracy). That takes ~5-15 minutes once. Subsequent configures are seconds.

Available presets:
- `windows-msvc-debug` — Debug build, validation layer ON, Tracy ON, overlay ON.
- `windows-msvc-release` — RelWithDebInfo, validation OFF, Tracy ON, overlay ON.

## Build

```powershell
cmake --build build/windows-msvc-debug --target Game --config Debug
```

Or open `build/windows-msvc-debug/Helio.sln` in Visual Studio / Rider.

## Run

```powershell
./build/windows-msvc-debug/bin/Debug/Game.exe
```

## IDE setup

**Rider** — `File → Open → Solution` → pick `build/windows-msvc-debug/Helio.sln`. Rider recognizes vcpkg automatically through the toolchain file in the preset.

**VSCode** — install the *CMake Tools* extension. It auto-detects `CMakePresets.json`. Pick the `windows-msvc-debug` preset from the status bar.

## Common errors

- **`Could not find toolchain file: $env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`** — `VCPKG_ROOT` is not set in your environment. Set it or restart your shell after setting it persistently.
- **`Could not find a package configuration file provided by "volk"`** — vcpkg dependencies failed to install. Re-run the configure step; check `build/windows-msvc-debug/vcpkg-manifest-install.log` for the actual failure.
- **`slangc not found`** — install or repair the Vulkan SDK; ensure `VULKAN_SDK` points at its install directory.
