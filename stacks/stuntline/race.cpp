#include "race.hpp"

#include <Jolt/Math/Trigonometry.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#include <baker/std_interop.hpp>
#include <base/rng.hpp>
#include <base/words.hpp>

#include <algorithm>
#include <cmath>

namespace stuntline {
namespace {

// Every function of a float that feeds the simulation is Jolt's (JPH::Sin, Cos, Tan, ATan2):
// the C library's are not the same bits on every platform, and a race must replay on any.
// sqrt is IEEE's, correctly rounded everywhere.
constexpr float kRadians = 0.017453292f;
constexpr float kGravity = 9.81f;
constexpr float kThickness = 0.25f;  // half the road's thickness
constexpr float kOpen = 1000;        // a speed limit that does not limit

std::string sym(const baker::Array& a, const char* tag) {
    auto s = a.find_sym(baker::Symbol(tag), baker::Symbol());
    return s ? std::string(s->str()) : std::string();
}

// Heading, then pitch up, then roll: the orientation of a stretch of road.
JPH::Quat orient(float yaw, float pitch, float roll) {
    return JPH::Quat::sRotation(JPH::Vec3::sAxisY(), yaw) *
           JPH::Quat::sRotation(JPH::Vec3::sAxisX(), -pitch) *
           JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), roll);
}

void store(const JPH::Quat& q, float out[4]) {
    out[0] = q.GetX();
    out[1] = q.GetY();
    out[2] = q.GetZ();
    out[3] = q.GetW();
}

// The fastest a car takes a banked curve of radius r with friction mu.
float corner_speed(float r, float bank_radians, float mu) {
    const float t = JPH::Tan(bank_radians);
    const float denominator = std::max(0.05f, 1 - mu * t);
    return std::sqrt(kGravity * r * (mu + t) / denominator);
}

}  // namespace

int Kit::piece(const std::string& name) const {
    for (size_t i = 0; i < pieces.size(); ++i)
        if (pieces[i].name == name) return int(i);
    return -1;
}
int Kit::track(const std::string& name) const {
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].name == name) return int(i);
    return -1;
}
int Kit::driver(const std::string& name) const {
    for (size_t i = 0; i < drivers.size(); ++i)
        if (drivers[i].name == name) return int(i);
    return -1;
}

