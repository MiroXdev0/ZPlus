#include "PhysicsInternal.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace zplus::physics {
namespace {

b2ShapeDef shapeDefinition(const detail::ShapeRef& shape) {
    b2ShapeDef definition = b2DefaultShapeDef();
    definition.density = shape.density;
    definition.material.friction = shape.friction;
    definition.material.restitution = shape.restitution;
    definition.filter.categoryBits = shape.category;
    definition.filter.maskBits = shape.mask;
    definition.isSensor = shape.sensor;
    definition.enableSensorEvents = true;
    definition.enableContactEvents = true;
    return definition;
}

b2ShapeId createNativeShape(
    const detail::ShapeRef& shape,
    b2BodyId body
) {
    b2ShapeDef definition = shapeDefinition(shape);
    if (shape.kind == detail::ShapeRef::Kind::Circle) {
        b2Circle circle{};
        circle.center = {0.0f, 0.0f};
        circle.radius = shape.radius;
        return b2CreateCircleShape(body, &definition, &circle);
    }
    if (shape.kind == detail::ShapeRef::Kind::Segment) {
        b2Segment segment{shape.vertices[0], shape.vertices[1]};
        return b2CreateSegmentShape(body, &definition, &segment);
    }
    const b2Hull hull = b2ComputeHull(
        shape.vertices.data(), static_cast<int>(shape.vertices.size()));
    if (hull.count < 3) {
        throw std::invalid_argument("polygon vertices must form a non-degenerate convex hull");
    }
    const b2Polygon polygon = b2MakePolygon(&hull, 0.0f);
    return b2CreatePolygonShape(body, &definition, &polygon);
}

std::vector<b2Vec2> parseVertices(const std::string& value) {
    std::vector<b2Vec2> vertices;
    std::string_view input(value);
    while (!input.empty()) {
        const auto separator = input.find(';');
        const auto point = input.substr(0, separator);
        const auto comma = point.find(',');
        if (comma == std::string_view::npos || comma == 0 || comma + 1 >= point.size()) {
            throw std::invalid_argument(
                "polygon vertices must use 'x,y;x,y;...' format");
        }
        float x = 0.0f;
        float y = 0.0f;
        const auto xResult = std::from_chars(
            point.data(), point.data() + comma, x);
        const auto yResult = std::from_chars(
            point.data() + comma + 1, point.data() + point.size(), y);
        if (xResult.ec != std::errc{} || xResult.ptr != point.data() + comma ||
            yResult.ec != std::errc{} || yResult.ptr != point.data() + point.size() ||
            !std::isfinite(x) || !std::isfinite(y)) {
            throw std::invalid_argument("polygon vertices must be finite numeric coordinates");
        }
        vertices.push_back({x, y});
        if (vertices.size() > B2_MAX_POLYGON_VERTICES) {
            throw std::invalid_argument("polygon exceeds Box2D's maximum vertex count");
        }
        if (separator == std::string_view::npos) break;
        input.remove_prefix(separator + 1);
        if (input.empty()) {
            throw std::invalid_argument("polygon vertex list must not end with ';'");
        }
    }
    if (vertices.size() < 3) {
        throw std::invalid_argument("polygon requires at least three vertices");
    }
    return vertices;
}

std::int64_t create(std::int64_t worldHandle, std::int64_t bodyHandle,
    detail::ShapeRef shape) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    const auto& body = detail::checkedBody(value, bodyHandle);
    if (body.world != worldHandle) {
        throw std::invalid_argument("shape body belongs to a different physics world");
    }
    detail::checkedWorld(value, worldHandle);
    shape.world = worldHandle;
    shape.body = bodyHandle;
    shape.id = createNativeShape(shape, body.id);
    if (!b2Shape_IsValid(shape.id)) {
        throw std::runtime_error("Box2D failed to create a collision shape");
    }
    const auto handle = detail::nextHandle(value);
    value.shapes.emplace(handle, std::move(shape));
    return handle;
}

} // namespace

std::int64_t createCircle(std::int64_t world, std::int64_t body, double radius) {
    detail::requireFinite(radius, "radius");
    if (radius <= 0.0) throw std::invalid_argument("circle radius must be greater than zero");
    detail::ShapeRef shape{};
    shape.kind = detail::ShapeRef::Kind::Circle;
    shape.radius = static_cast<float>(radius);
    return create(world, body, std::move(shape));
}

std::int64_t createBox(std::int64_t world, std::int64_t body,
    double width, double height) {
    detail::requireFinite(width, "width");
    detail::requireFinite(height, "height");
    if (width <= 0.0 || height <= 0.0) {
        throw std::invalid_argument("box width and height must be greater than zero");
    }
    detail::ShapeRef shape{};
    shape.kind = detail::ShapeRef::Kind::Polygon;
    shape.vertices = {
        {-static_cast<float>(width * 0.5), -static_cast<float>(height * 0.5)},
        { static_cast<float>(width * 0.5), -static_cast<float>(height * 0.5)},
        { static_cast<float>(width * 0.5),  static_cast<float>(height * 0.5)},
        {-static_cast<float>(width * 0.5),  static_cast<float>(height * 0.5)}
    };
    return create(world, body, std::move(shape));
}

