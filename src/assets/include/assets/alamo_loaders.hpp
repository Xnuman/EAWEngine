#pragma once

// Loaders from Alamo file-level data (ALO models, ALA animations) to engine-native resources.

#include "assets/animation.hpp"
#include "assets/skeleton.hpp"
#include "formats/ala_animation.hpp"
#include "formats/alo_model.hpp"

namespace eaw {

[[nodiscard]] Skeleton load_skeleton(const AloModelData& model);
// An ALA stores one key more than a loop plays (the last repeats the first).
[[nodiscard]] Animation load_animation(const AlaAnimationData& animation);

} // namespace eaw
