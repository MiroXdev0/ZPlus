#pragma once

#include <cstdint>
#include <string>

namespace zplus::physics {

std::int64_t createWorld(double gravityX, double gravityY);
void destroyWorld(std::int64_t world);
void setGravity(std::int64_t world, double x, double y);
double getGravityX(std::int64_t world);
double getGravityY(std::int64_t world);
void setFixedTimeStep(std::int64_t world, double seconds);
void setIterations(std::int64_t world, std::int64_t count);
void setPaused(std::int64_t world, bool paused);
bool isPaused(std::int64_t world);
void update(std::int64_t world, double deltaTime);
std::int64_t bodyCount(std::int64_t world);
std::int64_t shapeCount(std::int64_t world);
std::int64_t contactCount(std::int64_t world);

std::int64_t createBody(std::int64_t world, const std::string& type, double x, double y);
void destroyBody(std::int64_t body);
void setPosition(std::int64_t body, double x, double y);
double getPositionX(std::int64_t body);
double getPositionY(std::int64_t body);
void setRotation(std::int64_t body, double angle);
double getRotation(std::int64_t body);
void setVelocity(std::int64_t body, double x, double y);
double getVelocityX(std::int64_t body);
double getVelocityY(std::int64_t body);
void setAngularVelocity(std::int64_t body, double angularVelocity);
double getAngularVelocity(std::int64_t body);
void setMass(std::int64_t body, double mass);
double getMass(std::int64_t body);
void setLinearDamping(std::int64_t body, double damping);
void setAngularDamping(std::int64_t body, double damping);
void applyForce(std::int64_t body, double x, double y);
void applyImpulse(std::int64_t body, double x, double y);
void applyTorque(std::int64_t body, double torque);

std::int64_t createCircle(std::int64_t world, std::int64_t body, double radius);
std::int64_t createBox(std::int64_t world, std::int64_t body, double width, double height);
std::int64_t createPolygon(std::int64_t world, std::int64_t body, const std::string& vertices);
std::int64_t createSegment(std::int64_t world, std::int64_t body,
    double x1, double y1, double x2, double y2);
void destroyShape(std::int64_t shape);
void setCollisionCategory(std::int64_t shape, std::int64_t category);
void setCollisionMask(std::int64_t shape, std::int64_t mask);
void setSensor(std::int64_t shape, bool sensor);
void setFriction(std::int64_t shape, double friction);
void setRestitution(std::int64_t shape, double restitution);
void setDensity(std::int64_t shape, double density);

bool isTouching(std::int64_t bodyA, std::int64_t bodyB);
bool collisionStarted(std::int64_t world, std::int64_t bodyA, std::int64_t bodyB);
bool collisionEnded(std::int64_t world, std::int64_t bodyA, std::int64_t bodyB);

std::int64_t createDistanceJoint(std::int64_t world, std::int64_t bodyA,
    std::int64_t bodyB, double length);
std::int64_t createRevoluteJoint(std::int64_t world, std::int64_t bodyA,
    std::int64_t bodyB, double anchorX, double anchorY);
std::int64_t createWeldJoint(std::int64_t world, std::int64_t bodyA,
    std::int64_t bodyB, double anchorX, double anchorY);
std::int64_t createMouseJoint(std::int64_t world, std::int64_t body,
    double targetX, double targetY, double maxForce);
void setMouseJointTarget(std::int64_t joint, double x, double y);
void destroyJoint(std::int64_t joint);

std::int64_t raycast(std::int64_t world, double x1, double y1, double x2, double y2);
double raycastFraction(std::int64_t world);
double raycastPointX(std::int64_t world);
double raycastPointY(std::int64_t world);
double raycastNormalX(std::int64_t world);
double raycastNormalY(std::int64_t world);
std::int64_t pointQuery(std::int64_t world, double x, double y);
std::int64_t areaQuery(std::int64_t world, double x1, double y1, double x2, double y2);
std::int64_t queryBodyAt(std::int64_t world, std::int64_t index);
void setQueryFilter(std::int64_t world, std::int64_t category, std::int64_t mask);

} // namespace zplus::physics
