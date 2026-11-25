#include "spike_world.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorder.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/RegisterTypes.h>

#include <cstring>

JPH_SUPPRESS_WARNINGS

using namespace JPH;

namespace tutti::game {

namespace {

namespace Layers {
constexpr ObjectLayer NON_MOVING = 0;
constexpr ObjectLayer MOVING = 1;
constexpr uint NUM_LAYERS = 2;
}  // namespace Layers

namespace BroadPhaseLayers {
constexpr BroadPhaseLayer NON_MOVING(0);
constexpr BroadPhaseLayer MOVING(1);
constexpr uint NUM_LAYERS = 2;
}  // namespace BroadPhaseLayers

// Body user data, so the contact listener can tell what hit what
constexpr uint64 kUserGround = 0;
constexpr uint64 kUserCarBase = 1;     // 1..4
constexpr uint64 kUserLooseBase = 100; // 100..109
constexpr uint64 kUserWall = 200;

constexpr float kTickSeconds = 1.0f / 60.0f;
constexpr float kArenaHalfSize = 80.0f;
constexpr uint32_t kAirborneTicksForJump = 15;

// Jolt's global registration is process-wide and must happen exactly once.
void ensure_jolt_registered() {
    static bool registered = false;
    if (registered) return;
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();
    registered = true;
}

// Snapshot storage backed by a flat byte vector. Jolt's own
// StateRecorderImpl goes through a std::stringstream, which is far too slow
// to represent what a rollback would cost.
class BufferRecorder final : public StateRecorder {
public:
    explicit BufferRecorder(std::vector<uint8_t>& buffer) : buffer_(buffer) {}

    void WriteBytes(const void* data, size_t size) override {
        const auto* bytes = static_cast<const uint8_t*>(data);
        buffer_.insert(buffer_.end(), bytes, bytes + size);
    }
    void ReadBytes(void*, size_t) override { failed_ = true; }
    bool IsEOF() const override { return true; }
    bool IsFailed() const override { return failed_; }

private:
    std::vector<uint8_t>& buffer_;
    bool failed_ = false;
};

class BufferReader final : public StateRecorder {
public:
    BufferReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    void WriteBytes(const void*, size_t) override { failed_ = true; }
    void ReadBytes(void* out, size_t size) override {
        if (size > size_ - pos_) {
            failed_ = true;
            std::memset(out, 0, size);
            pos_ = size_;
            return;
        }
        std::memcpy(out, data_ + pos_, size);
        pos_ += size;
    }
    bool IsEOF() const override { return pos_ >= size_; }
    bool IsFailed() const override { return failed_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool failed_ = false;
};

// FNV-1a, 64 bit
struct Hasher {
    uint64_t value = 14695981039346656037ull;

    void bytes(const void* data, size_t size) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            value ^= p[i];
            value *= 1099511628211ull;
        }
    }
    void f(float v) { bytes(&v, sizeof(v)); }
    void i(int32_t v) { bytes(&v, sizeof(v)); }
    void vec3(Vec3Arg v) {
        f(v.GetX());
        f(v.GetY());
        f(v.GetZ());
    }
    void quat(QuatArg q) {
        f(q.GetX());
        f(q.GetY());
        f(q.GetZ());
        f(q.GetW());
    }
};

void add_quad(TriangleList& tris, Vec3Arg a, Vec3Arg b, Vec3Arg c, Vec3Arg d) {
    // Counter-clockwise seen from the front face
    tris.push_back(Triangle(a, b, c));
    tris.push_back(Triangle(a, c, d));
}

// A ramp rising along +Z from ground level at z0 to `height` at z1, between
// x0 and x1, closed at the sides and back so nothing gets under it.
void add_ramp(TriangleList& tris, float x0, float x1, float z0, float z1, float height) {
    Vec3 a(x0, 0, z0), b(x1, 0, z0), c(x1, height, z1), d(x0, height, z1);
    Vec3 e(x0, 0, z1), f(x1, 0, z1);
    add_quad(tris, a, d, c, b);  // sloped top, facing up
    add_quad(tris, d, e, f, c);  // back, facing +Z
    tris.push_back(Triangle(a, e, d));  // side at x0, facing -X
    tris.push_back(Triangle(b, c, f));  // side at x1, facing +X
}

}  // namespace

