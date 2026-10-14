#include "puzzle.hpp"

#include <Jolt/Math/Trigonometry.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>

#include <baker/std_interop.hpp>
#include <base/rng.hpp>
#include <base/words.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace contraption {
namespace {

constexpr float kRadians = 0.017453292f;
constexpr float kDepth = 0.5f;  // half the depth of every piece, in z

bool read_piece(const baker::Array& a, const Catalog& c, Piece& out, std::string& error) {
    out.name = base::name(a);
    auto k = a.find_sym(baker::Symbol("kind"), baker::Symbol());
    out.kind = k ? c.kind(std::string(k->str())) : -1;
    if (out.kind < 0) {
        error = "piece " + out.name + " has no (kind ...) of (kinds ...)";
        return false;
    }
    out.x = base::number(a, "pos", 0, 0);
    out.y = base::number(a, "pos", 1, 0);
    out.angle = base::number(a, "angle", 0);
    out.fixed = base::number(a, "fixed", 0) != 0;
    return true;
}

}  // namespace

int Catalog::kind(const std::string& name) const {
    for (size_t i = 0; i < kinds.size(); ++i)
        if (kinds[i].name == name) return int(i);
    return -1;
}
int Catalog::level(const std::string& name) const {
    for (size_t i = 0; i < levels.size(); ++i)
        if (levels[i].name == name) return int(i);
    return -1;
}

bool load(const baker::Ref<baker::Array>& root, Catalog& out, std::string& error) {
    Catalog c;
    c.tree = root;
    if (const baker::Array* kinds = root->try_find(baker::Symbol("kinds")))
        for (const baker::Array& a : kinds->subarrays()) {
            Kind k;
            k.name = base::name(a);
            auto shape = a.find_sym(baker::Symbol("shape"), baker::Symbol("box"));
            const std::string sh = shape ? std::string(shape->str()) : "box";
            k.shape = sh == "sphere" ? Shape::Sphere : sh == "bucket" ? Shape::Bucket : Shape::Box;
            k.w = std::max(0.02f, base::number(a, "size", 0, 1));
            k.h = std::max(0.02f, base::number(a, "size", 1, k.w));
            k.mass = std::max(0.0f, base::number(a, "mass", 0));
            k.friction = base::number(a, "friction", 0.5f);
            k.bounce = base::number(a, "bounce", 0.1f);
            k.hinge = base::number(a, "hinge", 0) != 0;
            c.kinds.push_back(std::move(k));
        }
    if (c.kinds.empty()) {
        error = "no (kinds ...) in the data";
        return false;
    }
    if (const baker::Array* levels = root->try_find(baker::Symbol("levels")))
        for (const baker::Array& a : levels->subarrays()) {
            Level l;
            l.name = base::name(a);
            if (auto d = a.find_str(baker::Symbol("desc"), "")) l.desc = std::string(*d);
            if (const baker::Array* p = a.try_find(baker::Symbol("pieces")))
                for (const baker::Array& pa : p->subarrays()) {
                    Piece piece;
                    if (!read_piece(pa, c, piece, error)) return false;
                    l.pieces.push_back(std::move(piece));
                }
            if (const baker::Array* t = a.try_find(baker::Symbol("tray")))
                for (const baker::Array& ta : t->subarrays()) {
                    const int k = c.kind(base::name(ta));
                    if (k < 0) {
                        error = "level " + l.name + ": the tray names " + base::name(ta) +
                                ", which is not one of (kinds ...)";
                        return false;
                    }
                    l.tray.emplace_back(k, ta.size() > 1 ? int(ta[1].as_int().value_or(1)) : 1);
                }
            if (const baker::Array* p = a.try_find(baker::Symbol("intended")))
                for (const baker::Array& pa : p->subarrays()) {
                    Piece piece;
                    if (!read_piece(pa, c, piece, error)) return false;
                    l.intended.push_back(std::move(piece));
                }
            if (const baker::Array* g = a.try_find(baker::Symbol("goal")); g && g->size() > 1)
                l.goal = (*g)[1];
            for (int i = 0; i < 4; ++i)
                l.bounds[i] = base::number(a, "bounds", size_t(i), l.bounds[i]);
            c.levels.push_back(std::move(l));
        }
    if (c.levels.empty()) {
        error = "no (levels ...) in the data";
        return false;
    }
    out = std::move(c);
    return true;
}

