#include "compute_and_apply_rhs.hpp"

#include "Region.hpp"
#include "TestData.hpp"
#include "Types.hpp"
#include "sphere_operators.hpp"

#include <fstream>
#include <iomanip>

#include <assert.h>

namespace TinMan {

struct update_state {
  const Control m_data;
  const Region m_region;

  struct KernelVariables {
    KOKKOS_INLINE_FUNCTION
    KernelVariables(const TeamPolicy &team,
                    ExecViewUnmanaged<const Real[2][2][NP][NP]> dinv,
                    ExecViewUnmanaged<Real[NP][NP]> scalar_buf,
                    ExecViewUnmanaged<Real[2][NP][NP]> vector_buf_1)
        : team(team), ie(team.league_rank()), c_dinv(dinv),
          c_scalar_buf(scalar_buf), c_vector_buf(vector_buf_1) {}
    const TeamPolicy &team;
    const int ie;
    ExecViewUnmanaged<const Real[2][2][NP][NP]> c_dinv;
    ExecViewUnmanaged<Real[NP][NP]> c_scalar_buf;
    ExecViewUnmanaged<Real[2][NP][NP]> c_vector_buf;
  };

  KOKKOS_INLINE_FUNCTION
  update_state(const Control &data, const Region &region)
      : m_data(data), m_region(region) {}