bool load(const baker::Array& root, Kit& out, std::string& error) {
    Kit k;
    if (const baker::Array* c = root.try_find(baker::Symbol("car"))) {
        Car& car = k.car;
        car.mass = base::number(*c, "mass", car.mass);
        car.half_width = base::number(*c, "width", car.half_width * 2) / 2;
        car.half_height = base::number(*c, "height", car.half_height * 2) / 2;
        car.half_length = base::number(*c, "length", car.half_length * 2) / 2;
        car.wheel_radius = base::number(*c, "wheel_radius", car.wheel_radius);
        car.wheel_width = base::number(*c, "wheel_width", car.wheel_width);
        car.max_torque = base::number(*c, "max_torque", car.max_torque);
        car.max_rpm = base::number(*c, "max_rpm", car.max_rpm);
        car.max_steer = base::number(*c, "max_steer", car.max_steer);
        car.suspension_min = base::number(*c, "suspension_min", car.suspension_min);
        car.suspension_max = base::number(*c, "suspension_max", car.suspension_max);
        car.suspension_frequency =
            base::number(*c, "suspension_frequency", car.suspension_frequency);
        car.suspension_damping = base::number(*c, "suspension_damping", car.suspension_damping);
        car.grip = base::number(*c, "grip", car.grip);
    }
    if (const baker::Array* list = root.try_find(baker::Symbol("pieces")))
        for (const baker::Array& a : list->subarrays()) {
            PieceDef p;
            p.name = base::name(a);
            const std::string kind = sym(a, "kind");
            p.kind = kind == "ramp"   ? Kind::Ramp
                     : kind == "gap"  ? Kind::Gap
                     : kind == "turn" ? Kind::Turn
                                      : Kind::Straight;
            p.length = std::max(1.0f, base::number(a, "length", p.length));
            p.rise = base::number(a, "rise", 0);
            p.radius = std::max(5.0f, base::number(a, "radius", p.radius));
            p.angle = base::number(a, "angle", p.angle);
            p.bank = base::number(a, "bank", 0);
            p.width = std::max(3.0f, base::number(a, "width", p.width));
            p.in = sym(a, "in");
            p.out = sym(a, "out");
            k.pieces.push_back(std::move(p));
        }
    if (k.pieces.empty()) {
        error = "no (pieces ...) in the data";
        return false;
    }
    if (const baker::Array* list = root.try_find(baker::Symbol("tracks")))
        for (const baker::Array& a : list->subarrays()) {
            Track t;
            t.name = base::name(a);
            if (auto d = a.find_str(baker::Symbol("desc"), "")) t.desc = std::string(*d);
            t.par = base::number(a, "par", t.par);
            if (const baker::Array* lay = a.try_find(baker::Symbol("lay")))
                for (size_t i = 1; i < lay->size(); ++i) {
                    auto s = (*lay)[i].as_sym();
                    const int p = s ? k.piece(std::string(s->str())) : -1;
                    if (p < 0) {
                        error = "track " + t.name + " lays " +
                                (s ? std::string(s->str()) : std::string("something")) +
                                ", which is not one of (pieces ...)";
                        return false;
                    }
                    t.lay.push_back(p);
                }
            if (t.lay.empty()) {
                error = "track " + t.name + " needs (lay piece...)";
                return false;
            }
            k.tracks.push_back(std::move(t));
        }
    if (k.tracks.empty()) {
        error = "no (tracks ...) in the data";
        return false;
    }
    if (const baker::Array* list = root.try_find(baker::Symbol("drivers")))
        for (const baker::Array& a : list->subarrays()) {
            Driver d;
            d.name = base::name(a);
            d.pace = base::number(a, "pace", d.pace);
            d.noise = base::number(a, "noise", d.noise);
            d.look_ahead = base::number(a, "look_ahead", d.look_ahead);
            d.top_speed = base::number(a, "top_speed", d.top_speed);
            k.drivers.push_back(std::move(d));
        }
    out = std::move(k);
    return true;
}

