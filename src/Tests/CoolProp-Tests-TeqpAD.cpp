// Optional teqp automatic-differentiation validation.
//
// This translation unit is built only with
// COOLPROP_ENABLE_TEQP_AD_VALIDATION=ON.  It is deliberately a separate C++20
// object target: CoolProp's production sources and public interface remain
// C++17, and the analytic Helmholtz implementation remains the value returned
// by the library.

#if defined(ENABLE_CATCH)

#    include <catch2/catch_all.hpp>

#    include "CoolProp/fluids/Helmholtz.h"

#    include <teqp/derivs.hpp>

#    include <algorithm>
#    include <array>
#    include <cmath>
#    include <cstddef>
#    include <vector>

namespace {

using CoolProp::HelmholtzDerivatives;
using CoolProp::ResidualHelmholtzGeneralizedExponential;

// teqp differentiates with respect to reciprocal temperature and density.  By
// choosing tau=1/T and delta=rho, get_Agenxy<i,j>() returns the CoolProp raw
// derivative multiplied by tau^i*delta^j.
struct GeneralizedExponentialTeqpModel
{
    const ResidualHelmholtzGeneralizedExponential& term;

    template <typename TType, typename RhoType, typename MoleFractionType>
    auto alphar(const TType& T, const RhoType& rho, const MoleFractionType&) const {
        return term.base_templated(teqp::forceeval(1.0 / T), rho);
    }
};

ResidualHelmholtzGeneralizedExponential make_representative_term() {
    ResidualHelmholtzGeneralizedExponential term;

    // Polynomial and density-damped terms.
    term.add_Power({0.42, -0.18, 0.025}, {1.0, 2.0, 5.0}, {0.25, 1.5, 0.875}, {0.0, 1.0, 2.0});
    term.add_Exponential({-0.21, 0.031}, {1.0, 3.0}, {2.25, 0.5}, {1.2, 0.35}, {1.0, 2.0});

    // Gaussian and GERG-style linear/quadratic density damping.
    term.add_Gaussian({0.18, -0.04}, {1.0, 2.0}, {1.1, 3.2}, {1.3, 0.75}, {0.8, 1.1}, {0.9, 1.4}, {1.0, 0.7});
    term.add_GERG2008Gaussian({0.02}, {2.0}, {1.2}, {0.6}, {0.9}, {0.3}, {1.1});

    // Simultaneous density and reciprocal-temperature exponential damping,
    // including non-integer powers supported by the generalized form.
    term.add_Lemmon2005({-0.03, 0.008}, {2.0, 4.0}, {4.2, 0.7}, {1.5, 2.0}, {2.3, 1.0});
    term.add_DoubleExponential({0.014, -0.006}, {1.5, 3.0}, {0.9, 2.2}, {0.8, 1.1}, {1.25, 2.5}, {0.45, 0.7},
                               {1.75, 0.5});
    term.finish();
    return term;
}

template <int ITau, int IDelta>
double teqp_scaled_derivative(const GeneralizedExponentialTeqpModel& model, const double tau, const double delta,
                              const Eigen::ArrayXd& mole_fractions) {
    using TDX = teqp::TDXDerivatives<GeneralizedExponentialTeqpModel, double, Eigen::ArrayXd>;
    return static_cast<double>(TDX::template get_Agenxy<ITau, IDelta>(model, 1.0 / tau, delta, mole_fractions));
}

constexpr std::array<double, 5> relative_tolerance = {5e-13, 1e-12, 2e-11, 2e-10, 2e-9};
constexpr std::array<double, 5> absolute_tolerance = {5e-14, 1e-13, 2e-12, 2e-11, 2e-10};
constexpr double near_zero_raw_relative_tolerance = 1e-8;

void check_close(const double actual, const double expected, const std::size_t order, const double relative_tolerance_floor = 0.0) {
    REQUIRE(std::isfinite(actual));
    REQUIRE(std::isfinite(expected));
    const double scale = (std::max)({std::abs(actual), std::abs(expected)});
    const double relative_limit = (std::max)(relative_tolerance[order], relative_tolerance_floor);
    const double limit = absolute_tolerance[order] + relative_limit * scale;
    CHECK(std::abs(actual - expected) <= limit);
}

enum class ValidationKind
{
    parity,
    finiteness_only
};

template <int ITau, int IDelta>
void check_derivative(const GeneralizedExponentialTeqpModel& model, HelmholtzDerivatives& analytic, const double tau, const double delta,
                      const Eigen::ArrayXd& mole_fractions, const ValidationKind validation_kind,
                      const double raw_relative_tolerance_floor) {
    constexpr std::size_t order = ITau + IDelta;
    const double factor = std::pow(tau, ITau) * std::pow(delta, IDelta);
    const double analytic_raw = analytic.get(ITau, IDelta);
    const double analytic_scaled = analytic_raw * factor;
    const double automatic_scaled = teqp_scaled_derivative<ITau, IDelta>(model, tau, delta, mole_fractions);
    const double automatic_raw = automatic_scaled / factor;

    INFO("tau derivative order = " << ITau);
    INFO("delta derivative order = " << IDelta);
    INFO("analytic raw derivative = " << analytic_raw);
    INFO("teqp raw derivative = " << automatic_raw);
    INFO("analytic scaled derivative = " << analytic_scaled);
    INFO("teqp scaled derivative = " << automatic_scaled);

    if (validation_kind == ValidationKind::finiteness_only) {
        REQUIRE(factor > 0.0);
        REQUIRE(std::isfinite(analytic_raw));
        REQUIRE(std::isfinite(automatic_raw));
        REQUIRE(std::isfinite(analytic_scaled));
        REQUIRE(std::isfinite(automatic_scaled));
        return;
    }

    check_close(automatic_scaled, analytic_scaled, order);
    check_close(automatic_raw, analytic_raw, order, raw_relative_tolerance_floor);
}

void check_all_derivatives(ResidualHelmholtzGeneralizedExponential& term, const double tau, const double delta,
                           const ValidationKind validation_kind, const double raw_relative_tolerance_floor = 0.0) {
    HelmholtzDerivatives analytic;
    term.all(tau, delta, analytic);

    const GeneralizedExponentialTeqpModel model{term};
    Eigen::ArrayXd mole_fractions(1);
    mole_fractions << 1.0;

    check_derivative<0, 0>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<1, 0>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<0, 1>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<2, 0>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<1, 1>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<0, 2>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<3, 0>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<2, 1>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<1, 2>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<0, 3>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<4, 0>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<3, 1>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<2, 2>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<1, 3>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
    check_derivative<0, 4>(model, analytic, tau, delta, mole_fractions, validation_kind, raw_relative_tolerance_floor);
}

struct ValidationPoint
{
    double tau;
    double delta;
    double raw_relative_tolerance_floor;
};

}  // namespace

