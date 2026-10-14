#include "base/nav.hpp"

#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace base {
namespace {

constexpr int kMaxPolys = 512;      // in one path query
constexpr int kMaxNodes = 4096;     // Detour's search
constexpr unsigned char kArea = 0;  // every polygon and link: abilities are flags, not areas

// Recast's working memory for one tile, freed whatever happens.
struct Build {
    rcContext ctx{false};
    rcHeightfield* solid = rcAllocHeightfield();
    rcCompactHeightfield* compact = rcAllocCompactHeightfield();
    rcContourSet* contours = rcAllocContourSet();
    rcPolyMesh* mesh = rcAllocPolyMesh();
    rcPolyMeshDetail* detail = rcAllocPolyMeshDetail();
    Build() = default;
    Build(const Build&) = delete;
    Build& operator=(const Build&) = delete;
    ~Build() {
        rcFreeHeightField(solid);
        rcFreeCompactHeightfield(compact);
        rcFreeContourSet(contours);
        rcFreePolyMesh(mesh);
        rcFreePolyMeshDetail(detail);
    }
};

bool fail(std::string* error, const char* what) {
    if (error) *error = what;
    return false;
}

}  // namespace

void NavGeometry::box(NavPoint c, NavPoint h, float yaw) {
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    const int base = int(verts.size() / 3);
    for (int i = 0; i < 8; ++i) {
        const float lx = (i & 1) ? h.x : -h.x, ly = (i & 2) ? h.y : -h.y, lz = (i & 4) ? h.z : -h.z;
        verts.push_back(c.x + cs * lx + sn * lz);
        verts.push_back(c.y + ly);
        verts.push_back(c.z - sn * lx + cs * lz);
    }
    // Each face as two triangles, then wound so its normal points away from the center.
    static constexpr int kFaces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4},
                                         {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    auto v = [&](int i, int axis) { return verts[size_t(base + i) * 3 + size_t(axis)]; };
    for (const auto& f : kFaces) {
        for (const auto& t : {std::array<int, 3>{f[0], f[1], f[2]}, {f[0], f[2], f[3]}}) {
            float e0[3], e1[3], n[3], out[3];
            for (int a = 0; a < 3; ++a) {
                e0[a] = v(t[1], a) - v(t[0], a);
                e1[a] = v(t[2], a) - v(t[0], a);
                out[a] = (v(t[0], a) + v(t[1], a) + v(t[2], a)) / 3 - (a == 0   ? c.x
                                                                       : a == 1 ? c.y
                                                                                : c.z);
            }
            // Recast's normal: e0 x e1.
            n[0] = e0[1] * e1[2] - e0[2] * e1[1];
            n[1] = e0[2] * e1[0] - e0[0] * e1[2];
            n[2] = e0[0] * e1[1] - e0[1] * e1[0];
            const bool outward = n[0] * out[0] + n[1] * out[1] + n[2] * out[2] > 0;
            tris.push_back(base + t[0]);
            tris.push_back(base + (outward ? t[1] : t[2]));
            tris.push_back(base + (outward ? t[2] : t[1]));
        }
    }
}

void NavGeometry::link(NavPoint from, NavPoint to, uint16_t flags, bool both_ways) {
    links.push_back({from, to, flags, both_ways});
}

struct NavMesh::Impl {
    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;
    ~Impl() {
        dtFreeNavMeshQuery(query);
        dtFreeNavMesh(mesh);
    }
};

NavMesh::NavMesh() : impl_(std::make_unique<Impl>()) {}
NavMesh::~NavMesh() = default;

