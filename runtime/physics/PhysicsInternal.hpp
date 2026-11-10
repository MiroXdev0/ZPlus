#pragma once

#include "Physics.hpp"

#include <box2d/box2d.h>

#include <cstdint>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

namespace zplus::physics::detail {

struct BodyPair {
    std::int64_t first;
    std::int64_t second;
    bool operator<(const BodyPair& other) const {
        return first < other.first || (first == other.first && second < other.second);
    }
};

struct BodyRef {
    std::int64_t world;
    b2BodyId id;
};

struct ShapeRef {
    std::int64_t world;
    std::int64_t body;
    b2ShapeId id;
    enum class Kind { Circle, Polygon, Segment } kind = Kind::Circle;
    std::vector<b2Vec2> vertices;
    float radius = 0.0f;
    float friction = 0.3f;
    float restitution = 0.0f;
    float density = 1.0f;
    std::uint64_t category = 1;
    std::uint64_t mask = UINT64_MAX;
    bool sensor = false;
};

struct JointRef {
    std::int64_t world;
    std::int64_t bodyA;
    std::int64_t bodyB;
    b2JointId id;
    bool mouse = false;
};

struct World {
    b2WorldId id{};
    b2BodyId ground{};
    double fixedTimeStep = 1.0 / 60.0;
    double accumulator = 0.0;
    int iterations = 4;
    bool paused = false;
    std::uint64_t queryCategory = UINT64_MAX;
    std::uint64_t queryMask = UINT64_MAX;
    std::set<BodyPair> touching;
    std::set<BodyPair> started;
    std::set<BodyPair> ended;
    std::vector<std::int64_t> queryBodies;
    b2RayResult ray{};
    std::int64_t rayBody = 0;
};

struct State {
    std::mutex mutex;
    std::unordered_map<std::int64_t, World> worlds;
    std::unordered_map<std::int64_t, BodyRef> bodies;
    std::unordered_map<std::int64_t, ShapeRef> shapes;
    std::unordered_map<std::int64_t, JointRef> joints;
    std::int64_t nextHandle = 1;
};

State& state();
std::int64_t nextHandle(State& state);
World& checkedWorld(State& state, std::int64_t handle);
BodyRef& checkedBody(State& state, std::int64_t handle);
ShapeRef& checkedShape(State& state, std::int64_t handle);
JointRef& checkedJoint(State& state, std::int64_t handle);
void requireFinite(double value, const char* name);
void requireNonNegative(double value, const char* name);
BodyPair bodyPair(std::int64_t a, std::int64_t b);
std::int64_t bodyHandleFromNative(std::int64_t world, b2BodyId body);
void refreshTouching(std::int64_t worldHandle, World& world);

} // namespace zplus::physics::detail
