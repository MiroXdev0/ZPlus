#include "Physics.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<typename Function>
void requireThrows(Function&& function, const char* message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void worldAndBodyLifecycle() {
    const auto world = zplus::physics::createWorld(0.0, 10.0);
    const auto body = zplus::physics::createBody(world, "dynamic", 2.0, 0.0);
    const auto circle = zplus::physics::createCircle(world, body, 1.0);
    require(zplus::physics::bodyCount(world) == 1, "body count");
    require(zplus::physics::shapeCount(world) == 1, "shape count");
    require(zplus::physics::getMass(body) > 0.0, "dynamic shape mass");

    zplus::physics::update(world, 1.0 / 60.0);
    require(zplus::physics::getPositionY(body) > 0.0, "gravity integration");
    zplus::physics::setPaused(world, true);
    const double pausedY = zplus::physics::getPositionY(body);
    zplus::physics::update(world, 0.1);
    require(zplus::physics::getPositionY(body) == pausedY, "paused world does not advance");
    zplus::physics::setPaused(world, false);

    zplus::physics::destroyShape(circle);
    require(zplus::physics::shapeCount(world) == 0, "shape destruction");
    zplus::physics::destroyBody(body);
    require(zplus::physics::bodyCount(world) == 0, "body destruction");
    requireThrows([&] { zplus::physics::getPositionX(body); }, "destroyed body handle");
    zplus::physics::destroyWorld(world);
    requireThrows([&] { zplus::physics::update(world, 0.01); }, "destroyed world handle");
}

void shapesAndQueries() {
    const auto world = zplus::physics::createWorld(0.0, 0.0);
    const auto boxBody = zplus::physics::createBody(world, "static", 10.0, 10.0);
    const auto box = zplus::physics::createBox(world, boxBody, 4.0, 4.0);
    const auto polygonBody = zplus::physics::createBody(world, "static", 30.0, 30.0);
    const auto polygon = zplus::physics::createPolygon(
        world, polygonBody, "0,0;4,0;0,4");
    const auto segmentBody = zplus::physics::createBody(world, "static", 0.0, 0.0);
    const auto segment = zplus::physics::createSegment(
        world, segmentBody, 0.0, 20.0, 20.0, 20.0);

    require(zplus::physics::pointQuery(world, 10.0, 10.0) == 1, "exact point query");
    require(zplus::physics::queryBodyAt(world, 0) == boxBody, "point result handle");
    require(zplus::physics::areaQuery(world, 8.0, 8.0, 12.0, 12.0) == 1,
        "area query");
    require(zplus::physics::raycast(world, 0.0, 10.0, 20.0, 10.0) == boxBody,
        "raycast hit body");
    require(zplus::physics::raycastFraction(world) >= 0.0 &&
        zplus::physics::raycastFraction(world) <= 1.0, "raycast hit fraction");

    zplus::physics::setSensor(box, true);
    zplus::physics::setCollisionCategory(polygon, 2);
    zplus::physics::setCollisionMask(polygon, 3);
    zplus::physics::setFriction(box, 0.7);
    zplus::physics::setRestitution(box, 0.25);
    zplus::physics::setDensity(box, 2.0);
    zplus::physics::setQueryFilter(world, 1, 2);
    require(zplus::physics::pointQuery(world, 30.5, 30.5) == 1,
        "query filter includes selected category");
    zplus::physics::setQueryFilter(world, 1, 1);
    require(zplus::physics::pointQuery(world, 30.5, 30.5) == 0,
        "query filter excludes other categories");
    zplus::physics::setQueryFilter(world, 1, 7);

    const auto circleBody = zplus::physics::createBody(world, "static", 50.0, 50.0);
    zplus::physics::createCircle(world, circleBody, 10.0);
    require(zplus::physics::areaQuery(world, 59.0, 59.0, 60.0, 60.0) == 0,
        "area query rejects separated shapes inside overlapping AABBs");

    requireThrows([&] {
        zplus::physics::createPolygon(world, polygonBody, "0,0;4,0;2,1;4,4;0,4");
    }, "concave polygon rejection");
    zplus::physics::destroyWorld(world);
    requireThrows([&] { zplus::physics::destroyShape(segment); },
        "world destroys dependent shapes");
}

void contactsAndJoints() {
    const auto world = zplus::physics::createWorld(0.0, 10.0);
    const auto floor = zplus::physics::createBody(world, "static", 0.0, 100.0);
    zplus::physics::createBox(world, floor, 200.0, 10.0);
    const auto falling = zplus::physics::createBody(world, "dynamic", 0.0, 0.0);
    zplus::physics::createCircle(world, falling, 2.0);
    zplus::physics::setRestitution(
        zplus::physics::createCircle(world, falling, 0.5), 0.0);
    for (int i = 0; i < 360; ++i) zplus::physics::update(world, 1.0 / 60.0);
    require(zplus::physics::getPositionY(falling) < 100.0,
        "falling body is resolved by floor");
    require(zplus::physics::isTouching(floor, falling), "contact event state");
    require(zplus::physics::contactCount(world) > 0, "contact count");

    const auto first = zplus::physics::createBody(world, "dynamic", 250.0, 0.0);
    const auto second = zplus::physics::createBody(world, "dynamic", 260.0, 0.0);
    zplus::physics::createCircle(world, first, 1.0);
    zplus::physics::createCircle(world, second, 1.0);
    const auto distance = zplus::physics::createDistanceJoint(world, first, second, 10.0);
    const auto revolute = zplus::physics::createRevoluteJoint(
        world, floor, first, 0.0, 100.0);
    const auto weld = zplus::physics::createWeldJoint(
        world, first, second, 255.0, 0.0);
    const auto mouse = zplus::physics::createMouseJoint(world, second, 260.0, 0.0, 100.0);
    zplus::physics::setMouseJointTarget(mouse, 261.0, 1.0);
    zplus::physics::destroyJoint(mouse);
    zplus::physics::destroyJoint(weld);
    zplus::physics::destroyJoint(revolute);
    zplus::physics::destroyJoint(distance);
    requireThrows([&] { zplus::physics::destroyJoint(distance); },
        "double joint destruction is rejected");
    zplus::physics::destroyWorld(world);
}

void sensorsAndFiltering() {
    const auto world = zplus::physics::createWorld(0.0, 0.0);
    const auto sensorBody = zplus::physics::createBody(world, "static", 0.0, 0.0);
    const auto sensor = zplus::physics::createBox(world, sensorBody, 10.0, 10.0);
    zplus::physics::setSensor(sensor, true);
    const auto visitor = zplus::physics::createBody(world, "dynamic", 0.0, 0.0);
    zplus::physics::createCircle(world, visitor, 1.0);
    zplus::physics::update(world, 1.0 / 60.0);
    require(zplus::physics::isTouching(sensorBody, visitor), "sensor overlap");
    require(zplus::physics::collisionStarted(world, sensorBody, visitor),
        "sensor start event");
    zplus::physics::setPosition(visitor, 20.0, 0.0);
    zplus::physics::update(world, 1.0 / 60.0);
    require(!zplus::physics::isTouching(sensorBody, visitor), "sensor separation");
    require(zplus::physics::collisionEnded(world, sensorBody, visitor),
        "sensor end event");
    zplus::physics::setPosition(visitor, 0.0, 0.0);
    zplus::physics::update(world, 1.0 / 60.0);
    require(zplus::physics::collisionStarted(world, sensorBody, visitor),
        "sensor re-entry event");
    zplus::physics::destroyBody(visitor);
    require(zplus::physics::collisionEnded(world, sensorBody, visitor),
        "destroyed body end event");

    const auto filteredA = zplus::physics::createBody(world, "dynamic", 40.0, 0.0);
    const auto filteredB = zplus::physics::createBody(world, "dynamic", 40.0, 0.0);
    const auto shapeA = zplus::physics::createCircle(world, filteredA, 2.0);
    zplus::physics::createCircle(world, filteredB, 2.0);
    zplus::physics::setCollisionMask(shapeA, 0);
    zplus::physics::update(world, 1.0 / 60.0);
    require(!zplus::physics::isTouching(filteredA, filteredB),
        "collision mask filters contact");

    const auto multiShape = zplus::physics::createBody(world, "dynamic", 80.0, 0.0);
    const auto other = zplus::physics::createBody(world, "dynamic", 80.0, 0.0);
    const auto firstShape = zplus::physics::createCircle(world, multiShape, 2.0);
    const auto secondShape = zplus::physics::createCircle(world, multiShape, 3.0);
    zplus::physics::createCircle(world, other, 2.0);
    zplus::physics::update(world, 1.0 / 60.0);
    require(zplus::physics::isTouching(multiShape, other), "multi-shape contact");
    zplus::physics::destroyShape(firstShape);
    require(zplus::physics::isTouching(multiShape, other),
        "removing one contact shape preserves other overlap");
    zplus::physics::destroyShape(secondShape);
    require(!zplus::physics::isTouching(multiShape, other),
        "removing last contact shape ends overlap");
    zplus::physics::destroyWorld(world);
}

void repeatedLifecycle() {
    for (int i = 0; i < 100; ++i) {
        const auto world = zplus::physics::createWorld(0.0, 0.0);
        const auto body = zplus::physics::createBody(world, "dynamic", 0.0, 0.0);
        zplus::physics::createCircle(world, body, 1.0);
        zplus::physics::destroyWorld(world);
    }
}

} // namespace

int main() {
    try {
        worldAndBodyLifecycle();
        shapesAndQueries();
        contactsAndJoints();
        sensorsAndFiltering();
        repeatedLifecycle();
        std::cout << "ZPhysics API tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ZPhysics API tests failed: " << error.what() << '\n';
        return 1;
    }
}