bool NavMesh::init(const NavSettings& settings) {
    settings_ = settings;
    impl_ = std::make_unique<Impl>();
    impl_->mesh = dtAllocNavMesh();
    impl_->query = dtAllocNavMeshQuery();
    if (!impl_->mesh || !impl_->query) return false;
    dtNavMeshParams p{};
    p.orig[0] = settings.origin_x;
    p.orig[1] = settings.bottom;
    p.orig[2] = settings.origin_z;
    p.tileWidth = settings.tile_size;
    p.tileHeight = settings.tile_size;
    int tile_bits = 1;
    while ((1 << tile_bits) < settings.max_tiles) ++tile_bits;
    tile_bits = std::min(tile_bits, 14);
    p.maxTiles = 1 << tile_bits;
    p.maxPolys = 1 << (22 - tile_bits);
    if (dtStatusFailed(impl_->mesh->init(&p))) return false;
    return !dtStatusFailed(impl_->query->init(impl_->mesh, kMaxNodes));
}

void NavMesh::tile_of(float x, float z, int& tx, int& tz) const {
    tx = int(std::floor((x - settings_.origin_x) / settings_.tile_size));
    tz = int(std::floor((z - settings_.origin_z) / settings_.tile_size));
}

bool NavMesh::has_tile(int tx, int tz) const {
    return impl_->mesh && impl_->mesh->getTileRefAt(tx, tz, 0) != 0;
}

void NavMesh::remove_tile(int tx, int tz) {
    if (!impl_->mesh) return;
    if (const dtTileRef ref = impl_->mesh->getTileRefAt(tx, tz, 0))
        impl_->mesh->removeTile(ref, nullptr, nullptr);
}

