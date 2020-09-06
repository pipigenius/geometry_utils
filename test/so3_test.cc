#include <chrono>
#include <iostream>

#include "numerical_derivative.hpp"
#include "so3.hpp"
#include "test_utils.hpp"

// TODO(gareth): Automatically do the same tests on double + float.
namespace math {

// Test skew-symmetric operator.
TEST(SO3Test, TestSkew3) {
  using Vector3d = Vector<double, 3>;
  using Matrix3d = Matrix<double, 3, 3>;
  const Vector3d x = (Vector3d() << 1, 2, 3).finished();
  const Vector3d y = (Vector3d() << 1, 1, 1).finished();
  EXPECT_EIGEN_NEAR(Vector3d::Zero(), Skew3(x) * x, tol::kPico);
  EXPECT_EIGEN_NEAR(x.cross(y), Skew3(x) * y, tol::kPico);
  EXPECT_EIGEN_NEAR(Matrix3d::Zero(), Skew3(x) + Skew3(x).transpose(), tol::kPico);
}

// Test quaternion multiplication.
TEST(SO3Test, TestQuaternionMulMatrix) {
  const auto to_vec = [](const Quaternion<double>& q) -> Vector<double, 4> {
    return Vector<double, 4>(q.w(), q.x(), q.y(), q.z());
  };
  const Quaternion<double> q0{-0.5, 0.2, 0.1, 0.8};
  const Quaternion<double> q1{0.4, -0.3, 0.2, 0.45};
  EXPECT_EIGEN_NEAR(to_vec(q0 * q1), QuaternionMulMatrix(q0) * to_vec(q1), tol::kPico);
  EXPECT_EIGEN_NEAR(to_vec(q1 * q0), QuaternionMulMatrix(q1) * to_vec(q0), tol::kPico);
}

// Simple test of exponential map by series comparison + numerical derivative.
class TestQuaternionExp : public ::testing::Test {
 public:
  template <typename Scalar>
  void TestOmega(const Vector<Scalar, 3>& w, const Scalar matrix_tol,
                 const Scalar deriv_tol) const {
    const QuaternionExpDerivative<Scalar> q_and_deriv{w};
    // compare to the exponential map as a power series ~ 50 terms
    ASSERT_EIGEN_NEAR(ExpMatrixSeries(Skew3(w), 50), q_and_deriv.q.matrix(), matrix_tol)
        << "w = " << w.transpose();

    // compare to Eigen implementation
    const Eigen::AngleAxis<Scalar> aa(w.norm(), w.normalized());
    ASSERT_EIGEN_NEAR(aa.toRotationMatrix(), q_and_deriv.q.matrix(), matrix_tol)
        << "w = " << w.transpose();

    // check derivative numerically
    const Matrix<Scalar, 4, 3> J_numerical =
        NumericalJacobian(w, [](const Vector<Scalar, 3>& w) -> Vector<Scalar, 4> {
          // convert to correct order here
          const Quaternion<Scalar> q = math::QuaternionExp(w);
          return Vector<Scalar, 4>(q.w(), q.x(), q.y(), q.z());
        });
    ASSERT_EIGEN_NEAR(J_numerical, q_and_deriv.q_D_w, deriv_tol) << "w = " << w.transpose();
  }

  void TestMap() const {
    const auto angle_range = Range(-M_PI, M_PI, 0.2);
    for (const double x : angle_range) {
      for (const double y : angle_range) {
        for (const double z : angle_range) {
          TestOmega<double>({x, y, z}, tol::kPico, tol::kNano);
          TestOmega<float>({x, y, z}, tol::kMicro, tol::kMilli / 10);
        }
      }
    }
  }

  void TestMapNearZero() const {
    TestOmega<double>({1.0e-7, 0.5e-6, 3.5e-8}, tol::kNano, tol::kMicro);
    TestOmega<double>({0.0, 0.0, 0.0}, tol::kNano, tol::kMicro);
    TestOmega<float>({1.0e-7, 0.5e-6, 3.5e-8}, tol::kNano, tol::kMicro);
    TestOmega<float>({0.0, 0.0, 0.0}, tol::kNano, tol::kMicro);
  }
};

TEST_FIXTURE(TestQuaternionExp, TestMap);
TEST_FIXTURE(TestQuaternionExp, TestMapNearZero);

// Check that RotationLog does the inverse of QuaternionExp.
TEST(SO3Test, TestRotationLog) {
  // test quaternion
  const Vector<double, 3> v1{-0.7, 0.0, 0.4};
  const Quaternion<double> r1 = QuaternionExp(v1);
  EXPECT_EIGEN_NEAR(v1, RotationLog(r1), tol::kPico);
  // test matrix
  const Vector<float, 3> v2{0.01, -0.5, 0.03};
  const Quaternion<float> r2 = QuaternionExp(v2);
  EXPECT_EIGEN_NEAR(v2, RotationLog(r2.matrix()), tol::kMicro);
  // test identity
  const auto zero = Vector<double, 3>::Zero();
  EXPECT_EIGEN_NEAR(zero, RotationLog(Quaternion<double>::Identity()), tol::kPico);
  EXPECT_EIGEN_NEAR(zero.cast<float>(), RotationLog(Matrix<float, 3, 3>::Identity()), tol::kMicro);
}

// Test the SO(3) jacobian.
class TestSO3Jacobian : public ::testing::Test {
 public:
  template <typename Scalar>
  static void TestJacobian(const Vector<Scalar, 3>& w_a, const Scalar deriv_tol) {
    const Matrix<Scalar, 3, 3> J_analytical = math::SO3Jacobian(w_a, true);
    // This jacobian is only valid for small `w`, so evaluate about zero.
    const Matrix<Scalar, 3, 3> J_numerical =
        NumericalJacobian(Vector<Scalar, 3>::Zero(), [&](const Vector<Scalar, 3>& w) {
          return RotationLog(QuaternionExp(w_a) * QuaternionExp(w));
        });
    PRINT(J_analytical.inverse());
    PRINT(J_numerical);
    PRINT(deriv_tol);
  }

