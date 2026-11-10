#include "Physics.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifdef ZPHYSICS_RUNTIME_EXPORTS
#define ZPHYSICS_API extern "C" __declspec(dllexport)
#else
#define ZPHYSICS_API extern "C" __declspec(dllimport)
#endif
#else
#define ZPHYSICS_API extern "C" __attribute__((visibility("default")))
#endif

namespace {

[[noreturn]] void fail(const char* message) noexcept {
    std::fprintf(stderr, "native Z+ physics runtime error: %s\n", message);
    std::fflush(stderr);
    std::exit(EXIT_FAILURE);
}

template<typename Result, typename Function>
Result guard(Function&& function) noexcept {
    try {
        return function();
    } catch (const std::exception& error) {
        fail(error.what());
    } catch (...) {
        fail("unknown physics runtime failure");
    }
}

double number(std::int64_t bits) {
    return std::bit_cast<double>(static_cast<std::uint64_t>(bits));
}

std::int64_t encodedNumber(double value) {
    return std::bit_cast<std::int64_t>(value);
}

std::int64_t integerNumber(std::int64_t bits) {
    const double decoded = number(bits);
    if (!std::isfinite(decoded) || std::trunc(decoded) != decoded ||
        decoded < -0x1p63 || decoded >= 0x1p63) {
        throw std::invalid_argument("native physics integer argument is out of range");
    }
    return static_cast<std::int64_t>(decoded);
}

std::string checkedText(const char* value, const char* name) {
    if (!value) throw std::invalid_argument(std::string(name) + " must not be null");
    return value;
}

} // namespace

#define ZP_VOID(name, parameters, invocation) \
    ZPHYSICS_API std::int64_t zpphysics_##name parameters noexcept { \
        return guard<std::int64_t>([&] { invocation; return 0; }); \
    }
#define ZP_RET(name, parameters, invocation) \
    ZPHYSICS_API std::int64_t zpphysics_##name parameters noexcept { \
        return guard<std::int64_t>([&] { return static_cast<std::int64_t>(invocation); }); \
    }
#define ZP_NUM(name, parameters, invocation) \
    ZPHYSICS_API std::int64_t zpphysics_##name parameters noexcept { \
        return guard<std::int64_t>([&] { return encodedNumber(invocation); }); \
    }
#define ZP_BOOL(name, parameters, invocation) \
    ZPHYSICS_API std::int64_t zpphysics_##name parameters noexcept { \
        return guard<std::int64_t>([&] { return (invocation) ? 1 : 0; }); \
    }

ZP_RET(createWorld, (std::int64_t x, std::int64_t y),
    zplus::physics::createWorld(number(x), number(y)))
ZP_VOID(destroyWorld, (std::int64_t w), zplus::physics::destroyWorld(w))
ZP_VOID(setGravity, (std::int64_t w, std::int64_t x, std::int64_t y),
    zplus::physics::setGravity(w, number(x), number(y)))
ZP_NUM(getGravityX, (std::int64_t w), zplus::physics::getGravityX(w))
ZP_NUM(getGravityY, (std::int64_t w), zplus::physics::getGravityY(w))
ZP_VOID(setFixedTimeStep, (std::int64_t w, std::int64_t s),
    zplus::physics::setFixedTimeStep(w, number(s)))
ZP_VOID(setIterations, (std::int64_t w, std::int64_t n),
    zplus::physics::setIterations(w, integerNumber(n)))
ZP_VOID(setPaused, (std::int64_t w, std::int64_t p),
    zplus::physics::setPaused(w, p != 0))
ZP_BOOL(isPaused, (std::int64_t w), zplus::physics::isPaused(w))
ZP_VOID(update, (std::int64_t w, std::int64_t dt),
    zplus::physics::update(w, number(dt)))