bool NavMesh::add_tile(int tx, int tz, const NavGeometry& g, std::string* error) {
    if (!impl_->mesh) return fail(error, "nav: not initialized");
    remove_tile(tx, tz);
    const NavSettings& s = settings_;
    rcConfig c{};
    c.cs = s.cell_size;
    c.ch = s.cell_height;
    c.walkableSlopeAngle = s.max_slope;
    c.walkableHeight = int(std::ceil(s.agent_height / c.ch));
    c.walkableClimb = int(std::floor(s.max_climb / c.ch));
    c.walkableRadius = int(std::ceil(s.agent_radius / c.cs));
    c.maxEdgeLen = int(12 / c.cs);
    c.maxSimplificationError = 1.3f;
    c.minRegionArea = 8 * 8;
    c.mergeRegionArea = 20 * 20;
    c.maxVertsPerPoly = 6;
    c.tileSize = int(s.tile_size / c.cs);
    c.borderSize = c.walkableRadius + 3;
    c.width = c.tileSize + c.borderSize * 2;
    c.height = c.width;
    c.detailSampleDist = 6 * c.cs;
    c.detailSampleMaxError = c.ch;
    const float x0 = s.origin_x + float(tx) * s.tile_size,
                z0 = s.origin_z + float(tz) * s.tile_size;
    c.bmin[0] = x0 - float(c.borderSize) * c.cs;
    c.bmin[1] = s.bottom;
    c.bmin[2] = z0 - float(c.borderSize) * c.cs;
    c.bmax[0] = x0 + s.tile_size + float(c.borderSize) * c.cs;
    c.bmax[1] = s.top;
    c.bmax[2] = z0 + s.tile_size + float(c.borderSize) * c.cs;

    Build b;
    const int nverts = int(g.verts.size() / 3), ntris = int(g.tris.size() / 3);
    if (!b.solid || !b.compact || !b.contours || !b.mesh || !b.detail)
        return fail(error, "nav: out of memory");
    if (!rcCreateHeightfield(&b.ctx, *b.solid, c.width, c.height, c.bmin, c.bmax, c.cs, c.ch))
        return fail(error, "nav: could not make the heightfield");
    std::vector<unsigned char> areas(size_t(ntris), 0);
    if (ntris > 0) {
        rcMarkWalkableTriangles(&b.ctx, c.walkableSlopeAngle, g.verts.data(), nverts, g.tris.data(),
                                ntris, areas.data());
        if (!rcRasterizeTriangles(&b.ctx, g.verts.data(), nverts, g.tris.data(), areas.data(),
                                  ntris, *b.solid, c.walkableClimb))
            return fail(error, "nav: could not rasterize");
    }
    rcFilterLowHangingWalkableObstacles(&b.ctx, c.walkableClimb, *b.solid);
    rcFilterLedgeSpans(&b.ctx, c.walkableHeight, c.walkableClimb, *b.solid);
    rcFilterWalkableLowHeightSpans(&b.ctx, c.walkableHeight, *b.solid);
    if (!rcBuildCompactHeightfield(&b.ctx, c.walkableHeight, c.walkableClimb, *b.solid, *b.compact))
        return fail(error, "nav: could not compact the heightfield");
    if (b.compact->spanCount == 0) return true;  // nothing to stand on here
    if (!rcErodeWalkableArea(&b.ctx, c.walkableRadius, *b.compact) ||
        !rcBuildDistanceField(&b.ctx, *b.compact) ||
        !rcBuildRegions(&b.ctx, *b.compact, c.borderSize, c.minRegionArea, c.mergeRegionArea))
        return fail(error, "nav: could not build regions");
    if (!rcBuildContours(&b.ctx, *b.compact, c.maxSimplificationError, c.maxEdgeLen, *b.contours) ||
        !rcBuildPolyMesh(&b.ctx, *b.contours, c.maxVertsPerPoly, *b.mesh) ||
        !rcBuildPolyMeshDetail(&b.ctx, *b.mesh, *b.compact, c.detailSampleDist,
                               c.detailSampleMaxError, *b.detail))
        return fail(error, "nav: could not build polygons");
    if (b.mesh->npolys == 0) return true;
    for (int i = 0; i < b.mesh->npolys; ++i) {
        b.mesh->flags[i] = b.mesh->areas[i] == RC_WALKABLE_AREA ? nav::kWalk : 0;
        b.mesh->areas[i] = kArea;
    }

    // The links that start in this tile.
    std::vector<float> link_verts, link_radii;
    std::vector<unsigned short> link_flags;
    std::vector<unsigned char> link_areas, link_dirs;
    std::vector<unsigned int> link_ids;
    for (size_t i = 0; i < g.links.size(); ++i) {
        const NavGeometry::Link& l = g.links[i];
        if (l.from.x < x0 || l.from.x >= x0 + s.tile_size || l.from.z < z0 ||
            l.from.z >= z0 + s.tile_size)
            continue;
        for (const NavPoint& p : {l.from, l.to}) {
            link_verts.push_back(p.x);
            link_verts.push_back(p.y);
            link_verts.push_back(p.z);
        }
        link_radii.push_back(s.agent_radius * 2);
        link_flags.push_back(l.flags);
        link_areas.push_back(kArea);
        link_dirs.push_back(l.both_ways ? DT_OFFMESH_CON_BIDIR : 0);
        link_ids.push_back(unsigned(i));
    }

    dtNavMeshCreateParams p{};
    p.verts = b.mesh->verts;
    p.vertCount = b.mesh->nverts;
    p.polys = b.mesh->polys;
    p.polyAreas = b.mesh->areas;
    p.polyFlags = b.mesh->flags;
    p.polyCount = b.mesh->npolys;
    p.nvp = b.mesh->nvp;
    p.detailMeshes = b.detail->meshes;
    p.detailVerts = b.detail->verts;
    p.detailVertsCount = b.detail->nverts;
    p.detailTris = b.detail->tris;
    p.detailTriCount = b.detail->ntris;
    p.offMeshConVerts = link_verts.data();
    p.offMeshConRad = link_radii.data();
    p.offMeshConFlags = link_flags.data();
    p.offMeshConAreas = link_areas.data();
    p.offMeshConDir = link_dirs.data();
    p.offMeshConUserID = link_ids.data();
    p.offMeshConCount = int(link_radii.size());
    p.walkableHeight = s.agent_height;
    p.walkableRadius = s.agent_radius;
    p.walkableClimb = s.max_climb;
    p.tileX = tx;
    p.tileY = tz;
    p.tileLayer = 0;
    rcVcopy(p.bmin, b.mesh->bmin);
    rcVcopy(p.bmax, b.mesh->bmax);
    p.cs = c.cs;
    p.ch = c.ch;
    p.buildBvTree = true;
    unsigned char* data = nullptr;
    int size = 0;
    if (!dtCreateNavMeshData(&p, &data, &size)) return fail(error, "nav: could not make the tile");
    if (dtStatusFailed(impl_->mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr))) {
        dtFree(data);
        return fail(error, "nav: could not add the tile");
    }
    return true;
}

