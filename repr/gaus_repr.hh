#include "eigen_util.hh"
#include <cmath>
#include <random>

#ifndef GAUS_REPR_HH_
#define GAUS_REPR_HH_

template<typename Matrix, typename ReprType>
struct gaus_repr_t;

struct dense_repr_type {};
struct sparse_repr_type {};

////////////////////////////////////////////////////////////////
template<typename Scalar>
using DenseMat = Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>;

template<typename Scalar>
using DenseReprMat = gaus_repr_t<DenseMat<Scalar>, dense_repr_type>;

template<typename Scalar>
using SparseMat = Eigen::SparseMatrix<Scalar, Eigen::ColMajor>;

template<typename Scalar>
using SparseReprMat = gaus_repr_t<SparseMat<Scalar>, sparse_repr_type>;

////////////////////////////////////////////////////////////////
// Gaussian representation class to accumulate stochastic gradients
//
// Stochastic gradients
//
// G1 = sum F[s] Eps[s] / Sd[s]
// G2 = 1/2 * sum F[s] (Eps[s] * Eps[s] - 1) / Var[s]
//

template<typename Derived>
auto make_gaus_repr(const Eigen::MatrixBase<Derived> &y);

template<typename Derived>
auto make_gaus_repr(const Eigen::SparseMatrixBase<Derived> &y);

////////////////////////////////////////////////////////////////
template<typename Matrix, typename RT>
void clear_repr(gaus_repr_t<Matrix, RT> &);

// accumulating stochastic evidence of F(eps[s])
template<typename Matrix, typename RT, typename T>
void update_gradient(gaus_repr_t<Matrix, RT> &repr, const T &F);

template<typename Matrix, typename RT, typename T>
void update_mean(gaus_repr_t<Matrix, RT> &repr, const T &M);

template<typename Matrix, typename RT, typename T>
void update_var(gaus_repr_t<Matrix, RT> &repr, const T &V);

template<typename Matrix, typename RT, typename Rnorm>
const Matrix &sample_repr(gaus_repr_t<Matrix, RT> &repr, const Rnorm &rnorm);

template<typename Matrix, typename Derived, typename OtherDerived, typename Rnorm>
const Matrix &sample_repr(gaus_repr_t<Matrix, dense_repr_type> &repr,
                          const Eigen::MatrixBase<Derived> &left,
                          const Eigen::MatrixBase<OtherDerived> &right,
                          const Rnorm &rnorm);

template<typename Matrix, typename Derived, typename OtherDerived, typename Rnorm>
const Matrix &sample_repr(gaus_repr_t<Matrix, sparse_repr_type> &repr,
                          const Eigen::SparseMatrixBase<Derived> &left,
                          const Eigen::SparseMatrixBase<OtherDerived> &right,
                          const Rnorm &rnorm);

template<typename Matrix, typename RT>
const Matrix &get_sampled_repr(gaus_repr_t<Matrix, RT> &repr);

////////////////////////////////////////////////////////////////
struct dummy_rotation_t {};

template<typename Matrix>
const Matrix &
operator*(const Matrix &lhs, const dummy_rotation_t &) {
  return lhs;
}

template<typename Matrix>
const Matrix &
operator*(const dummy_rotation_t &, const Matrix &rhs) {
  return rhs;
}

////////////////////////////////////////////////////////////////
template<typename Matrix, typename ReprType>
struct gaus_repr_t {
  using DataMatrix = Matrix;
  using Scalar = typename Matrix::Scalar;
  using Index = typename Matrix::Index;
  using Tag = ReprType;

  explicit gaus_repr_t(const Index _n, const Index _m) : n(_n), m(_m) {
    summarized = false;
    n_add_sgd = 0;
  }

  const Matrix &get_grad_type1() {
    if (!summarized)
      summarize();
    return G1;
  }

  const Matrix &get_grad_type2() {
    if (!summarized)
      summarize();
    return G2;
  }

