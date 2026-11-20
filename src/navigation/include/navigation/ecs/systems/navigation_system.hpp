
#include "ecs/system_view.hpp"
#include "ecs/component_pool.hpp"
#include "junk/entity_id.hpp"

class JobSystem;


// Navigation (simulation_navigation.cpp; docs/ecs-design.md, 6.3, phase 3): route searches under
// their budget, the remaining routes, group cohesion, and space avoidance.
class NavigationSystem
{
public:
    using View = SystemView<Read<IdentityColumns>, Read<PlacementColumns>, Write<LocomotorColumns>, Read<GroundMoverColumns>, Write<RouteColumns>, Read<ComponentPool<FlightColumns>>,
                            Read<ComponentPool<CombatColumns>>, Write<ComponentPool<GroupColumns>>, Read<GroundState>, Write<Avoidance>, Write<PathQueue>, Write<LandFormations>, Read<SlotTable>,
                            Read<Clock>, Read<ComponentPool<SquadColumns>>, Read<MotionColumns>, Read<ComponentPool<CrushColumns>>, Read<Rules>, Read<ComponentPool<SquadMemberColumns>>,
                            Read<ComponentPool<TurnClipColumns>>>;

    void run(const View& view);

    void set_jobs(JobSystem* jobs) noexcept
    {
        jobs_ = jobs;
    }
    void ground_changed() noexcept
    {
        navigation_data_.reset();
    }

    std::uint64_t path_searches{};
    std::uint64_t partial_paths{};
    std::uint64_t path_expansions{};
    std::uint64_t shared_routes{};
    std::array<std::uint64_t, route_trace_kind_count> route_changes{};
    std::uint64_t blocked_stops{};
    std::size_t waiting_members{};
    std::uint64_t released_members{};

private:
    void process_path_queue(const View& view);
    [[nodiscard]] bool plan_shared_route(const View& view, PathRequest& request, std::uint64_t spent_before, bool first_in_tick);
    [[nodiscard]] bool plan_formation(const View& view, PathRequest& request, std::uint64_t spent_before, bool first_in_tick);
    void form_up(const View& view, PathRequest& request);


    struct PlannedRoute
    {
        bool unavailable{};
        // Its part of a group's shared route; and the search's stages while routes are traced.
        bool shared{};
        PathTrace trace;
        std::vector<PathPoint> waypoints;
        std::uint64_t searches{};
        std::uint64_t partials{};
        std::uint64_t expansions{};
    };

    struct RouteScratch
    {
        GridPathfinder route{0, ChunkLayout::refined};
        GridPathfinder legs{4096};
    };

    [[nodiscard]] PlannedRoute plan_route(const View& view, std::size_t index, const Vector3& target, RouteScratch& scratch);
    [[nodiscard]] PlannedRoute plan_member(const View& view, const PathRequest& request, std::size_t index, const Vector3& goal, RouteScratch& scratch);
    void place_route_end(const View& view, std::size_t index, PlannedRoute& planned, RouteScratch& scratch);
    void give_route(const View& view, std::size_t index, const Vector3& goal, const PlannedRoute& planned);

    template <typename Plan>
    void plan_batch(Plan&& plan);

    [[nodiscard]] std::size_t batch_limit() const noexcept;
    struct BatchEntry
    {
        std::size_t index{};
        Vector3 goal;
    };
    std::vector<BatchEntry> batch_;
    std::vector<PlannedRoute> planned_;
    std::vector<RouteScratch> route_scratch_ = std::vector<RouteScratch>(1);

    void plan_space_avoidance(const View& view);
    void measure_ground_units(const View& view);
    void plan_ground_avoidance(const View& view);
    std::vector<std::pair<float, std::uint32_t>> nearest_;
    std::vector<OrcaLine> orca_lines_;
    void measure_agents(const View& view);
    [[nodiscard]] static bool ground_steers(const View& view, std::size_t index) noexcept;
    [[nodiscard]] static Vector2 heading_toward_waypoint(const View& view, std::size_t index) noexcept;
    void resolve_blocks(const View& view);
    [[nodiscard]] bool plan_around(const View& view, std::size_t index, std::size_t blocker);
    static constexpr std::uint32_t around_budget = 2048;
    GridPathfinder around_{around_budget};
    static constexpr std::uint32_t around_whole_budget = 16384;
    GridPathfinder around_whole_{around_whole_budget};
    std::vector<std::uint8_t> obstacle_cells_;
    std::vector<std::uint32_t> marked_cells_;
    std::vector<std::uint32_t> nearby_;
    std::vector<EntityId> blocks_;
    void set_route(const View& view, std::size_t index, const std::vector<PathPoint>& waypoints, float height);
    void update_group_cohesion(const View& view);
    NavigationData navigation_data_;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::deque<RouteTrace>> unit_traces_;
    void record_route(const View& view, std::size_t index, RouteTraceKind kind, const PathTrace& stages);

public:
    void set_tracing(bool tracing);
    [[nodiscard]] const std::deque<RouteTrace>* unit_traces(EntityId id) const;

private:
    GridPathfinder pathfinder_{0, ChunkLayout::refined};

public:
    void prepare(const GroundMap& ground, const Footing& footing)
    {
        pathfinder_.prepare(navigation_data_, ground, footing);
    }

private:
    JobSystem* jobs_{};

    std::vector<std::uint32_t> query_;
    std::vector<std::uint32_t> neighbours_;
};
NavigationSystem navigation_;