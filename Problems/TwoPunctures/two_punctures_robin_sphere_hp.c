#include <sc_reduce.h>
#include <pXest.h>
#include <d4est_util.h>
#include <d4est_linalg.h>
#include <problem.h>
#include <d4est_elliptic_data.h>
#include <d4est_elliptic_eqns.h>
#include <d4est_estimator_bi.h>
#include <d4est_solver_cg.h>
#include <d4est_amr.h>
#include <d4est_amr_smooth_pred.h>
#include <d4est_geometry.h>
#include <d4est_geometry_cubed_sphere.h>
#include <d4est_vtk.h>
#include <d4est_output.h>
#include <d4est_mesh.h>
#include <ini.h>
#include <d4est_element_data.h>
#include <d4est_estimator_stats.h>
#include <d4est_poisson.h>
#include <d4est_poisson_flux_sipg.h>
#include <d4est_solver_newton.h>
#include <krylov_petsc.h>
#include <d4est_util.h>
#include <time.h>
#include "two_punctures_fcns.h"

typedef struct {
  
  int do_not_solve;
  int deg_vol_quad_inc_inner;
  int deg_vol_quad_inc_outer;
  
} two_punctures_init_params_t;

static
int two_punctures_init_params_handler
(
 void* user,
 const char* section,
 const char* name,
 const char* value
)
{
  two_punctures_init_params_t* pconfig = (two_punctures_init_params_t*)user;
  if (d4est_util_match_couple(section,"problem",name,"do_not_solve")) {
    D4EST_ASSERT(pconfig->do_not_solve == -1);
    pconfig->do_not_solve = atoi(value);
  }
  else if (d4est_util_match_couple(section,"problem",name,"deg_vol_quad_inc_inner")) {
    D4EST_ASSERT(pconfig->deg_vol_quad_inc_inner == -1);
    pconfig->deg_vol_quad_inc_inner = atoi(value);
  }
  else if (d4est_util_match_couple(section,"problem",name,"deg_vol_quad_inc_outer")) {
    D4EST_ASSERT(pconfig->deg_vol_quad_inc_outer == -1);
    pconfig->deg_vol_quad_inc_outer = atoi(value);
  }
    
  else {
    return 0;  /* unknown section/name, error */
  }
  return 1;
}


static
two_punctures_init_params_t
two_punctures_init_params_input
(
 const char* input_file
)
{
  two_punctures_init_params_t input;
  input.do_not_solve = -1;
  input.deg_vol_quad_inc_inner = -1;
  input.deg_vol_quad_inc_outer = -1;

  if (ini_parse(input_file, two_punctures_init_params_handler, &input) < 0) {
    D4EST_ABORT("Can't load input file");
  }

  D4EST_CHECK_INPUT("problem", input.do_not_solve, -1);
  D4EST_CHECK_INPUT("problem", input.deg_vol_quad_inc_inner, -1);
  D4EST_CHECK_INPUT("problem", input.deg_vol_quad_inc_outer, -1);
  
  return input;
}



static
int
amr_mark_element
(
 p4est_t* p4est,
 double eta2,
 d4est_estimator_stats_t** stats,
 d4est_element_data_t* elem_data,
 void* user
)
{
  problem_ctx_t* ctx = user;
  d4est_amr_smooth_pred_params_t* params = ctx->smooth_pred_params;

  int elem_bin;
  
  /* outer shell */
  if (elem_data->tree < 6){
    elem_bin = 0;
  }
  /* inner shell */
  else if(elem_data->tree < 12){
    elem_bin = 1;
  }
  /* center cube */
  else {
    elem_bin = 2;
  }
 
  double eta2_percentile
    = d4est_estimator_stats_get_percentile(stats[elem_bin], params->percentile);

  return (eta2 >= eta2_percentile);
}

static
gamma_params_t
amr_set_element_gamma
(
 p4est_t* p4est,
 double eta2,
 d4est_estimator_stats_t** stats,
 d4est_element_data_t* elem_data,
 void* user
)
{
  problem_ctx_t* ctx = user;
  d4est_amr_smooth_pred_params_t* params = ctx->smooth_pred_params;
  
  gamma_params_t gamma_hpn;
  gamma_hpn.gamma_h = params->gamma_h;
  gamma_hpn.gamma_p = params->gamma_p;
  gamma_hpn.gamma_n = params->gamma_n;

  return gamma_hpn;
}




int
in_bin_fcn
(
 d4est_element_data_t* elem_data,
 int bin
)
{
  int elem_bin;

  /* outer shell */
  if (elem_data->tree < 6){
    elem_bin = 0;
  }
  /* inner shell */
  else if(elem_data->tree < 12){
    elem_bin = 1;
  }
  /* center cube */
  else {
    elem_bin = 2;
  }

  return (elem_bin == bin);
}