Layout lay_out(const Kit& kit, const Track& track) {
    Layout out;
    out.lay = track.lay;
    float x = 0, y = 0, z = 0, yaw = 0, s = 0;
    out.lowest = 0;
    auto sample = [&](float px, float py, float pz, float pyaw, float limit, int piece, bool gap) {
        out.line.push_back({{px, py, pz}, pyaw, s, limit, piece, gap});
    };
    // A straight stretch of road from the cursor: length along the heading, rising `rise`.
    auto stretch = [&](float length, float rise, float roll, float width, int piece, bool road,
                       float limit) {
        const float pitch = JPH::ATan2(rise, length);
        const float slope = std::sqrt(length * length + rise * rise);
        const JPH::Quat q = orient(yaw, pitch, roll);
        const float dx = JPH::Sin(yaw), dz = JPH::Cos(yaw);
        if (road) {
            const JPH::Vec3 n = q * JPH::Vec3::sAxisY();
            Segment seg{};
            seg.center[0] = x + dx * length / 2 - n.GetX() * kThickness;
            seg.center[1] = y + rise / 2 - n.GetY() * kThickness;
            seg.center[2] = z + dz * length / 2 - n.GetZ() * kThickness;
            seg.half[0] = width / 2;
            seg.half[1] = kThickness;
            seg.half[2] = slope / 2 + 0.05f;  // overlap the next, so no seam catches a wheel
            store(q, seg.quat);
            seg.piece = piece;
            out.road.push_back(seg);
            out.lowest = std::min(out.lowest, std::min(y, y + rise));
        }
        const int n = std::max(1, int(std::ceil(slope)));
        for (int i = 0; i < n; ++i) {
            const float t = float(i) / float(n);
            sample(x + dx * length * t, y + rise * t, z + dz * length * t, yaw, limit, piece,
                   !road);
            s += slope / float(n);
        }
        x += dx * length;
        y += rise;
        z += dz * length;
    };
    for (size_t i = 0; i < track.lay.size(); ++i) {
        const PieceDef& p = kit.pieces[size_t(track.lay[i])];
        if (i > 0 && out.connected) {
            const PieceDef& before = kit.pieces[size_t(track.lay[i - 1])];
            if (before.out != p.in) {
                out.connected = false;
                out.problem = before.name + " ends in " + before.out + " but " + p.name +
                              " begins with " + p.in;
            }
        }
        const int piece = int(i);
        switch (p.kind) {
            case Kind::Straight: stretch(p.length, 0, 0, p.width, piece, true, kOpen); break;
            case Kind::Ramp: stretch(p.length, p.rise, 0, p.width, piece, true, kOpen); break;
            case Kind::Gap: stretch(p.length, 0, 0, p.width, piece, false, kOpen); break;
            case Kind::Turn: {
                const float total = p.angle * kRadians;
                const int n = std::max(1, int(std::ceil(std::fabs(p.angle) / 6)));
                const float step = total / float(n);
                const float chord = 2 * p.radius * JPH::Sin(std::fabs(step) / 2);
                const float bank = p.bank * kRadians;
                const float limit = corner_speed(p.radius, bank, kit.car.grip);
                for (int k = 0; k < n; ++k) {
                    yaw += step / 2;
                    stretch(chord * 1.02f, 0, total > 0 ? -bank : bank, p.width, piece, true,
                            limit);
                    yaw += step / 2;
                }
                break;
            }
        }
    }
    sample(x, y, z, yaw, kOpen, int(track.lay.size()) - 1, false);
    out.length = s;
    return out;
}

// --- The race. ---

struct Race::Vehicle {
    JPH::Body* body = nullptr;
    JPH::Ref<JPH::VehicleConstraint> constraint;
    JPH::Ref<JPH::VehicleCollisionTester> tester;
};

