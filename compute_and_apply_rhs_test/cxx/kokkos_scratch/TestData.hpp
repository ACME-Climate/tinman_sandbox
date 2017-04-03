#ifndef DATA_STRUCTURES_HPP
#define DATA_STRUCTURES_HPP

#include "Dimensions.hpp"

#include "Types.hpp"

namespace TinMan {

struct PhysicalConstants {
  static constexpr Real Rwater_vapor = 461.5;
  static constexpr Real Rgas = 287.04;
  static constexpr Real cp = 1005.0;
  static constexpr Real kappa = Rgas / cp;
  static constexpr Real rrearth = 1.0 / 6.376e6;

  static constexpr Real eta_ave_w = 1.0;
};

class Control {
public:
  // num_elems is te number of elements in the simulation

  // This constructor should only be used by the host
  Control(int num_elems);

  /* These functions must be called from device code */
  KOKKOS_INLINE_FUNCTION int num_elems() const { return m_num_elems; }
  /* Tracer timelevel */
  KOKKOS_INLINE_FUNCTION int qn0() const { return m_qn0; }
  /* dt * 2 */
  KOKKOS_INLINE_FUNCTION Real dt2() const { return m_dt2; }

  /* ps0 component of hybrid coordinate-interfaces
   * The A part of the pressure at a level
   * Complemented by hybrid_b, which is not needed in this code
   */
  KOKKOS_INLINE_FUNCTION Real hybrid_a(int level) const {
    return m_hybrid_a(level);
  }

  /* Global constant - boundary condition at the top of the atmosphere
   * This is used to ensure that the top level has pressure ptop
   * The pressure at a level k is approximately:
   * p(k) = A(k) ps0 + B(k) ps_v
   */
  KOKKOS_INLINE_FUNCTION Real ps0() const { return m_ps0; }

  KOKKOS_INLINE_FUNCTION Real dvv(int x, int y) const { return m_dvv(x, y); }

  KOKKOS_INLINE_FUNCTION ExecViewUnmanaged<const Real[NP][NP]> dvv() const {
    return m_dvv;
  }

  KOKKOS_INLINE_FUNCTION ExecViewUnmanaged<Real[NUM_LEV][NP][NP]> const
  pressure(const TeamPolicy &team) const {
    return Kokkos::subview(m_pressure, team.league_rank(), Kokkos::ALL,
                           Kokkos::ALL, Kokkos::ALL);
  }

  KOKKOS_INLINE_FUNCTION ExecViewUnmanaged<Real[NUM_LEV][NP][NP]> const
  pressure(const int &ie) const {
    return Kokkos::subview(m_pressure, ie, Kokkos::ALL, Kokkos::ALL,
                           Kokkos::ALL);
  }

  KOKKOS_INLINE_FUNCTION Real &
  pressure(const int &ie, const int &ilev, const int &igp, const int &jgp) {
    return m_pressure(ie, ilev, igp, jgp);
  }

  KOKKOS_INLINE_FUNCTION ExecViewUnmanaged<Real[NUM_LEV][NP][NP]> const
  T_v(const TeamPolicy &team) const {
    return Kokkos::subview(m_T_v, team.league_rank(), Kokkos::ALL, Kokkos::ALL,
                           Kokkos::ALL);
  }

  KOKKOS_INLINE_FUNCTION ExecViewUnmanaged<Real[NUM_LEV][NP][NP]> const
  div_vdp(const TeamPolicy &team) const {
    return Kokkos::subview(m_div_vdp, team.league_rank(), Kokkos::ALL,
                           Kokkos::ALL, Kokkos::ALL);
  }

  KOKKOS_INLINE_FUNCTION ExecViewUnmanaged<Real[NUM_LEV][2][NP][NP]> const
  vector_buf(const TeamPolicy &team) const {
    return Kokkos::subview(m_vector_buf, team.league_rank(), Kokkos::ALL,
                           Kokkos::ALL, Kokkos::ALL, Kokkos::ALL);
  }

private:
  const int m_num_elems;

  // tracer timelevel, inclusive range of 0-1
  const int m_qn0;

  const int m_ps0;

  // dt
  const Real m_dt2;

  /* Device objects, to reduce the memory transfer required */
  ExecViewManaged<Real[NUM_LEV_P]> m_hybrid_a;
  ExecViewManaged<Real[NP][NP]> m_dvv; // Laplacian
  ExecViewManaged<Real * [NUM_LEV][NP][NP]> m_pressure;
  ExecViewManaged<Real * [NUM_LEV][NP][NP]> m_T_v;
  ExecViewManaged<Real * [NUM_LEV][NP][NP]> m_div_vdp;
  ExecViewManaged<Real * [NUM_LEV][2][NP][NP]> m_vector_buf;
};

} // Namespace TinMan

#endif // DATA_STRUCTURES_HPP