int
problem_set_mortar_degree
(
 d4est_element_data_t* elem_data,
 void* user_ctx
)
{
  return elem_data->deg_vol_quad;
}



void
problem_set_degrees_after_amr
(
 d4est_element_data_t* elem_data,
 void* user_ctx
)
{

  two_punctures_init_params_t* init_params = user_ctx;
  
  /* outer shell */
  if (elem_data->tree < 6){
    elem_data->deg_vol_quad = elem_data->deg + init_params->deg_vol_quad_inc_outer;
  }
  /* inner shell */
  else if(elem_data->tree < 12){
    elem_data->deg_vol_quad = elem_data->deg + init_params->deg_vol_quad_inc_inner;
  }
  /* center cube */
  else {
    elem_data->deg_vol_quad = elem_data->deg;
  }

}



void
problem_init
(
 p4est_t* p4est,
 p4est_ghost_t** ghost,
 d4est_element_data_t** ghost_data,
 d4est_operators_t* d4est_ops,
 d4est_geometry_t* d4est_geom,
 d4est_quadrature_t* d4est_quad,
 d4est_mesh_geometry_storage_t* geometric_factors,
 int initial_nodes,
 const char* input_file,
 sc_MPI_Comm mpicomm
)
{

  /*  */
  two_punctures_init_params_t init_params = two_punctures_init_params_input(input_file
                                                                           );
  
  two_punctures_params_t two_punctures_params;
  init_two_punctures_data(&two_punctures_params);
  
  d4est_amr_smooth_pred_params_t smooth_pred_params = d4est_amr_smooth_pred_params_input(input_file);
  d4est_poisson_robin_bc_t bc_data_for_jac;
  bc_data_for_jac.robin_coeff = two_punctures_robin_coeff_sphere_fcn;
  bc_data_for_jac.robin_rhs = two_punctures_robin_bc_rhs_fcn;

  d4est_poisson_robin_bc_t bc_data_for_res;
   bc_data_for_res.robin_coeff = two_punctures_robin_coeff_sphere_fcn;
  bc_data_for_res.robin_rhs = two_punctures_robin_bc_rhs_fcn;
  
  d4est_poisson_dirichlet_bc_t bc_data_for_res_bi;
  bc_data_for_res_bi.dirichlet_fcn = zero_fcn;
  
  d4est_poisson_flux_data_t* flux_data_for_jac = d4est_poisson_flux_new(p4est, input_file, BC_ROBIN, &bc_data_for_jac, problem_set_mortar_degree, NULL);
  
  d4est_poisson_flux_data_t* flux_data_for_res = d4est_poisson_flux_new(p4est, input_file,  BC_ROBIN, &bc_data_for_res, problem_set_mortar_degree, NULL);

  problem_ctx_t ctx;
  ctx.two_punctures_params = &two_punctures_params;
  ctx.smooth_pred_params = &smooth_pred_params;
  ctx.flux_data_for_jac = flux_data_for_jac;
  ctx.flux_data_for_res = flux_data_for_res;
                           
  d4est_elliptic_eqns_t prob_fcns;
  prob_fcns.build_residual = two_punctures_build_residual;
  prob_fcns.apply_lhs = two_punctures_apply_jac;
  prob_fcns.user = &ctx;
  
  
  d4est_elliptic_data_t prob_vecs;
  prob_vecs.Au = P4EST_ALLOC(double, initial_nodes);
  prob_vecs.u = P4EST_ALLOC(double, initial_nodes);
  prob_vecs.local_nodes = initial_nodes;

  double* error = P4EST_ALLOC(double, prob_vecs.local_nodes);
  double* u_prev = P4EST_ALLOC(double, prob_vecs.local_nodes);
  
  d4est_poisson_flux_sipg_params_t* sipg_params = flux_data_for_jac->flux_data;
  
  d4est_estimator_bi_penalty_data_t penalty_data;
  penalty_data.u_penalty_fcn = houston_u_prefactor_maxp_minh;
  penalty_data.u_dirichlet_penalty_fcn = houston_u_dirichlet_prefactor_maxp_minh;
  penalty_data.gradu_penalty_fcn = houston_gradu_prefactor_maxp_minh;
  penalty_data.penalty_prefactor = sipg_params->sipg_penalty_prefactor;
  penalty_data.sipg_flux_h = sipg_params->sipg_flux_h;
  
  d4est_amr_smooth_pred_marker_t amr_marker;
  amr_marker.user = (void*)&ctx;
  amr_marker.mark_element_fcn = amr_mark_element;
  amr_marker.set_element_gamma_fcn = amr_set_element_gamma;

  d4est_amr_t* d4est_amr =
    d4est_amr_init
    (
     p4est,
     input_file,
     "[D4EST_AMR]:",
     &amr_marker
    );


  d4est_mesh_init_field
    (
     p4est,
     prob_vecs.u,
     two_punctures_initial_guess,
     d4est_ops,
     d4est_geom,
     NULL
    );
    
  d4est_linalg_copy_1st_to_2nd(prob_vecs.u, u_prev, prob_vecs.local_nodes);


  d4est_output_energy_norm_fit_t* fit = d4est_output_new_energy_norm_fit(d4est_amr->num_of_amr_steps);
  
  for (int level = 0; level < d4est_amr->num_of_amr_steps + 1; ++level){
    
    d4est_estimator_bi_compute
      (
       p4est,
       &prob_vecs,
       &prob_fcns,
       penalty_data,
       zero_fcn,
       *ghost,
       *ghost_data,
       d4est_ops,
       d4est_geom,
       d4est_quad,
       NO_DIAM_APPROX,
       problem_set_mortar_degree,
       NULL
      );

  d4est_estimator_stats_t* stats [3];
    for (int i = 0; i < 3; i++){
      stats[i] = P4EST_ALLOC(d4est_estimator_stats_t, 1);
    }
    
    double local_eta2 = d4est_estimator_stats_compute_per_bin
                        (
                         p4est,
                         &stats[0],
                         3,
                         in_bin_fcn
                        );

    d4est_mesh_print_number_of_elements_per_tree(p4est);
    d4est_estimator_stats_compute_max_percentiles_across_proc
      (
       stats,
       3
      );

    if (p4est->mpirank == 0){
      for (int i = 0; i < 3; i++){
        d4est_estimator_stats_print(stats[i]);
      }
    }

    d4est_linalg_vec_axpyeqz(-1., prob_vecs.u, u_prev, error, prob_vecs.local_nodes);
    
    d4est_output_vtk
      (
       p4est,
       d4est_ops,
       d4est_geom,
       prob_vecs.u,
       u_prev,
       error,
       input_file,
       "two_punctures",
       prob_vecs.local_nodes,
       level,
       1
      );

    d4est_output_vtk_degree_mesh
      (
       p4est,
       d4est_ops,
       d4est_geom,
       d4est_quad,
       input_file,
       "uniform_two_punctures_degree_mesh",
       1,
       level
      );

    d4est_ip_energy_norm_data_t ip_norm_data;
    ip_norm_data.u_penalty_fcn = sipg_params->sipg_penalty_fcn;
    ip_norm_data.sipg_flux_h = sipg_params->sipg_flux_h;
    ip_norm_data.penalty_prefactor = sipg_params->sipg_penalty_prefactor;
    
    d4est_output_norms
      (
       p4est,
       d4est_ops,
       d4est_geom,
       d4est_quad,
       *ghost,
       *ghost_data,
       &ip_norm_data,
       stats[0]->total + stats[1]->total + stats[2]->total,
       error,
       fit
      );


    if (level != d4est_amr->num_of_amr_steps){

      if (p4est->mpirank == 0)
        printf("[D4EST_INFO]: AMR REFINEMENT LEVEL %d\n", level+1);

      d4est_amr_step
        (
         p4est,
         ghost,
         ghost_data,
         d4est_ops,
         d4est_amr,
         &prob_vecs.u,
         stats
        );
      
    }

    for (int i = 0; i < 3; i++){
      P4EST_FREE(stats[i]);
    }
    

    prob_vecs.local_nodes = d4est_mesh_update
                  (
                   p4est,
                   *ghost,
                   *ghost_data,
                   d4est_ops,
                   d4est_geom,
                   d4est_quad,
                   geometric_factors,
                   INITIALIZE_QUADRATURE_DATA,
                   INITIALIZE_GEOMETRY_DATA,
                   INITIALIZE_GEOMETRY_ALIASES,
                   problem_set_degrees_after_amr,
                   &init_params
                  );

    
    prob_vecs.Au = P4EST_REALLOC(prob_vecs.Au, double, prob_vecs.local_nodes);
    u_prev = P4EST_REALLOC(u_prev, double, prob_vecs.local_nodes);
    error = P4EST_REALLOC(error, double, prob_vecs.local_nodes);
    d4est_linalg_copy_1st_to_2nd(prob_vecs.u, u_prev, prob_vecs.local_nodes);

    if (!init_params.do_not_solve){
    d4est_solver_newton_solve
      (
       p4est,
       &prob_vecs,
       &prob_fcns,
       ghost,
       ghost_data,
       d4est_ops,
       d4est_geom,
       d4est_quad,
       input_file,
       NULL
      );
    }

  }

  printf("[D4EST_INFO]: Starting garbage collection...\n");
  d4est_amr_destroy(d4est_amr);
  d4est_poisson_flux_destroy(flux_data_for_jac);  
  d4est_poisson_flux_destroy(flux_data_for_res);  
  d4est_output_destroy_energy_norm_fit(fit);
  P4EST_FREE(error);
  P4EST_FREE(u_prev);
  P4EST_FREE(prob_vecs.u);
  P4EST_FREE(prob_vecs.Au);
}