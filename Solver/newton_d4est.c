#include "../pXest/pXest.h"
#include "../LinearAlgebra/linalg.h"
#include "../Utilities/util.h"
#include "../Solver/matrix_sym_tester.h"
#include "sc_reduce.h"

typedef struct {

  double rtol;
  double atol;
  int imax;
  int imin;
  
} newton_d4est_params_t;


static
int newton_d4est_input_handler
(
 void* user,
 const char* section,
 const char* name,
 const char* value
)
{
  newton_d4est_params_t* pconfig = (newton_d4est_params_t*)user;
  
  if (util_match_couple(section,"newton",name,"atol")) {
    mpi_assert(pconfig->atol == -1);
    pconfig->atol = atof(value);
  }
  else if (util_match_couple(section,"newton",name,"rtol")) {
    mpi_assert(pconfig->rtol == -1);
    pconfig->rtol = atof(value);
  }
  else if (util_match_couple(section,"newton",name,"imax")) {
    mpi_assert(pconfig->imax == -1);
    pconfig->imax = atoi(value);
  }
  else if (util_match_couple(section,"newton",name,"imin")) {
    mpi_assert(pconfig->imin == -1);
    pconfig->imin = atoi(value);
  }
  else {
    return 0;  /* unknown section/name, error */
  }
  return 1;  
}


newton_d4est_params_t
newton_d4est_input
(
 const char* input_file
)
{
  newton_d4est_params_t input;
  input.atol = -1;
  input.rtol = -1;
  input.imin = -1;
  input.imax = -1;
  
  if (ini_parse(input_file, newton_d4est_input_handler, &input) < 0) {
    mpi_abort("Can't load input file");
  }

  D4EST_CHECK_INPUT("newton", input.atol, -1);
  D4EST_CHECK_INPUT("newton", input.rtol, -1);
  D4EST_CHECK_INPUT("newton", input.imin, -1);
  D4EST_CHECK_INPUT("newton", input.imax, -1);
  
  return input;
}

typedef
void
(*krylov_solver_fcn_t)
(
 p4est_t*,
 problem_data_t*,
 weakeqn_ptrs_t*,
 double*,
 int
);


/** 
 * Make sure Jacobian has zeroed boundary conditions and build residual has normal 
 * boundary condtions
 * 
 * @param p4est 
 * @param vecs 
 * @param fcns 
 * @param params 
 * @param ghost 
 * @param ghost_data 
 * @param dgmath_jit_dbase 
 * @param krylov_solve 
 * 
 * @return 
 */
int
newton_d4est_solve
(
 p4est_t* p4est,
 problem_data_t* vecs,
 void* fcns,
 p4est_ghost_t** ghost,
 void** ghost_data, 
 dgmath_jit_dbase_t* dgmath_jit_dbase,
 d4est_geometry_t* d4est_geom,
 const char* input_file,
 krylov_pc_t* krylov_pc
)
{
  int ierr = 0;
  int local_nodes = vecs->local_nodes;
  int n = local_nodes;
  int krylov_final_iter;
  double krylov_final_fnrm;
  problem_data_t vecs_for_linsolve;
  problem_data_t vecs_for_res_build;


  double* xt = P4EST_ALLOC(double, local_nodes);
  double* ft = P4EST_ALLOC(double, local_nodes);
  double* f0 = P4EST_ALLOC(double, local_nodes);
  double* step = P4EST_ALLOC(double, local_nodes);
  double* Jstep = P4EST_ALLOC(double, local_nodes);
  
  /* these don't change */
  double* x = vecs->u;
  problem_data_copy_ptrs(vecs, &vecs_for_linsolve);
  problem_data_copy_ptrs(vecs, &vecs_for_res_build);
  
  /******** external parameters ********/
  newton_d4est_params_t* nr_params = newton_d4est_input(input_file);
  
  double atol = nr_params->atol;
  double rtol = nr_params->rtol;
  int maxit = nr_params->imaxer;
  int minit = nr_params->iminer;
  
  vecs_for_res_build.rhs = vecs->rhs;
  vecs_for_res_build.Au = f0;
  vecs_for_res_build.u = x;
  
  /* build initial residual vector */
  fcns->build_residual(p4est, *ghost, *ghost_data, &vecs_for_res_build, dgmath_jit_dbase, d4est_geom);
  
  double fnrm = linalg_vec_dot(f0,f0,n);
  double fnrm_global;
  
  sc_allreduce
    (
     &fnrm,
     &fnrm_global,
     1,
     sc_MPI_DOUBLE,
     sc_MPI_SUM, 
     sc_MPI_COMM_WORLD
    );

  fnrm = sqrt(fnrm_global);
  /* printf("[NEWTON_SOLVER]: INITIAL FNRM = %f\n", fnrm); */
  
  double fnrmo = 1.;
  double stop_tol = atol + rtol*fnrm;
  int itc = 0;

  while((fnrm > stop_tol || itc < minit) && (itc < maxit)){

    /* double ratio = fnrm/fnrmo; */
    fnrmo = fnrm;
    itc++;
    linalg_vec_scale(-1., f0, n);
    
    vecs_for_linsolve.u0 = x;
    vecs_for_linsolve.rhs = f0;
    vecs_for_linsolve.u = step;
    vecs_for_linsolve.Au = Jstep;

    /* set initial guess */
    linalg_fill_vec(vecs_for_linsolve.u, 0., n);


    krylov_solve
      (
       p4est,
       &vecs_for_linsolve,
       fcns,
       ghost,
       ghost_data,
       dgmath_jit_dbase,
       d4est_geom,
       input_file,
       krylov_pc
      );
    
    /* xt = x + lambda*step */
    linalg_vec_axpyeqz(1.0, step, x, xt, n);

    /* calculate new residual vector */
    vecs_for_res_build.u = xt;
    vecs_for_res_build.Au = ft;
    fcns->build_residual(p4est, *ghost, *ghost_data, &vecs_for_res_build, dgmath_jit_dbase, d4est_geom);    

    /* flip pointers */
    double* tmp = x;
    x = xt;
    xt = tmp;
    tmp = f0;
    f0 = ft;
    ft = tmp;

    /* calculate new residual norm */
    fnrm = linalg_vec_dot(f0,f0,n);

    sc_allreduce
      (
       &fnrm,
       &fnrm_global,
       1,
       sc_MPI_DOUBLE,
       sc_MPI_SUM,
       sc_MPI_COMM_WORLD
      );

    fnrm = sqrt(fnrm_global);

    if (p4est->mpi_rank == 0 && nr_params->monitor)
      printf("[NEWTON_D4EST]: ITER %03d PRE-FNRM %.30f POST-FNRM %.30f INNER ITER %d INNER FNRM %.30f\n" ,itc, fnrmo,  fnrm, krylov_final_iter, krylov_final_fnrm);
    
    if (krylov_final_iter == 0){
      sc_MPI_Barrier(sc_MPI_COMM_WORLD);
      break;
    }
  }
  
 /* clean: */
  nr_params->final_iter = itc;
  nr_params->final_fnrm = fnrm;

  if (fnrm > stop_tol && ierr == 0){
    ierr = 1;
  }

  if(vecs->u != x){
    /* vecs->u = x; */
    linalg_copy_1st_to_2nd(x, vecs->u, vecs->local_nodes);
    P4EST_FREE(x);
  }
  else {
    P4EST_FREE(xt);
  }

  P4EST_FREE(Jstep);
  P4EST_FREE(step);
  P4EST_FREE(f0);
  P4EST_FREE(ft);

  return ierr;
}
