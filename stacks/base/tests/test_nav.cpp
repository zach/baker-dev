#include "base/nav.hpp"

#include <doctest.h>

namespace {

// Two rooms joined by a door, and a shelf 3 m up that only a climber reaches.
base::NavGeometry rooms() {
    base::NavGeometry g;
    g.box({0, -0.1f, 0}, {10, 0.1f, 6});             // the floor, 20 by 12
    g.box({0, 1.25f, -3.5f}, {0.15f, 1.25f, 2.5f});  // the wall between, with a door at z 0..2
    g.box({0, 1.25f, 4}, {0.15f, 1.25f, 2});
    g.box({7, 1.5f, 4}, {2, 1.5f, 1.5f});  // the shelf: its top is at y 3
    g.link({7, 0, 2}, {7, 3, 4}, base::nav::kClimb);
    return g;
}

void build(base::NavMesh& nav, int tiles_across = 1) {
    REQUIRE(nav.init({.tile_size = 24.0f / float(tiles_across), .origin_x = -12, .origin_z = -12}));
    const base::NavGeometry g = rooms();
    for (int x = 0; x < tiles_across; ++x)
        for (int z = 0; z < tiles_across; ++z) {
            std::string error;
            CHECK_MESSAGE(nav.add_tile(x, z, g, &error), error);
        }
}

}  // namespace

TEST_CASE("nav: a path goes through the door, not the wall") {
    base::NavMesh nav;
    build(nav);
    CHECK(nav.tiles() == 1);
    CHECK(nav.polygons() > 0);
    std::vector<base::NavPoint> path;
    REQUIRE(nav.path({-6, 0, -3}, {6, 0, -3}, path));
    REQUIRE(path.size() >= 3);  // it turns at the door
    bool through_door = false;
    for (const base::NavPoint& p : path)
        if (p.x > -1 && p.x < 1) through_door |= p.z > -1.2f && p.z < 2.2f;
    CHECK(through_door);
    const float d = nav.distance({-6, 0, -3}, {6, 0, -3});
    CHECK(d > 12.5f);  // longer than the straight line through the wall
    CHECK(d < 20.0f);
}

TEST_CASE("nav: a link is taken only with its ability") {
    base::NavMesh nav;
    build(nav);
    base::NavPoint top;
    REQUIRE(nav.nearest({7, 3, 4}, top, 1));
    CHECK(top.y > 2.9f);
    std::vector<base::NavPoint> path;
    CHECK_FALSE(nav.path({-6, 0, -3}, {7, 3, 4}, path, base::nav::kWalk));
    CHECK(nav.path({-6, 0, -3}, {7, 3, 4}, path, base::nav::kWalk | base::nav::kClimb));
    CHECK(nav.distance({-6, 0, -3}, {7, 3, 4}, base::nav::kGlide) < 0);
}

TEST_CASE("nav: tiles join, and a path crosses them; a tile can leave") {
    base::NavMesh nav;
    build(nav, 2);
    CHECK(nav.tiles() == 4);
    CHECK(nav.distance({-6, 0, -3}, {3, 0, 3}) > 0);  // through the door, across all four
    std::vector<base::NavPoint> path;
    CHECK(nav.path({-6, 0, -3}, {7, 3, 4}, path, base::nav::kAll));
    nav.remove_tile(1, 1);
    CHECK(nav.tiles() == 3);
    CHECK_FALSE(nav.has_tile(1, 1));
    CHECK(nav.distance({-6, 0, -3}, {3, 0, 3}) < 0);
}