ZP_NUM(bodyCount, (std::int64_t w), static_cast<double>(zplus::physics::bodyCount(w)))
ZP_NUM(shapeCount, (std::int64_t w), static_cast<double>(zplus::physics::shapeCount(w)))
ZP_NUM(contactCount, (std::int64_t w), static_cast<double>(zplus::physics::contactCount(w)))
ZPHYSICS_API std::int64_t zpphysics_createBody(
    std::int64_t w, const char* type, std::int64_t x, std::int64_t y) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::physics::createBody(
            w, checkedText(type, "bodyType"), number(x), number(y));
    });
}
ZP_VOID(destroyBody, (std::int64_t b), zplus::physics::destroyBody(b))
ZP_VOID(setPosition, (std::int64_t b, std::int64_t x, std::int64_t y),
    zplus::physics::setPosition(b, number(x), number(y)))
ZP_NUM(getPositionX, (std::int64_t b), zplus::physics::getPositionX(b))
ZP_NUM(getPositionY, (std::int64_t b), zplus::physics::getPositionY(b))
ZP_VOID(setRotation, (std::int64_t b, std::int64_t angle),
    zplus::physics::setRotation(b, number(angle)))
ZP_NUM(getRotation, (std::int64_t b), zplus::physics::getRotation(b))
ZP_VOID(setVelocity, (std::int64_t b, std::int64_t x, std::int64_t y),
    zplus::physics::setVelocity(b, number(x), number(y)))
ZP_NUM(getVelocityX, (std::int64_t b), zplus::physics::getVelocityX(b))
ZP_NUM(getVelocityY, (std::int64_t b), zplus::physics::getVelocityY(b))
ZP_VOID(setAngularVelocity, (std::int64_t b, std::int64_t v),
    zplus::physics::setAngularVelocity(b, number(v)))
ZP_NUM(getAngularVelocity, (std::int64_t b), zplus::physics::getAngularVelocity(b))
ZP_VOID(setMass, (std::int64_t b, std::int64_t m),
    zplus::physics::setMass(b, number(m)))
ZP_NUM(getMass, (std::int64_t b), zplus::physics::getMass(b))
ZP_VOID(setLinearDamping, (std::int64_t b, std::int64_t d),
    zplus::physics::setLinearDamping(b, number(d)))
ZP_VOID(setAngularDamping, (std::int64_t b, std::int64_t d),
    zplus::physics::setAngularDamping(b, number(d)))
ZP_VOID(applyForce, (std::int64_t b, std::int64_t x, std::int64_t y),
    zplus::physics::applyForce(b, number(x), number(y)))
ZP_VOID(applyImpulse, (std::int64_t b, std::int64_t x, std::int64_t y),
    zplus::physics::applyImpulse(b, number(x), number(y)))
ZP_VOID(applyTorque, (std::int64_t b, std::int64_t t),
    zplus::physics::applyTorque(b, number(t)))
ZP_RET(createCircle, (std::int64_t w, std::int64_t b, std::int64_t r),
    zplus::physics::createCircle(w, b, number(r)))
ZP_RET(createBox, (std::int64_t w, std::int64_t b, std::int64_t x, std::int64_t y),
    zplus::physics::createBox(w, b, number(x), number(y)))
ZPHYSICS_API std::int64_t zpphysics_createPolygon(
    std::int64_t w, std::int64_t b, const char* vertices) noexcept {
    return guard<std::int64_t>([&] {
        return zplus::physics::createPolygon(w, b, checkedText(vertices, "vertices"));
    });
}
ZP_RET(createSegment,
    (std::int64_t w, std::int64_t b, std::int64_t x1, std::int64_t y1,
     std::int64_t x2, std::int64_t y2),
    zplus::physics::createSegment(w, b, number(x1), number(y1), number(x2), number(y2)))
ZP_VOID(destroyShape, (std::int64_t s), zplus::physics::destroyShape(s))
ZP_VOID(setCollisionCategory, (std::int64_t s, std::int64_t c),
    zplus::physics::setCollisionCategory(s, integerNumber(c)))