  // Depends on PHI (after preq_hydrostatic), PECND
  // Modifies Ephi_grad
  KOKKOS_INLINE_FUNCTION void
  compute_energy_grad(const KernelVariables &k_locals, const int ilev) const {
    ExecViewUnmanaged<Real[NP][NP]> Ephi = k_locals.c_scalar_buf;
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, NP * NP),
                         KOKKOS_LAMBDA(const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;
      // Kinetic energy + PHI (thermal energy?) + PECND (potential energy?)
      Ephi(igp, jgp) =
          0.5 * (m_region.U_current(k_locals.ie, ilev)(igp, jgp) *
                     m_region.U_current(k_locals.ie, ilev)(igp, jgp) +
                 m_region.V_current(k_locals.ie, ilev)(igp, jgp) *
                     m_region.V_current(k_locals.ie, ilev)(igp, jgp)) +
          m_region.PHI_update(k_locals.ie)(ilev, igp, jgp) +
          m_region.PECND(k_locals.ie, ilev)(igp, jgp);
    });
    gradient_sphere_update(k_locals.team, Ephi, m_data, k_locals.c_dinv,
                           m_data.vector_buf(k_locals.ie, 0, ilev),
                           k_locals.c_vector_buf);
  }

  // Depends on pressure, PHI, U_current, V_current, METDET,
  // D, DINV, U, V, FCOR, SPHEREMP, T_v, ETA_DPDN
  KOKKOS_INLINE_FUNCTION void
  compute_velocity_eta_dpdn(const KernelVariables &k_locals) const {
    Kokkos::parallel_for(Kokkos::TeamThreadRange(k_locals.team, NUM_LEV),
                         KOKKOS_LAMBDA(const int ilev) {
      compute_eta_dpdn(k_locals, ilev);
      compute_velocity(k_locals, ilev);
      compute_stuff(k_locals, ilev);
    });
  }

  // Depends on pressure, PHI, U_current, V_current, METDET,
  // D, DINV, U, V, FCOR, SPHEREMP, T_v
  KOKKOS_INLINE_FUNCTION
  void compute_velocity(const KernelVariables &k_locals, int ilev) const {
    const ExecViewUnmanaged<const Real[NUM_LEV][NP][NP]> T_v =
        m_data.T_v(k_locals.team);

    ExecViewUnmanaged<const Real[NP][NP]> p_ilev =
        m_data.pressure(k_locals.ie, ilev);

    gradient_sphere(k_locals.team, p_ilev, m_data, k_locals.c_dinv,
                    m_data.vector_buf(k_locals.ie, 0, ilev),
                    k_locals.c_vector_buf);
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, 2 * NP * NP),
                         KOKKOS_LAMBDA(const int idx) {
      const int hgp = (idx / NP) / NP;
      const int igp = (idx / NP) % NP;
      const int jgp = idx % NP;

      k_locals.c_vector_buf(hgp, igp, jgp) *= PhysicalConstants::Rgas;
      k_locals.c_vector_buf(hgp, igp, jgp) *= T_v(ilev, igp, jgp);
      k_locals.c_vector_buf(hgp, igp, jgp) /= p_ilev(igp, jgp);
    });

    // k_locals.c_vector_buf -> Ephi_grad + glnpsi
    compute_energy_grad(k_locals, ilev);

    ExecViewUnmanaged<Real[NP][NP]> vort = m_data.scalar_buf(k_locals.ie, ilev);
    vorticity_sphere(k_locals.team, m_region.U_current(k_locals.ie, ilev),
                     m_region.V_current(k_locals.ie, ilev), m_data,
                     m_region.METDET(k_locals.ie), m_region.D(k_locals.ie),
                     m_data.vector_buf(k_locals.ie, 0, ilev), vort);

    Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, NP * NP),
                         KOKKOS_LAMBDA(const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      vort(igp, jgp) += m_region.FCOR(k_locals.ie)(igp, jgp);

      k_locals.c_vector_buf(0, igp, jgp) *= -1;
      k_locals.c_vector_buf(0, igp, jgp) += // v_vadv(igp, jgp) +
          m_region.V_current(k_locals.ie, ilev)(igp, jgp) * vort(igp, jgp);
      k_locals.c_vector_buf(0, igp, jgp) *= m_data.dt2();
      k_locals.c_vector_buf(0, igp, jgp) +=
          m_region.U_previous(k_locals.ie)(ilev, igp, jgp);
      m_region.U_future(k_locals.ie)(ilev, igp, jgp) =
          m_region.SPHEREMP(k_locals.ie)(igp, jgp) *
          k_locals.c_vector_buf(0, igp, jgp);

      k_locals.c_vector_buf(1, igp, jgp) *= -1;
      k_locals.c_vector_buf(1, igp, jgp) += // v_vadv(igp, jgp) +
          -m_region.U_current(k_locals.ie, ilev)(igp, jgp) * vort(igp, jgp);
      k_locals.c_vector_buf(1, igp, jgp) *= m_data.dt2();
      k_locals.c_vector_buf(1, igp, jgp) +=
          m_region.V_previous(k_locals.ie)(ilev, igp, jgp);
      m_region.V_future(k_locals.ie)(ilev, igp, jgp) =
          m_region.SPHEREMP(k_locals.ie)(igp, jgp) *
          k_locals.c_vector_buf(1, igp, jgp);
    });
  }

  // Depends on ETA_DPDN
  KOKKOS_INLINE_FUNCTION
  void compute_eta_dpdn(const KernelVariables &k_locals, int ilev) const {
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, NP * NP),
                         KOKKOS_LAMBDA(const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      // TODO: Compute the actual value for this
      // Real eta_dot_dpdn_ie = 0.0;

      m_region.ETA_DPDN_update(k_locals.ie)(ilev, igp, jgp) =
          m_region.ETA_DPDN(k_locals.ie)(ilev, igp, jgp);
      // + PhysicalConstants::eta_ave_w * eta_dot_dpdn_ie;
    });
  }

  // Depends on PHIS, DP3D, PHI, pressure, T_v
  // Modifies PHI
  KOKKOS_INLINE_FUNCTION
  void preq_hydrostatic(const KernelVariables &k_locals) const {
    const ExecViewUnmanaged<const Real[NUM_LEV][NP][NP]> pressure =
        m_data.pressure(k_locals.team);
    const ExecViewUnmanaged<const Real[NUM_LEV][NP][NP]> T_v =
        m_data.T_v(k_locals.team);

    ExecViewUnmanaged<const Real[NP][NP]> phis = m_region.PHIS(k_locals.ie);

    ExecViewUnmanaged<const Real[NUM_LEV][NP][NP]> dp =
        m_region.DP3D_current(k_locals.ie);

    ExecViewUnmanaged<const Real[NUM_LEV][NP][NP]> phi =
        m_region.PHI(k_locals.ie);
    ExecViewUnmanaged<Real[NUM_LEV][NP][NP]> phi_update =
        m_region.PHI_update(k_locals.ie);

    for (int igp = 0; igp < NP; ++igp) {
      for (int jgp = 0; jgp < NP; ++jgp) {

        // Real phii = k_locals.c_scalar_buf(0, 0);
        {
          // const Real hk = k_locals.c_scalar_buf(0, 1);
          k_locals.c_scalar_buf(0, 1) =
              dp(NUM_LEV - 1, igp, jgp) / pressure(NUM_LEV - 1, igp, jgp);
          k_locals.c_scalar_buf(0, 0) = PhysicalConstants::Rgas *
                                        T_v(NUM_LEV - 1, igp, jgp) *
                                        k_locals.c_scalar_buf(0, 1);
          phi_update(NUM_LEV - 1, igp, jgp) =
              phis(igp, jgp) + k_locals.c_scalar_buf(0, 0) * 0.5;
        }

        for (int ilev = NUM_LEV - 2; ilev > 0; --ilev) {
          // const Real hk = k_locals.c_scalar_buf(0, 1);
          k_locals.c_scalar_buf(0, 1) =
              dp(ilev, igp, jgp) / pressure(ilev, igp, jgp);
          // const Real lev_term = k_locals.c_scalar_buf(0, 2);
          k_locals.c_scalar_buf(0, 2) = PhysicalConstants::Rgas *
                                        T_v(ilev, igp, jgp) *
                                        k_locals.c_scalar_buf(0, 1);

          phi_update(ilev, igp, jgp) = phis(igp, jgp) +
                                       k_locals.c_scalar_buf(0, 0) +
                                       k_locals.c_scalar_buf(0, 2) * 0.5;

          k_locals.c_scalar_buf(0, 0) += k_locals.c_scalar_buf(0, 2);
        }

        {
          // const Real hk = k_locals.c_scalar_buf(0, 1);
          k_locals.c_scalar_buf(0, 1) =
              0.5 * dp(0, igp, jgp) / pressure(0, igp, jgp);
          phi_update(0, igp, jgp) = phis(igp, jgp) +
                                    k_locals.c_scalar_buf(0, 0) +
                                    PhysicalConstants::Rgas * T_v(0, igp, jgp) *
                                        k_locals.c_scalar_buf(0, 1);
        }
      }
    }
  }

  KOKKOS_INLINE_FUNCTION
  void preq_omega_ps_init(const KernelVariables &k_locals) const {
    ExecViewUnmanaged<const Real[NP][NP]> p_ilev =
        m_data.pressure(k_locals.ie, 0);
    ExecViewUnmanaged<Real[2][NP][NP]> grad_p =
        m_data.vector_buf(k_locals.ie, 0, 0);
    gradient_sphere(k_locals.team, p_ilev, m_data, k_locals.c_dinv,
                    k_locals.c_vector_buf, grad_p);

    Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, NP * NP),
                         KOKKOS_LAMBDA(const int loop_idx) {
      const int igp = loop_idx / NP;
      const int jgp = loop_idx % NP;

      m_data.omega_p(k_locals.ie, 0, igp, jgp) =
          (m_region.U_current(k_locals.ie, 0)(igp, jgp) * grad_p(0, igp, jgp) +
           m_region.V_current(k_locals.ie, 0)(igp, jgp) * grad_p(1, igp, jgp)) /
              p_ilev(igp, jgp) -
          0.5 / p_ilev(igp, jgp) * m_data.div_vdp(k_locals.ie, 0, igp, jgp);
      k_locals.c_scalar_buf(igp, jgp) =
          m_data.div_vdp(k_locals.ie, 0, igp, jgp);
    });
  }

  KOKKOS_INLINE_FUNCTION
  void preq_omega_ps_loop(const KernelVariables &k_locals) const {
    // Another candidate for parallel scan
    ExecViewUnmanaged<const Real[NP][NP]> p_ilev;
    ExecViewUnmanaged<Real[2][NP][NP]> grad_p =
        m_data.vector_buf(k_locals.ie, 0, 0);
    for (int ilev = 1; ilev < NUM_LEV - 1; ++ilev) {
      p_ilev = m_data.pressure(k_locals.ie, ilev);
      gradient_sphere(k_locals.team, p_ilev, m_data, k_locals.c_dinv,
                      k_locals.c_vector_buf, grad_p);

      Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, NP * NP),
                           KOKKOS_LAMBDA(const int loop_idx) {
        const int igp = loop_idx / NP;
        const int jgp = loop_idx % NP;
        const Real vgrad_p = m_region.U_current(k_locals.ie, ilev)(igp, jgp) *
                                 grad_p(0, igp, jgp) +
                             m_region.V_current(k_locals.ie, ilev)(igp, jgp) *
                                 grad_p(1, igp, jgp);

        const Real ckk = 0.5 / p_ilev(igp, jgp);
        const Real ckl = 2.0 * ckk;
        m_data.omega_p(k_locals.ie, ilev, igp, jgp) =
            vgrad_p / p_ilev(igp, jgp) -
            ckl * k_locals.c_vector_buf(0, igp, jgp) -
            ckk * m_data.div_vdp(k_locals.ie, ilev, igp, jgp);
        k_locals.c_vector_buf(0, igp, jgp) +=
            m_data.div_vdp(k_locals.ie, ilev, igp, jgp);
      });
    }
  }

  KOKKOS_INLINE_FUNCTION
  void preq_omega_ps_tail(const KernelVariables &k_locals) const {
    ExecViewUnmanaged<const Real[NP][NP]> p_ilev =
        m_data.pressure(k_locals.ie, NUM_LEV - 1);
    ExecViewUnmanaged<Real[2][NP][NP]> grad_p =
        m_data.vector_buf(k_locals.ie, 0, 0);
    gradient_sphere(k_locals.team, p_ilev, m_data, k_locals.c_dinv,
                    k_locals.c_vector_buf, grad_p);

    Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, NP * NP),
                         KOKKOS_LAMBDA(const int loop_idx) {
      const int igp = loop_idx / NP;
      const int jgp = loop_idx % NP;
      // const Real vgrad_p = k_locals.c_scalar_buf(0, 0);
      //     m_region.U_current(k_locals.ie, NUM_LEV - 1)(igp, jgp) *
      //         grad_p(0, igp, jgp) +
      //     m_region.V_current(k_locals.ie, NUM_LEV - 1)(igp, jgp) *
      //         grad_p(1, igp, jgp);
      k_locals.c_scalar_buf(0, 0) =
          m_region.U_current(k_locals.ie, NUM_LEV - 1)(igp, jgp) *
          grad_p(0, igp, jgp);
      // FMA, so no temporary needed
      k_locals.c_scalar_buf(0, 0) +=
          m_region.V_current(k_locals.ie, NUM_LEV - 1)(igp, jgp) *
          grad_p(1, igp, jgp);

      // (vgrad_p - vector_buf_1 - div_vdp) / p_ilev
      k_locals.c_scalar_buf(0, 0) -= k_locals.c_vector_buf(0, igp, jgp);
      // FMA, so no temporary needed
      k_locals.c_scalar_buf(0, 0) +=
          -0.5 * m_data.div_vdp(k_locals.ie, NUM_LEV - 1, igp, jgp);

      // const Real ckk = 0.5 * ckl;

      m_data.omega_p(k_locals.ie, NUM_LEV - 1, igp, jgp) =
          k_locals.c_scalar_buf(0, 0) / p_ilev(igp, jgp);
    });
  }

  // Depends on pressure, U_current, V_current, div_vdp, omega_p
  KOKKOS_INLINE_FUNCTION
  void preq_omega_ps(const KernelVariables &k_locals) const {
    // NOTE: we can't use a single TeamThreadRange loop, since
    //       gradient_sphere requires a 'consistent' pressure,
    //       meaning that we cannot update the different pressure
    //       points within a level before the gradient is complete!
    preq_omega_ps_init(k_locals);
    preq_omega_ps_loop(k_locals);
    preq_omega_ps_tail(k_locals);
  }

  // Depends on DP3D
  KOKKOS_INLINE_FUNCTION
  void compute_pressure(const KernelVariables &k_locals) const {
    ExecViewUnmanaged<Real[NUM_LEV][NP][NP]> pressure =
        m_data.pressure(k_locals.team);
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, NP * NP),
                         KOKKOS_LAMBDA(const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;
      k_locals.c_scalar_buf(0, 0) = m_data.hybrid_a(0) * m_data.ps0();
      k_locals.c_scalar_buf(0, 1) =
          0.5 * m_region.DP3D_current(k_locals.ie)(0, igp, jgp);
      pressure(0, igp, jgp) =
          k_locals.c_scalar_buf(0, 0) + k_locals.c_scalar_buf(0, 1);
      for (int ilev = 1; ilev < NUM_LEV; ilev++) {
        k_locals.c_scalar_buf(0, 0) =
            0.5 * (m_region.DP3D_current(k_locals.ie)(ilev - 1, igp, jgp) +
                   m_region.DP3D_current(k_locals.ie)(ilev, igp, jgp));
        pressure(ilev, igp, jgp) =
            pressure(ilev - 1, igp, jgp) + k_locals.c_scalar_buf(0, 0);
      }
    });
  }

  // Depends on DP3D, PHIS, DP3D, PHI, T_v
  // Modifies pressure, PHI
  KOKKOS_INLINE_FUNCTION
  void compute_scan_properties(const KernelVariables &k_locals) const {
    if (k_locals.team.team_rank() == 0) {
      compute_pressure(k_locals);
      preq_hydrostatic(k_locals);
      preq_omega_ps(k_locals);
    }
  }

  KOKKOS_INLINE_FUNCTION
  void compute_temperature_no_tracers_helper(const KernelVariables &k_locals,
                                             int ilev) const {
    ExecViewUnmanaged<Real[NUM_LEV][NP][NP]> T_v = m_data.T_v(k_locals.team);
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, NP * NP),
                         KOKKOS_LAMBDA(const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;
      T_v(ilev, igp, jgp) = m_region.T_current(k_locals.ie)(ilev, igp, jgp);
    });
  }

  KOKKOS_INLINE_FUNCTION
  void compute_temperature_tracers_helper(const KernelVariables &k_locals,
                                          int ilev) const {
    ExecViewUnmanaged<Real[NUM_LEV][NP][NP]> T_v = m_data.T_v(k_locals.team);
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, NP * NP),
                         KOKKOS_LAMBDA(const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      // Real Qt = k_locals.c_scalar_buf(0, 0);
      k_locals.c_scalar_buf(0, 0) =
          m_region.QDP(k_locals.ie, 0, m_data.qn0())(ilev, igp, jgp) /
          m_region.DP3D_current(k_locals.ie)(ilev, igp, jgp);
      k_locals.c_scalar_buf(0, 0) *=
          PhysicalConstants::Rwater_vapor / PhysicalConstants::Rgas - 1.0;
      k_locals.c_scalar_buf(0, 0) += 1.0;
      T_v(ilev, igp, jgp) = m_region.T_current(k_locals.ie)(ilev, igp, jgp) *
                            k_locals.c_scalar_buf(0, 0);
    });
  }

  // Depends on DERIVED_UN0, DERIVED_VN0, METDET, DINV
  // Initializes div_vdp, which is used 2 times afterwards
  // Modifies DERIVED_UN0, DERIVED_VN0
  // Requires NUM_LEV * 5 * NP * NP
  KOKKOS_INLINE_FUNCTION
  void compute_div_vdp(const KernelVariables &k_locals, int ilev) const {
    // Create subviews to explicitly have static dimensions
    // ExecViewUnmanaged<Real[2][NP][NP]> vdp_ilev = k_locals.c_vector_buf;

    Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, NP * NP),
                         KOKKOS_LAMBDA(const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      k_locals.c_vector_buf(0, igp, jgp) =
          m_region.U_current(k_locals.ie, ilev)(igp, jgp) *
          m_region.DP3D_current(k_locals.ie)(ilev, igp, jgp);
      k_locals.c_vector_buf(1, igp, jgp) =
          m_region.V_current(k_locals.ie, ilev)(igp, jgp) *
          m_region.DP3D_current(k_locals.ie)(ilev, igp, jgp);

      k_locals.c_scalar_buf(0, 0) =
          PhysicalConstants::eta_ave_w * k_locals.c_vector_buf(0, igp, jgp);
      m_region.DERIVED_UN0_update(k_locals.ie, ilev)(igp, jgp) =
          m_region.DERIVED_UN0(k_locals.ie, ilev)(igp, jgp) +
          k_locals.c_scalar_buf(0, 0);

      k_locals.c_scalar_buf(0, 0) =
          PhysicalConstants::eta_ave_w * k_locals.c_vector_buf(1, igp, jgp);
      m_region.DERIVED_VN0_update(k_locals.ie, ilev)(igp, jgp) =
          m_region.DERIVED_VN0(k_locals.ie, ilev)(igp, jgp) +
          k_locals.c_scalar_buf(0, 0);
    });

    ExecViewUnmanaged<Real[NP][NP]> div_vdp_ilev = Kokkos::subview(
        m_data.div_vdp(k_locals.team), ilev, Kokkos::ALL(), Kokkos::ALL());
    divergence_sphere(k_locals.team, k_locals.c_vector_buf, m_data,
                      m_region.METDET(k_locals.ie), k_locals.c_dinv,
                      k_locals.c_vector_buf, div_vdp_ilev);
  }

  // Depends on T_current, DERIVE_UN0, DERIVED_VN0, METDET, DINV
  // Might depend on QDP, DP3D_current
  KOKKOS_INLINE_FUNCTION
  void compute_temperature_div_vdp(const KernelVariables &k_locals) const {
    if (m_data.qn0() == -1) {
      Kokkos::parallel_for(Kokkos::TeamThreadRange(k_locals.team, NUM_LEV),
                           KOKKOS_LAMBDA(const int ilev) {
        compute_temperature_no_tracers_helper(k_locals, ilev);
        compute_div_vdp(k_locals, ilev);
      });
    } else {
      Kokkos::parallel_for(Kokkos::TeamThreadRange(k_locals.team, NUM_LEV),
                           KOKKOS_LAMBDA(const int ilev) {
        compute_temperature_tracers_helper(k_locals, ilev);
        compute_div_vdp(k_locals, ilev);
      });
    }
  }

  // Requires 7 x NP x NP thread memory
  // Depends on DERIVED_UN0, DERIVED_VN0, U, V,
  // Modifies DERIVED_UN0, DERIVED_VN0, OMEGA_P, T, and DP3D
  // block_3d_scalars
  KOKKOS_INLINE_FUNCTION
  void compute_stuff(const KernelVariables &k_locals, int ilev) const {
    ExecViewUnmanaged<const Real[NUM_LEV][NP][NP]> pressure =
        m_data.pressure(k_locals.team);

    // Depends on T (global), OMEGA_P (global), U (global), V (global),
    // SPHEREMP (global), T_v, and omega_p
    // Requires 2 * NUM_LEV * NP * NP scratch memory
    // Create subviews to explicitly have static dimensions
    ExecViewUnmanaged<const Real[NP][NP]> temperature = Kokkos::subview(
        m_region.T_current(k_locals.ie), ilev, Kokkos::ALL(), Kokkos::ALL());

    ExecViewUnmanaged<Real[2][NP][NP]> grad_tmp =
        Kokkos::subview(m_data.vector_buf(k_locals.team, 0), ilev, Kokkos::ALL,
                        Kokkos::ALL, Kokkos::ALL);

    gradient_sphere(k_locals.team, temperature, m_data, k_locals.c_dinv,
                    k_locals.c_vector_buf, grad_tmp);

    Kokkos::parallel_for(Kokkos::ThreadVectorRange(k_locals.team, NP * NP),
                         [&](const int idx) {
      const int igp = idx / NP;
      const int jgp = idx % NP;

      k_locals.c_scalar_buf(0, 0) = PhysicalConstants::eta_ave_w *
                                    m_data.omega_p(k_locals.ie, ilev, igp, jgp);
      m_region.OMEGA_P_update(k_locals.ie, ilev)(igp, jgp) =
          m_region.OMEGA_P(k_locals.ie, ilev)(igp, jgp) +
          k_locals.c_scalar_buf(0, 0);

      // const Real ttens = k_locals.c_scalar_buf(0, 0);
      k_locals.c_scalar_buf(0, 0) =
          m_region.U_current(k_locals.ie)(ilev, igp, jgp) *
          grad_tmp(0, igp, jgp);
      k_locals.c_scalar_buf(0, 1) =
          m_region.V_current(k_locals.ie)(ilev, igp, jgp) *
          grad_tmp(1, igp, jgp);
      k_locals.c_scalar_buf(0, 0) += k_locals.c_scalar_buf(0, 1);
      k_locals.c_scalar_buf(0, 1) =
          PhysicalConstants::kappa * m_data.T_v(k_locals.ie, ilev, igp, jgp);
      k_locals.c_scalar_buf(0, 0) +=
          k_locals.c_scalar_buf(0, 1) *
          m_data.omega_p(k_locals.ie, ilev, igp, jgp);

      // ttens * dt2 + T_previous
      k_locals.c_scalar_buf(0, 0) *= m_data.dt2();
      k_locals.c_scalar_buf(0, 0) +=
          m_region.T_previous(k_locals.ie)(ilev, igp, jgp);

      m_region.T_future(k_locals.ie)(ilev, igp, jgp) =
          m_region.SPHEREMP(k_locals.ie)(igp, jgp) *
          k_locals.c_scalar_buf(0, 0);

      k_locals.c_scalar_buf(0, 0) =
          m_data.div_vdp(k_locals.ie, ilev, igp, jgp) * -m_data.dt2();
      k_locals.c_scalar_buf(0, 0) +=
          m_region.DP3D_previous(k_locals.ie)(ilev, igp, jgp);
      m_region.DP3D_future(k_locals.ie)(ilev, igp, jgp) =
          m_region.SPHEREMP(k_locals.ie)(igp, jgp) *
          k_locals.c_scalar_buf(0, 0);
    });
  }

  // Computes the vertical advection of T and v
  // Not currently used
  KOKKOS_INLINE_FUNCTION
  void preq_vertadv(
      const TeamPolicy &team,
      const ExecViewUnmanaged<const Real[NUM_LEV][NP][NP]> T,
      const ExecViewUnmanaged<const Real[NUM_LEV][2][NP][NP]> v,
      const ExecViewUnmanaged<const Real[NUM_LEV_P][NP][NP]> eta_dp_deta,
      const ExecViewUnmanaged<const Real[NUM_LEV][NP][NP]> rpdel,
      ExecViewUnmanaged<Real[NUM_LEV][NP][NP]> T_vadv,
      ExecViewUnmanaged<Real[NUM_LEV][2][NP][NP]> v_vadv) {
    constexpr const int k_0 = 0;
    for (int j = 0; j < NP; ++j) {
      for (int i = 0; i < NP; ++i) {
        Real facp = 0.5 * rpdel(k_0, j, i) * eta_dp_deta(k_0 + 1, j, i);
        T_vadv(k_0, j, i) = facp * (T(k_0 + 1, j, i) - T(k_0, j, i));
        for (int h = 0; h < 2; ++h) {
          v_vadv(k_0, h, j, i) = facp * (v(k_0 + 1, h, j, i) - v(k_0, h, j, i));
        }
      }
    }
    constexpr const int k_f = NUM_LEV - 1;
    for (int k = k_0 + 1; k < k_f; ++k) {
      for (int j = 0; j < NP; ++j) {
        for (int i = 0; i < NP; ++i) {
          Real facp = 0.5 * rpdel(k, j, i) * eta_dp_deta(k + 1, j, i);
          Real facm = 0.5 * rpdel(k, j, i) * eta_dp_deta(k, j, i);
          T_vadv(k, j, i) = facp * (T(k + 1, j, i) - T(k, j, i)) +
                            facm * (T(k, j, i) - T(k - 1, j, i));
          for (int h = 0; h < 2; ++h) {
            v_vadv(k, h, j, i) = facp * (v(k + 1, h, j, i) - v(k, h, j, i)) +
                                 facm * (v(k, h, j, i) - v(k - 1, h, j, i));
          }
        }
      }
    }
    for (int j = 0; j < NP; ++j) {
      for (int i = 0; i < NP; ++i) {
        Real facm = 0.5 * rpdel(k_f, j, i) * eta_dp_deta(k_f, j, i);
        T_vadv(k_f, j, i) = facm * (T(k_f, j, i) - T(k_f - 1, j, i));
        for (int h = 0; h < 2; ++h) {
          v_vadv(k_f, h, j, i) = facm * (v(k_f, h, j, i) - v(k_f - 1, h, j, i));
        }
      }
    }
  }

  KOKKOS_INLINE_FUNCTION
  void init_const_cache(const TeamPolicy &team,
                        ExecViewUnmanaged<Real[2][2][NP][NP]> c_dinv) const {
    const int ie = team.league_rank();
    Kokkos::parallel_for(Kokkos::ThreadVectorRange(team, NP * NP),
                         KOKKOS_LAMBDA(int idx) {
      const int hgp = idx / NP;
      const int igp = idx % NP;
      for (int jgp = 0; jgp < 2; ++jgp) {
        for (int kgp = 0; kgp < 2; ++kgp) {
          c_dinv(jgp, kgp, hgp, igp) = m_region.DINV(ie)(jgp, kgp, hgp, igp);
        }
      }
    });
  }

  template <typename Data>
  KOKKOS_INLINE_FUNCTION Real *allocate_team(TeamPolicy &team) const {
    ViewType<Data, ScratchMemSpace, Kokkos::MemoryUnmanaged> view(
        team.team_scratch(0));
    return view.data();
  }

  template <typename Data>
  KOKKOS_INLINE_FUNCTION Real *allocate_thread(TeamPolicy &team) const {
    ViewType<Data, ScratchMemSpace, Kokkos::MemoryUnmanaged> view(
        team.thread_scratch(0));
    return view.data();
  }

  KOKKOS_INLINE_FUNCTION
  void operator()(TeamPolicy team) const {
    // Cache dinv, and dvv
    ExecViewUnmanaged<Real[2][NP][NP]> c_vector_buf(
        allocate_thread<Real[2][NP][NP]>(team));
    ExecViewUnmanaged<Real[NP][NP]> c_scalar_buf(
        allocate_thread<Real[NP][NP]>(team));
    ExecViewUnmanaged<Real[2][2][NP][NP]> c_dinv(
        allocate_team<Real[2][2][NP][NP]>(team));

    init_const_cache(team, c_dinv);
    KernelVariables k_locals(team, c_dinv, c_scalar_buf, c_vector_buf);

    compute_temperature_div_vdp(k_locals);

    compute_scan_properties(k_locals);

    compute_velocity_eta_dpdn(k_locals);
  }

  KOKKOS_INLINE_FUNCTION
  size_t shmem_size(const int team_size) const {
    size_t mem_size =
        sizeof(Real[2][2][NP][NP]) +
        (sizeof(Real[2][NP][NP]) + sizeof(Real[NP][NP])) * team_size;
    return mem_size;
  }
};

