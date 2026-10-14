// The base's navigation (game-stacks.md §2.1): Recast builds a navigation mesh from a level's
// triangles, Detour finds paths on it. One NavMesh is a grid of square tiles, so a streamed
// world adds a cell's tile when it loads and drops it when it leaves (Wayfarer), and a house
// is one tile (Hearth).
//
//   base::NavGeometry g;
//   g.box({0, -0.1f, 0}, {8, 0.1f, 8});          // a floor
//   g.box({2, 1, 0}, {0.1f, 1, 3});              // a wall
//   g.link({4, 0, 0}, {4, 3, 2}, base::nav::kClimb);  // a ledge only a climber takes
//   base::NavMesh nav;
//   nav.init({.tile_size = 16});
//   nav.add_tile(0, 0, g);
//   std::vector<base::NavPoint> path;
//   nav.path({-6, 0, -6}, {6, 0, 6}, path);                     // walking only
//   nav.path({-6, 0, -6}, {4, 3, 2}, path, base::nav::kAll);    // with every ability
//
// A link is an off-mesh connection: a move the mesh cannot show (climbing a ledge, gliding
// over a gap). Its flags say which ability takes it; a path query names the abilities the
// walker has, so "reachable by then" is a query, not a simulation.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace base {

namespace nav {
// Abilities, as path-query flags. The mesh's own polygons are kWalk.
constexpr uint16_t kWalk = 1;
constexpr uint16_t kClimb = 2;
constexpr uint16_t kGlide = 4;
constexpr uint16_t kSwim = 8;
constexpr uint16_t kAll = 0xffff;
}  // namespace nav

struct NavPoint {
    float x = 0, y = 0, z = 0;
};

// What a tile is built from: triangles (y is up), and links.
struct NavGeometry {
    struct Link {
        NavPoint from, to;
        uint16_t flags = nav::kClimb;
        bool both_ways = false;
    };
    std::vector<float> verts;  // x y z
    std::vector<int> tris;     // three vertex indices each, wound as Recast reads a face that
                               // points up: box() winds every face outward
    std::vector<Link> links;

    // A box: center, half extents, and a turn about y (radians).
    void box(NavPoint center, NavPoint half, float yaw = 0);
    void link(NavPoint from, NavPoint to, uint16_t flags, bool both_ways = false);
};

struct NavSettings {
    float tile_size = 32;  // metres, square
    float origin_x = 0;    // the corner of tile (0, 0)
    float origin_z = 0;
    float cell_size = 0.25f;  // the voxel grid
    float cell_height = 0.1f;
    float agent_height = 1.8f;
    float agent_radius = 0.35f;
    float max_climb = 0.4f;         // a step the walker takes without an ability
    float max_slope = 45;           // degrees
    float bottom = -50, top = 100;  // the heights tiles span
    int max_tiles = 1024;
};

class NavMesh {
public:
    NavMesh();
    ~NavMesh();
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    [[nodiscard]] bool init(const NavSettings& settings);
    // Builds tile (tx, tz) from `g`, of which it uses what falls inside the tile and its
    // border; links belong to the tile their start is in. Replaces a tile already there.
    // False, with the reason, if Recast or Detour fails; an empty tile is not a failure.
    bool add_tile(int tx, int tz, const NavGeometry& g, std::string* error = nullptr);
    void remove_tile(int tx, int tz);
    [[nodiscard]] bool has_tile(int tx, int tz) const;
    // The tile a point is in.
    void tile_of(float x, float z, int& tx, int& tz) const;

    // The nearest point on the mesh within `reach` metres across and 4 up or down; false
    // if there is none.
    bool nearest(NavPoint p, NavPoint& out, float reach = 2, uint16_t abilities = nav::kAll) const;
    // A path of straight segments from `from` to `to`, using only links whose flags
    // `abilities` includes. True if it reaches `to` (within the mesh's reach of it); false
    // with `out` empty if there is no mesh near either end, or with `out` ending as near as
    // it got.
    bool path(NavPoint from, NavPoint to, std::vector<NavPoint>& out,
              uint16_t abilities = nav::kWalk) const;
    // The length of path(), in metres; negative if it does not reach.
    [[nodiscard]] float distance(NavPoint from, NavPoint to, uint16_t abilities = nav::kWalk) const;

    [[nodiscard]] int tiles() const;
    [[nodiscard]] int polygons() const;
    [[nodiscard]] const NavSettings& settings() const noexcept { return settings_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    NavSettings settings_;
};

}  // namespace base