ZP_VOID(setCollisionMask, (std::int64_t s, std::int64_t m),
    zplus::physics::setCollisionMask(s, integerNumber(m)))
ZP_VOID(setSensor, (std::int64_t s, std::int64_t v),
    zplus::physics::setSensor(s, v != 0))
ZP_VOID(setFriction, (std::int64_t s, std::int64_t f),
    zplus::physics::setFriction(s, number(f)))
ZP_VOID(setRestitution, (std::int64_t s, std::int64_t r),
    zplus::physics::setRestitution(s, number(r)))
ZP_VOID(setDensity, (std::int64_t s, std::int64_t d),
    zplus::physics::setDensity(s, number(d)))
ZP_BOOL(isTouching, (std::int64_t a, std::int64_t b),
    zplus::physics::isTouching(a, b))
ZP_BOOL(collisionStarted, (std::int64_t w, std::int64_t a, std::int64_t b),
    zplus::physics::collisionStarted(w, a, b))
ZP_BOOL(collisionEnded, (std::int64_t w, std::int64_t a, std::int64_t b),
    zplus::physics::collisionEnded(w, a, b))
ZP_RET(createDistanceJoint,
    (std::int64_t w, std::int64_t a, std::int64_t b, std::int64_t length),
    zplus::physics::createDistanceJoint(w, a, b, number(length)))
ZP_RET(createRevoluteJoint,
    (std::int64_t w, std::int64_t a, std::int64_t b, std::int64_t x, std::int64_t y),
    zplus::physics::createRevoluteJoint(w, a, b, number(x), number(y)))
ZP_RET(createWeldJoint,
    (std::int64_t w, std::int64_t a, std::int64_t b, std::int64_t x, std::int64_t y),
    zplus::physics::createWeldJoint(w, a, b, number(x), number(y)))
ZP_RET(createMouseJoint,
    (std::int64_t w, std::int64_t b, std::int64_t x, std::int64_t y,
     std::int64_t force),
    zplus::physics::createMouseJoint(w, b, number(x), number(y), number(force)))
ZP_VOID(setMouseJointTarget, (std::int64_t j, std::int64_t x, std::int64_t y),
    zplus::physics::setMouseJointTarget(j, number(x), number(y)))
ZP_VOID(destroyJoint, (std::int64_t j), zplus::physics::destroyJoint(j))
ZP_RET(raycast,
    (std::int64_t w, std::int64_t x1, std::int64_t y1, std::int64_t x2, std::int64_t y2),
    zplus::physics::raycast(w, number(x1), number(y1), number(x2), number(y2)))
ZP_NUM(raycastFraction, (std::int64_t w), zplus::physics::raycastFraction(w))
ZP_NUM(raycastPointX, (std::int64_t w), zplus::physics::raycastPointX(w))
ZP_NUM(raycastPointY, (std::int64_t w), zplus::physics::raycastPointY(w))
ZP_NUM(raycastNormalX, (std::int64_t w), zplus::physics::raycastNormalX(w))
ZP_NUM(raycastNormalY, (std::int64_t w), zplus::physics::raycastNormalY(w))
ZP_NUM(pointQuery, (std::int64_t w, std::int64_t x, std::int64_t y),
    static_cast<double>(zplus::physics::pointQuery(w, number(x), number(y))))
ZP_NUM(areaQuery,
    (std::int64_t w, std::int64_t x1, std::int64_t y1,
     std::int64_t x2, std::int64_t y2),
    static_cast<double>(zplus::physics::areaQuery(w, number(x1), number(y1), number(x2), number(y2))))
ZP_RET(queryBodyAt, (std::int64_t w, std::int64_t i),
    zplus::physics::queryBodyAt(w, integerNumber(i)))
ZP_VOID(setQueryFilter, (std::int64_t w, std::int64_t c, std::int64_t m),
    zplus::physics::setQueryFilter(w,
        integerNumber(c), integerNumber(m)))

#undef ZP_VOID
#undef ZP_RET
#undef ZP_NUM
#undef ZP_BOOL