  const Matrix &get_mean() const { return Mean; }
  const Matrix &get_var() const { return Var; }

  void summarize() {
    if (n_add_sgd > 0) {
      G1 = (FepsSdCum - epsSdCum.cwiseProduct(Fcum / n_add_sgd)) / n_add_sgd;
      G2 = 0.5 * (Feps1VarCum - eps1VarCum.cwiseProduct(Fcum / n_add_sgd)) / n_add_sgd;
    }
    summarized = true;
    n_add_sgd = 0;
  }

  const Index rows() const { return n; }
  const Index cols() const { return m; }

  const Index n;
  const Index m;

  Matrix G1;    // stoch gradient wrt mean
  Matrix G2;    // stoch gradient wrt var
  Matrix Eta;   // random eta = Mean + Eps * Sd
  Matrix Eps;   // Eps ~ N(0,1)
  Matrix Mean;  // current mean
  Matrix Var;   // current var

  Matrix Fcum;         // accumulation of F
  Matrix FepsSdCum;    // F * eps / Sd
  Matrix Feps1VarCum;  // F * (eps^2 - 1) / Var
  Matrix epsSdCum;     // eps / Sd
  Matrix eps1VarCum;   // (eps^2 - 1) / Var

  Matrix epsSd;    // eps / Sd
  Matrix eps1Var;  // (eps^2 - 1) / Var

  bool summarized;
  Scalar n_add_sgd;

  // helper functors
  // eps / sqrt(var + 1e-8)
  struct eps_sd_op_t {
    const Scalar operator()(const Scalar &eps, const Scalar &var) const { return eps / std::sqrt(var_min + var); }
    static constexpr Scalar var_min = 1e-8;
  } EpsSd_op;
  // (eps*eps - 1) / (var + 1e-8)
  struct eps_1var_op_t {
    const Scalar operator()(const Scalar &eps, const Scalar &var) const {
      return (eps * eps - one_val) / (var_min + var);
    }
    static constexpr Scalar var_min = 1e-8;
    static constexpr Scalar one_val = 1.0;
  } Eps1Var_op;

  // dummy stuffs
  dummy_rotation_t dummy;
};

template<typename Derived>
auto make_gaus_repr(const Eigen::MatrixBase<Derived> &y) {
  DenseReprMat<typename Derived::Scalar> ret(y.rows(), y.cols());
  clear_repr(ret);
  return ret;
}

template<typename Derived>
auto make_gaus_repr(const Eigen::SparseMatrixBase<Derived> &y) {
  SparseReprMat<typename Derived::Scalar> ret(y.rows(), y.cols());

  initialize(y, ret.G1);
  initialize(y, ret.G2);
  initialize(y, ret.Eta);
  initialize(y, ret.Eps);
  initialize(y, ret.Mean);
  initialize(y, ret.Var);
  initialize(y, ret.Fcum);
  initialize(y, ret.FepsSdCum);
  initialize(y, ret.Feps1VarCum);
  initialize(y, ret.epsSdCum);
  initialize(y, ret.eps1VarCum);
  initialize(y, ret.epsSd);
  initialize(y, ret.eps1Var);

  clear_repr(ret);

  return ret;
}

template<typename Matrix>
void clear_repr(gaus_repr_t<Matrix, dense_repr_type> &repr) {
  const auto n = repr.n;
  const auto m = repr.m;

  repr.G1.setZero(n, m);
  repr.G2.setZero(n, m);
  repr.Eta.setZero(n, m);
  repr.Eps.setZero(n, m);
  repr.Mean.setZero(n, m);
  repr.Var.setZero(n, m);
  repr.Fcum.setZero(n, m);
  repr.FepsSdCum.setZero(n, m);
  repr.Feps1VarCum.setZero(n, m);
  repr.epsSdCum.setZero(n, m);
  repr.eps1VarCum.setZero(n, m);
  repr.epsSd.setZero(n, m);
  repr.eps1Var.setZero(n, m);
}