Race::Race(const Kit& kit, const Layout& layout)
    : kit_(kit), layout_(layout), vehicle_(std::make_unique<Vehicle>()) {
    JPH::BodyInterface& bi = world_.bodies();
    for (const Segment& seg : layout_.road) {
        JPH::BodyCreationSettings s(
            new JPH::BoxShape(JPH::Vec3(seg.half[0], seg.half[1], seg.half[2])),
            JPH::RVec3(seg.center[0], seg.center[1], seg.center[2]),
            JPH::Quat(seg.quat[0], seg.quat[1], seg.quat[2], seg.quat[3]), JPH::EMotionType::Static,
            base::layers::kStatic);
        s.mFriction = 1.0f;
        bi.CreateAndAddBody(s, JPH::EActivation::DontActivate);
    }

    // The car, after Jolt's own vehicle sample: x is left, y up, z forward.
    const Car& c = kit_.car;
    JPH::RefConst<JPH::Shape> shape =
        JPH::OffsetCenterOfMassShapeSettings(
            JPH::Vec3(0, -c.half_height, 0),
            new JPH::BoxShape(JPH::Vec3(c.half_width, c.half_height, c.half_length)))
            .Create()
            .Get();
    JPH::BodyCreationSettings body(shape, JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
                                   JPH::EMotionType::Dynamic, base::layers::kMoving);
    body.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    body.mMassPropertiesOverride.mMass = c.mass;
    vehicle_->body = bi.CreateBody(body);
    bi.AddBody(vehicle_->body->GetID(), JPH::EActivation::Activate);

    JPH::VehicleConstraintSettings vehicle;
    vehicle.mMaxPitchRollAngle = 60 * kRadians;
    const float wx[4] = {c.half_width, -c.half_width, c.half_width, -c.half_width};
    const float wz[4] = {c.half_length - 2 * c.wheel_radius, c.half_length - 2 * c.wheel_radius,
                         -c.half_length + 2 * c.wheel_radius, -c.half_length + 2 * c.wheel_radius};
    for (int i = 0; i < 4; ++i) {
        auto* w = new JPH::WheelSettingsWV;
        w->mPosition = JPH::Vec3(wx[i], -0.9f * c.half_height, wz[i]);
        w->mSuspensionDirection = JPH::Vec3(0, -1, 0);
        w->mSteeringAxis = JPH::Vec3(0, 1, 0);
        w->mWheelUp = JPH::Vec3(0, 1, 0);
        w->mWheelForward = JPH::Vec3(0, 0, 1);
        w->mSuspensionMinLength = c.suspension_min;
        w->mSuspensionMaxLength = c.suspension_max;
        w->mSuspensionSpring.mFrequency = c.suspension_frequency;
        w->mSuspensionSpring.mDamping = c.suspension_damping;
        w->mMaxSteerAngle = i < 2 ? c.max_steer * kRadians : 0.0f;
        if (i < 2) w->mMaxHandBrakeTorque = 0;
        w->mRadius = c.wheel_radius;
        w->mWidth = c.wheel_width;
        vehicle.mWheels.push_back(w);
    }
    auto* controller = new JPH::WheeledVehicleControllerSettings;
    controller->mEngine.mMaxTorque = c.max_torque;
    controller->mEngine.mMaxRPM = c.max_rpm;
    controller->mDifferentials.resize(1);
    controller->mDifferentials[0].mLeftWheel = 0;
    controller->mDifferentials[0].mRightWheel = 1;
    vehicle.mController = controller;
    vehicle.mAntiRollBars.resize(2);
    vehicle.mAntiRollBars[0].mLeftWheel = 0;
    vehicle.mAntiRollBars[0].mRightWheel = 1;
    vehicle.mAntiRollBars[1].mLeftWheel = 2;
    vehicle.mAntiRollBars[1].mRightWheel = 3;
    vehicle_->constraint = new JPH::VehicleConstraint(*vehicle_->body, vehicle);
    // As Jolt's sample does, for the tire forces it was tuned with.
    static_cast<JPH::WheeledVehicleController*>(vehicle_->constraint->GetController())
        ->SetTireMaxImpulseCallback([](JPH::uint, float& longitudinal, float& lateral,
                                       float suspension, float longitudinal_friction,
                                       float lateral_friction, float, float, float) {
            longitudinal = 10.0f * longitudinal_friction * suspension;
            lateral = lateral_friction * suspension;
        });
    vehicle_->tester = new JPH::VehicleCollisionTesterCastCylinder(base::layers::kMoving);
    vehicle_->constraint->SetVehicleCollisionTester(vehicle_->tester);
    world_.system().AddConstraint(vehicle_->constraint);
    world_.system().AddStepListener(vehicle_->constraint);
    respawn(0);
}

Race::~Race() {
    world_.system().RemoveStepListener(vehicle_->constraint);
    world_.system().RemoveConstraint(vehicle_->constraint);
}

void Race::place(const Sample& at) {
    JPH::BodyInterface& bi = world_.bodies();
    const JPH::BodyID id = vehicle_->body->GetID();
    bi.SetPositionAndRotation(id, JPH::RVec3(at.pos[0], at.pos[1] + 1.0f, at.pos[2]),
                              JPH::Quat::sRotation(JPH::Vec3::sAxisY(), at.yaw),
                              JPH::EActivation::Activate);
    bi.SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    sample_ = int(&at - layout_.line.data());
    s_ = at.s;
    upside_down_ = stuck_ = 0;
    grace_ = 60;
}

