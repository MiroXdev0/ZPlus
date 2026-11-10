#include "PhysicsInternal.hpp"

#include <cmath>
#include <stdexcept>

namespace zplus::physics {
namespace {

std::pair<detail::BodyRef&, detail::BodyRef&> checkedPair(
    detail::State& state, std::int64_t worldHandle,
    std::int64_t bodyA, std::int64_t bodyB) {
    auto& world = detail::checkedWorld(state, worldHandle);
    auto& a = detail::checkedBody(state, bodyA);
    auto& b = detail::checkedBody(state, bodyB);
    if (a.world != worldHandle || b.world != worldHandle) {
        throw std::invalid_argument("joint bodies must belong to the specified physics world");
    }
    if (bodyA == bodyB) throw std::invalid_argument("a joint requires two different bodies");
    (void)world;
    return {a, b};
}

std::int64_t storeJoint(detail::State& state, std::int64_t world,
    std::int64_t bodyA, std::int64_t bodyB, b2JointId id, bool mouse = false) {
    if (!b2Joint_IsValid(id)) throw std::runtime_error("Box2D failed to create a joint");
    const auto handle = detail::nextHandle(state);
    state.joints.emplace(handle, detail::JointRef{world, bodyA, bodyB, id, mouse});
    return handle;
}

b2Vec2 localAnchor(b2BodyId body, double x, double y) {
    return b2Body_GetLocalPoint(body, {static_cast<float>(x), static_cast<float>(y)});
}

} // namespace

std::int64_t createDistanceJoint(std::int64_t worldHandle, std::int64_t bodyA,
    std::int64_t bodyB, double length) {
    detail::requireFinite(length, "jointLength");
    if (length <= 0.0) throw std::invalid_argument("distance joint length must be positive");
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    auto [a, b] = checkedPair(state, worldHandle, bodyA, bodyB);
    b2DistanceJointDef definition = b2DefaultDistanceJointDef();
    definition.bodyIdA = a.id;
    definition.bodyIdB = b.id;
    definition.length = static_cast<float>(length);
    const b2Vec2 centerA = b2Body_GetPosition(a.id);
    const b2Vec2 centerB = b2Body_GetPosition(b.id);
    definition.localAnchorA = localAnchor(a.id,
        (centerA.x + centerB.x) * 0.5, (centerA.y + centerB.y) * 0.5);
    definition.localAnchorB = localAnchor(b.id,
        (centerA.x + centerB.x) * 0.5, (centerA.y + centerB.y) * 0.5);
    return storeJoint(state, worldHandle, bodyA, bodyB,
        b2CreateDistanceJoint(detail::checkedWorld(state, worldHandle).id, &definition));
}

std::int64_t createRevoluteJoint(std::int64_t worldHandle, std::int64_t bodyA,
    std::int64_t bodyB, double anchorX, double anchorY) {
    detail::requireFinite(anchorX, "anchorX");
    detail::requireFinite(anchorY, "anchorY");
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    auto [a, b] = checkedPair(state, worldHandle, bodyA, bodyB);
    b2RevoluteJointDef definition = b2DefaultRevoluteJointDef();
    definition.bodyIdA = a.id;
    definition.bodyIdB = b.id;
    definition.localAnchorA = localAnchor(a.id, anchorX, anchorY);
    definition.localAnchorB = localAnchor(b.id, anchorX, anchorY);
    return storeJoint(state, worldHandle, bodyA, bodyB,
        b2CreateRevoluteJoint(detail::checkedWorld(state, worldHandle).id, &definition));
}

std::int64_t createWeldJoint(std::int64_t worldHandle, std::int64_t bodyA,
    std::int64_t bodyB, double anchorX, double anchorY) {
    detail::requireFinite(anchorX, "anchorX");
    detail::requireFinite(anchorY, "anchorY");
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    auto [a, b] = checkedPair(state, worldHandle, bodyA, bodyB);
    b2WeldJointDef definition = b2DefaultWeldJointDef();
    definition.bodyIdA = a.id;
    definition.bodyIdB = b.id;
    definition.localAnchorA = localAnchor(a.id, anchorX, anchorY);
    definition.localAnchorB = localAnchor(b.id, anchorX, anchorY);
    return storeJoint(state, worldHandle, bodyA, bodyB,
        b2CreateWeldJoint(detail::checkedWorld(state, worldHandle).id, &definition));
}

std::int64_t createMouseJoint(std::int64_t worldHandle, std::int64_t bodyHandle,
    double targetX, double targetY, double maxForce) {
    detail::requireFinite(targetX, "targetX");
    detail::requireFinite(targetY, "targetY");
    detail::requireNonNegative(maxForce, "maxForce");
    if (maxForce == 0.0) throw std::invalid_argument("mouse joint maxForce must be positive");
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    auto& world = detail::checkedWorld(state, worldHandle);
    auto& body = detail::checkedBody(state, bodyHandle);
    if (body.world != worldHandle) {
        throw std::invalid_argument("mouse joint body belongs to a different physics world");
    }
    if (b2Body_GetType(body.id) != b2_dynamicBody) {
        throw std::invalid_argument("mouse joints can only control dynamic bodies");
    }
    b2MouseJointDef definition = b2DefaultMouseJointDef();
    definition.bodyIdA = world.ground;
    definition.bodyIdB = body.id;
    definition.target = {static_cast<float>(targetX), static_cast<float>(targetY)};
    definition.maxForce = static_cast<float>(maxForce);
    return storeJoint(state, worldHandle, 0, bodyHandle,
        b2CreateMouseJoint(world.id, &definition), true);
}

void setMouseJointTarget(std::int64_t handle, double x, double y) {
    detail::requireFinite(x, "targetX");
    detail::requireFinite(y, "targetY");
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    auto& joint = detail::checkedJoint(state, handle);
    if (!joint.mouse) throw std::invalid_argument("joint handle is not a mouse joint");
    b2MouseJoint_SetTarget(joint.id, {static_cast<float>(x), static_cast<float>(y)});
}

void destroyJoint(std::int64_t handle) {
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    const auto joint = detail::checkedJoint(state, handle);
    b2DestroyJoint(joint.id);
    state.joints.erase(handle);
}

} // namespace zplus::physics
