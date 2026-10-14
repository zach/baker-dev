#include "base/physics.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <mutex>

namespace base {
namespace {

namespace broad {
constexpr JPH::BroadPhaseLayer kStatic(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr JPH::uint kCount = 2;
}  // namespace broad

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return broad::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == layers::kStatic ? broad::kStatic : broad::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == broad::kStatic ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
        return layer == layers::kMoving || bp == broad::kMoving;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == layers::kMoving || b == layers::kMoving;
    }
};

// Jolt's allocator, factory, and types are process-wide: set up once, never torn down (a
// game makes and drops worlds all through a search).
void setup_jolt() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

uint64_t mix(uint64_t h, const void* data, size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

}  // namespace

struct Physics::Impl {
    BroadPhaseLayers broad_phase;
    ObjectVsBroadPhase object_vs_broad;
    ObjectPairs pairs;
    JPH::TempAllocatorImpl temp{8 * 1024 * 1024};
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
    JPH::PhysicsSystem system;
};

Physics::Physics(const PhysicsOptions& options) {
    setup_jolt();
    impl_ = std::make_unique<Impl>();
    impl_->system.Init(options.max_bodies, 0, options.max_body_pairs, options.max_contacts,
                       impl_->broad_phase, impl_->object_vs_broad, impl_->pairs);
    impl_->system.SetGravity(options.gravity);
    system_ = &impl_->system;
}

Physics::~Physics() = default;

void Physics::step(float dt, int substeps) {
    impl_->system.Update(dt, substeps, &impl_->temp, &impl_->jobs);
}

uint64_t Physics::hash() const {
    JPH::BodyIDVector ids;
    impl_->system.GetBodies(ids);
    std::sort(ids.begin(), ids.end());
    uint64_t h = 0xCBF29CE484222325ull;
    const JPH::BodyLockInterfaceNoLock& locks = impl_->system.GetBodyLockInterfaceNoLock();
    for (const JPH::BodyID id : ids) {
        JPH::BodyLockRead lock(locks, id);
        if (!lock.Succeeded()) continue;
        const JPH::Body& b = lock.GetBody();
        const JPH::RVec3 p = b.GetPosition();
        const JPH::Quat q = b.GetRotation();
        const JPH::Vec3 lv = b.GetLinearVelocity();
        const JPH::Vec3 av = b.GetAngularVelocity();
        const float values[13] = {float(p.GetX()), float(p.GetY()), float(p.GetZ()), q.GetX(),
                                  q.GetY(),        q.GetZ(),        q.GetW(),        lv.GetX(),
                                  lv.GetY(),       lv.GetZ(),       av.GetX(),       av.GetY(),
                                  av.GetZ()};
        h = mix(h, values, sizeof values);
    }
    return h;
}

}  // namespace base