template<typename Matrix>
void clear_repr(gaus_repr_t<Matrix, sparse_repr_type> &repr) {
  setConstant(repr.G1, 0.0);
  setConstant(repr.G2, 0.0);
  setConstant(repr.Eta, 0.0);
  setConstant(repr.Eps, 0.0);
  setConstant(repr.Mean, 0.0);
  setConstant(repr.Var, 0.0);
  setConstant(repr.Fcum, 0.0);
  setConstant(repr.FepsSdCum, 0.0);
  setConstant(repr.Feps1VarCum, 0.0);
  setConstant(repr.epsSdCum, 0.0);
  setConstant(repr.eps1VarCum, 0.0);
  setConstant(repr.epsSd, 0.0);
  setConstant(repr.eps1Var, 0.0);
}

// Eps could be generated with prescribed covariance matrix
// 1. Eps[i,j] ~ N(0,1)
// 2. Eps = L * Eps * R
// 3. eta = M + Eps .* sqrt(V)
template<typename Repr, typename Left, typename Right, typename Rnorm>
const typename Repr::DataMatrix &_impl_sample_repr(Repr &repr,
                                                   const Left &left,
                                                   const Right &right,
                                                   const Rnorm &rnorm) {
  // 1. sample from Gaussian
  repr.Eps = repr.Eps.unaryExpr([&rnorm](const auto &x) { return rnorm(); });

  // 2. rotate by left and right matrix
  repr.Eps = left * repr.Eps * right;

  // 3. add mean and scale by variance
  repr.Eta = repr.Mean + repr.Eps.cwiseProduct(repr.Var.cwiseSqrt());

  return repr.Eta;
}

template<typename Matrix, typename Type, typename Rnorm>
const Matrix &sample_repr(gaus_repr_t<Matrix, Type> &repr, const Rnorm &rnorm) {
  return _impl_sample_repr(repr, repr.dummy, repr.dummy, rnorm);
}

template<typename Matrix, typename Derived, typename OtherDerived, typename Rnorm>
const Matrix &sample_repr(gaus_repr_t<Matrix, dense_repr_type> &repr,
                          const Eigen::MatrixBase<Derived> &left,
                          const Eigen::MatrixBase<OtherDerived> &right,
                          const Rnorm &rnorm) {
  return _impl_sample_repr(repr, left, right, rnorm);
}

template<typename Matrix, typename Type, typename Rnorm>
const Matrix &sample_repr(gaus_repr_t<Matrix, Type> &repr,
                          const dummy_rotation_t &,
                          const dummy_rotation_t &,
                          const Rnorm &rnorm) {

  repr.Eps = repr.Eps.unaryExpr([&rnorm](const auto &x) { return rnorm(); });
  // 2. add mean and scale by variance
  repr.Eta = repr.Mean + repr.Eps.cwiseProduct(repr.Var.cwiseSqrt());

  return repr.Eta;
}

template<typename Matrix, typename Derived, typename Rnorm>
const Matrix &sample_repr(gaus_repr_t<Matrix, dense_repr_type> &repr,
                          const dummy_rotation_t &,
                          const Eigen::MatrixBase<Derived> &right,
                          const Rnorm &rnorm) {
  return _impl_sample_repr(repr, repr.dummy, right, rnorm);
}

template<typename Matrix, typename Derived, typename Rnorm>
const Matrix &sample_repr(gaus_repr_t<Matrix, dense_repr_type> &repr,
                          const Eigen::MatrixBase<Derived> &left,
                          const dummy_rotation_t &,
                          const Rnorm &rnorm) {
  return _impl_sample_repr(repr, left, repr.dummy, rnorm);
}

