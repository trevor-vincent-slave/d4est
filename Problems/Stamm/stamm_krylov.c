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
#include <d4est_poisson.h>
#include <d4est_poisson_flux_sipg.h>
#include <newton_petsc.h>
#include <krylov_petsc.h>
#include <d4est_util.h>
#include <time.h>
#include "stamm_fcns.h"

typedef struct {

  int deg;
  int deg_quad;
  
} problem_initial_degree_input_t;

static
int problem_initial_degree_input_handler
(
 void* user,
 const char* section,
 const char* name,
 const char* value
)
{
  problem_initial_degree_input_t* pconfig = (problem_initial_degree_input_t*)user;
  if (d4est_util_match_couple(section,"initial_grid",name,"deg")) {
    D4EST_ASSERT(pconfig->deg == -1);
    pconfig->deg = atoi(value);
  }
  else if (d4est_util_match_couple(section,"initial_grid",name,"deg_quad")) {
    D4EST_ASSERT(pconfig->deg_quad == -1);
    pconfig->deg_quad = atoi(value);
  }
  else {
    return 0;
  }
  return 1;
}

static
problem_initial_degree_input_t
problem_initial_degree_input
(
 const char* input_file
)
{
  problem_initial_degree_input_t input;
  input.deg = -1;
  input.deg_quad = -1;
  
  if (ini_parse(input_file, problem_initial_degree_input_handler, &input) < 0) {
    D4EST_ABORT("Can't load input file");
  }

  D4EST_CHECK_INPUT("initial_grid", input.deg, -1);
  D4EST_CHECK_INPUT("initial_grid", input.deg_quad, -1);
  printf("[PROBLEM]: deg = %d\n",input.deg);
  printf("[PROBLEM]: deg_quad = %d\n",input.deg_quad);
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
  stamm_params_t* params = user;
  double eta2_avg = stats[0]->mean;
  return (eta2 >= params->sigma*eta2_avg);
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
  stamm_params_t* params = user;
  
  gamma_params_t gamma_hpn;
  gamma_hpn.gamma_h = params->gamma_h;
  gamma_hpn.gamma_p = params->gamma_p;
  gamma_hpn.gamma_n = params->gamma_n;

  return gamma_hpn;
}

void
problem_set_degrees_init
(
 d4est_element_data_t* elem_data,
 void* user_ctx
)
{
  problem_initial_degree_input_t* input = user_ctx;
  elem_data->deg = input->deg;
  elem_data->deg_vol_quad = input->deg_quad;
}


int
problem_set_mortar_degree_init
(
 d4est_element_data_t* elem_data,
 void* user_ctx
)
{
  problem_initial_degree_input_t* input = user_ctx;
  return elem_data->deg;
}



void
problem_set_degrees_after_amr
(
 d4est_element_data_t* elem_data,
 void* user_ctx
)
{
  elem_data->deg_vol_quad = elem_data->deg;
}

static void
init_vec_with_elem_data
(
 p4est_t* p4est,
 double* u
)
{
  for (p4est_topidx_t tt = p4est->first_local_tree;
       tt <= p4est->last_local_tree;
       ++tt)
    {
      p4est_tree_t* tree = p4est_tree_array_index (p4est->trees, tt);
      sc_array_t* tquadrants = &tree->quadrants;
      int QQ = (p4est_locidx_t) tquadrants->elem_count;
      for (int qq = 0; qq < QQ; ++qq) {
        p4est_quadrant_t* quad = p4est_quadrant_array_index (tquadrants, qq);
        d4est_element_data_t* ed = (d4est_element_data_t*)(quad->p.user_data);
        int volume_nodes = d4est_lgl_get_nodes((P4EST_DIM), ed->deg);

        for (int i = 0; i < volume_nodes; i++){
          double x = ed->xyz[0][i];
          double y = ed->xyz[1][i];
          u[ed->nodal_stride + i] = (x*x + y*y)*(ed->id + 1);
        }
      }
    }
}


