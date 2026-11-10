#include "PhysicsInternal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace zplus::physics::detail {

State& state() {
    static State value;
    return value;
}

std::int64_t nextHandle(State& value) {
    if (value.nextHandle <= 0 ||
        value.nextHandle == std::numeric_limits<std::int64_t>::max()) {
        throw std::overflow_error("physics handle space is exhausted");
    }
    return value.nextHandle++;
}

World& checkedWorld(State& value, std::int64_t handle) {
    const auto found = value.worlds.find(handle);
    if (handle <= 0 || found == value.worlds.end() ||
        !b2World_IsValid(found->second.id)) {
        throw std::out_of_range("invalid or destroyed physics world handle");
    }
    return found->second;
}

BodyRef& checkedBody(State& value, std::int64_t handle) {
    const auto found = value.bodies.find(handle);
    if (handle <= 0 || found == value.bodies.end() ||
        !b2Body_IsValid(found->second.id)) {
        throw std::out_of_range("invalid or destroyed physics body handle");
    }
    return found->second;
}

ShapeRef& checkedShape(State& value, std::int64_t handle) {
    const auto found = value.shapes.find(handle);
    if (handle <= 0 || found == value.shapes.end() ||
        !b2Shape_IsValid(found->second.id)) {
        throw std::out_of_range("invalid or destroyed physics shape handle");
    }
    return found->second;
}

JointRef& checkedJoint(State& value, std::int64_t handle) {
    const auto found = value.joints.find(handle);
    if (handle <= 0 || found == value.joints.end() ||
        !b2Joint_IsValid(found->second.id)) {
        throw std::out_of_range("invalid or destroyed physics joint handle");
    }
    return found->second;
}

void requireFinite(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

void requireNonNegative(double value, const char* name) {
    requireFinite(value, name);
    if (value < 0.0) {
        throw std::invalid_argument(std::string(name) + " must be non-negative");
    }
}

BodyPair bodyPair(std::int64_t a, std::int64_t b) {
    return a < b ? BodyPair{a, b} : BodyPair{b, a};
}

std::int64_t bodyHandleFromNative(std::int64_t world, b2BodyId id) {
    const auto& bodies = state().bodies;
    for (const auto& [handle, body] : bodies) {
        if (body.world == world && B2_ID_EQUALS(body.id, id)) return handle;
    }
    return 0;
}

void refreshTouching(std::int64_t worldHandle, World& world) {
    State& current = state();
    std::set<BodyPair> touching;
    for (const auto& [handle, body] : current.bodies) {
        if (body.world != worldHandle) continue;
        const int contactCapacity = b2Body_GetContactCapacity(body.id);
        std::vector<b2ContactData> contacts(static_cast<std::size_t>(contactCapacity));
        const int contactCount = b2Body_GetContactData(
            body.id, contacts.data(), contactCapacity);
        for (int i = 0; i < contactCount; ++i) {
            const auto& contact = contacts[static_cast<std::size_t>(i)];
            if (!b2Shape_IsValid(contact.shapeIdA) ||
                !b2Shape_IsValid(contact.shapeIdB)) continue;
            const auto a = bodyHandleFromNative(
                worldHandle, b2Shape_GetBody(contact.shapeIdA));
            const auto b = bodyHandleFromNative(
                worldHandle, b2Shape_GetBody(contact.shapeIdB));
            if (a != 0 && b != 0 && a != b) touching.insert(bodyPair(a, b));
        }
    }
    for (const auto& [handle, shape] : current.shapes) {
        if (shape.world != worldHandle || !shape.sensor ||
            !b2Shape_IsValid(shape.id)) continue;
        const int overlapCapacity = b2Shape_GetSensorCapacity(shape.id);
        std::vector<b2ShapeId> overlaps(static_cast<std::size_t>(overlapCapacity));
        const int overlapCount = b2Shape_GetSensorOverlaps(
            shape.id, overlaps.data(), overlapCapacity);
        for (int i = 0; i < overlapCount; ++i) {
            const auto overlap = overlaps[static_cast<std::size_t>(i)];
            if (!b2Shape_IsValid(overlap)) continue;
            const auto other = bodyHandleFromNative(
                worldHandle, b2Shape_GetBody(overlap));
            if (other != 0 && other != shape.body) {
                touching.insert(bodyPair(shape.body, other));
            }
        }
    }
    world.touching = std::move(touching);
}

} // namespace zplus::physics::detail

