#pragma once

// The Direct3D 12 visual sandbox: a window, the renderer, and the map or model session (scene
// builds and hot reload, the simulation, cameras, selection and orders, debug drawing, the
// statistics panel) until the window closes.

#include "engine/sandbox_options.hpp"

#include "vfs/game_installation.hpp"
#include "vfs/virtual_file_system.hpp"

namespace eaw {

// Returns the process exit code (1 when the window or device cannot be created).
[[nodiscard]] int run_sandbox(const SandboxOptions& options, VirtualFileSystem& vfs, const GameProfile& profile);

} // namespace eaw
