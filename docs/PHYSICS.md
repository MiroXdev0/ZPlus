# ZPhysics 0.1

ZPhysics is a 2D rigid-body module built on Box2D 3.1.1. It is independent of
ZGraphics and can run in interpreter or native programs.

```zp
use zp.physics

start {
    let world = physics.createWorld(0, 980)
    let floor = physics.createBody(world, "static", 320, 420)
    physics.createBox(world, floor, 600, 20)
    let ball = physics.createBody(world, "dynamic", 320, 80)
    physics.createCircle(world, ball, 18)

    for (let frame: number = 0; frame < 180; frame++) {
        physics.update(world, 1 / 60)
    } end for

    say(physics.getPositionY(ball))
    physics.destroyWorld(world)
} stop
```

## World lifecycle and stepping

`createWorld(gravityX, gravityY)` returns a numeric world handle.
`destroyWorld(world)` destroys the world and invalidates every body, shape, and
joint handle owned by it. Worlds are independent. `setGravity` and
`getGravityX`/`getGravityY` configure and query gravity. Gravity and coordinates
are expressed in application-defined world units; choose a consistent scale.

`setFixedTimeStep(world, seconds)` accepts a positive step no larger than 0.25
seconds. `setIterations(world, count)` accepts 1 through 16 and controls Box2D's
substep count. `update(world, deltaTime)` accumulates frame time and simulates
fixed steps; each call adds at most 0.25 seconds and executes at most eight
steps, discarding excess backlog. `setPaused`/`isPaused` control stepping.
`bodyCount`, `shapeCount`, and `contactCount` report counts for the selected
world.

## Bodies and forces

`createBody(world, type, x, y)` supports `"static"`, `"dynamic"`, and
`"kinematic"` body types. Bodies are configured with:

- `setPosition`, `getPositionX`, `getPositionY`
- `setRotation`, `getRotation` (radians)
- `setVelocity`, `getVelocityX`, `getVelocityY`
- `setAngularVelocity`, `getAngularVelocity` (radians per second)
- `setMass`, `getMass`
- `setLinearDamping`, `setAngularDamping`
- `applyForce`, `applyImpulse`, `applyTorque`
- `destroyBody`

Forces and impulses act at the center of mass. Static and kinematic bodies
cannot be assigned a mass. Dynamic body mass is calculated from attached shape
density; `setMass` overrides it until shape density changes again.

## Shapes and materials

Shapes are created in the owning body's local coordinates:

- `createCircle(world, body, radius)`
- `createBox(world, body, width, height)` creates a centered box.
- `createPolygon(world, body, vertices)` accepts a string such as
  `"0,0;40,0;20,30"`. Use three to eight unique finite points that form a
  non-degenerate convex polygon; interior, duplicate, and collinear input points
  are rejected. Box2D normalizes vertex winding.
- `createSegment(world, body, x1, y1, x2, y2)` creates a local line segment and
  is restricted to static bodies.

Bodies may have multiple shapes. `destroyShape` removes one shape. Shape
properties are configured with `setFriction`, `setRestitution`, `setDensity`,
`setSensor`, `setCollisionCategory`, and `setCollisionMask`. Friction,
restitution, and density must be non-negative. Categories and masks are
positive integer bit fields; masks may be zero.

## Contacts and queries

`isTouching(bodyA, bodyB)` reports current body contact or sensor overlap.
`collisionStarted(world, bodyA, bodyB)` and
`collisionEnded(world, bodyA, bodyB)` report transitions collected during the
most recent simulation update. They do not execute Z+ callbacks from physics
worker threads.

`raycast(world, x1, y1, x2, y2)` returns the body handle of the nearest hit, or
zero if there is no hit. After a raycast, `raycastFraction`, `raycastPointX`,
`raycastPointY`, `raycastNormalX`, and `raycastNormalY` query the cached result.
For no hit, fraction is `-1` and point/normal components are zero.

`pointQuery(world, x, y)` and `areaQuery(world, x1, y1, x2, y2)` return the
number of distinct bodies found. `queryBodyAt(world, index)` returns a body
handle from the most recent query; indices are zero-based and results are
ordered by handle. A later point/area query replaces the previous query result.
`setQueryFilter(world, category, mask)` selects the query category and mask;
new worlds query all categories by default.

## Joints

- `createDistanceJoint(world, bodyA, bodyB, length)`
- `createRevoluteJoint(world, bodyA, bodyB, anchorX, anchorY)`
- `createWeldJoint(world, bodyA, bodyB, anchorX, anchorY)`
- `createMouseJoint(world, body, targetX, targetY, maxForce)`
- `setMouseJointTarget(joint, x, y)` updates a mouse joint's target.
- `destroyJoint(joint)` explicitly destroys a joint.

Anchors for revolute and weld joints are specified in world coordinates when
the joint is created. Mouse joints can only target dynamic bodies. Destroying
either attached body or its world invalidates dependent joint handles.

## Handles, errors, and cleanup

Handles are unique validated numeric IDs, not Box2D pointers or ZMemory-managed
objects. Invalid and destroyed handles raise runtime errors. Explicitly call
`destroyBody`, `destroyShape`, `destroyJoint`, or `destroyWorld`; destroying a
world releases all of its dependent resources. A shape may be unloaded while
the world is idle; Box2D invalidates its contacts before returning. Do not retain
handles after destruction.

Interpreter errors are reported as runtime errors. Native physics runtime errors
are reported to standard error and terminate the application with a failure
status. No Z+ code runs in Box2D worker or callback threads.

## Backends and limitations

The Box2D engine and C++ runtime, interpreter bindings, and native Windows x64
runtime are implemented. Linux x86-64 ELF import and runtime staging paths are
implemented, but Linux compilation and execution have not been validated in
this Windows environment. Do not infer Linux support from the shared engine
code alone.

Native Z+ numeric values are integer-oriented. Native physics inputs support
fractional numeric literals at the runtime boundary, but computed fractional
values and fractional results cannot be represented consistently by the
current native numeric ABI. Interpreter programs retain full floating-point
behavior. Use integer-valued native queries where exact result values matter.

Handles require explicit cleanup; they are not automatically collected by
ZMemory. Segment shapes are static-only. The iteration setting maps to Box2D
substeps rather than exposing internal solver iterations. No device-independent
performance claims are made.

## Examples and tests

Runnable samples are in [`test_program/physics/`](../test_program/physics/):
falling objects, mixed-shape collisions, a forces-and-materials sandbox, joint
demonstrations, and a ZGraphics visualization. The headless API and interpreter
and native integration tests are registered with CTest as `zphysics_*`;
ZBuild staging and packaging are exercised by `zbuild_physics_integration`.