Run::Run(const Catalog& catalog, const Level& level, const std::vector<Piece>& placed)
    : catalog_(catalog) {
    JPH::BodyInterface& bi = world_.bodies();
    // The floor: y = 0 is its top.
    JPH::BodyCreationSettings floor(new JPH::BoxShape(JPH::Vec3(40, 0.5f, 4)),
                                    JPH::RVec3(0, -0.5f, 0), JPH::Quat::sIdentity(),
                                    JPH::EMotionType::Static, base::layers::kStatic);
    floor.mFriction = 0.6f;
    bi.CreateAndAddBody(floor, JPH::EActivation::DontActivate);

    for (const auto* list : {&level.pieces, &placed})
        for (const Piece& p : *list) {
            const Kind& k = catalog_.kinds[size_t(p.kind)];
            JPH::Ref<JPH::Shape> shape;
            if (k.shape == Shape::Sphere) {
                shape = new JPH::SphereShape(k.w / 2);
            } else if (k.shape == Shape::Bucket) {
                // A floor and two walls, open at the top.
                JPH::StaticCompoundShapeSettings cup;
                const float t = 0.05f;
                cup.AddShape(JPH::Vec3(0, -k.h / 2 + t, 0), JPH::Quat::sIdentity(),
                             new JPH::BoxShape(JPH::Vec3(k.w / 2, t, kDepth)));
                cup.AddShape(JPH::Vec3(-k.w / 2 + t, 0, 0), JPH::Quat::sIdentity(),
                             new JPH::BoxShape(JPH::Vec3(t, k.h / 2, kDepth)));
                cup.AddShape(JPH::Vec3(k.w / 2 - t, 0, 0), JPH::Quat::sIdentity(),
                             new JPH::BoxShape(JPH::Vec3(t, k.h / 2, kDepth)));
                shape = cup.Create().Get();
            } else {
                shape = new JPH::BoxShape(JPH::Vec3(k.w / 2, k.h / 2, kDepth));
            }
            const bool moving = !p.fixed && k.mass > 0 && k.shape != Shape::Bucket;
            JPH::BodyCreationSettings s(
                shape, JPH::RVec3(p.x, p.y, 0),
                JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), p.angle * kRadians),
                moving ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
                moving ? base::layers::kMoving : base::layers::kStatic);
            s.mFriction = k.friction;
            s.mRestitution = k.bounce;
            if (moving) {
                s.mAllowedDOFs = JPH::EAllowedDOFs::Plane2D;
                s.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                s.mMassPropertiesOverride.mMass = k.mass;
            }
            const JPH::BodyID id = bi.CreateAndAddBody(
                s, moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            if (moving && k.hinge) {
                JPH::HingeConstraintSettings hinge;
                hinge.mSpace = JPH::EConstraintSpace::WorldSpace;
                hinge.mPoint1 = hinge.mPoint2 = JPH::RVec3(p.x, p.y, 0);
                hinge.mHingeAxis1 = hinge.mHingeAxis2 = JPH::Vec3::sAxisZ();
                hinge.mNormalAxis1 = hinge.mNormalAxis2 = JPH::Vec3::sAxisX();
                JPH::BodyLockWrite lock(world_.system().GetBodyLockInterfaceNoLock(), id);
                world_.system().AddConstraint(
                    hinge.Create(JPH::Body::sFixedToWorld, lock.GetBody()));
            }
            pieces_.push_back(&p);
            bodies_.push_back(id);
        }
}

void Run::step(float dt) {
    world_.step(dt);
}

Run::Pose Run::pose(size_t i) const {
    JPH::BodyInterface& bi = const_cast<base::Physics&>(world_).bodies();
    const JPH::RVec3 p = bi.GetPosition(bodies_[i]);
    const JPH::Quat q = bi.GetRotation(bodies_[i]);
    // Locked to the plane, the rotation is about z alone. Jolt's trigonometry, not the C
    // library's, so a goal judged from it is the same on every platform.
    const float angle = 2.0f * JPH::ATan2(q.GetZ(), q.GetW());
    return {float(p.GetX()), float(p.GetY()), angle, {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}};
}

int Run::find(const std::string& name) const {
    for (size_t i = 0; i < pieces_.size(); ++i)
        if (pieces_[i]->name == name) return int(i);
    return -1;
}

