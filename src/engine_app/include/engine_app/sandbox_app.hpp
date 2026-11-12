#pragma once

// The Direct3D 12 visual sandbox: a window, the renderer, and the map or model session (scene
// builds and hot reload, the simulation, cameras, selection and orders, debug drawing, the
// statistics panel) until the window closes.

#include "vfs/game_installation.hpp"
#include "vfs/virtual_file_system.hpp"
#include "sandbox/sandbox_options.hpp"

namespace eaw::engine_app {

// Returns the process exit code (1 when the window or device cannot be created).
[[nodiscard]] int run_sandbox(const SandboxOptions& options, VirtualFileSystem& vfs, const GameProfile& profile);

} // namespace eaw::engine_app
