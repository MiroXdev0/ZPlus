#include "PhysicsInternal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace zplus::physics {
namespace {

struct QueryContext {
    std::int64_t world;
    std::vector<std::int64_t>* bodies;
    bool pointQuery;
    b2Vec2 point;
};

b2QueryFilter queryFilter(const detail::World& world) {
    b2QueryFilter filter = b2DefaultQueryFilter();
    filter.categoryBits = world.queryCategory;
    filter.maskBits = world.queryMask;
    return filter;
}

bool queryCallback(b2ShapeId shape, void* opaque) {
    auto& context = *static_cast<QueryContext*>(opaque);
    if (context.pointQuery && !b2Shape_TestPoint(shape, context.point)) return true;
    const auto nativeBody = b2Shape_GetBody(shape);
    const auto body = detail::bodyHandleFromNative(context.world, nativeBody);
    if (body != 0 && std::find(context.bodies->begin(), context.bodies->end(), body) ==
        context.bodies->end()) {
        context.bodies->push_back(body);
    }
    return true;
}

} // namespace

std::int64_t raycast(std::int64_t handle, double x1, double y1, double x2, double y2) {
    detail::requireFinite(x1, "x1");
    detail::requireFinite(y1, "y1");
    detail::requireFinite(x2, "x2");
    detail::requireFinite(y2, "y2");
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    auto& world = detail::checkedWorld(state, handle);
    const b2Vec2 origin{static_cast<float>(x1), static_cast<float>(y1)};
    const b2Vec2 translation{static_cast<float>(x2 - x1), static_cast<float>(y2 - y1)};
    world.ray = b2World_CastRayClosest(
        world.id, origin, translation, queryFilter(world));
    world.rayBody = world.ray.hit
        ? detail::bodyHandleFromNative(handle, b2Shape_GetBody(world.ray.shapeId))
        : 0;
    return world.rayBody;
}

double raycastFraction(std::int64_t handle) {
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    return detail::checkedWorld(state, handle).ray.hit
        ? detail::checkedWorld(state, handle).ray.fraction : -1.0;
}

double raycastPointX(std::int64_t handle) {
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    const auto& ray = detail::checkedWorld(state, handle).ray;
    return ray.hit ? ray.point.x : 0.0;
}

double raycastPointY(std::int64_t handle) {
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    const auto& ray = detail::checkedWorld(state, handle).ray;
    return ray.hit ? ray.point.y : 0.0;
}

double raycastNormalX(std::int64_t handle) {
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    const auto& ray = detail::checkedWorld(state, handle).ray;
    return ray.hit ? ray.normal.x : 0.0;
}

double raycastNormalY(std::int64_t handle) {
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    const auto& ray = detail::checkedWorld(state, handle).ray;
    return ray.hit ? ray.normal.y : 0.0;
}

std::int64_t pointQuery(std::int64_t handle, double x, double y) {
    detail::requireFinite(x, "x");
    detail::requireFinite(y, "y");
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    auto& world = detail::checkedWorld(state, handle);
    world.queryBodies.clear();
    constexpr float extent = 0.0001f;
    const b2Vec2 point{static_cast<float>(x), static_cast<float>(y)};
    const b2AABB aabb{{point.x - extent, point.y - extent},
                      {point.x + extent, point.y + extent}};
    QueryContext context{handle, &world.queryBodies, true, point};
    b2World_OverlapAABB(world.id, aabb, queryFilter(world), queryCallback, &context);
    std::sort(world.queryBodies.begin(), world.queryBodies.end());
    return static_cast<std::int64_t>(world.queryBodies.size());
}

std::int64_t areaQuery(std::int64_t handle, double x1, double y1, double x2, double y2) {
    detail::requireFinite(x1, "x1");
    detail::requireFinite(y1, "y1");
    detail::requireFinite(x2, "x2");
    detail::requireFinite(y2, "y2");
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    auto& world = detail::checkedWorld(state, handle);
    world.queryBodies.clear();
    const float lowerX = static_cast<float>(std::min(x1, x2));
    const float lowerY = static_cast<float>(std::min(y1, y2));
    const float upperX = static_cast<float>(std::max(x1, x2));
    const float upperY = static_cast<float>(std::max(y1, y2));
    const float halfWidth = std::max((upperX - lowerX) * 0.5f, 0.0001f);
    const float halfHeight = std::max((upperY - lowerY) * 0.5f, 0.0001f);
    const b2Polygon polygon = b2MakeBox(halfWidth, halfHeight);
    const b2Vec2 center{(lowerX + upperX) * 0.5f, (lowerY + upperY) * 0.5f};
    const b2ShapeProxy proxy = b2MakeOffsetProxy(
        polygon.vertices, polygon.count, polygon.radius, center, b2MakeRot(0.0f));
    QueryContext context{handle, &world.queryBodies, false, {}};
    b2World_OverlapShape(
        world.id, &proxy, queryFilter(world), queryCallback, &context);
    std::sort(world.queryBodies.begin(), world.queryBodies.end());
    return static_cast<std::int64_t>(world.queryBodies.size());
}

std::int64_t queryBodyAt(std::int64_t handle, std::int64_t index) {
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    const auto& world = detail::checkedWorld(state, handle);
    if (index < 0 || static_cast<std::size_t>(index) >= world.queryBodies.size()) {
        throw std::out_of_range("physics query result index is out of range");
    }
    return world.queryBodies[static_cast<std::size_t>(index)];
}

void setQueryFilter(std::int64_t handle, std::int64_t category, std::int64_t mask) {
    if (category <= 0) throw std::invalid_argument("query category must be positive");
    if (mask < 0) throw std::invalid_argument("query mask must be non-negative");
    auto& state = detail::state();
    std::lock_guard lock(state.mutex);
    auto& world = detail::checkedWorld(state, handle);
    world.queryCategory = static_cast<std::uint64_t>(category);
    world.queryMask = static_cast<std::uint64_t>(mask);
}

} // namespace zplus::physics