void Race::respawn(int piece) {
    // Five metres into the last flat piece at or before it, so a jump gets its run-up back.
    for (; piece >= 0; --piece) {
        const Kind kind = kit_.pieces[size_t(layout_.lay[size_t(piece)])].kind;
        if (piece > 0 && (kind == Kind::Ramp || kind == Kind::Gap)) continue;
        const auto first = std::find_if(layout_.line.begin(), layout_.line.end(),
                                        [&](const Sample& s) { return s.piece == piece; });
        if (first == layout_.line.end()) continue;
        auto at = first;
        while (at + 1 != layout_.line.end() && at->piece == piece && at->s < first->s + 5) ++at;
        place(*at);
        return;
    }
    place(layout_.line.front());
}

void Race::step(const Controls& c) {
    if (finished_) return;
    auto* controller =
        static_cast<JPH::WheeledVehicleController*>(vehicle_->constraint->GetController());
    controller->SetDriverInput(c.forward, c.right, c.brake, c.handbrake);
    JPH::BodyInterface& bi = world_.bodies();
    const JPH::BodyID id = vehicle_->body->GetID();
    if (c.forward != 0 || c.right != 0 || c.brake != 0) bi.ActivateBody(id);
    world_.step(kDt, 1);
    ++steps_;

    const JPH::RVec3 p = bi.GetPosition(id);
    const JPH::Quat q = bi.GetRotation(id);
    const JPH::Vec3 forward = q * JPH::Vec3::sAxisZ();
    speed_ = bi.GetLinearVelocity(id).Dot(forward);
    top_speed = std::max(top_speed, speed_);

    // Progress: the nearest sample, near the last one.
    const int lo = std::max(0, sample_ - 10);
    const int hi = std::min(int(layout_.line.size()) - 1, sample_ + 60);
    float best = 1e30f;
    for (int i = lo; i <= hi; ++i) {
        const Sample& s = layout_.line[size_t(i)];
        const float dx = float(p.GetX()) - s.pos[0], dy = float(p.GetY()) - s.pos[1],
                    dz = float(p.GetZ()) - s.pos[2];
        const float d = dx * dx + dy * dy * 0.25f + dz * dz;
        if (d < best) {
            best = d;
            sample_ = i;
        }
    }
    s_ = layout_.line[size_t(sample_)].s;
    const Sample& here = layout_.line[size_t(sample_)];
    if (here.gap && here.piece > last_gap_seen_) {
        last_gap_seen_ = here.piece;
        gap_arrival.push_back(speed_);
    }
    if (s_ >= layout_.length - 2) {
        finished_ = true;
        return;
    }

    bool touching = false;
    for (const JPH::Wheel* w : vehicle_->constraint->GetWheels()) touching |= w->HasContact();
    airborne_ = !touching;
    if (grace_ > 0) {
        --grace_;
    } else if (airborne_) {
        airtime += kDt;
    }

    // A crash: fallen below the road, on its back for a second, or stuck for four.
    const bool fell = float(p.GetY()) < layout_.lowest - 5;
    upside_down_ = (q * JPH::Vec3::sAxisY()).GetY() < 0.3f ? upside_down_ + 1 : 0;
    stuck_ = (grace_ == 0 && std::fabs(speed_) < 0.5f) ? stuck_ + 1 : 0;
    if (fell || upside_down_ > 60 || stuck_ > 240) {
        ++crashes;
        crash_pieces.push_back(here.piece);
        respawn(here.piece);
    }
}

void Race::body(float pos[3], float quat[4]) const {
    JPH::BodyInterface& bi = const_cast<base::Physics&>(world_).bodies();
    const JPH::RVec3 p = bi.GetPosition(vehicle_->body->GetID());
    pos[0] = float(p.GetX());
    pos[1] = float(p.GetY());
    pos[2] = float(p.GetZ());
    store(bi.GetRotation(vehicle_->body->GetID()), quat);
}