void compute_and_apply_rhs(const Control &data, Region &region,
                           int threads_per_team, int vectors_per_thread) {
  update_state f(data, region);
  Kokkos::parallel_for(Kokkos::TeamPolicy<ExecSpace>(data.num_elems(),
                                                     threads_per_team,
                                                     vectors_per_thread),
                       f);
  ExecSpace::fence();
}

void print_results_2norm(const Control &data, const Region &region) {
  Real vnorm(0.), tnorm(0.), dpnorm(0.);
  for (int ie = 0; ie < data.num_elems(); ++ie) {
    auto U = Kokkos::create_mirror_view(region.U_current(ie));
    Kokkos::deep_copy(U, region.U_future(ie));

    auto V = Kokkos::create_mirror_view(region.V_current(ie));
    Kokkos::deep_copy(V, region.V_future(ie));

    auto T = Kokkos::create_mirror_view(region.T_current(ie));
    Kokkos::deep_copy(T, region.T_future(ie));

    auto DP3D = Kokkos::create_mirror_view(region.DP3D_current(ie));
    Kokkos::deep_copy(DP3D, region.DP3D_future(ie));

    vnorm += std::pow(compute_norm(U), 2);
    vnorm += std::pow(compute_norm(V), 2);
    tnorm += std::pow(compute_norm(T), 2);
    dpnorm += std::pow(compute_norm(DP3D), 2);
  }

  std::cout << std::setprecision(15);
  std::cout << "   ---> Norms:\n"
            << "          ||v||_2  = " << std::sqrt(vnorm) << "\n"
            << "          ||T||_2  = " << std::sqrt(tnorm) << "\n"
            << "          ||dp||_2 = " << std::sqrt(dpnorm) << "\n";
}

} // Namespace TinMan