struct SpikeWorld::Impl final : public ContactListener {
    BroadPhaseLayerInterfaceTable broad_phase_layers{Layers::NUM_LAYERS, BroadPhaseLayers::NUM_LAYERS};
    ObjectLayerPairFilterTable object_pair_filter{Layers::NUM_LAYERS};
    std::unique_ptr<ObjectVsBroadPhaseLayerFilterTable> object_vs_broad_phase;
    TempAllocatorImpl temp_allocator{4 * 1024 * 1024};
    JobSystemSingleThreaded job_system{cMaxPhysicsJobs};
    PhysicsSystem physics;

    BodyID car_bodies[kCars];
    BodyID loose_bodies[kLooseBodies];
    Ref<VehicleConstraint> vehicles[kCars];
    Ref<VehicleCollisionTester> wheel_tester;

    CarInput inputs[kCars];
    uint32_t tick = 0;
    uint32_t events = 0;
    uint32_t airborne_ticks[kCars] = {};

    Impl() {
        broad_phase_layers.MapObjectToBroadPhaseLayer(Layers::NON_MOVING, BroadPhaseLayers::NON_MOVING);
        broad_phase_layers.MapObjectToBroadPhaseLayer(Layers::MOVING, BroadPhaseLayers::MOVING);
        object_pair_filter.EnableCollision(Layers::MOVING, Layers::NON_MOVING);
        object_pair_filter.EnableCollision(Layers::MOVING, Layers::MOVING);
        object_vs_broad_phase = std::make_unique<ObjectVsBroadPhaseLayerFilterTable>(
            broad_phase_layers, BroadPhaseLayers::NUM_LAYERS, object_pair_filter, Layers::NUM_LAYERS);

        physics.Init(256, 0, 2048, 2048, broad_phase_layers, *object_vs_broad_phase, object_pair_filter);
        physics.SetGravity(Vec3(0, -9.81f, 0));
        physics.SetContactListener(this);

        build_static_scene();
        build_loose_bodies();
        build_cars();
        physics.OptimizeBroadPhase();
    }

    ~Impl() override {
        for (auto& vehicle : vehicles) {
            physics.RemoveStepListener(vehicle);
            physics.RemoveConstraint(vehicle);
        }
    }