  void TestGeneral() {
    const auto angle_range = Range(-M_PI / 2, M_PI / 2, 0.2);
    for (const double x : angle_range) {
      for (const double y : angle_range) {
        for (const double z : angle_range) {
          TestJacobian<double>({x, y, z}, tol::kNano / 10);
        }
      }
    }
  }

  void TestNearZero() {}
};

TEST_FIXTURE(TestSO3Jacobian, TestGeneral);

// Test the derivative of the exponential map, matrix form.
class TestMatrixExpDerivative : public ::testing::Test {
 public:
  template <typename Scalar>
  static Vector<Scalar, 9> VecExpMatrix(const Vector<Scalar, 3>& w) {
    // Convert to vectorized format.
    const Matrix<Scalar, 3, 3> R = math::QuaternionExp(w).matrix();
    return Eigen::Map<const Vector<Scalar, 9>>(R.data());
  }

  template <typename Scalar>
  static void TestDerivative(const Vector<Scalar, 3>& w, const Scalar deriv_tol) {
    const Matrix<Scalar, 9, 3> D_w = math::SO3ExpMatrixDerivative(w);
    const Matrix<Scalar, 9, 3> J_numerical =
        NumericalJacobian(w, &TestMatrixExpDerivative::VecExpMatrix<Scalar>);
    ASSERT_EIGEN_NEAR(J_numerical, D_w, deriv_tol);
  }

  void TestGeneral() {
    const auto angle_range = Range(-M_PI, M_PI, 0.2);
    for (const double x : angle_range) {
      for (const double y : angle_range) {
        for (const double z : angle_range) {
          TestDerivative<double>({x, y, z}, tol::kNano / 10);
          TestDerivative<float>({x, y, z}, tol::kMilli / 10);
        }
      }
    }
  }

  void TestNearZero() {
    TestDerivative<double>({-1.0e-7, 1.0e-8, 0.5e-6}, tol::kMicro);
    TestDerivative<float>({-1.0e-7, 1.0e-8, 0.5e-6}, tol::kMicro);

    // at exactly zero it should be identically equal to the generators of SO(3)
    const Matrix<double, 9, 3> J_at_zero =
        math::SO3ExpMatrixDerivative(Vector<double, 3>::Zero().eval());
    const auto i_hat = Vector<double, 3>::UnitX();
    const auto j_hat = Vector<double, 3>::UnitY();
    const auto k_hat = Vector<double, 3>::UnitZ();
    EXPECT_EIGEN_NEAR(Skew3(-i_hat), J_at_zero.block(0, 0, 3, 3), tol::kPico);
    EXPECT_EIGEN_NEAR(Skew3(-j_hat), J_at_zero.block(3, 0, 3, 3), tol::kPico);
    EXPECT_EIGEN_NEAR(Skew3(-k_hat), J_at_zero.block(6, 0, 3, 3), tol::kPico);
  }
};

TEST_FIXTURE(TestMatrixExpDerivative, TestGeneral);
TEST_FIXTURE(TestMatrixExpDerivative, TestNearZero);

TEST(SO3Test, RetractDerivative) {
  // create the matrix R we multiply against
  const Vector<double, 3> R_log{0.6, -0.1, 0.4};
  const Quaternion<double> R = math::QuaternionExp(R_log);

  // functor that holds R fixed and multiplies on w
  const auto fix_r_functor = [&](const Vector<double, 3>& w) -> Vector<double, 3> {
    return math::RotationLog(R * math::QuaternionExp(w));
  };

  // try a bunch of values for omega
  // clang-format off
  const std::vector<Vector<double, 3>> samples = {
    {0.6, -0.1, 0.4},
    {0.8, 0.0, 0.2},
    {-1.2, 0.6, 1.5},
    {0.0, 0.0, 0.1},
    {1.5, 1.7, -1.2},
    {-0.3, 0.3, 0.3}
  };
  // clang-format on
  for (const auto& w : samples) {
    const Matrix<double, 3, 3> J_analytical = math::SO3RetractDerivative(R, w);
    const Matrix<double, 3, 3> J_numerical = NumericalJacobian(w, fix_r_functor);
    ASSERT_EIGEN_NEAR(J_numerical, J_analytical, tol::kNano) << "w = " << w.transpose();
  }
}

TEST(SO3Test, RetractDerivativeNearZero) {
  // test small angle cases
  // clang-format off
  const std::vector<Vector<double, 3>> samples = {
    {0.0, 0.0, 0.0},
    {-1.0e-5, 1.0e-5, 0.3e-5},
    {0.1e-5, 0.0, -0.1e-5},
    {-0.2e-8, 0.3e-7, 0.0},
  };
  // clang-format on

  // for small hangle to hold, R should be identity
  const Quaternion<double> R = Quaternion<double>::Identity();

  // functor that holds R fixed and multiplies on w
  const auto fix_r_functor = [&](const Vector<double, 3>& w) -> Vector<double, 3> {
    return math::RotationLog(R * math::QuaternionExp(w));
  };

  for (const auto& w : samples) {
    const Matrix<double, 3, 3> J_analytical = math::SO3RetractDerivative(R, w);
    const Matrix<double, 3, 3> J_numerical = NumericalJacobian(w, fix_r_functor);
    ASSERT_EIGEN_NEAR(J_numerical, J_analytical, tol::kNano);
  }
}

}  // namespace math