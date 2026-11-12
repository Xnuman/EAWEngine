#pragma once

// Loaders from Alamo file-level data (ALO models, ALA animations) to engine-native resources.

#include "formats/ala_animation.hpp"
#include "formats/alo_model.hpp"
#include "assets/animation.hpp"
#include "assets/model.hpp"
#include "assets/skeleton.hpp"

namespace eaw {

[[nodiscard]] Skeleton load_skeleton(const AloModelData& model);
// The skeleton, every mesh with its materials interpreted (LegacyMaterial) and its default appearance
// decided by the ALO naming rules, skin palettes resolved to model-wide bones, and the proxies.
[[nodiscard]] Model load_model(const AloModelData& model);
// An ALA stores one key more than a loop plays (the last repeats the first).
[[nodiscard]] Animation load_animation(const AlaAnimationData& animation);

} // namespace eaw