    void build_static_scene() {
        BodyInterface& bodies = physics.GetBodyInterfaceNoLock();

        TriangleList tris;
        // Ground: a grid of quads rather than one huge pair of triangles,
        // closer to what a real track mesh will look like.
        constexpr int kCells = 16;
        constexpr float kCell = 2.0f * kArenaHalfSize / kCells;
        for (int ix = 0; ix < kCells; ++ix) {
            for (int iz = 0; iz < kCells; ++iz) {
                float x0 = -kArenaHalfSize + kCell * static_cast<float>(ix);
                float z0 = -kArenaHalfSize + kCell * static_cast<float>(iz);
                float x1 = x0 + kCell;
                float z1 = z0 + kCell;
                add_quad(tris, Vec3(x0, 0, z0), Vec3(x0, 0, z1), Vec3(x1, 0, z1), Vec3(x1, 0, z0));
            }
        }
        // Jump ramp across car 0's lane
        add_ramp(tris, -8.5f, -3.5f, 38.0f, 46.0f, 2.2f);
        // One-sided wedge under the left wheels of car 1, to tip it over
        add_ramp(tris, -1.9f, -0.6f, 26.0f, 31.0f, 1.7f);

        MeshShapeSettings mesh_settings(tris);
        mesh_settings.SetEmbedded();
        ShapeRefC mesh = mesh_settings.Create().Get();
        BodyCreationSettings ground(mesh, RVec3::sZero(), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
        ground.mUserData = kUserGround;
        ground.mFriction = 1.0f;
        bodies.CreateAndAddBody(ground, EActivation::DontActivate);

        // Four walls keep everything in the arena for the whole run
        const float h = kArenaHalfSize;
        const float wall_height = 3.0f;
        const float t = 1.0f;
        struct Wall {
            Vec3 half_extent;
            Vec3 position;
        };
        const Wall walls[4] = {
            {Vec3(h + t, wall_height, t), Vec3(0, wall_height, h + t)},
            {Vec3(h + t, wall_height, t), Vec3(0, wall_height, -h - t)},
            {Vec3(t, wall_height, h + t), Vec3(h + t, wall_height, 0)},
            {Vec3(t, wall_height, h + t), Vec3(-h - t, wall_height, 0)},
        };
        for (const Wall& wall : walls) {
            BodyCreationSettings settings(new BoxShape(wall.half_extent), RVec3(wall.position), Quat::sIdentity(),
                                          EMotionType::Static, Layers::NON_MOVING);
            settings.mUserData = kUserWall;
            bodies.CreateAndAddBody(settings, EActivation::DontActivate);
        }
    }

    void build_loose_bodies() {
        BodyInterface& bodies = physics.GetBodyInterfaceNoLock();
        // A 4-3-2-1 pyramid of crates straddling the lanes of cars 2 and 3
        RefConst<Shape> crate = new BoxShape(Vec3(0.5f, 0.5f, 0.5f));
        int index = 0;
        for (int row = 0; row < 4; ++row) {
            int count = 4 - row;
            float start_x = 4.0f - 0.525f * static_cast<float>(count - 1);
            for (int i = 0; i < count; ++i) {
                RVec3 position(start_x + 1.05f * static_cast<float>(i), 0.5f + 1.0f * static_cast<float>(row), 22.0f);
                BodyCreationSettings settings(crate, position, Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
                settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = 40.0f;
                settings.mUserData = kUserLooseBase + static_cast<uint64>(index);
                loose_bodies[index] = bodies.CreateAndAddBody(settings, EActivation::Activate);
                ++index;
            }
        }
    }

    void build_cars() {
        BodyInterface& bodies = physics.GetBodyInterfaceNoLock();

        const float wheel_radius = 0.3f;
        const float wheel_width = 0.1f;
        const float half_width = 0.9f;
        const float half_length = 2.0f;
        const float half_height = 0.2f;
        const float max_steer = 0.5235988f;  // 30 degrees

        RefConst<Shape> car_shape =
            OffsetCenterOfMassShapeSettings(Vec3(0, -half_height, 0), new BoxShape(Vec3(half_width, half_height, half_length)))
                .Create()
                .Get();

        wheel_tester = new VehicleCollisionTesterCastCylinder(Layers::MOVING);

        for (int car = 0; car < kCars; ++car) {
            RVec3 position(-6.0f + 4.0f * static_cast<float>(car), 1.0f, 0.0f);
            BodyCreationSettings body_settings(car_shape, position, Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
            body_settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
            body_settings.mMassPropertiesOverride.mMass = 1500.0f;
            body_settings.mUserData = kUserCarBase + static_cast<uint64>(car);
            // Cars never sleep: one less state for the two builds to disagree on
            body_settings.mAllowSleeping = false;
            Body* body = bodies.CreateBody(body_settings);
            car_bodies[car] = body->GetID();
            bodies.AddBody(body->GetID(), EActivation::Activate);

            VehicleConstraintSettings vehicle;
            const Vec3 wheel_positions[4] = {
                Vec3(half_width, -0.9f * half_height, half_length - 2.0f * wheel_radius),
                Vec3(-half_width, -0.9f * half_height, half_length - 2.0f * wheel_radius),
                Vec3(half_width, -0.9f * half_height, -half_length + 2.0f * wheel_radius),
                Vec3(-half_width, -0.9f * half_height, -half_length + 2.0f * wheel_radius),
            };
            for (int w = 0; w < 4; ++w) {
                auto* wheel = new WheelSettingsWV;
                wheel->mPosition = wheel_positions[w];
                wheel->mSuspensionMinLength = 0.3f;
                wheel->mSuspensionMaxLength = 0.5f;
                wheel->mSuspensionSpring.mFrequency = 1.5f;
                wheel->mSuspensionSpring.mDamping = 0.5f;
                wheel->mRadius = wheel_radius;
                wheel->mWidth = wheel_width;
                const bool front = w < 2;
                wheel->mMaxSteerAngle = front ? max_steer : 0.0f;
                if (front) wheel->mMaxHandBrakeTorque = 0.0f;
                vehicle.mWheels.push_back(wheel);
            }

            auto* controller = new WheeledVehicleControllerSettings;
            vehicle.mController = controller;
            controller->mEngine.mMaxTorque = 500.0f;
            // Four-wheel drive
            controller->mDifferentials.resize(2);
            controller->mDifferentials[0].mLeftWheel = 0;
            controller->mDifferentials[0].mRightWheel = 1;
            controller->mDifferentials[0].mEngineTorqueRatio = 0.5f;
            controller->mDifferentials[1].mLeftWheel = 2;
            controller->mDifferentials[1].mRightWheel = 3;
            controller->mDifferentials[1].mEngineTorqueRatio = 0.5f;

            vehicles[car] = new VehicleConstraint(*body, vehicle);
            vehicles[car]->SetVehicleCollisionTester(wheel_tester);
            physics.AddConstraint(vehicles[car]);
            physics.AddStepListener(vehicles[car]);
        }
    }

    // ContactListener. The job system is single-threaded, so plain writes.
    void OnContactAdded(const Body& a, const Body& b, const ContactManifold&, ContactSettings&) override {
        const uint64 ua = a.GetUserData();
        const uint64 ub = b.GetUserData();
        const auto is_car = [](uint64 u) { return u >= kUserCarBase && u < kUserCarBase + kCars; };
        const auto is_loose = [](uint64 u) { return u >= kUserLooseBase && u < kUserLooseBase + kLooseBodies; };
        if (is_car(ua) && is_car(ub)) events |= kEventCarHitCar;
        if ((is_car(ua) && is_loose(ub)) || (is_loose(ua) && is_car(ub))) events |= kEventCarHitLoose;
        if ((is_car(ua) && ub == kUserWall) || (ua == kUserWall && is_car(ub))) events |= kEventCarHitWall;
    }

    void step() {
        for (int car = 0; car < kCars; ++car) {
            auto* controller = static_cast<WheeledVehicleController*>(vehicles[car]->GetController());
            const CarInput& in = inputs[car];
            controller->SetDriverInput(in.forward, in.right, in.brake, in.handbrake);
        }

        physics.Update(kTickSeconds, 1, &temp_allocator, &job_system);
        ++tick;

        const BodyInterface& bodies = physics.GetBodyInterfaceNoLock();
        for (int car = 0; car < kCars; ++car) {
            bool any_contact = false;
            for (const Wheel* wheel : vehicles[car]->GetWheels()) any_contact = any_contact || wheel->HasContact();
            airborne_ticks[car] = any_contact ? 0 : airborne_ticks[car] + 1;
            if (airborne_ticks[car] >= kAirborneTicksForJump) events |= kEventJump;

            const Vec3 up = bodies.GetRotation(car_bodies[car]).RotateAxisY();
            if (up.GetY() < 0.0f) events |= kEventRollover;
        }
    }

    void hash_body(Hasher& h, BodyID id) const {
        const BodyInterface& bodies = physics.GetBodyInterfaceNoLock();
        h.vec3(Vec3(bodies.GetPosition(id)));
        h.quat(bodies.GetRotation(id));
        h.vec3(bodies.GetLinearVelocity(id));
        h.vec3(bodies.GetAngularVelocity(id));
    }
};

SpikeWorld::SpikeWorld() {
    ensure_jolt_registered();
    impl_ = std::make_unique<Impl>();
}

SpikeWorld::~SpikeWorld() = default;

CarInput SpikeWorld::scripted_input(uint32_t tick, int car) {
    CarInput in;

    // First ten seconds: each car does its set piece.
    if (tick < 600) {
        in.forward = 1.0f;
        switch (car) {
            case 0:  // straight over the jump ramp
                break;
            case 1:  // straight over the one-sided wedge
                break;
            case 2:  // through the crates, then into car 3
                if (tick >= 200 && tick < 290) in.right = -0.5f;
                break;
            case 3:  // through the crates, then into car 2
                if (tick >= 200 && tick < 290) in.right = 0.5f;
                break;
        }
        return in;
    }

    // After that: pseudo-random driving, a new decision every half second.
    uint32_t h = (tick / 30u) * 2654435761u + static_cast<uint32_t>(car) * 40503u + 12345u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;

    static const float kForward[8] = {1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, -1.0f, 1.0f};
    static const float kRight[8] = {0.0f, 0.0f, -1.0f, 1.0f, -0.5f, 0.5f, -0.25f, 0.25f};
    in.forward = kForward[h & 7u];
    in.right = kRight[(h >> 3) & 7u];
    in.handbrake = ((h >> 6) & 15u) == 0u ? 1.0f : 0.0f;
    in.brake = ((h >> 10) & 15u) == 0u ? 1.0f : 0.0f;
    return in;
}

void SpikeWorld::set_input(int car, const CarInput& input) {
    if (car < 0 || car >= kCars) return;
    impl_->inputs[car] = input;
}

void SpikeWorld::step() { impl_->step(); }

void SpikeWorld::step_scripted() {
    for (int car = 0; car < kCars; ++car) impl_->inputs[car] = scripted_input(impl_->tick, car);
    impl_->step();
}

uint32_t SpikeWorld::tick() const { return impl_->tick; }
uint32_t SpikeWorld::events() const { return impl_->events; }

uint64_t SpikeWorld::state_hash() const {
    Hasher h;
    h.i(static_cast<int32_t>(impl_->tick));
    for (int car = 0; car < kCars; ++car) {
        impl_->hash_body(h, impl_->car_bodies[car]);
        const VehicleConstraint& vehicle = *impl_->vehicles[car];
        for (const Wheel* wheel : vehicle.GetWheels()) {
            h.f(wheel->GetAngularVelocity());
            h.f(wheel->GetRotationAngle());
            // The steer angle is left out: Jolt derives it from the input
            // at the start of each step and does not snapshot it.
            h.f(wheel->GetSuspensionLength());
            h.i(wheel->HasContact() ? 1 : 0);
        }
        const auto* controller = static_cast<const WheeledVehicleController*>(vehicle.GetController());
        h.f(controller->GetEngine().GetCurrentRPM());
        h.i(controller->GetTransmission().GetCurrentGear());
    }
    for (BodyID id : impl_->loose_bodies) impl_->hash_body(h, id);
    return h.value;
}

void SpikeWorld::save_state(std::vector<uint8_t>& out) const {
    out.clear();
    BufferRecorder recorder(out);
    impl_->physics.SaveState(recorder);
    recorder.Write(impl_->tick);
    recorder.Write(impl_->events);
    for (uint32_t ticks : impl_->airborne_ticks) recorder.Write(ticks);
    for (const CarInput& in : impl_->inputs) {
        recorder.Write(in.forward);
        recorder.Write(in.right);
        recorder.Write(in.brake);
        recorder.Write(in.handbrake);
    }
}

bool SpikeWorld::restore_state(const uint8_t* data, size_t size) {
    BufferReader reader(data, size);
    if (!impl_->physics.RestoreState(reader)) return false;
    reader.Read(impl_->tick);
    reader.Read(impl_->events);
    for (uint32_t& ticks : impl_->airborne_ticks) reader.Read(ticks);
    for (CarInput& in : impl_->inputs) {
        reader.Read(in.forward);
        reader.Read(in.right);
        reader.Read(in.brake);
        reader.Read(in.handbrake);
    }
    return !reader.IsFailed();
}

void SpikeWorld::read_state(float* out) const {
    const BodyInterface& bodies = impl_->physics.GetBodyInterfaceNoLock();
    const auto write_body = [&](BodyID id) {
        const Vec3 p(bodies.GetPosition(id));
        const Quat q = bodies.GetRotation(id);
        const Vec3 v = bodies.GetLinearVelocity(id);
        const Vec3 w = bodies.GetAngularVelocity(id);
        const float values[kFloatsPerBody] = {p.GetX(), p.GetY(), p.GetZ(), q.GetX(), q.GetY(), q.GetZ(), q.GetW(),
                                              v.GetX(), v.GetY(), v.GetZ(), w.GetX(), w.GetY(), w.GetZ()};
        std::memcpy(out, values, sizeof(values));
        out += kFloatsPerBody;
    };
    for (BodyID id : impl_->car_bodies) write_body(id);
    for (BodyID id : impl_->loose_bodies) write_body(id);
}

}  // namespace tutti::game
