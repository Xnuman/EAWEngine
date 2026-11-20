// Positions on an entity are given in its own frame: x forward (along its heading), y to its
// left, z up, in world units.
struct LocalPoint
{
    float forward{};
    float left{};
    float up{};

    friend bool operator==(const LocalPoint&, const LocalPoint&) = default;
};