#ifndef HOMME_REGION_HPP
#define HOMME_REGION_HPP

#include "Types.hpp"
#include "Utility.hpp"

#include <Kokkos_Core.hpp>

#include <random>

namespace Homme {

/* Per element data - specific velocity, temperature, pressure, etc. */
class Elements {
public:
  // Coriolis term
  ExecViewManaged<Real * [NP][NP]> m_fcor;
  // Differential geometry things
  ExecViewManaged<Real * [NP][NP]> m_spheremp;
  ExecViewManaged<Real * [NP][NP]> m_metdet;
  // Prescrived surface geopotential height at eta = 1
  ExecViewManaged<Real * [NP][NP]> m_phis;

  // Differential geometry tensors
  ExecViewManaged<Real * [2][2][NP][NP]> m_d;
  ExecViewManaged<Real * [2][2][NP][NP]> m_dinv;

  // Omega is the pressure vertical velocity
  ExecViewManaged<Scalar * [NP][NP][NUM_LEV]> m_omega_p;
  // ???
  ExecViewManaged<Scalar * [NP][NP][NUM_LEV]> m_pecnd;
  // Geopotential height field
  ExecViewManaged<Scalar * [NP][NP][NUM_LEV]> m_phi;
  // ???
  ExecViewManaged<Scalar * [NP][NP][NUM_LEV]> m_derived_un0;
  // ???
  ExecViewManaged<Scalar * [NP][NP][NUM_LEV]> m_derived_vn0;

  // Lateral Velocity
  ExecViewManaged<Scalar * [NUM_TIME_LEVELS][NP][NP][NUM_LEV]> m_u;
  ExecViewManaged<Scalar * [NUM_TIME_LEVELS][NP][NP][NUM_LEV]> m_v;
  // Temperature
  ExecViewManaged<Scalar * [NUM_TIME_LEVELS][NP][NP][NUM_LEV]> m_t;
  // ???
  ExecViewManaged<Scalar * [NUM_TIME_LEVELS][NP][NP][NUM_LEV]> m_dp3d;

  // q is the specific humidity
  ExecViewManaged<Scalar * [Q_NUM_TIME_LEVELS][QSIZE_D][NP][NP][NUM_LEV]> m_qdp;
  // eta is the vertical coordinate
  // eta dot is the flux through the vertical level interface
  //    (note there are NUM_LEV_P of them)
  // dpdn is the derivative of pressure with respect to eta
  ExecViewManaged<Scalar * [NP][NP][NUM_LEV_P]> m_eta_dot_dpdn;

  struct BufferViews {

    BufferViews() = default;
    void init(const int num_elems);
    ExecViewManaged<Scalar*    [NP][NP][NUM_LEV]> pressure;
    ExecViewManaged<Scalar* [2][NP][NP][NUM_LEV]> pressure_grad;
    ExecViewManaged<Scalar*    [NP][NP][NUM_LEV]> temperature_virt;
    ExecViewManaged<Scalar* [2][NP][NP][NUM_LEV]> temperature_grad;
    // TODO: Remove omega_p from the buffers
    ExecViewManaged<Scalar*    [NP][NP][NUM_LEV]> omega_p;
    ExecViewManaged<Scalar* [2][NP][NP][NUM_LEV]> vdp;
    ExecViewManaged<Scalar*    [NP][NP][NUM_LEV]> div_vdp;
    ExecViewManaged<Scalar*    [NP][NP][NUM_LEV]> ephi;
    ExecViewManaged<Scalar* [2][NP][NP][NUM_LEV]> energy_grad;
    ExecViewManaged<Scalar*    [NP][NP][NUM_LEV]> vorticity;

    // Buffers for EulerStepFunctor
    ExecViewManaged<Scalar*          [2][NP][NP][NUM_LEV]>  vstar;
    ExecViewManaged<Scalar* [QSIZE_D]   [NP][NP][NUM_LEV]>  qtens;
    ExecViewManaged<Scalar* [QSIZE_D][2][NP][NP][NUM_LEV]>  vstar_qdp;

    ExecViewManaged<Real* [NP][NP]> preq_buf;
    // Buffers for spherical operators
    ExecViewManaged<Scalar* [2][NP][NP][NUM_LEV]> div_buf;
    ExecViewManaged<Scalar* [2][NP][NP][NUM_LEV]> grad_buf;
    ExecViewManaged<Scalar* [2][NP][NP][NUM_LEV]> vort_buf;
  } buffers;

  Elements() = default;

  void init(const int num_elems);

  void random_init(int num_elems, std::mt19937_64 &engine);

  int num_elems() const { return m_num_elems; }

  // Fill the exec space views with data coming from F90 pointers
  void init_2d(CF90Ptr &D, CF90Ptr &Dinv, CF90Ptr &fcor, CF90Ptr &spheremp,
               CF90Ptr &metdet, CF90Ptr &phis);

  // Fill the exec space views with data coming from F90 pointers
  void pull_from_f90_pointers(CF90Ptr &state_v, CF90Ptr &state_t,
                              CF90Ptr &state_dp3d, CF90Ptr &derived_phi,
                              CF90Ptr &derived_pecnd, CF90Ptr &derived_omega_p,
                              CF90Ptr &derived_v, CF90Ptr &derived_eta_dot_dpdn,
                              CF90Ptr &state_qdp);
  void pull_3d(CF90Ptr &derived_phi, CF90Ptr &derived_pecnd,
               CF90Ptr &derived_omega_p, CF90Ptr &derived_v);
  void pull_4d(CF90Ptr &state_v, CF90Ptr &state_t, CF90Ptr &state_dp3d);
  void pull_eta_dot(CF90Ptr &derived_eta_dot_dpdn);
  void pull_qdp(CF90Ptr &state_qdp);

  // Push the results from the exec space views to the F90 pointers
  void push_to_f90_pointers(F90Ptr &state_v, F90Ptr &state_t, F90Ptr &state_dp,
                            F90Ptr &derived_phi, F90Ptr &derived_pecnd,
                            F90Ptr &derived_omega_p, F90Ptr &derived_v,
                            F90Ptr &derived_eta_dot_dpdn,
                            F90Ptr &state_qdp) const;
  void push_3d(F90Ptr &derived_phi, F90Ptr &derived_pecnd,
               F90Ptr &derived_omega_p, F90Ptr &derived_v) const;
  void push_4d(F90Ptr &state_v, F90Ptr &state_t, F90Ptr &state_dp3d) const;
  void push_eta_dot(F90Ptr &derived_eta_dot_dpdn) const;
  void push_qdp(F90Ptr &state_qdp) const;

  void d(Real *d_ptr, int ie) const;
  void dinv(Real *dinv_ptr, int ie) const;

private:
  int m_num_elems;
};

// TODO: DON'T USE SINGLETONS
Elements &get_elements();

} // Homme

#endif // HOMME_REGION_HPP