namespace zplus::physics {

std::int64_t createWorld(double gravityX, double gravityY) {
    detail::requireFinite(gravityX, "gravityX");
    detail::requireFinite(gravityY, "gravityY");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    b2WorldDef definition = b2DefaultWorldDef();
    definition.gravity = {static_cast<float>(gravityX), static_cast<float>(gravityY)};
    const b2WorldId native = b2CreateWorld(&definition);
    if (!b2World_IsValid(native)) {
        throw std::runtime_error("Box2D failed to create a physics world");
    }
    const auto handle = detail::nextHandle(value);
    detail::World world;
    world.id = native;
    b2BodyDef groundDefinition = b2DefaultBodyDef();
    world.ground = b2CreateBody(native, &groundDefinition);
    value.worlds.emplace(handle, std::move(world));
    return handle;
}

void destroyWorld(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& world = detail::checkedWorld(value, handle);
    b2DestroyWorld(world.id);
    value.worlds.erase(handle);
    std::erase_if(value.bodies, [handle](const auto& entry) {
        return entry.second.world == handle;
    });
    std::erase_if(value.shapes, [handle](const auto& entry) {
        return entry.second.world == handle;
    });
    std::erase_if(value.joints, [handle](const auto& entry) {
        return entry.second.world == handle;
    });
}

void setGravity(std::int64_t handle, double x, double y) {
    detail::requireFinite(x, "gravityX");
    detail::requireFinite(y, "gravityY");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    b2World_SetGravity(detail::checkedWorld(value, handle).id,
        {static_cast<float>(x), static_cast<float>(y)});
}

double getGravityX(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    return b2World_GetGravity(detail::checkedWorld(value, handle).id).x;
}

double getGravityY(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    return b2World_GetGravity(detail::checkedWorld(value, handle).id).y;
}

void setFixedTimeStep(std::int64_t handle, double seconds) {
    detail::requireFinite(seconds, "fixedTimeStep");
    if (seconds <= 0.0 || seconds > 0.25) {
        throw std::invalid_argument("fixedTimeStep must be greater than 0 and at most 0.25 seconds");
    }
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    detail::checkedWorld(value, handle).fixedTimeStep = seconds;
}

void setIterations(std::int64_t handle, std::int64_t count) {
    if (count < 1 || count > 16) {
        throw std::invalid_argument("physics iterations must be from 1 through 16");
    }
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    detail::checkedWorld(value, handle).iterations = static_cast<int>(count);
}

void setPaused(std::int64_t handle, bool paused) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    detail::checkedWorld(value, handle).paused = paused;
}

bool isPaused(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    return detail::checkedWorld(value, handle).paused;
}

void update(std::int64_t handle, double deltaTime) {
    detail::requireNonNegative(deltaTime, "deltaTime");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& world = detail::checkedWorld(value, handle);
    world.started.clear();
    world.ended.clear();
    if (world.paused || deltaTime == 0.0) return;
    world.accumulator += std::min(deltaTime, 0.25);
    constexpr int maxSteps = 8;
    int steps = 0;
    while (world.accumulator >= world.fixedTimeStep && steps < maxSteps) {
        const auto touchingBeforeStep = world.touching;
        b2World_Step(world.id, static_cast<float>(world.fixedTimeStep), world.iterations);
        detail::refreshTouching(handle, world);
        for (const auto& pair : world.touching) {
            if (!touchingBeforeStep.contains(pair)) world.started.insert(pair);
        }
        for (const auto& pair : touchingBeforeStep) {
            if (!world.touching.contains(pair)) world.ended.insert(pair);
        }
        world.accumulator -= world.fixedTimeStep;
        ++steps;
    }
    if (steps == maxSteps && world.accumulator >= world.fixedTimeStep) {
        world.accumulator = std::fmod(world.accumulator, world.fixedTimeStep);
    }
}

std::int64_t bodyCount(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    const auto& world = detail::checkedWorld(value, handle);
    return static_cast<std::int64_t>(std::count_if(
        value.bodies.begin(), value.bodies.end(),
        [handle](const auto& entry) { return entry.second.world == handle; }));
}

std::int64_t shapeCount(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    detail::checkedWorld(value, handle);
    return static_cast<std::int64_t>(std::count_if(
        value.shapes.begin(), value.shapes.end(),
        [handle](const auto& entry) { return entry.second.world == handle; }));
}

std::int64_t contactCount(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    return static_cast<std::int64_t>(detail::checkedWorld(value, handle).touching.size());
}

bool isTouching(std::int64_t bodyA, std::int64_t bodyB) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    const auto& a = detail::checkedBody(value, bodyA);
    const auto& b = detail::checkedBody(value, bodyB);
    if (a.world != b.world) {
        throw std::invalid_argument("contact bodies belong to different physics worlds");
    }
    return detail::checkedWorld(value, a.world).touching.contains(
        detail::bodyPair(bodyA, bodyB));
}

bool collisionStarted(std::int64_t worldHandle, std::int64_t bodyA,
    std::int64_t bodyB) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    const auto& a = detail::checkedBody(value, bodyA);
    const auto& b = detail::checkedBody(value, bodyB);
    if (a.world != worldHandle || b.world != worldHandle) {
        throw std::invalid_argument("collision bodies must belong to the specified physics world");
    }
    return detail::checkedWorld(value, worldHandle).started.contains(
        detail::bodyPair(bodyA, bodyB));
}

bool collisionEnded(std::int64_t worldHandle, std::int64_t bodyA,
    std::int64_t bodyB) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& world = detail::checkedWorld(value, worldHandle);
    const auto pair = detail::bodyPair(bodyA, bodyB);
    if (world.ended.contains(pair)) return true;
    const auto& a = detail::checkedBody(value, bodyA);
    const auto& b = detail::checkedBody(value, bodyB);
    if (a.world != worldHandle || b.world != worldHandle) {
        throw std::invalid_argument("collision bodies must belong to the specified physics world");
    }
    return false;
}

} // namespace zplus::physics
