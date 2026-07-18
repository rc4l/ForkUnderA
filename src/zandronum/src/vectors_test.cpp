// [rc4l] Unit tests for the engine's header-only vector math (src/vectors.h).
#include "vectors.h"

#include <gtest/gtest.h>

namespace {

using Vec2 = TVector2<double>;
using Vec3 = TVector3<double>;

TEST(Vector3, ConstructionIndexingAndZero) {
  Vec3 v(1.0, 2.0, 3.0);
  EXPECT_DOUBLE_EQ(v[0], 1.0);
  EXPECT_DOUBLE_EQ(v[1], 2.0);
  EXPECT_DOUBLE_EQ(v[2], 3.0);
  v[0] = 9.0;  // [rc4l] Non-const operator[] returns a writable reference.
  EXPECT_DOUBLE_EQ(v[0], 9.0);
  v.Zero();
  EXPECT_TRUE(v == Vec3(0.0, 0.0, 0.0));
  Vec3 copy;
  copy = Vec3(4.0, 5.0, 6.0);  // [rc4l] Assignment operator.
  EXPECT_TRUE(copy == Vec3(4.0, 5.0, 6.0));
}

TEST(Vector3, LengthDotAndCross) {
  const Vec3 v(1.0, 2.0, 2.0);
  EXPECT_DOUBLE_EQ(v.LengthSquared(), 9.0);
  EXPECT_DOUBLE_EQ(v.Length(), 3.0);
  EXPECT_DOUBLE_EQ(Vec3(1, 2, 3) | Vec3(4, 5, 6), 32.0);
  EXPECT_TRUE((Vec3(1, 0, 0) ^ Vec3(0, 1, 0)) == Vec3(0, 0, 1));
}

TEST(Vector3, Arithmetic) {
  const Vec3 a(1.0, 2.0, 3.0);
  const Vec3 b(10.0, 20.0, 30.0);
  EXPECT_TRUE(a + b == Vec3(11, 22, 33));
  EXPECT_TRUE(b - a == Vec3(9, 18, 27));
  EXPECT_TRUE(-a == Vec3(-1, -2, -3));
  EXPECT_TRUE(a * 2.0 == Vec3(2, 4, 6));
  EXPECT_TRUE(2.0 * a == Vec3(2, 4, 6));
  EXPECT_TRUE(a / 2.0 == Vec3(0.5, 1.0, 1.5));
  EXPECT_TRUE(a + 1.0 == Vec3(2, 3, 4));
  EXPECT_TRUE(1.0 + a == Vec3(2, 3, 4));
  EXPECT_TRUE(a - 1.0 == Vec3(0, 1, 2));
  Vec3 m(1.0, 2.0, 3.0);
  m += b; m -= Vec3(10, 20, 30); m += 1.0; m -= 1.0; m *= 2.0; m /= 2.0;
  EXPECT_TRUE(m == a);
}

TEST(Vector3, UnitCoversZeroAndNonZero) {
  Vec3 v(0.0, 3.0, 4.0);
  EXPECT_TRUE(v.Unit().ApproximatelyEquals(Vec3(0.0, 0.6, 0.8)));
  v.MakeUnit();
  EXPECT_NEAR(v.Length(), 1.0, 1e-12);
  Vec3 zero(0.0, 0.0, 0.0);  // [rc4l] Zero-length hits the `len == 0` branch.
  EXPECT_TRUE(zero.Unit() == Vec3(0, 0, 0));
  zero.MakeUnit();
  EXPECT_TRUE(zero == Vec3(0, 0, 0));
}

TEST(Vector2, ArithmeticLengthAndUnit) {
  const Vec2 a(3.0, 4.0);
  EXPECT_DOUBLE_EQ(a.Length(), 5.0);
  EXPECT_DOUBLE_EQ(a.LengthSquared(), 25.0);
  EXPECT_DOUBLE_EQ(Vec2(1, 2) | Vec2(3, 4), 11.0);
  EXPECT_TRUE(a.Unit().ApproximatelyEquals(Vec2(0.6, 0.8)));
  Vec2 v(3.0, 4.0);
  EXPECT_DOUBLE_EQ(v.MakeUnit(), 5.0);  // [rc4l] TVector2::MakeUnit returns the old length.
  Vec2 zero(0.0, 0.0);
  EXPECT_TRUE(zero.Unit() == Vec2(0, 0));
  EXPECT_DOUBLE_EQ(zero.MakeUnit(), 0.0);
  Vec2 m(1.0, 2.0);
  m += Vec2(9, 18); m -= Vec2(9, 18); m += 1.0; m -= 1.0; m *= 2.0; m /= 2.0;
  EXPECT_TRUE(m == Vec2(1, 2));
  EXPECT_TRUE(-m == Vec2(-1, -2));
  EXPECT_TRUE(m + 1.0 == Vec2(2, 3));
  EXPECT_TRUE(1.0 + m == Vec2(2, 3));
  EXPECT_TRUE(m - 1.0 == Vec2(0, 1));
  EXPECT_TRUE(m + Vec2(1, 1) == Vec2(2, 3));
  EXPECT_TRUE(m - Vec2(1, 1) == Vec2(0, 1));
  EXPECT_TRUE(2.0 * m == Vec2(2, 4));
  EXPECT_TRUE(m * 2.0 == Vec2(2, 4));
  EXPECT_TRUE(m / 2.0 == Vec2(0.5, 1.0));
  Vec2 idx(7.0, 8.0);
  idx[1] = 9.0;
  EXPECT_DOUBLE_EQ(idx[0], 7.0);
  EXPECT_DOUBLE_EQ(idx[1], 9.0);
  idx.Zero();
  EXPECT_TRUE(idx == Vec2(0, 0));
}

// [rc4l] Drive both sides of every && / || in the equality operators from a table.
TEST(Vector3, EqualityAndApproximateEqualityBranches) {
  const Vec3 base(1.0, 1.0, 1.0);
  const double tiny = 1e-9;   // [rc4l] Well within EQUAL_EPSILON (~1.5e-5).
  const double big = 1.0;     // [rc4l] Well outside EQUAL_EPSILON.
  struct Case { Vec3 other; bool equal; bool approx; };
  const Case cases[] = {
    {Vec3(1, 1, 1), true, true},
    {Vec3(2, 1, 1), false, false},               // [rc4l] First component differs.
    {Vec3(1, 2, 1), false, false},               // [rc4l] Second component differs.
    {Vec3(1, 1, 2), false, false},               // [rc4l] Third component differs.
    {Vec3(1 + tiny, 1 - tiny, 1 + tiny), false, true},
    {Vec3(1 + big, 1, 1), false, false},
    {Vec3(1, 1 + big, 1), false, false},
    {Vec3(1, 1, 1 + big), false, false},
  };
  for (const auto& c : cases) {
    EXPECT_EQ(base == c.other, c.equal);
    EXPECT_EQ(base != c.other, !c.equal);
    EXPECT_EQ(base.ApproximatelyEquals(c.other), c.approx);
    EXPECT_EQ(base.DoesNotApproximatelyEqual(c.other), !c.approx);
  }
}

TEST(Vector2, EqualityAndApproximateEqualityBranches) {
  const Vec2 base(1.0, 1.0);
  const double tiny = 1e-9;
  const double big = 1.0;
  struct Case { Vec2 other; bool equal; bool approx; };
  const Case cases[] = {
    {Vec2(1, 1), true, true},
    {Vec2(2, 1), false, false},
    {Vec2(1, 2), false, false},
    {Vec2(1 + tiny, 1 - tiny), false, true},
    {Vec2(1 + big, 1), false, false},
    {Vec2(1, 1 + big), false, false},
  };
  for (const auto& c : cases) {
    EXPECT_EQ(base == c.other, c.equal);
    EXPECT_EQ(base != c.other, !c.equal);
    EXPECT_EQ(base.ApproximatelyEquals(c.other), c.approx);
    EXPECT_EQ(base.DoesNotApproximatelyEqual(c.other), !c.approx);
  }
}

}  // namespace
