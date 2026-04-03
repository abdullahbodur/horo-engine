#include "physics/narrowphase/SAT.h"

#include <array>
#include <cmath>
#include <limits>

#include "math/MathUtils.h"
#include "physics/BoxCollider.h"
#include "physics/RigidBody.h"

namespace Horo {

namespace SAT {

// Project extent of OBB onto axis
static float ProjectOBB(const Vec3& halfExtents, const Mat3& rot, const Vec3& axis) {
  return halfExtents.x * std::abs(Vec3::Dot(rot.GetColumn(0), axis)) +
         halfExtents.y * std::abs(Vec3::Dot(rot.GetColumn(1), axis)) +
         halfExtents.z * std::abs(Vec3::Dot(rot.GetColumn(2), axis));
}

// Return all 8 world-space corners of a box
static std::array<Vec3, 8> GetBoxVertices(const RigidBody& body, const BoxCollider& box) {
  Mat3 rot = body.orientation.ToMat3();
  Vec3 c0 = rot.GetColumn(0) * box.halfExtents.x;
  Vec3 c1 = rot.GetColumn(1) * box.halfExtents.y;
  Vec3 c2 = rot.GetColumn(2) * box.halfExtents.z;
  Vec3 p = body.position;
  return {
      p + c0 + c1 + c2,
      p - c0 + c1 + c2,
      p - c0 - c1 + c2,
      p + c0 - c1 + c2,
      p + c0 + c1 - c2,
      p - c0 + c1 - c2,
      p - c0 - c1 - c2,
      p + c0 - c1 - c2,
  };
}

// Generate contact points for a face-vs-body contact.
// refNormal: outward normal of the reference face (in world space, pointing toward incident body).
// refBody: body that contributes the reference face.
// incBody: body whose vertices we test against the reference face.
// bestAxis: final manifold normal (B→A).
static void GenerateFaceContacts(ContactManifold& result,
                                 const RigidBody& refBody,
                                 const BoxCollider& refBox,
                                 const RigidBody& incBody,
                                 const BoxCollider& incBox,
                                 const Vec3& refNormal,
                                 const Vec3& bestAxis) {
  Mat3 refRot = refBody.orientation.ToMat3();

  // Distance of the reference face plane from origin along refNormal
  float refExtent = 0.0f;
  for (int i = 0; i < 3; i++)
    refExtent += refBox.halfExtents[i] * std::abs(Vec3::Dot(refRot.GetColumn(i), refNormal));
  float refPlaneD = Vec3::Dot(refBody.position, refNormal) + refExtent;

  // Test all 8 vertices of the incident body
  auto incVerts = GetBoxVertices(incBody, incBox);
  for (const Vec3& v : incVerts) {
    float d = Vec3::Dot(v, refNormal);
    float pen = refPlaneD - d;
    if (pen >= -0.001f)
      result.AddContact(v, bestAxis, std::max(0.0f, pen));
    if (result.count >= ContactManifold::MAX_CONTACTS)
      break;
  }
}

// Which feature type produced the minimum penetration axis
enum class AxisSource { FaceA, FaceB, Edge };

ContactManifold TestBoxBox(const RigidBody& a, const RigidBody& b) {
  ContactManifold result;
  if (!a.collider || !b.collider)
    return result;
  if (a.collider->type != ColliderType::Box || b.collider->type != ColliderType::Box)
    return result;

  auto* boxA = static_cast<const BoxCollider*>(a.collider.get());
  auto* boxB = static_cast<const BoxCollider*>(b.collider.get());

  Mat3 rotA = a.orientation.ToMat3();
  Mat3 rotB = b.orientation.ToMat3();

  Vec3 axesA[3] = {rotA.GetColumn(0), rotA.GetColumn(1), rotA.GetColumn(2)};
  Vec3 axesB[3] = {rotB.GetColumn(0), rotB.GetColumn(1), rotB.GetColumn(2)};

  Vec3 T = b.position - a.position;
  float minPen = std::numeric_limits<float>::max();
  Vec3 bestAxis;
  AxisSource bestSource = AxisSource::FaceA;

  auto testAxis = [&](Vec3 axis, AxisSource src) -> bool {
    float len = axis.Length();
    if (len < EPSILON)
      return true;
    axis = axis * (1.0f / len);

    float pA = ProjectOBB(boxA->halfExtents, rotA, axis);
    float pB = ProjectOBB(boxB->halfExtents, rotB, axis);
    float d = std::abs(Vec3::Dot(T, axis));
    float pen = (pA + pB) - d;
    if (pen < 0)
      return false;
    if (pen < minPen) {
      minPen = pen;
      bestAxis = axis;
      bestSource = src;
    }
    return true;
  };

  for (int i = 0; i < 3; i++)
    if (!testAxis(axesA[i], AxisSource::FaceA))
      return result;
  for (int i = 0; i < 3; i++)
    if (!testAxis(axesB[i], AxisSource::FaceB))
      return result;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      if (!testAxis(Vec3::Cross(axesA[i], axesB[j]), AxisSource::Edge))
        return result;

  // Ensure normal points from B to A (opposite to T which is A→B)
  if (Vec3::Dot(bestAxis, T) > 0)
    bestAxis = -bestAxis;

  if (bestSource == AxisSource::FaceA) {
    // Reference = A, incident = B.  refNormal points from A toward B = -bestAxis.
    GenerateFaceContacts(result, a, *boxA, b, *boxB, -bestAxis, bestAxis);
  } else if (bestSource == AxisSource::FaceB) {
    // Reference = B, incident = A.  refNormal points from B toward A = bestAxis.
    GenerateFaceContacts(result, b, *boxB, a, *boxA, bestAxis, bestAxis);
  } else {
    // Edge-edge contact: single point
    result.AddContact(b.position + bestAxis * (minPen * 0.5f), bestAxis, minPen);
  }

  // Safety: if face clipping produced nothing (degenerate geometry), add a fallback
  if (result.count == 0)
    result.AddContact(b.position + bestAxis * (minPen * 0.5f), bestAxis, minPen);

  return result;
}

}  // namespace SAT
}  // namespace Horo
