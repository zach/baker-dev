#include "base/physics.hpp"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <doctest.h>

#include <cstdio>

namespace {

// A floor, a stack of boxes, and a ball thrown at it: contacts, friction, and rotation.
uint64_t topple(int steps) {
    base::Physics world;
    JPH::BodyInterface& bi = world.bodies();
    JPH::BodyCreationSettings floor(new JPH::BoxShape(JPH::Vec3(20, 0.5f, 20)),
                                    JPH::RVec3(0, -0.5f, 0), JPH::Quat::sIdentity(),
                                    JPH::EMotionType::Static, base::layers::kStatic);
    bi.CreateAndAddBody(floor, JPH::EActivation::DontActivate);
    for (int i = 0; i < 6; ++i) {
        JPH::BodyCreationSettings box(new JPH::BoxShape(JPH::Vec3(0.25f, 0.25f, 0.25f)),
                                      JPH::RVec3(0.01f * float(i), 0.25f + 0.5f * float(i), 0),
                                      JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic,
                                      base::layers::kMoving);
        bi.CreateAndAddBody(box, JPH::EActivation::Activate);
    }
    JPH::BodyCreationSettings ball(new JPH::SphereShape(0.3f), JPH::RVec3(-4, 1.5f, 0),
                                   JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic,
                                   base::layers::kMoving);
    const JPH::BodyID id = bi.CreateAndAddBody(ball, JPH::EActivation::Activate);
    bi.SetLinearVelocity(id, JPH::Vec3(9, 1, 0.3f));
    for (int i = 0; i < steps; ++i) world.step(1.0f / 60.0f);
    return world.hash();
}

}  // namespace

TEST_CASE("physics: the same world, stepped the same, is the same bit for bit") {
    const uint64_t a = topple(240);
    CHECK(a == topple(240));
    CHECK(a != topple(239));
    // The same on every compiler and CPU the stacks build with (Jolt's
    // JPH_CROSS_PLATFORM_DETERMINISTIC, no contracted floating point): this number was
    // computed on Linux with GCC; the MSVC job checks it on Windows.
    MESSAGE("topple hash: ", a);
    CHECK(a == 13931925006267741186ull);
}
