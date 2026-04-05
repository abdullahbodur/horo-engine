# Math Module

`math/` provides the engine’s custom SIMD-friendly math primitives and transforms.

## Responsibilities

- Vector math: `Vec2`, `Vec3`, `Vec4`
- Matrix math: `Mat3`, `Mat4` (column-major, OpenGL-compatible)
- Rotation math: `Quaternion`
- Transform composition: `Transform`
- Utility helpers: interpolation, distance, reflection, epsilon helpers

## Main Types

- `Vec2/Vec3/Vec4`
  - Arithmetic operators, normalization, dot/cross (Vec3), lerp
- `Mat3/Mat4`
  - Translation/rotation/scale composition
  - Perspective/orthographic projection and `LookAt`
  - Determinant, inverse, transpose
- `Quaternion`
  - Axis-angle and Euler conversion
  - Slerp/Lerp, vector rotation, look rotation
- `Transform`
  - Position/rotation/scale container
  - `ToMatrix()` and local→world point conversion

## Conventions

- Radians are used for all angular APIs.
- `Mat4` is column-major (`m[col][row]`) and can be uploaded directly to GLSL uniforms.
- Forward direction convention is `Vec3::Forward() == (0, 0, -1)`.

## Example

```cpp
Monolith::Transform t({0.0f, 2.0f, 5.0f},
                      Monolith::Quaternion::FromEuler(0.0f, 0.7f, 0.0f),
                      {1.0f, 1.0f, 1.0f});

Monolith::Mat4 model = t.ToMatrix();
```