bool NavMesh::nearest(NavPoint p, NavPoint& out, float reach, uint16_t abilities) const {
    if (!impl_->query) return false;
    dtQueryFilter filter;
    filter.setIncludeFlags(abilities | nav::kWalk);
    const float center[3] = {p.x, p.y, p.z}, extents[3] = {reach, 4, reach};
    dtPolyRef ref = 0;
    float at[3];
    if (dtStatusFailed(impl_->query->findNearestPoly(center, extents, &filter, &ref, at)) || !ref)
        return false;
    // Detour finds the polygon nearest within the box; its nearest point can lie outside it.
    if ((at[0] - p.x) * (at[0] - p.x) + (at[2] - p.z) * (at[2] - p.z) > reach * reach) return false;
    out = {at[0], at[1], at[2]};
    return true;
}

bool NavMesh::path(NavPoint from, NavPoint to, std::vector<NavPoint>& out,
                   uint16_t abilities) const {
    out.clear();
    if (!impl_->query) return false;
    dtQueryFilter filter;
    filter.setIncludeFlags(abilities | nav::kWalk);
    const float extents[3] = {2, 4, 2};
    const float a[3] = {from.x, from.y, from.z}, z[3] = {to.x, to.y, to.z};
    dtPolyRef start = 0, end = 0;
    float sa[3], sz[3];
    if (dtStatusFailed(impl_->query->findNearestPoly(a, extents, &filter, &start, sa)) || !start ||
        dtStatusFailed(impl_->query->findNearestPoly(z, extents, &filter, &end, sz)) || !end)
        return false;
    dtPolyRef polys[kMaxPolys];
    int npolys = 0;
    const dtStatus found =
        impl_->query->findPath(start, end, sa, sz, &filter, polys, &npolys, kMaxPolys);
    if (dtStatusFailed(found) || npolys == 0) return false;
    float goal[3] = {sz[0], sz[1], sz[2]};
    if (polys[npolys - 1] != end)
        impl_->query->closestPointOnPoly(polys[npolys - 1], sz, goal, nullptr);
    float straight[kMaxPolys * 3];
    int nstraight = 0;
    impl_->query->findStraightPath(sa, goal, polys, npolys, straight, nullptr, nullptr, &nstraight,
                                   kMaxPolys);
    for (int i = 0; i < nstraight; ++i)
        out.push_back({straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2]});
    return polys[npolys - 1] == end && !dtStatusDetail(found, DT_PARTIAL_RESULT);
}

float NavMesh::distance(NavPoint from, NavPoint to, uint16_t abilities) const {
    std::vector<NavPoint> p;
    if (!path(from, to, p, abilities)) return -1;
    float d = 0;
    for (size_t i = 1; i < p.size(); ++i)
        d += std::sqrt((p[i].x - p[i - 1].x) * (p[i].x - p[i - 1].x) +
                       (p[i].y - p[i - 1].y) * (p[i].y - p[i - 1].y) +
                       (p[i].z - p[i - 1].z) * (p[i].z - p[i - 1].z));
    return d;
}

int NavMesh::tiles() const {
    if (!impl_->mesh) return 0;
    int n = 0;
    const dtNavMesh* m = impl_->mesh;
    for (int i = 0; i < m->getMaxTiles(); ++i)
        if (const dtMeshTile* t = m->getTile(i); t && t->header) ++n;
    return n;
}

int NavMesh::polygons() const {
    if (!impl_->mesh) return 0;
    int n = 0;
    const dtNavMesh* m = impl_->mesh;
    for (int i = 0; i < m->getMaxTiles(); ++i)
        if (const dtMeshTile* t = m->getTile(i); t && t->header) n += t->header->polyCount;
    return n;
}

}  // namespace base
