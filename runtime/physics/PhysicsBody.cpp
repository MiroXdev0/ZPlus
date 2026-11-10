#include "PhysicsInternal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace zplus::physics {

std::int64_t createBody(
    std::int64_t worldHandle,
    const std::string& type,
    double x,
    double y
) {
    detail::requireFinite(x, "positionX");
    detail::requireFinite(y, "positionY");
    b2BodyType nativeType;
    if (type == "static") nativeType = b2_staticBody;
    else if (type == "dynamic") nativeType = b2_dynamicBody;
    else if (type == "kinematic") nativeType = b2_kinematicBody;
    else throw std::invalid_argument("body type must be 'static', 'dynamic', or 'kinematic'");

    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& world = detail::checkedWorld(value, worldHandle);
    b2BodyDef definition = b2DefaultBodyDef();
    definition.type = nativeType;
    definition.position = {static_cast<float>(x), static_cast<float>(y)};
    const b2BodyId id = b2CreateBody(world.id, &definition);
    if (!b2Body_IsValid(id)) throw std::runtime_error("Box2D failed to create a body");
    const auto handle = detail::nextHandle(value);
    value.bodies.emplace(handle, detail::BodyRef{worldHandle, id});
    return handle;
}

void destroyBody(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    const auto ref = detail::checkedBody(value, handle);
    auto& world = detail::checkedWorld(value, ref.world);
    for (auto it = world.touching.begin(); it != world.touching.end();) {
        if (it->first == handle || it->second == handle) {
            world.ended.insert(*it);
            it = world.touching.erase(it);
        } else ++it;
    }
    b2DestroyBody(ref.id);
    value.bodies.erase(handle);
    std::erase_if(value.shapes, [handle](const auto& entry) {
        return entry.second.body == handle;
    });
    std::erase_if(value.joints, [handle](const auto& entry) {
        return entry.second.bodyA == handle || entry.second.bodyB == handle;
    });
}

void setPosition(std::int64_t handle, double x, double y) {
    detail::requireFinite(x, "positionX");
    detail::requireFinite(y, "positionY");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& body = detail::checkedBody(value, handle);
    b2Body_SetTransform(body.id, {static_cast<float>(x), static_cast<float>(y)},
        b2Body_GetRotation(body.id));
}

double getPositionX(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    return b2Body_GetPosition(detail::checkedBody(value, handle).id).x;
}

double getPositionY(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    return b2Body_GetPosition(detail::checkedBody(value, handle).id).y;
}

void setRotation(std::int64_t handle, double angle) {
    detail::requireFinite(angle, "rotation");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& body = detail::checkedBody(value, handle);
    const b2Vec2 position = b2Body_GetPosition(body.id);
    b2Body_SetTransform(body.id, position, b2MakeRot(static_cast<float>(angle)));
}

double getRotation(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    const auto rotation = b2Body_GetRotation(detail::checkedBody(value, handle).id);
    return b2Rot_GetAngle(rotation);
}

void setVelocity(std::int64_t handle, double x, double y) {
    detail::requireFinite(x, "velocityX");
    detail::requireFinite(y, "velocityY");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    b2Body_SetLinearVelocity(detail::checkedBody(value, handle).id,
        {static_cast<float>(x), static_cast<float>(y)});
}

double getVelocityX(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    return b2Body_GetLinearVelocity(detail::checkedBody(value, handle).id).x;
}

double getVelocityY(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    return b2Body_GetLinearVelocity(detail::checkedBody(value, handle).id).y;
}

void setAngularVelocity(std::int64_t handle, double velocity) {
    detail::requireFinite(velocity, "angularVelocity");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    b2Body_SetAngularVelocity(detail::checkedBody(value, handle).id,
        static_cast<float>(velocity));
}

double getAngularVelocity(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    return b2Body_GetAngularVelocity(detail::checkedBody(value, handle).id);
}

void setMass(std::int64_t handle, double mass) {
    detail::requireFinite(mass, "mass");
    if (mass <= 0.0) throw std::invalid_argument("dynamic body mass must be greater than zero");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    const auto& body = detail::checkedBody(value, handle);
    if (b2Body_GetType(body.id) != b2_dynamicBody) {
        throw std::invalid_argument("mass can only be set on a dynamic body");
    }
    b2MassData data = b2Body_GetMassData(body.id);
    const float oldMass = std::max(data.mass, 0.0001f);
    data.rotationalInertia *= static_cast<float>(mass) / oldMass;
    data.mass = static_cast<float>(mass);
    b2Body_SetMassData(body.id, data);
}

double getMass(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    return b2Body_GetMass(detail::checkedBody(value, handle).id);
}

void setLinearDamping(std::int64_t handle, double damping) {
    detail::requireNonNegative(damping, "linearDamping");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    b2Body_SetLinearDamping(detail::checkedBody(value, handle).id,
        static_cast<float>(damping));
}

void setAngularDamping(std::int64_t handle, double damping) {
    detail::requireNonNegative(damping, "angularDamping");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    b2Body_SetAngularDamping(detail::checkedBody(value, handle).id,
        static_cast<float>(damping));
}

void applyForce(std::int64_t handle, double x, double y) {
    detail::requireFinite(x, "forceX");
    detail::requireFinite(y, "forceY");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    b2Body_ApplyForceToCenter(detail::checkedBody(value, handle).id,
        {static_cast<float>(x), static_cast<float>(y)}, true);
}

void applyImpulse(std::int64_t handle, double x, double y) {
    detail::requireFinite(x, "impulseX");
    detail::requireFinite(y, "impulseY");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    b2Body_ApplyLinearImpulseToCenter(detail::checkedBody(value, handle).id,
        {static_cast<float>(x), static_cast<float>(y)}, true);
}

void applyTorque(std::int64_t handle, double torque) {
    detail::requireFinite(torque, "torque");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    b2Body_ApplyTorque(detail::checkedBody(value, handle).id,
        static_cast<float>(torque), true);
}

} // namespace zplus::physics
