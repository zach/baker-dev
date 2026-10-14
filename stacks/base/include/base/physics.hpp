// The base's physics (game-stacks.md §2.1): one Jolt world, built cross-platform
// deterministic, for the stacks that need rigid bodies, constraints, or vehicles. A game
// builds its bodies through Jolt's own API (`system()`, `bodies()`); this wraps what every
// such game repeats: Jolt's one-time setup, the two object layers, single-threaded stepping,
// and a hash of the world's state, so a test can prove a run replays bit for bit.
//
//   base::Physics world;                       // empty; a game makes a new one per attempt
//   JPH::BodyInterface& bi = world.bodies();
//   bi.CreateAndAddBody(settings, JPH::EActivation::Activate);
//   world.step(1.0f / 60.0f);
//   uint64_t h = world.hash();                 // positions, rotations, velocities, bit for bit
//
// Determinism, as Jolt defines it: the same bodies, created in the same order, stepped with
// the same time steps, on a build with JPH_CROSS_PLATFORM_DETERMINISTIC and floating point
// that does not contract (stacks_deterministic in cmake/dependencies.cmake). The job system
// is single-threaded: a prototype's worlds are small, and the order of work is then fixed.
#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <cstdint>
#include <memory>

namespace base {

// Object layers: what never moves, and what does. Static does not collide with static.
namespace layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
}  // namespace layers

struct PhysicsOptions {
    uint32_t max_bodies = 4096;
    uint32_t max_body_pairs = 8192;
    uint32_t max_contacts = 8192;
    JPH::Vec3 gravity = JPH::Vec3(0, -9.81f, 0);
};

class Physics {
public:
    explicit Physics(const PhysicsOptions& options = {});
    ~Physics();
    Physics(const Physics&) = delete;
    Physics& operator=(const Physics&) = delete;

    [[nodiscard]] JPH::PhysicsSystem& system() noexcept { return *system_; }
    [[nodiscard]] JPH::BodyInterface& bodies() noexcept {
        return system_->GetBodyInterfaceNoLock();
    }

    // Scratch memory for Jolt's queries and characters (CharacterVirtual::ExtendedUpdate).
    [[nodiscard]] JPH::TempAllocator& temp() noexcept;
    // One step of `dt` seconds, in `substeps` collision steps.
    void step(float dt, int substeps = 1);
    // FNV-1a over every body's position, rotation, and velocities, as bits, in body-ID order.
    [[nodiscard]] uint64_t hash() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    JPH::PhysicsSystem* system_ = nullptr;
};

}  // namespace base