bool Run::inside(int a, int b) const {
    if (a < 0 || b < 0) return false;
    const Kind& cup = catalog_.kinds[size_t(pieces_[size_t(b)]->kind)];
    const Pose pa = pose(size_t(a)), pb = pose(size_t(b));
    // a's center in b's frame.
    const float dx = pa.x - pb.x, dy = pa.y - pb.y;
    const float c = JPH::Cos(-pb.angle_rad), s = JPH::Sin(-pb.angle_rad);
    const float lx = c * dx - s * dy, ly = s * dx + c * dy;
    return std::fabs(lx) < cup.w / 2 - 0.1f && ly > -cup.h / 2 + 0.1f && ly < cup.h / 2;
}

Attempt attempt(const Catalog& catalog, const Level& level, const std::vector<Piece>& placed,
                const Rules& rules, const GoalTest& goal) {
    Run run(catalog, level, placed);
    const float dt = 1.0f / rules.step_hz;
    const int total = static_cast<int>(rules.run_seconds * rules.step_hz + 0.5f);
    const int hold = std::max(1, static_cast<int>(rules.hold_seconds * rules.step_hz + 0.5f));
    Attempt a;
    int held = 0;
    for (int i = 0; i < total; ++i) {
        run.step(dt);
        a.steps = i + 1;
        held = goal(run) ? held + 1 : 0;
        if (held >= hold) {
            a.solved = true;
            a.seconds = float(i + 1 - hold) * dt;
            break;
        }
        // Everything at rest and the goal not holding: nothing more will happen.
        if (held == 0 && run.physics().system().GetNumActiveBodies(JPH::EBodyType::RigidBody) == 0)
            break;
    }
    return a;
}

Piece snapped(Piece p, const Rules& rules) {
    if (rules.grid > 0) {
        p.x = std::round(p.x / rules.grid) * rules.grid;
        p.y = std::round(p.y / rules.grid) * rules.grid;
    }
    if (rules.angle_step > 0) p.angle = std::round(p.angle / rules.angle_step) * rules.angle_step;
    return p;
}

double robustness(const Catalog& catalog, const Level& level, const Rules& rules,
                  const GoalTest& goal, int samples, float metres, float degrees, uint64_t seed) {
    if (samples <= 0) return 0;
    base::Rng rng(seed, 0x5851F42D4C957F2Dull);
    int solved = 0;
    for (int i = 0; i < samples; ++i) {
        std::vector<Piece> placed = level.intended;
        for (Piece& p : placed) {
            p.x += rng.uniform(-metres, metres);
            p.y += rng.uniform(-metres, metres);
            p.angle += rng.uniform(-degrees, degrees);
        }
        solved += attempt(catalog, level, placed, rules, goal).solved ? 1 : 0;
    }
    return double(solved) / samples;
}

Cheese cheese(const Catalog& catalog, const Level& level, const Rules& rules, const GoalTest& goal,
              int tries, uint64_t seed) {
    Cheese out;
    base::Rng rng(seed ^ 0xC4EE5Eull, 0x5851F42D4C957F2Dull);
    // The tray as a list of kinds, one entry per piece.
    std::vector<int> tray;
    for (const auto& [kind, count] : level.tray)
        for (int i = 0; i < count; ++i) tray.push_back(kind);
    const int intended = int(level.intended.size());
    for (int k = 0; k < intended && k <= int(tray.size()); ++k) {
        const int n = k == 0 ? 1 : tries;
        for (int t = 0; t < n; ++t) {
            std::vector<int> pool = tray;
            std::vector<Piece> placed;
            for (int i = 0; i < k; ++i) {
                const size_t pick = rng.next() % pool.size();
                Piece p;
                p.name = "try" + std::to_string(i);
                p.kind = pool[pick];
                pool.erase(pool.begin() + std::ptrdiff_t(pick));
                p.x = rng.uniform(level.bounds[0], level.bounds[2]);
                p.y = rng.uniform(level.bounds[1], level.bounds[3]);
                p.angle = rng.uniform(-60, 60);
                placed.push_back(snapped(p, rules));
            }
            ++out.tried;
            if (attempt(catalog, level, placed, rules, goal).solved) {
                out.fewest = k;
                out.found = std::move(placed);
                return out;
            }
        }
    }
    return out;
}

std::string to_dta(const std::vector<Piece>& placed, const Catalog& catalog) {
    std::string s = "(placed";
    char line[160];
    for (const Piece& p : placed) {
        std::snprintf(line, sizeof line, "\n   (%s (kind %s) (pos %.3g %.3g) (angle %.3g))",
                      p.name.c_str(), catalog.kinds[size_t(p.kind)].name.c_str(), double(p.x),
                      double(p.y), double(p.angle));
        s += line;
    }
    return s + ")";
}

}  // namespace contraption