void
problem_init
(
 p4est_t* p4est,
 p4est_ghost_t** ghost,
 d4est_element_data_t** ghost_data,
 d4est_geometry_t* d4est_geom,
 d4est_operators_t* d4est_ops,
 const char* input_file,
 sc_MPI_Comm mpicomm
)
{
  D4EST_ASSERT(d4est_geom->geom_type == GEOM_BRICK);
  problem_initial_degree_input_t input = problem_initial_degree_input(input_file);
  stamm_params_t stamm_params = stamm_params_input(input_file);
  
  d4est_mesh_geometry_storage_t* geometric_factors = d4est_mesh_geometry_storage_init(p4est);
  d4est_quadrature_t* d4est_quad = d4est_quadrature_new(p4est, d4est_ops, d4est_geom, input_file, "quadrature", "[QUADRATURE]");
  d4est_poisson_flux_data_t* flux_data_for_apply_lhs = d4est_poisson_flux_new(p4est, input_file, zero_fcn, NULL, problem_set_mortar_degree_init, &input);
  d4est_poisson_flux_data_t* flux_data_for_build_rhs = d4est_poisson_flux_new(p4est, input_file, stamm_boundary_fcn, NULL, problem_set_mortar_degree_init, &input);
  
  d4est_elliptic_eqns_t prob_fcns;
  prob_fcns.build_residual = stamm_build_residual;
  prob_fcns.apply_lhs = stamm_apply_lhs;
  prob_fcns.user = flux_data_for_apply_lhs;
  
  double* error = NULL;
  double* u_analytic = NULL;
  int local_nodes = 0;
  
  d4est_elliptic_data_t prob_vecs;
  prob_vecs.Au = NULL;
  prob_vecs.u = NULL;
  prob_vecs.rhs = NULL;
  prob_vecs.local_nodes = 0;

  d4est_poisson_flux_sipg_params_t* sipg_params = flux_data_for_apply_lhs->user;
  d4est_estimator_bi_penalty_data_t penalty_data;
  penalty_data.u_penalty_fcn = houston_u_prefactor_maxp_minh;
  penalty_data.u_dirichlet_penalty_fcn = houston_u_dirichlet_prefactor_maxp_minh;
  penalty_data.gradu_penalty_fcn = houston_gradu_prefactor_maxp_minh;
  penalty_data.penalty_prefactor = sipg_params->sipg_penalty_prefactor;
  penalty_data.sipg_flux_h = sipg_params->sipg_flux_h;
  
  smooth_pred_marker_t amr_marker;
  amr_marker.user = (void*)&stamm_params;
  amr_marker.mark_element_fcn = amr_mark_element;
  amr_marker.set_element_gamma_fcn = amr_set_element_gamma;
  amr_marker.name = "stamm_marker";

  d4est_amr_t* d4est_amr =
    d4est_amr_init
    (
     p4est,
     input_file,
     "[D4EST_AMR]:",
     &amr_marker
    );

  d4est_amr_t* d4est_amr_uniform = d4est_amr_init_uniform_h(p4est);
  d4est_amr_t* d4est_amr_random = d4est_amr_init_random_hp(p4est, d4est_amr->max_degree, d4est_amr->num_of_amr_steps);


    local_nodes = d4est_mesh_update
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
                   problem_set_degrees_init,
                   (void*)&input
                  );

      prob_vecs.u = P4EST_REALLOC(prob_vecs.u, double, local_nodes);
      d4est_mesh_init_field
        (
         p4est,
         prob_vecs.u,
         stamm_initial_guess,
         d4est_ops,
         d4est_geom,
         NULL
        );
    
    prob_vecs.Au = P4EST_REALLOC(prob_vecs.Au, double, local_nodes);
    prob_vecs.rhs = P4EST_REALLOC(prob_vecs.rhs, double, local_nodes);
    prob_vecs.local_nodes = local_nodes;
    
    d4est_poisson_build_rhs_with_strong_bc
      (
       p4est,
       *ghost,
       *ghost_data,
       d4est_ops,
       d4est_geom,
       d4est_quad,
       &prob_vecs,
       flux_data_for_build_rhs,
       prob_vecs.rhs,
       stamm_rhs_fcn,
       &stamm_params
      );


  
  for (int level = 0; level < d4est_amr->num_of_amr_steps + 1; ++level){

    /* stamm_build_residual */
    /*   ( */
    /*    p4est, */
    /*    *ghost, */
    /*    *ghost_data, */
    /*    &prob_vecs, */
    /*    d4est_ops, */
    /*    d4est_geom, */
    /*    d4est_quad, */
    /*    flux_data_for_apply_lhs */
    /*   ); */

    /* printf("local_nodes = %d\n", local_nodes); */
    /* DEBUG_PRINT_ARR_DBL_SUM(prob_vecs.rhs, local_nodes); */
    /* DEBUG_PRINT_ARR_DBL_SUM(prob_vecs.Au, local_nodes); */
    /* DEBUG_PRINT_ARR_DBL_SUM(prob_vecs.u, local_nodes); */

    init_vec_with_elem_data
      (
       p4est,
       prob_vecs.u
      );

    
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
       problem_set_mortar_degree_init,
       &input
      );

    d4est_estimator_stats_t* stats = P4EST_ALLOC(d4est_estimator_stats_t,1);
    d4est_estimator_stats_compute(p4est, stats);
    d4est_estimator_stats_print(stats);

    d4est_output_vtk_with_analytic_error
      (
       p4est,
       d4est_ops,
       d4est_geom,
       d4est_quad,
       &prob_vecs,
       input_file,
       "uniform_stamm",
       stamm_analytic_solution,
       &stamm_params,
       level
      );

    d4est_output_vtk_degree_mesh
      (
       p4est,
       d4est_ops,
       d4est_geom,
       d4est_quad,
       input_file,
       "uniform_stamm_degree_mesh",
       1,
       level
      );
    
    d4est_output_norms_using_analytic_solution
      (
       p4est,
       d4est_ops,
       d4est_geom,
       d4est_quad,
       stats,
       &prob_vecs,
       stamm_analytic_solution,
       &stamm_params);

    P4EST_FREE(stats);
    
    if (level != d4est_amr->num_of_amr_steps){

      if (p4est->mpirank == 0)
        printf("[D4EST_INFO]: AMR REFINEMENT LEVEL %d\n", level+1);

      d4est_amr_step
        (
         p4est,
         ghost,
         ghost_data,
         d4est_ops,
         d4est_amr_random,//(level > 1) ? d4est_amr : d4est_amr_uniform,
         &prob_vecs.u,
         &stats
        );
      
    }


    local_nodes = d4est_mesh_update
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
                   (void*)&input
                  );

    
    prob_vecs.Au = P4EST_REALLOC(prob_vecs.Au, double, local_nodes);
    prob_vecs.rhs = P4EST_REALLOC(prob_vecs.rhs, double, local_nodes);
    prob_vecs.local_nodes = local_nodes;
    
    
    d4est_poisson_build_rhs_with_strong_bc
      (
       p4est,
       *ghost,
       *ghost_data,
       d4est_ops,
       d4est_geom,
       d4est_quad,
       &prob_vecs,
       flux_data_for_build_rhs,
       prob_vecs.rhs,
       stamm_rhs_fcn,
       &stamm_params
      );

    init_vec_with_elem_data
      (
       p4est,
       prob_vecs.u
      );

    
    d4est_poisson_apply_aij(p4est, *ghost, *ghost_data, &prob_vecs, flux_data_for_apply_lhs, d4est_ops, d4est_geom, d4est_quad);

    DEBUG_PRINT_ARR_DBL_SUM(prob_vecs.Au, local_nodes);
    DEBUG_PRINT_ARR_DBL_SUM(prob_vecs.u, local_nodes);
    DEBUG_PRINT_ARR_DBL(prob_vecs.u, local_nodes);
    
    int checksum = p4est_checksum(p4est);
    printf("p4est checksum = %d\n", checksum);
    
    /* d4est_solver_cg_params_t params; */
    /* d4est_solver_cg_input */
    /*   ( */
    /*    p4est, */
    /*    input_file, */
    /*    "d4est_solver_cg", */
    /*    "[D4EST_SOLVER_CG]", */
    /*    &params */
    /*   ); */


    /* krylov_petsc_params_t krylov_petsc_params; */
    /* krylov_petsc_input(p4est, input_file, "krylov_petsc", "[KRYLOV_PETSC]", &krylov_petsc_params); */

    /* krylov_petsc_solve */
    /*   ( */
    /*    p4est, */
    /*    &prob_vecs, */
    /*    &prob_fcns, */
    /*    ghost, */
    /*    ghost_data,  */
    /*    d4est_ops, */
    /*    d4est_geom, */
    /*    d4est_quad, */
    /*    &krylov_petsc_params, */
    /*    NULL */
    /*   ); */

  }

  printf("[D4EST_INFO]: Starting garbage collection...\n");
  d4est_amr_destroy(d4est_amr);
  d4est_amr_destroy(d4est_amr_uniform);
  d4est_mesh_geometry_storage_destroy(geometric_factors);
  d4est_poisson_flux_destroy(flux_data_for_apply_lhs);  
  d4est_poisson_flux_destroy(flux_data_for_build_rhs);  
  d4est_quadrature_destroy(p4est, d4est_ops, d4est_geom, d4est_quad);
  /*  */
  P4EST_FREE(error);
  P4EST_FREE(u_analytic);
}