void Race::wheel(int i, float pos[3], float quat[4]) const {
    const JPH::RMat44 m = vehicle_->constraint->GetWheelWorldTransform(
        JPH::uint(i), JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY());
    const JPH::RVec3 t = m.GetTranslation();
    pos[0] = float(t.GetX());
    pos[1] = float(t.GetY());
    pos[2] = float(t.GetZ());
    store(m.GetQuaternion(), quat);
}

Controls drive(const Driver& d, const Kit& kit, const Race& race, base::Rng& rng) {
    const Layout& l = race.layout();
    Controls c;
    float pos[3], quat[4];
    race.body(pos, quat);
    const JPH::Quat q(quat[0], quat[1], quat[2], quat[3]);
    const JPH::Vec3 forward = q * JPH::Vec3::sAxisZ(), left = q * JPH::Vec3::sAxisX();

    // Steer toward the line, look_ahead metres on.
    const int n = int(l.line.size());
    int target = race.sample();
    while (target < n - 1 && l.line[size_t(target)].s < race.progress() + d.look_ahead) ++target;
    const Sample& t = l.line[size_t(target)];
    const JPH::Vec3 to(t.pos[0] - pos[0], 0, t.pos[2] - pos[2]);
    const float angle = JPH::ATan2(to.Dot(left), to.Dot(forward));
    const float max_steer = kit.car.max_steer * kRadians;
    c.right = std::clamp(-angle / max_steer + d.noise * static_cast<float>(rng.unit() * 2.0 - 1.0),
                         -1.0f, 1.0f);

    // Speed: the slowest corner within braking distance, at this driver's pace.
    const float v = race.speed();
    const float braking = v * v / (2 * 6.0f) + 10;
    float wanted = d.top_speed;
    for (int i = race.sample(); i < n && l.line[size_t(i)].s < race.progress() + braking; ++i)
        wanted = std::min(wanted, l.line[size_t(i)].speed_limit * d.pace);
    if (v < wanted - 0.5f) {
        c.forward = 1;
    } else if (v > wanted + 1) {
        c.brake = std::min(1.0f, (v - wanted) / 5);
    } else {
        c.forward = 0.3f;
    }
    return c;
}

Result race(const Kit& kit, const Layout& layout, const Driver& d, uint64_t seed,
            float max_seconds) {
    Race r(kit, layout);
    base::Rng rng(seed, 0x2545F4914F6CDD1Dull);
    const int steps = int(max_seconds / Race::kDt);
    for (int i = 0; i < steps && !r.finished(); ++i) r.step(drive(d, kit, r, rng));
    Result out;
    out.finished = r.finished();
    out.seconds = r.seconds();
    out.crashes = r.crashes;
    out.crash_pieces = r.crash_pieces;
    out.airtime = r.airtime;
    out.top_speed = r.top_speed;
    out.gap_arrival = r.gap_arrival;
    out.hash = r.physics().hash();
    return out;
}

std::vector<float> gap_margins(const Kit& kit, const Track& track,
                               const std::vector<float>& speeds) {
    std::vector<float> out;
    size_t g = 0;
    for (size_t i = 0; i < track.lay.size(); ++i) {
        const PieceDef& p = kit.pieces[size_t(track.lay[i])];
        if (p.kind != Kind::Gap) continue;
        const float v = g < speeds.size() ? speeds[g] : 0;
        ++g;
        float launch = 0;
        if (i > 0) {
            const PieceDef& lip = kit.pieces[size_t(track.lay[i - 1])];
            if (lip.kind == Kind::Ramp) launch = JPH::ATan2(lip.rise, lip.length);
        }
        // Back down to the lip's height: v^2 sin(2 theta) / g.
        const float flight = v * v * JPH::Sin(2 * launch) / kGravity;
        out.push_back(flight / p.length);
    }
    return out;
}

}  // namespace stuntline