TEST_CASE("teqp AD matches generalized-exponential Helmholtz derivatives through fourth order", "[teqp][autodiff][validation]") {
    auto term = make_representative_term();

    // At delta=1e-8, converting teqp's scaled result to the raw CoolProp
    // convention amplifies floating-point roundoff by delta^-j. A 1e-8
    // relative floor still provides meaningful raw-derivative parity.
    const std::array<ValidationPoint, 12> points = {{{0.55, 0.05, 0.0},
                                                     {0.8, 0.5, 0.0},
                                                     {1.0, 0.9, 0.0},
                                                     {1.3, 0.9, 0.0},
                                                     {2.0, 2.0, 0.0},
                                                     {3.0, 0.2, 0.0},
                                                     {0.7, 1e-8, near_zero_raw_relative_tolerance},
                                                     {1.0, 1e-8, near_zero_raw_relative_tolerance},
                                                     {1.5, 1e-8, near_zero_raw_relative_tolerance},
                                                     {3.0, 1e-8, near_zero_raw_relative_tolerance},
                                                     {1.0, 1e-4, 0.0},
                                                     {1.5, 1e-4, 0.0}}};

    for (const auto& point : points) {
        CAPTURE(point.tau, point.delta);
        check_all_derivatives(term, point.tau, point.delta, ValidationKind::parity, point.raw_relative_tolerance_floor);
    }
}

TEST_CASE("teqp AD remains finite at extremely low positive density", "[teqp][autodiff][validation][near-zero]") {
    auto term = make_representative_term();
    constexpr std::array<double, 4> tau_values = {0.7, 1.0, 1.5, 3.0};

    // Scaling by delta^j makes numerical recovery of raw high-order density
    // derivatives ill-conditioned here. This is intentionally a finiteness
    // smoke test, not a parity claim.
    for (const double tau : tau_values) {
        CAPTURE(tau);
        check_all_derivatives(term, tau, 1e-12, ValidationKind::finiteness_only);
    }
}

#endif