template<typename Matrix, typename Derived, typename OtherDerived, typename Rnorm>
const Matrix &sample_repr(gaus_repr_t<Matrix, sparse_repr_type> &repr,
                          const Eigen::SparseMatrixBase<Derived> &left,
                          const Eigen::SparseMatrixBase<OtherDerived> &right,
                          const Rnorm &rnorm) {
  return _impl_sample_repr(repr, left, right, rnorm);
}

template<typename Matrix, typename Derived, typename Rnorm>
const Matrix &sample_repr(gaus_repr_t<Matrix, dense_repr_type> &repr,
                          const dummy_rotation_t &,
                          const Eigen::SparseMatrixBase<Derived> &right,
                          const Rnorm &rnorm) {
  return _impl_sample_repr(repr, repr.dummy, right, rnorm);
}

template<typename Matrix, typename Derived, typename Rnorm>
const Matrix &sample_repr(gaus_repr_t<Matrix, dense_repr_type> &repr,
                          const Eigen::SparseMatrixBase<Derived> &left,
                          const dummy_rotation_t &,
                          const Rnorm &rnorm) {
  return _impl_sample_repr(repr, left, repr.dummy, rnorm);
}

template<typename Matrix, typename RT>
const Matrix &get_sampled_repr(gaus_repr_t<Matrix, RT> &repr) {
  return repr.Eta;
}

////////////////////////////////////////////////////////////////
// Accumulate stats corrected by control variates
//
// G1 = mean_s Eps[s] / Sd[s] * (F[s] - EF)
//    = mean_s(Eps[s] / Sd[s] * F[s])
//      - mean_s(F[s]) * mean_s(Eps[s] / Sd[s])
//
// G2 = 0.5 * mean_s (Eps[s] * Eps[s] - 1) / Var[s] * (F[s] - EF)
//    = 0.5 * mean_s (Eps[s] * Eps[s] - 1) / Var[s] * F[s]
//      - 0.5 * mean_s(F[s]) * mean_s (Eps[s] * Eps[s] - 1) / Var[s]
//
template<typename Matrix, typename RT, typename T>
void update_gradient(gaus_repr_t<Matrix, RT> &repr, const T &F) {
  // pre-compute : (i) eps / sd (ii) (eps^2 - 1) / var
  // using Scalar = typename Matrix::Scalar;
  // const Scalar var_min = 1e-8;
  // auto EpsSd_op = [var_min](const auto& eps, const auto& var) { return eps / std::sqrt(var_min + var); };
  // auto Eps1Var_op = [var_min](const auto& eps, const auto& var) { return (eps * eps - 1.0) / (var_min + var); };

  repr.epsSd = repr.Eps.binaryExpr(repr.Var, repr.EpsSd_op);
  repr.eps1Var = repr.Eps.binaryExpr(repr.Var, repr.Eps1Var_op);

  if (repr.n_add_sgd == 0) {
    repr.Fcum = F;
    repr.epsSdCum = repr.epsSd;
    repr.eps1VarCum = repr.eps1Var;
    repr.FepsSdCum = F.cwiseProduct(repr.epsSd);
    repr.Feps1VarCum = F.cwiseProduct(repr.eps1Var);
  } else {
    repr.Fcum += F;
    repr.epsSdCum += repr.epsSd;
    repr.eps1VarCum += repr.eps1Var;
    repr.FepsSdCum += F.cwiseProduct(repr.epsSd);
    repr.Feps1VarCum += F.cwiseProduct(repr.eps1Var);
  }
  repr.n_add_sgd++;
  repr.summarized = false;
}

template<typename Matrix, typename RT, typename T>
void update_mean(gaus_repr_t<Matrix, RT> &repr, const T &M) {
  copy_matrix(M, repr.Mean);
  repr.summarized = false;
}

template<typename Matrix, typename RT, typename T>
void update_var(gaus_repr_t<Matrix, RT> &repr, const T &V) {
  copy_matrix(V, repr.Var);
  repr.summarized = false;
}

#endif
