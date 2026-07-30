# Scene Math

This document defines the normative scene-mathematics contract shared by the
runtime, renderer, editor viewport, picking, and transform tools.

## Coordinate And Matrix Convention

Horo scene space is right-handed, uses positive Y as up, and uses negative Z as
forward. Angles are radians. Matrices are column-major, multiply column vectors,
and compose authored transforms as `T * R * S`. A child world transform is
`parentToWorld * childLocalToParent`.

`Horo::Math` in `include/Horo/Math/SceneMath.h` is the single public authority for
these conventions. Consumers must not maintain private vector, projection,
inverse, ray, or intersection implementations.

## Public Values

The common value vocabulary is `Vec2`, `Vec3`, `Vec4`, `Quaternion`, `Mat4`,
`Transform`, `Ray`, `Plane`, `Aabb`, `BoundingSphere`, and `RayHit`. `Transform`
stores local translation, quaternion rotation, and scale. Quaternion Euler
conversion preserves the engine's existing X/Y/Z authored rotation order.

The default comparison epsilon is `1e-6`. Callers may select a larger epsilon for
accumulated or presentation-space computations. Inputs crossing an authoring,
serialization, device, or UI boundary must be checked for finiteness.

## Fallible And Validated Operations

Potentially invalid boundary data uses `Try` operations returning `Result<T>`.
The stable error domain is `horo.foundation.math`; errors distinguish non-finite
input, zero length, singular matrices, and invalid projection, view, bounds, or
ray data. A geometric miss is a successful empty `optional`, not an error.

Value-returning normalize, projection, and transform helpers are reserved for
already validated frame-hot data. Their preconditions are asserted in debug
builds. They must not silently return an identity matrix, zero vector, or other
plausible value for invalid input. Successful frame-hot math performs no heap
allocation.

Affine scene paths use `TryInverseAffine`. General `TryInverse` exists for
project/unproject and other genuinely projective matrices. Affine TRS
decomposition preserves valid negative scale where representable. When parented
non-uniform scale introduces shear, decomposition returns the deterministic
nearest representable TRS; singular or non-finite input is rejected.

Editor transform-gizmo evaluation is a UI-independent consumer of these
contracts. A drag begins only after its local/world transforms, parent inverse,
axis, and pointer scale are validated. Local and world Move/Rotate/Scale
evaluation returns a typed result. Negative authored scale signs are preserved
when a world-space result is projected back to local TRS. Singular parents and
failed decomposition cancel the transient preview instead of falling back to
identity axes or committing a partial transform.

## Camera Projection

`Runtime::CameraProjection` is the shared authored/editor projection enum.
Perspective and orthographic builders consume the same right-handed view-space
contract. `ClipDepthRange::NegativeOneToOne` maps near/far to `-1/1` for OpenGL.
`ClipDepthRange::ZeroToOne` maps them to `0/1` for Metal and Vulkan. Reverse-Z and
infinite-far projections are not part of this contract.

Renderer MVP construction, CPU picking, gizmo projection, and screen-ray
construction consume the same explicit clip-depth range. The active editor
viewport adapter supplies that typed value: OpenGL reports
`NegativeOneToOne`, while Metal and Vulkan report `ZeroToOne`. Editor consumers
must not infer the convention from a backend name or silently assume OpenGL.
Perspective rays originate at the camera position. Orthographic rays originate
at the applicable near-plane point.

## Bounds And Editor Viewport

Renderable instances carry a local AABB. World AABBs are produced by transforming
all eight corners with the instance affine transform, covering rotation, negative
scale, and non-uniform parent scale. Current UnitBox bounds are `[-0.5, 0.5]` on
all axes.

Focus-selected operates only on a selected instance present in the current
viewport snapshot. It frames a conservative bounding sphere with a 20 percent
margin and minimum radius of `0.25` scene units. Perspective framing uses the
narrower horizontal/vertical field of view; orthographic framing adjusts vertical
height. Missing or non-renderable selection is a no-op and creates no history or
document revision.

Editor world grids use the same camera units and XZ ground plane as scene
transforms. Adaptive minor spacing is selected from a stable `1/2/5 × 10ⁿ`
sequence using visible world height and viewport pixel height. Grid generation
is bounded and allocation-free on the frame path; it never changes scene,
selection, picking, or document state.

## Verification

Tests cover vector and quaternion validity, `T * R * S`, hierarchy composition,
affine/general inverse, negative and non-uniform scale, shear projection, both
clip-depth conventions, project/unproject, perspective and orthographic rays,
ray-plane and ray-AABB behavior, transformed bounds, focus framing, scale-aware
navigation, picking, transform-gizmo preview/commit/cancel behavior, and
UI-independent gizmo evaluation under singular, negative-scale, and rotated
non-uniform parents.
