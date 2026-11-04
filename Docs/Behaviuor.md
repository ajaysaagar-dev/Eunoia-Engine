# Eunoia Engine: Behaviour Scripting API Reference

> Applies to classes derived from `EunoiaBehaviour` (`Engine/EngineScene/Include/EngineScene/EunoiaBehaviour.h`).
> This document merges the current API with recommended additions.

## Status legend

| Tag | Meaning |
|---|---|
| ✅ | Exists today (documented in the current API doc and/or confirmed in `EunoiaBehaviour.h`) |
| ⚠️ | Documented but NOT found in the engine source when the repo was reviewed (`Instantiate`, `FindGameObject*`, `Destroy`). Verify before relying on it. |
| 🆕 | Proposed addition. Signature is a recommendation, not yet implemented. |
| 🗑️ | Deprecated alias. Keep working for now, remove later. |

---

## Table of contents

1. [Conventions](#1-conventions)
2. [Behaviour lifecycle](#2-behaviour-lifecycle)
3. [Transform](#3-transform)
4. [Translation and movement helpers](#4-translation-and-movement-helpers)
5. [Directional vectors](#5-directional-vectors)
6. [Input system](#6-input-system)
7. [Character mechanics](#7-character-mechanics)
8. [Animation graph](#8-animation-graph)
9. [Scene hierarchy and object queries](#9-scene-hierarchy-and-object-queries)
10. [Components](#10-components)
11. [Collision and trigger events](#11-collision-and-trigger-events)
12. [Physics queries and line trace](#12-physics-queries-and-line-trace)
13. [Timers](#13-timers)
14. [Coroutines and tweens](#14-coroutines-and-tweens)
15. [Events (message bus)](#15-events-message-bus)
16. [Math and random helpers](#16-math-and-random-helpers)
17. [Debug draw](#17-debug-draw)
18. [Audio](#18-audio)
19. [Level and save](#19-level-and-save)
20. [Logging](#20-logging)
21. [Editor-exposed properties and reflection](#21-editor-exposed-properties-and-reflection)
22. [Deprecations and alias cleanup](#22-deprecations-and-alias-cleanup)
23. [Performance rules](#23-performance-rules)
24. [Safety rules](#24-safety-rules)
25. [Complete example](#25-complete-example)
26. [Implementation roadmap](#26-implementation-roadmap)

---

## 1. Conventions

### Coordinate and axis convention

The current `RelativeTranslate` behaviour uses:

| Axis | Local meaning |
|---|---|
| `+X` | Forward |
| `-X` | Backward |
| `+Z` | Right |
| `-Z` | Left |
| `+Y` | Up |

Prefer `MoveForward / MoveRight / MoveUp` (🆕, section 4) in scripts so gameplay code does not depend on the axis layout.

### Rotation convention

Rotation is exposed as Euler angles in **degrees**:

| Component | Meaning |
|---|---|
| `X` | Pitch |
| `Y` | Yaw |
| `Z` | Roll |

🆕 Recommendation: store rotation internally as a quaternion and expose Euler angles on top. This avoids gimbal lock and allows `Slerp`.

### World vs Relative

| Term | Meaning |
|---|---|
| `World*` | Absolute scene space |
| `Relative*` | Space relative to the parent (local space) |
| `Translate(...)` (no prefix) | Same as `RelativeTranslate` |

### Naming rule (recommended)

One canonical name per action. Prefer `Location` over `Position` in new scripts. Prefer `GetX()`/`SetX()`-style or the property-call style `X(val)`. Do not mix styles in one script. See section 22.

### `TransformVector3`

The vector type returned by transform getters. Supports `+ - * /` with floats, doubles and ints.

| Member | Notes |
|---|---|
| `x, y, z` | Direct fields |
| `X()/Y()/Z()` | Getters (`GetX()` equivalents) |
| `GetValue()` / `SetValue(v)` | Convert to/from `glm::vec3` |

---

## 2. Behaviour lifecycle

### ✅ Virtual methods

```cpp
class MyBehaviour : public EunoiaBehaviour
{
public:
    void OnCreate() override {}                  // Object constructed (before Start)
    void OnEnable() override {}                  // Behaviour/object becomes active
    void Start() override {}                     // Once, before the first Update
    void Update(float dt) override {}            // Every frame
    void FixedUpdate(float fixedDt) override {}  // Fixed timestep (physics-rate logic)
    void LateUpdate(float dt) override {}        // After all Updates (cameras, follow logic)
    void OnDisable() override {}                 // Behaviour/object deactivated
    void OnDestroy() override {}                 // Cleanup (unbind input, events, timers)
};
```

### ✅ Delta time helpers

```cpp
float dt = DeltaTime();            // or GetDeltaTime() / GetWorldDeltaSeconds()
```

### ✅ Owner access

```cpp
GameObject* self = GetOwner();     // or GetEntity()
const auto& t = GetTransform();
```

### 🆕 Extra lifecycle hooks

```cpp
void OnBeginPlay() override {}               // Play mode starts (after all objects are created)
void OnEndPlay() override {}                 // Play mode ends
void OnValidate() override {}                // Editor only: a property changed in the Details panel
void OnApplicationPause(bool paused) override {}
```

### 🆕 Tick control (performance)

```cpp
SetTickEnabled(false);        // Stop calling Update entirely
SetTickInterval(0.1f);        // Call Update at most every 0.1 s (slow logic, AI thinking)
SetExecutionOrder(-10);       // Lower runs earlier. Make dependencies explicit.
```

A behaviour that does not override `Update`/`FixedUpdate`/`LateUpdate` should cost nothing per frame.

---

## 3. Transform

✅ Both `Transform` (on the behaviour) and `<GameObject>->Transform` expose identical properties and call syntax.

### 3.1 World and relative rotation (degrees)

```cpp
// Set all 3 axes
Transform.WorldRotation(x, y, z);
Transform.RelativeRotation(x, y, z);
Transform.WorldRotation(glm::vec3(x, y, z));
Transform.RelativeRotation(glm::vec3(x, y, z));

// Set one axis
Transform.WorldRotation.X(val);   Transform.WorldRotation.Y(val);   Transform.WorldRotation.Z(val);
Transform.RelativeRotation.X(val); Transform.RelativeRotation.Y(val); Transform.RelativeRotation.Z(val);

// Get
TransformVector3 worldRot = Transform.WorldRotation();     // or GetWorldRotation()
TransformVector3 relRot   = Transform.RelativeRotation();  // or GetRelativeRotation()
float yaw   = Transform.WorldRotation().GetY();            // or .y / .Y()
float pitch = Transform.WorldRotation().GetX();
float roll  = Transform.WorldRotation().GetZ();
```

### 3.2 World and relative location (position)

```cpp
// Set all 3 axes
Transform.WorldLocation(x, y, z);       // or WorldPosition(x, y, z)
Transform.RelativeLocation(x, y, z);    // or RelativePosition(x, y, z)
Transform.WorldLocation(glm::vec3(x, y, z));
Transform.RelativeLocation(glm::vec3(x, y, z));

// Set one axis
Transform.WorldLocation.X(val);   Transform.WorldLocation.Y(val);   Transform.WorldLocation.Z(val);
Transform.RelativeLocation.X(val); Transform.RelativeLocation.Y(val); Transform.RelativeLocation.Z(val);

// Get
TransformVector3 worldPos = Transform.WorldLocation();     // or GetWorldLocation() / GetWorldPosition()
TransformVector3 relPos   = Transform.RelativeLocation();  // or GetRelativeLocation() / GetRelativePosition()
float posX = Transform.WorldLocation().GetX();
float posY = Transform.WorldLocation().GetY();
float posZ = Transform.WorldLocation().GetZ();
```

### 3.3 World and relative scale

```cpp
// Set all 3 axes
Transform.WorldScale(x, y, z);
Transform.RelativeScale(x, y, z);
Transform.WorldScale(glm::vec3(x, y, z));
Transform.RelativeScale(glm::vec3(x, y, z));

// Set one axis
Transform.WorldScale.X(val);   Transform.WorldScale.Y(val);   Transform.WorldScale.Z(val);
Transform.RelativeScale.X(val); Transform.RelativeScale.Y(val); Transform.RelativeScale.Z(val);

// Get
TransformVector3 worldScale = Transform.WorldScale();      // or GetWorldScale()
TransformVector3 relScale   = Transform.RelativeScale();   // or GetRelativeScale()
```

### 3.4 🆕 Rotation helpers

```cpp
Transform.LookAt(targetPos, glm::vec3(0, 1, 0));            // Face a point (up vector optional)
Transform.LookAt(otherObject);                              // Face another object
Transform.RotateTowards(targetRot, degreesPerSecond);       // Smooth turn, uses dt internally
Transform.Rotate(axis, degrees, Space::Local);              // Space::Local or Space::World
Transform.SetForward(direction);                            // Orient so forward == direction
glm::quat q = Transform.GetWorldQuaternion();               // Quaternion access
Transform.SetWorldQuaternion(q);
```

### 3.5 🆕 Combined and utility calls

```cpp
Transform.SetWorld(pos, rot, scale);                        // One call, one matrix update
Transform.SetRelative(pos, rot, scale);
Transform.Teleport(pos, rot);                               // No sweep, no interpolation, resets velocity
Transform.SetParent(parentObject, /*keepWorld*/ true);      // Reparent without a visual jump
GameObject* parent = Transform.GetParent();

glm::vec3 wp = Transform.TransformPoint(localPoint);        // Local -> World
glm::vec3 lp = Transform.InverseTransformPoint(worldPoint); // World -> Local
glm::vec3 wd = Transform.TransformDirection(localDir);
glm::vec3 ld = Transform.InverseTransformDirection(worldDir);

float d       = Transform.DistanceTo(otherObject);
glm::vec3 dir = Transform.DirectionTo(otherObject);         // Normalised
glm::mat4 m   = Transform.GetLocalToWorld();                // Cached world matrix (read only)
```

> Setting X, Y and Z individually performs a read-modify-write each time. Prefer the 3-axis setters or `SetWorld/SetRelative` in hot paths.

---

## 4. Translation and movement helpers

### 4.1 ✅ Relative translate

Moves along the object's current facing: `+X` forward, `-X` backward, `+Z` right, `-Z` left, `+Y` up.

```cpp
Transform.RelativeTranslate(x, y, z);
Transform.RelativeTranslate(glm::vec3(x, y, z));
Transform.RelativeTranslate(fromVec, toVec);        // Moves by (toVec - fromVec)
Transform.RelativeTranslateTo(targetLocation);      // Sets relative location
```

### 4.2 ✅ World translate

Moves along absolute world axes.

```cpp
Transform.WorldTranslate(x, y, z);
Transform.WorldTranslate(glm::vec3(x, y, z));
Transform.WorldTranslate(fromVec, toVec);
Transform.WorldTranslateTo(targetLocation);         // Sets world location
```

### 4.3 ✅ General translate (same as RelativeTranslate)

```cpp
Transform.Translate(x, y, z);
Transform.Translate(glm::vec3(x, y, z));
Transform.TranslateTo(targetLocation);
```

### 4.4 🆕 Direction-explicit movement

```cpp
Transform.MoveForward(distance);    // Negative = backward
Transform.MoveRight(distance);      // Negative = left
Transform.MoveUp(distance);         // Negative = down
Transform.MoveTowards(targetPos, maxDelta);   // Never overshoots. Returns true when reached.
```

### 4.5 Frame-rate independence

Always scale continuous movement by delta time:

```cpp
Transform.MoveForward(speed * DeltaTime());
```

---

## 5. Directional vectors

✅

```cpp
TransformVector3 forward = Transform.GetForwardVector();  // World facing direction
TransformVector3 right   = Transform.GetRightVector();    // World right direction
TransformVector3 up      = Transform.GetUpVector();       // World up direction
```

🆕 Convenience:

```cpp
Transform.GetBackwardVector();
Transform.GetLeftVector();
Transform.GetDownVector();
```

---

## 6. Input system

### 6.1 ✅ Core (`InputSystem::Get()`)

```cpp
auto& input = InputSystem::Get();

// Keyboard
bool isHeld     = input.IsKeyDown(Key::W);          // Key::A, Key::S, Key::D, Key::Space, Key::LShift ...
bool isPressed  = input.IsKeyPressed(Key::Space);   // True only on the frame pressed
bool isReleased = input.IsKeyReleased(Key::LShift); // True only on the frame released

// Mouse buttons
bool leftHeld   = input.IsMouseButtonDown(MouseButton::Left);
bool rightHeld  = input.IsMouseButtonDown(MouseButton::Right);
bool leftClick  = input.IsMouseButtonPressed(MouseButton::Left);

// Mouse movement and axes
float mouseX  = input.GetMouseX();
float mouseY  = input.GetMouseY();
float mouseDx = input.GetMouseDeltaX();
float mouseDy = input.GetMouseDeltaY();
float axisX   = input.GetMouseAxisX(true);   // Inverted or raw axis delta
float axisY   = input.GetMouseAxisY(true);
float scroll  = input.GetMouseScrollDelta();
```

### 6.2 🆕 Mouse button release and cursor control

```cpp
bool leftReleased = input.IsMouseButtonReleased(MouseButton::Left);
input.SetCursorLocked(true);     // Capture the cursor for mouse-look
input.SetCursorVisible(false);
```

### 6.3 🆕 Action and axis mapping

Gameplay code reads **actions**, not raw keys. This makes rebinding and gamepad support free.

```cpp
float h = input.GetAxis("Horizontal");      // Keyboard + stick combined, deadzone applied
float v = input.GetAxis("Vertical");

input.BindAction("Jump", [this] { Jump(); });   // Auto-unbound on OnDestroy
bool fire = input.IsActionDown("Fire");
bool jump = input.IsActionPressed("Jump");

input.Rebind("Jump", Key::Space);           // Persisted in user settings
input.PushContext("Menu");                  // Input contexts: gameplay, menu, vehicle
input.PopContext();
```

### 🆕 6.4 Gamepad

```cpp
bool a    = input.IsGamepadButtonDown(Gamepad::A);
float lx  = input.GetGamepadAxis(Gamepad::LeftX);
float rt  = input.GetGamepadAxis(Gamepad::RightTrigger);
input.SetGamepadDeadzone(0.15f);
input.Vibrate(0.5f, 0.2f);                  // Strength, seconds
bool connected = input.IsGamepadConnected(0);
```

---

## 7. Character mechanics

### 7.1 ✅ Orientation and rotation (Unreal-style)

Available statically as `EngineSystem::CharacterMechanics::...` or on a `GameObject*`.

```cpp
EngineSystem::CharacterMechanics::SetOrientationToMovementDirection(true);
EngineSystem::CharacterMechanics::SetBodyObject(playerBodyObject);
EngineSystem::CharacterMechanics::SetBodyRotateToMovementDirection(true, gameObject);
EngineSystem::CharacterMechanics::SetRotationRate(720.0f);        // Degrees per second
EngineSystem::CharacterMechanics::SetBodyRotationRate(720.0f);

// Direct on GameObject
gameObject->SetOrientationToMovementDirection(true);
gameObject->SetBodyRotateToMovementDirection(true);
```

### 7.2 ✅ Velocity and speed

```cpp
float speed       = EngineSystem::CharacterMechanics::GetVelocity(1, 0, 1); // Horizontal speed (ignores Y)
float fullSpeed   = EngineSystem::CharacterMechanics::GetSpeed();
glm::vec3 velocity = EngineSystem::CharacterMechanics::GetVelocityVector();
```

### 7.3 🆕 Locomotion

```cpp
using Char = EngineSystem::CharacterMechanics;

Char::Jump();
Char::SetJumpHeight(1.2f);               // Metres. Engine computes the launch velocity.
Char::SetMaxWalkSpeed(6.0f);
Char::SetMaxRunSpeed(9.0f);
Char::SetAcceleration(40.0f);
Char::SetDeceleration(60.0f);
Char::SetAirControl(0.3f);               // 0..1
Char::SetGravityScale(1.0f);
Char::Launch(velocity, /*overrideXY*/ true, /*overrideZ*/ true);
Char::AddMovementInput(direction, scale);
```

### 🆕 7.4 State queries

```cpp
Char::IsGrounded();
Char::IsFalling();
Char::IsJumping();
Char::IsCrouching();
Char::GetGroundNormal();
Char::GetGroundObject();
```

### 🆕 7.5 Game-feel helpers

```cpp
Char::SetCoyoteTime(0.10f);     // Jump allowed shortly after leaving a ledge
Char::SetJumpBuffer(0.12f);     // Jump press remembered shortly before landing
Char::SetStepHeight(0.35f);
Char::SetSlopeLimit(45.0f);
Char::Crouch(true);
Char::SetCrouchHeight(1.0f);
```

---

## 8. Animation graph

### 8.1 ✅ Parameters (`AnimationGraph*`)

```cpp
// Controlling blend spaces and states
AnimGraph->SetFloat("Speed", speedValue);
AnimGraph->SetFloat("Direction", directionValue);
AnimGraph->SetBool("IsFalling", false);
AnimGraph->SetBool("IsJumping", true);
AnimGraph->SetTrigger("Jump");
AnimGraph->SetInt("Stance", 0);

// Reading
float currentSpeed = AnimGraph->GetFloat("Speed");
bool  isJumping    = AnimGraph->GetBool("IsJumping");
```

### 8.2 🆕 Cached parameter IDs (no string hashing per frame)

```cpp
class Player : public EunoiaBehaviour
{
    static inline const ParamId kSpeed = ParamId("Speed");   // Hashed once at startup

    void Update(float dt) override
    {
        AnimGraph->SetFloat(kSpeed, Char::GetSpeed());
    }
};
```

`ParamId("Name")` can also be `constexpr` (compile-time hash). The string overloads stay available for convenience.

### 🆕 8.3 Validation

```cpp
bool ok = AnimGraph->HasParam("Speed");
// Setting a missing parameter should log ONE clear error naming the object, graph and parameter.
```

### 🆕 8.4 State, blending and events

```cpp
AnimGraph->GetCurrentState();
AnimGraph->IsInState("Run");
AnimGraph->CrossFade("Run", 0.15f);
AnimGraph->SetLayerWeight("UpperBody", 0.5f);
AnimGraph->PlayMontage("Attack", /*speed*/ 1.0f, /*onFinished*/ [this] { OnAttackDone(); });
AnimGraph->StopMontage("Attack", 0.1f);
AnimGraph->OnAnimEvent("Footstep", [this] { PlayFootstep(); });   // Animation notify
AnimGraph->SetRootMotion(true);
AnimGraph->SetPlaybackSpeed(1.0f);
```

---

## 9. Scene hierarchy and object queries

### 9.1 ✅ Owner and hierarchy

```cpp
GameObject* self   = GetOwner();
GameObject* parent = GetOwner()->GetParent();
std::vector<GameObject*> children = GetOwner()->GetChildren();
```

### 9.2 ⚠️ Finding objects (documented, verify in source)

```cpp
GameObject* found = FindGameObject("CameraBoom");
GameObject* enemy = FindGameObjectWithTag("Enemy");
std::vector<GameObject*> allEnemies = FindGameObjectsWithTag("Enemy");
```

> Never call these every frame. Cache the result in `Start()`.

### 9.3 ⚠️ Spawning and destruction (documented, verify in source)

The header has `SpawnGameObject` / `DestroyGameObject`. The documented `Instantiate` / `Destroy` were not found.

```cpp
GameObject* spawned = Instantiate("BulletPrefab", spawnPosition, spawnRotation);
Destroy(targetObject);
Destroy(targetObject, 2.5f);   // Delayed destruction in seconds
```

### 🆕 9.4 Safe object references (prevents dangling pointers)

Scene objects can move in memory when the object list grows, and objects can be destroyed. A raw `GameObject*` may then dangle.

```cpp
ObjectRef boom = FindGameObject("CameraBoom");   // Weak, generation-checked handle
if (boom.IsValid())
{
    boom->Transform.WorldLocation(p);
}

ObjectRef target;                                // Store this in members instead of GameObject*
target.Reset();
```

Rules:
- Store `ObjectRef` as a member. Use `GameObject*` only for the duration of one function.
- `Destroy()` is **deferred to end of frame**. After calling it, `IsValid()` returns false from the next frame.

### 🆕 9.5 Extended queries

```cpp
GameObject* o  = FindObjectOfType<MyBehaviour>();
auto list      = FindObjectsOfType<EnemyBehaviour>();
GameObject* c  = GetOwner()->FindChild("Muzzle", /*recursive*/ true);
auto inRadius  = OverlapSphere(center, radius, layerMask);
GameObject* nearest = FindNearestWithTag("Enemy", Transform.WorldLocation());
```

### 🆕 9.6 Instantiate (full form) and activation

```cpp
GameObject* o = Instantiate(prefab, pos, rot, /*parent*/ nullptr);
GameObject* c = Instantiate(prefab, parent);              // Spawn as child, local identity

obj->SetActive(false);
bool active   = obj->IsActive();
bool inHier   = obj->IsActiveInHierarchy();
obj->SetTag("Enemy");
bool hasTag   = obj->HasTag("Enemy");
obj->SetLayer(Layer::Enemy);
```

---

## 10. Components

### ✅ Typed access

```cpp
auto* light  = GetComponent<LightComponent>();
auto* mesh   = GetComponent<PrimitiveMesh>();
auto* camera = GetComponent<OrbitCamera>();
```

> Today only those three types are specialised. For any other type the generic template returns `nullptr` silently.

### 🆕 Generic component API

```cpp
T*   TryGetComponent<T>();            // nullptr if missing (no log)
T*   GetComponent<T>();               // nullptr if missing, LOGS an error with owner + type
bool HasComponent<T>();
T*   AddComponent<T>();
void RemoveComponent<T>();
T*   GetComponentInChildren<T>();
T*   GetComponentInParent<T>();
std::vector<T*> GetComponentsInChildren<T>();
```

Implement through a component registry (type id to storage) so new component types do not need a hand-written specialisation.

---

## 11. Collision and trigger events

### 🆕 Callbacks (virtual methods)

```cpp
struct CollisionInfo
{
    GameObject* other;       // The other object
    glm::vec3   point;       // Contact point (world)
    glm::vec3   normal;      // Contact normal (pointing at this object)
    glm::vec3   impulse;
    float       relativeSpeed;
};

void OnCollisionEnter(const CollisionInfo& hit) override;
void OnCollisionStay(const CollisionInfo& hit) override;
void OnCollisionExit(GameObject* other) override;

void OnTriggerEnter(GameObject* other) override;
void OnTriggerStay(GameObject* other) override;
void OnTriggerExit(GameObject* other) override;
```

Example:

```cpp
void OnTriggerEnter(GameObject* other) override
{
    if (other->HasTag("Player"))
    {
        Events::Publish(PickupEvent{ GetOwner(), 10 });
        Destroy(GetOwner());
    }
}
```

Notes:
- Callbacks run on the game thread at a defined point of the frame (after physics, before `LateUpdate`).
- Do not destroy or reparent inside `Stay`/`Exit` loops directly. Use deferred `Destroy`.
- Add collision layers and a layer matrix so only relevant pairs generate events.

---

## 12. Physics queries and line trace

### 12.1 ✅ Line trace

```cpp
const RayTraceHit& hit = LineTrace(start, end, params);
const RayTraceHit& hit2 = LineTrace(start, end, ignoreObjectPtr);   // const GameObject*
const RayTraceHit& hit3 = LineTrace(start, end, ignoreObjectId);    // int

std::vector<RayTraceHit> all = LineTraceMulti(start, end, params);
bool blocked = LineTraceTest(start, end, params);                    // Cheapest: just yes/no
```

### 🆕 12.2 Shape queries and layers

```cpp
RayTraceParams p;
p.layerMask      = Layer::World | Layer::Enemy;
p.ignoreTriggers = true;
p.ignoreSelf     = true;

bool  hit = SphereTrace(start, end, radius, p, &outHit);
bool  box = BoxTrace(center, halfExtents, orientation, dir, dist, p, &outHit);
auto  ov  = OverlapSphere(center, radius, layerMask);
auto  ob  = OverlapBox(center, halfExtents, orientation, layerMask);
```

### 🆕 12.3 Rigid body helpers (physics plugin)

```cpp
Physics::AddForce(obj, force);
Physics::AddImpulse(obj, impulse);
Physics::SetVelocity(obj, v);
Physics::GetVelocity(obj);
Physics::SetGravityEnabled(obj, true);
Physics::SetMass(obj, 80.0f);
```

---

## 13. Timers

### 13.1 ✅ Start

Timers return a `TimerHandle` (`uint32_t`).

```cpp
TimerHandle t1 = StartTimer([this] { Explode(); }, 2.0f);   // Lambda
TimerHandle t2 = StartTimer("Explode", 2.0f);               // By function name (needs InvokeFunction)
TimerHandle t3 = StartTimer(&MyBehaviour::Explode, 2.0f);   // Member function pointer
TimerHandle t4 = StartTimer(&FreeFunction, 2.0f);           // Free function
// StartInterval(...) variants repeat every N seconds
```

### 13.2 ✅ Control and query

```cpp
StopTimer(id);      ClearTimer(id);      StopInterval(id);    ClearInterval(id);
ClearAllTimers();
PauseTimer(id);     ResumeTimer(id);
bool  active  = IsTimerActive(id);
bool  paused  = IsTimerPaused(id);
float left    = GetTimerRemaining(id);
float elapsed = GetTimerElapsed(id);
```

Lower-case aliases exist (`startTimer`, `stopTimer`, ...). See section 22.

### 🆕 13.3 Recommendations

- `StartTimer("FunctionName", ...)` calls `InvokeFunction(name)`, which does nothing by default. If the name is not registered it should **log an error** instead of failing silently (see `EUNOIA_FUNCTION`, section 21).
- Prefer lambdas or member-function pointers (compile-time checked) over string names.
- Timers are cleared automatically in `OnDestroy`. Verify this holds for timers capturing `this`.
- 🆕 `StartTimerUnscaled(...)` for timers that ignore time scale (pause menus, UI).

---

## 14. Coroutines and tweens

### 🆕 14.1 Coroutines (C++20)

The build tool already compiles with C++20, so coroutines are available. They replace most string-based timer chains.

```cpp
Coroutine FlashRoutine()
{
    Hide();
    co_await WaitSeconds(0.2f);
    Show();
    co_await WaitUntil([this] { return Char::IsGrounded(); });
    co_await WaitFrames(2);
    co_await WaitForEndOfFrame();
}

void Start() override { StartCoroutine(FlashRoutine()); }
void OnDestroy() override { StopAllCoroutines(); }   // Also automatic
```

API:

```cpp
CoroutineHandle h = StartCoroutine(MyRoutine());
StopCoroutine(h);
StopAllCoroutines();
bool running = IsCoroutineRunning(h);
```

### 🆕 14.2 Tweens

```cpp
Tween::To(Transform.WorldScale, glm::vec3(2.0f), 0.5f, Ease::OutBack);
Tween::Float(0.0f, 1.0f, 0.3f, Ease::InOutQuad, [this](float v) { SetAlpha(v); });
Tween::Move(GetOwner(), targetPos, 1.0f, Ease::OutCubic).OnComplete([this] { Done(); });
```

Easing set: `Linear, InQuad, OutQuad, InOutQuad, InCubic, OutCubic, InOutCubic, InBack, OutBack, InElastic, OutElastic, OutBounce`.

---

## 15. Events (message bus)

### 🆕 Typed events

Decouples systems (damage, pickups, quests, UI) without direct pointers.

```cpp
struct DamageEvent { ObjectRef target; float amount; ObjectRef source; };

// Subscribe (auto-unsubscribed when the behaviour is destroyed)
Events::Subscribe<DamageEvent>(this, [this](const DamageEvent& e)
{
    if (e.target == GetOwner()) health -= e.amount;
});

// Publish
Events::Publish(DamageEvent{ target, 25.0f, GetOwner() });

// Manual
Events::Unsubscribe<DamageEvent>(this);
```

Notes:
- Publishing is synchronous by default. Use `Events::PublishDeferred(...)` to deliver at end of frame.
- Never hold a raw pointer in an event. Use `ObjectRef`.

---

## 16. Math and random helpers

### 🆕 `Math::`

```cpp
Math::Lerp(a, b, t);
Math::InverseLerp(a, b, v);
Math::Remap(v, inMin, inMax, outMin, outMax);
Math::Clamp(v, lo, hi);
Math::Clamp01(v);
Math::MoveTowards(current, target, maxDelta);
Math::SmoothDamp(current, target, velocityRef, smoothTime);   // float and vec3 overloads
Math::LerpAngle(a, b, t);
Math::DeltaAngle(a, b);                                       // Shortest signed difference, degrees
Math::Approximately(a, b);
Math::Sign(v);
Math::Repeat(t, length);
Math::PingPong(t, length);
```

### 🆕 `Random::`

```cpp
Random::Range(min, max);              // float and int overloads
Random::Chance(0.25f);                // 25 % true
Random::InsideUnitSphere();
Random::OnUnitSphere();
Random::InsideUnitCircle();
Random::Direction2D();
Random::SetSeed(1234);                // Deterministic runs for tests and replays
```

---

## 17. Debug draw

### 🆕 `Debug::` (compiled out in shipping builds)

```cpp
Debug::DrawLine(a, b, Color::Red, /*seconds*/ 2.0f);
Debug::DrawArrow(from, direction, Color::Green);
Debug::DrawSphere(center, radius, Color::Yellow);
Debug::DrawBox(center, halfExtents, orientation, Color::Cyan);
Debug::DrawCapsule(a, b, radius, Color::Magenta);
Debug::DrawText(worldPos, "HP 80", Color::White);
Debug::DrawRay(origin, dir, length);
Debug::Pause();                       // Break into the editor on the current frame
```

Typical use: draw every `LineTrace` and `OverlapSphere` result while tuning gameplay.

---

## 18. Audio

### 🆕 Positional audio

```cpp
Audio::Play("UI_Click");                                   // 2D one-shot
Audio::PlayAt("Footstep", Transform.WorldLocation(), /*volume*/ 1.0f, /*pitch*/ 1.0f);
AudioSource* src = GetComponent<AudioSource>();
src->SetClip("Engine_Loop");
src->SetLoop(true);
src->SetVolume(0.6f);
src->SetPitch(1.2f);
src->SetSpatial(true, /*minDist*/ 2.0f, /*maxDist*/ 40.0f);
src->Play();   src->Pause();   src->Stop();

Audio::SetBusVolume("Music", 0.5f);                        // Buses: Master, Music, SFX, Voice, UI
Audio::StopAll("SFX");
```

---

## 19. Level and save

### 🆕 Level loading

```cpp
Level::Load("Level2");                        // Blocking
Level::Load("Level2", /*async*/ true, [this] { OnLevelReady(); });
Level::Reload();
std::string name = Level::GetCurrentName();
```

### 🆕 Save and load

```cpp
SaveGame::Set("coins", 120);
int coins = SaveGame::Get<int>("coins", /*default*/ 0);
SaveGame::Save("slot1");
SaveGame::Load("slot1");
SaveGame::Exists("slot1");
SaveGame::Delete("slot1");
```

---

## 20. Logging

### 20.1 ✅ Current

```cpp
Log::Info("Player health: " + std::to_string(health));
Log::Warning("Ammo low!");
Log::Error("Failed to find target waypoint!");
```

### 🆕 20.2 Formatted logging, categories and asserts

```cpp
Log::Info("Player health: {}", health);                     // std::format style, no std::to_string
Log::Warning("Ammo low: {}/{}", ammo, maxAmmo);
Log::Error("Waypoint '{}' not found", name);

Log::Info(LogCat::Combat, "Hit {} for {}", name, dmg);      // Category, filterable in the Output Log
Log::SetVerbosity(LogCat::AI, LogLevel::Warning);

Log::Ensure(ptr != nullptr, "Missing target");              // Logs once, script name + line, continues
Log::Check(ptr != nullptr, "Fatal: no player");             // Logs and breaks in debug builds
```

### 🆕 20.3 On-screen messages

```cpp
ScreenPrint::Show("Checkpoint!", /*seconds*/ 2.0f, Color::Green);
ScreenPrint::Show(/*key*/ "hp", "HP: " + std::to_string(hp));   // Same key updates in place
```

Guidelines:
- Do not format strings every frame for logging. Guard with a verbosity check or log on change only.
- Use categories so you can silence noisy systems.

---

## 21. Editor-exposed properties and reflection

### 🆕 Properties

One macro gives you the Details panel field, serialization (save/load), copy/paste and hot reload.

```cpp
class Turret : public EunoiaBehaviour
{
public:
    EUNOIA_PROPERTY(float,       FireRate, 2.0f, Range(0.1f, 20.0f), Tooltip("Shots per second"));
    EUNOIA_PROPERTY(float,       Damage,   10.0f);
    EUNOIA_PROPERTY(bool,        AutoAim,  true);
    EUNOIA_PROPERTY(ObjectRef,   Target);                       // Pick an object in the editor
    EUNOIA_PROPERTY(std::string, ProjectileName, "Bullet");
    EUNOIA_PROPERTY(glm::vec3,   MuzzleOffset, glm::vec3(0));
    EUNOIA_PROPERTY(Color,       TracerColor, Color::Yellow);

    EUNOIA_FUNCTION(Fire);                                      // Callable from the Details panel,
                                                                // node graph and string timers
    void Fire();
};
```

### 🆕 Behaviour registration

```cpp
EUNOIA_REGISTER_BEHAVIOUR(Turret);   // Makes it addable from the editor and serializable
```

Benefits:
- `InvokeFunction("Fire")` resolves automatically (no hand-written dispatch).
- Missing function names produce a clear error with the script and object name.
- Fits the plugin direction: every feature is a callable with parameters in and a result out.

---

## 22. Deprecations and alias cleanup

The current header contains many aliases for the same action. This increases compile time, autocomplete noise and ambiguity.

| Keep (canonical) | 🗑️ Deprecate |
|---|---|
| `Transform.WorldLocation(...)` | `Transform.WorldPosition(...)` |
| `Transform.RelativeLocation(...)` | `Transform.RelativePosition(...)` |
| `GetWorldLocation()` | `GetWorldPosition()` |
| `GetRelativeLocation()` | `GetRelativePosition()` |
| `RelativeTranslate(...)` | `Translate(...)` (ambiguous: relative or world?) |
| `RelativeTranslateTo(...)` | `TranslateTo(...)` |
| `WorldTranslate(from, to)` | `WorldTranslateFromTo(from, to)` |
| `X()` / `Y()` / `Z()` | `x()`, `SetX()`, lower-case duplicates |
| `LocalSpace / WorldSpace` | `RelativeSpace`, `SetLocalSpace`, `GetLocalSpace`, `locaspace`, `localspace`, `worldspace` |
| `StartTimer`, `StopTimer`, ... | `startTimer`, `stopTimer`, `clearTimer`, ... (lower-case) |
| `DeltaTime()` | `GetDeltaTime()`, `GetWorldDeltaSeconds()` |
| `GetOwner()` | `GetEntity()` |

Steps:
1. Mark aliases `[[deprecated("Use X instead")]]`.
2. Ship one release with warnings. Provide a script that auto-renames in user scripts.
3. Remove the aliases.

### 🚨 Pointer overloads that delete caller memory

Overloads like these take ownership and `delete` the argument:

```cpp
void LocalSpace(const glm::vec3* v);   // calls delete v
void WorldSpace(const glm::vec3* v);   // calls delete v
void WorldTranslate(const glm::vec3* delta);
void RelativeTranslate(const glm::vec3* delta);
void Translate(const glm::vec3* delta);
```

If the pointer is a stack address, a member, or shared, this is undefined behaviour (crash or double free). Fix:
- Remove them, or rename them (for example `...TakeOwnership`) and restrict them to the node graph only.
- Use by-value `glm::vec3` or `std::unique_ptr<glm::vec3>` instead.

---

## 23. Performance rules

1. **Cache lookups.** `FindGameObject`, `GetComponent`, `FindObjectOfType` and `ParamId` belong in `Start()`, not `Update()`.
2. **Opt-in ticking.** Only override `Update`/`FixedUpdate`/`LateUpdate` when you need them. Use `SetTickInterval` for slow logic.
3. **Avoid string work per frame.** No string concatenation, no string-keyed lookups, no `std::to_string` for logs in hot paths.
4. **Use handles.** `ParamId`, `ObjectRef`, tag ids and texture ids instead of strings.
5. **Prefer the cheapest query.** `LineTraceTest` (bool) before `LineTrace` (hit data) before `LineTraceMulti`.
6. **Batch transform writes.** Use `SetWorld(pos, rot, scale)` instead of nine separate axis setters.
7. **No allocation per frame.** Reuse `std::vector` members (`clear()` keeps capacity). Do not `new` objects in `Update`.
8. **Throttle expensive logic.** AI perception, pathfinding and target search at 5 to 10 Hz, not every frame.
9. **Use events instead of polling.** Subscribe to `DamageEvent` rather than checking health every frame.
10. **Profile.** Wrap suspicious code in `PROFILE_SCOPE("Name")` and check the editor frame-time overlay.

---

## 24. Safety rules

1. Never store raw `GameObject*` across frames. Store `ObjectRef` and check `IsValid()`.
2. `Destroy()` is deferred. Do not use the object in the same frame after destroying it unless you check `IsValid()`.
3. Do not modify the scene hierarchy (spawn, destroy, reparent) while iterating over children or find results. Collect, then apply.
4. Unbind input actions, event subscriptions, timers and coroutines in `OnDestroy` (🆕 automatic when registered through `this`).
5. Treat a `nullptr` return from `GetComponent<T>()` as a real possibility and handle it.
6. Do not call blocking operations (file IO, `Level::Load` blocking, long loops) in `Update`. Use async variants or coroutines.
7. Keep gameplay logic frame-rate independent: multiply by `DeltaTime()`, or use `FixedUpdate` for physics-rate logic.
8. Never modify files in a project's `Content/` folder from script code in a way that replaces real assets.

---

## 25. Complete example

A player behaviour using the recommended patterns.

```cpp
#include "EngineScene/EunoiaBehaviour.h"

using Char = EngineSystem::CharacterMechanics;

class PlayerBehaviour : public EunoiaBehaviour
{
public:
    EUNOIA_PROPERTY(float, MoveSpeed,   6.0f, Range(0.0f, 20.0f));
    EUNOIA_PROPERTY(float, JumpHeight,  1.2f, Range(0.1f, 5.0f));
    EUNOIA_PROPERTY(ObjectRef, CameraBoom);

    void Start() override
    {
        // Cache everything once
        m_anim = GetComponent<AnimationGraph>();
        if (!CameraBoom.IsValid())
            CameraBoom = FindGameObject("CameraBoom");

        Char::SetOrientationToMovementDirection(true);
        Char::SetRotationRate(720.0f);
        Char::SetJumpHeight(JumpHeight);
        Char::SetMaxWalkSpeed(MoveSpeed);
        Char::SetCoyoteTime(0.10f);
        Char::SetJumpBuffer(0.12f);

        InputSystem::Get().SetCursorLocked(true);
        InputSystem::Get().BindAction("Jump", [this] { Char::Jump(); });
    }

    void Update(float dt) override
    {
        auto& input = InputSystem::Get();

        // Camera look
        if (CameraBoom.IsValid())
        {
            auto rot = CameraBoom->Transform.RelativeRotation();
            float yaw   = rot.GetY() + input.GetMouseAxisX(true) * m_sensitivity;
            float pitch = Math::Clamp(rot.GetX() + input.GetMouseAxisY(true) * m_sensitivity, -80.0f, 80.0f);
            CameraBoom->Transform.RelativeRotation(pitch, yaw, 0.0f);
        }

        // Movement input
        glm::vec3 dir = Transform.GetForwardVector().GetValue() * input.GetAxis("Vertical")
                      + Transform.GetRightVector().GetValue()   * input.GetAxis("Horizontal");
        Char::AddMovementInput(dir, 1.0f);

        // Animation (cached ids, no string hashing per frame)
        m_anim->SetFloat(kSpeed,     Char::GetSpeed());
        m_anim->SetBool(kIsFalling,  Char::IsFalling());
        m_anim->SetBool(kIsGrounded, Char::IsGrounded());

        // Debug while tuning
        Debug::DrawArrow(Transform.WorldLocation().GetValue(), dir, Color::Green);
    }

    void OnTriggerEnter(GameObject* other) override
    {
        if (other->HasTag("Coin"))
        {
            Events::Publish(PickupEvent{ ObjectRef(other), 1 });
            Destroy(other);
            Audio::PlayAt("Coin", other->Transform.WorldLocation(), 1.0f, 1.0f);
        }
    }

    void OnDestroy() override
    {
        InputSystem::Get().SetCursorLocked(false);
        // Bindings, timers, coroutines and event subscriptions are cleared automatically.
    }

private:
    static inline const ParamId kSpeed      = ParamId("Speed");
    static inline const ParamId kIsFalling  = ParamId("IsFalling");
    static inline const ParamId kIsGrounded = ParamId("IsGrounded");

    AnimationGraph* m_anim = nullptr;
    float m_sensitivity = 0.15f;
};

EUNOIA_REGISTER_BEHAVIOUR(PlayerBehaviour);
```

---

## 26. Implementation roadmap

Suggested order (each step should build and keep existing scripts working):

| # | Work | Why first |
|---|---|---|
| 1 | Remove or rename the `delete`-ing pointer overloads. Add `ObjectRef` and deferred `Destroy`. | Fixes crash risk and dangling pointers |
| 2 | Verify and fix `Instantiate`, `FindGameObject*`, `Destroy` against the source. Update this doc. | Doc and code must match |
| 3 | Log an error when `InvokeFunction` or `GetComponent<T>` cannot resolve. | Ends silent failures |
| 4 | Collision and trigger events (section 11). | Core gameplay feature, currently missing |
| 5 | `Math::`, `Random::`, transform extras (`LookAt`, `MoveForward`, `TransformPoint`, ...). | Cheap, high daily value |
| 6 | `Debug::Draw*`. | Makes every later feature easier to tune |
| 7 | Input actions and gamepad. | Needed for rebinding and controller support |
| 8 | Character mechanics additions (jump, grounded, coyote time, ...). | Gameplay feel |
| 9 | `ParamId` for the animation graph and cached lookups. | Removes per-frame string cost |
| 10 | `EUNOIA_PROPERTY` / `EUNOIA_FUNCTION` reflection. | Details panel, serialization, hot reload |
| 11 | Coroutines, tweens, event bus. | Cleaner gameplay code |
| 12 | Audio, Level, SaveGame wrappers. | Feature completeness |
| 13 | Deprecate aliases (section 22), then remove. | Smaller header, faster compiles |
| 14 | Generate this reference from headers (Doxygen or a custom script). | Stops doc drift permanently |

### Compatibility rules

- Do not change behaviour of existing calls without a deprecation period.
- Do not change saved scene or project formats without a migration step.
- Add new APIs as new names. Keep old names working until step 13.