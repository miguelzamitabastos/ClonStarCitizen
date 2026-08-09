# ClonStarCitizen

Custom C++20 ECS/DOD game engine targeting Vulkan (GLFW window, Flecs ECS, GLM math).
See `.cursorrules` for the mandatory architecture rules (DOD/ECS, no dynamic allocation
in the main loop, GLM-only vector math via `include/engine/math/glm.hpp`).

## Cursor Cloud specific instructions

This is a single native executable (`clon_star_citizen`), not a client/server product.
There is one "service": the engine binary itself.

### Build / lint / run

- Build system: CMake. Dependencies `flecs` (v4.1.6) and `glm` (1.0.1) are pulled via
  `FetchContent` at configure time into `build/_deps` (needs network on first configure only).
- Configure + build: use the `fast_compile` skill (`.cursor/skills/fast_compile/SKILL.md`),
  i.e. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build -j$(nproc)`.
- Lint: there is no separate lint target. Linting is the compiler warning set
  (`-Wall -Wextra -Wpedantic`) enforced during the build; a clean build is a clean lint.
- Tests: there is currently no automated test suite in this repo.

### Running headless (no GPU / no physical display)

The engine opens a GLFW window and requires a Vulkan device with swapchain + present
support. Cloud VMs have no GPU and no display, so run against Mesa's software Vulkan
driver (lavapipe/llvmpipe) on a virtual X server (Xvfb). Required setup:

- A virtual display, e.g. `Xvfb :99 -screen 0 1280x720x24 -ac +extension GLX +render -noreset`
  (run it as a long-lived background process, e.g. in a tmux session).
- Environment variables when launching the binary:
  - `DISPLAY=:99`
  - `XDG_RUNTIME_DIR=/tmp/xdg-runtime` (create it `chmod 700`; Vulkan/X WSI needs it set)
  - `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json` (forces the lavapipe software ICD)

Sanity check the software driver with
`DISPLAY=:99 VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json vulkaninfo --summary`
(expect `deviceName = llvmpipe ...`, `deviceType = PHYSICAL_DEVICE_TYPE_CPU`).

### Gotchas

- The render loop only exits when the window closes, which never happens under Xvfb.
  Run the binary with a `timeout`, or start it in the background and kill the PID.
- The `main()` startup line (`ClonStarCitizen online — ...`) goes to `stdout`, which is
  fully buffered when not attached to a TTY. When piping/redirecting, wrap the run in
  `stdbuf -oL -eL` (or read `stderr`, which is unbuffered) or the line is lost on kill.
- To capture a rendered frame, screenshot the Xvfb root window while the app runs:
  `DISPLAY=:99 import -window root out.png` (ImageMagick).
- The default system compiler (`cc`/`c++`) is clang, which builds against the newest
  installed GCC toolchain; the matching `libstdc++-*-dev` must be present or linking
  fails with `cannot find -lstdc++`.