std::int64_t createPolygon(std::int64_t world, std::int64_t body,
    const std::string& vertices) {
    detail::ShapeRef shape{};
    shape.kind = detail::ShapeRef::Kind::Polygon;
    shape.vertices = parseVertices(vertices);
    const b2Hull hull = b2ComputeHull(shape.vertices.data(),
        static_cast<int>(shape.vertices.size()));
    if (hull.count != static_cast<int>(shape.vertices.size())) {
        throw std::invalid_argument(
            "polygon vertices must be unique, non-collinear, and form a convex polygon");
    }
    return create(world, body, std::move(shape));
}

std::int64_t createSegment(std::int64_t world, std::int64_t body,
    double x1, double y1, double x2, double y2) {
    for (const auto [value, name] : {
             std::pair{x1, "x1"}, std::pair{y1, "y1"},
             std::pair{x2, "x2"}, std::pair{y2, "y2"}}) {
        detail::requireFinite(value, name);
    }
    if (x1 == x2 && y1 == y2) {
        throw std::invalid_argument("segment endpoints must be distinct");
    }
    auto& value = detail::state();
    {
        std::lock_guard lock(value.mutex);
        const auto& bodyRef = detail::checkedBody(value, body);
        if (bodyRef.world != world) {
            throw std::invalid_argument("segment body belongs to a different physics world");
        }
        if (b2Body_GetType(bodyRef.id) != b2_staticBody) {
            throw std::invalid_argument("line segments may only be attached to static bodies");
        }
    }
    detail::ShapeRef shape{};
    shape.kind = detail::ShapeRef::Kind::Segment;
    shape.vertices = {{static_cast<float>(x1), static_cast<float>(y1)},
                      {static_cast<float>(x2), static_cast<float>(y2)}};
    return create(world, body, std::move(shape));
}

void destroyShape(std::int64_t handle) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    const auto shape = detail::checkedShape(value, handle);
    auto& world = detail::checkedWorld(value, shape.world);
    const auto touchingBefore = world.touching;
    b2DestroyShape(shape.id, true);
    value.shapes.erase(handle);
    detail::refreshTouching(shape.world, world);
    for (const auto& pair : touchingBefore) {
        if (!world.touching.contains(pair)) world.ended.insert(pair);
    }
}

void setCollisionCategory(std::int64_t handle, std::int64_t category) {
    if (category <= 0) throw std::invalid_argument("collision category must be positive");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& shape = detail::checkedShape(value, handle);
    shape.category = static_cast<std::uint64_t>(category);
    b2Filter filter = b2Shape_GetFilter(shape.id);
    filter.categoryBits = shape.category;
    b2Shape_SetFilter(shape.id, filter);
}

void setCollisionMask(std::int64_t handle, std::int64_t mask) {
    if (mask < 0) throw std::invalid_argument("collision mask must be non-negative");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& shape = detail::checkedShape(value, handle);
    shape.mask = static_cast<std::uint64_t>(mask);
    b2Filter filter = b2Shape_GetFilter(shape.id);
    filter.maskBits = shape.mask;
    b2Shape_SetFilter(shape.id, filter);
}

void setSensor(std::int64_t handle, bool sensor) {
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& shape = detail::checkedShape(value, handle);
    if (shape.sensor == sensor) return;
    auto& world = detail::checkedWorld(value, shape.world);
    const auto touchingBefore = world.touching;
    const auto& body = detail::checkedBody(value, shape.body);
    b2DestroyShape(shape.id, true);
    shape.sensor = sensor;
    shape.id = createNativeShape(shape, body.id);
    if (!b2Shape_IsValid(shape.id)) {
        throw std::runtime_error("Box2D failed to recreate collision shape as sensor");
    }
    detail::refreshTouching(shape.world, world);
    for (const auto& pair : touchingBefore) {
        if (!world.touching.contains(pair)) world.ended.insert(pair);
    }
}

void setFriction(std::int64_t handle, double friction) {
    detail::requireNonNegative(friction, "friction");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& shape = detail::checkedShape(value, handle);
    shape.friction = static_cast<float>(friction);
    auto material = b2Shape_GetSurfaceMaterial(shape.id);
    material.friction = shape.friction;
    b2Shape_SetSurfaceMaterial(shape.id, material);
}

void setRestitution(std::int64_t handle, double restitution) {
    detail::requireNonNegative(restitution, "restitution");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& shape = detail::checkedShape(value, handle);
    shape.restitution = static_cast<float>(restitution);
    auto material = b2Shape_GetSurfaceMaterial(shape.id);
    material.restitution = shape.restitution;
    b2Shape_SetSurfaceMaterial(shape.id, material);
}

void setDensity(std::int64_t handle, double density) {
    detail::requireNonNegative(density, "density");
    auto& value = detail::state();
    std::lock_guard lock(value.mutex);
    auto& shape = detail::checkedShape(value, handle);
    shape.density = static_cast<float>(density);
    b2Shape_SetDensity(shape.id, shape.density, true);
}

} // namespace zplus::physics
