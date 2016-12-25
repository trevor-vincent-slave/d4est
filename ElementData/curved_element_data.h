#ifndef CURVED_ELEMENT_DATA_H
#define CURVED_ELEMENT_DATA_H 

#include "../dGMath/dgmath.h"
#include "../GridFunctions/grid_functions.h"
#include <d4est_geometry.h>

typedef struct {

  /* double* J; */
  double* J_integ;
  double* xyz;
  double* xyz_integ;
  /* double* xyz_rst; */
  double* xyz_rst_integ;
  double* xyz_rst_Lobatto_integ;
  /* double* rst_xyz; */
  double* rst_xyz_integ;
  double* invM;
  /* double* invMface; */
  
} geometric_factors_t;

typedef struct {

  /* identification */
  int id;
  int mpi_rank;
  
  int sqr_nodal_stride;
  int sqr_trace_stride;
  int nodal_stride;
  int integ_stride;
  
  int tree;
  p4est_qcoord_t q [(P4EST_DIM)];
  p4est_qcoord_t dq;
  
  /* auxiliary node vector aliases */
  double *Au_elem;

  /* Storage aliases for trace space calculations */
  double *M_ustar_min_u_n [(P4EST_DIM)];
  double *M_qstar_min_q_dot_n;

  /* geometric factors */
  /* double* J; /\* Jacobian *\/ */
  double* J_integ; /* Jacobian */
  double* invM;
  /* double* invMface [P4EST_FACES]; */
  double* xyz [(P4EST_DIM)]; /* collocation points on physical grid */
  double* xyz_integ [(P4EST_DIM)]; /* collocation points on physical grid */
  /* double* xyz_rst[(P4EST_DIM)][(P4EST_DIM)]; /\* mapping derivatives *\/ */
  double* xyz_rst_integ[(P4EST_DIM)][(P4EST_DIM)]; /* mapping derivatives */
  double* xyz_rst_Lobatto_integ[(P4EST_DIM)][(P4EST_DIM)]; /* mapping derivatives */
  /* double* rst_xyz[(P4EST_DIM)][(P4EST_DIM)]; /\* inverse mapping derivatives *\/ */
  double* rst_xyz_integ[(P4EST_DIM)][(P4EST_DIM)]; /* inverse mapping derivatives */
  double diam; /* approximate value of element diameter*/

  double volume;
  double surface_area [(P4EST_FACES)];
  
  /* aposteriori/apriori error indicator for hp_amr or h_amr */
  double local_estimator;
  double local_predictor;
  
  /* storage for MPI transfers */
  double u_elem[MAX_NODES];
  double du_elem[(P4EST_DIM)][MAX_NODES];
  double q_elem[(P4EST_DIM)][MAX_NODES];
  
  /* nodal degree */
  int deg;
  int deg_integ;
  
  
  /* useful flag for debugging */
  int debug_flag;
  int on_bdry;
  
} curved_element_data_t;

void
curved_element_data_init_node_vec
(
 p4est_t* p4est,
 double* node_vec,
 grid_fcn_t init_fcn,
 dgmath_jit_dbase_t* dgmath_jit_dbase
);

void
curved_element_data_apply_Mij_on_vec
(
 p4est_t* p4est,
 double* u,
 double* Mu,
 dgmath_jit_dbase_t* dgmath_jit_dbase
);

/* void */
/* curved_element_data_init */
/* ( */
/*  p4est_t* p4est, */
/*  geometric_factors_t* geometric_factors, */
/*  dgmath_jit_dbase_t* dgmath_jit_dbase, */
/*  p4est_geometry_t* p4est_geometry, */
/*  int deg, */
/*  /\* int* deg_integ_diff, *\/ */
/*  int deg_integ, */
/*  integ_type_t integ_type */
/* ); */

void
curved_element_data_init
(
 p4est_t* p4est,
 geometric_factors_t* geometric_factors,
 dgmath_jit_dbase_t* dgmath_jit_dbase,
 d4est_geometry_t* d4est_geometry,
 int deg,
 int deg_integ
);

int
curved_element_data_get_local_nodes
(
 p4est_t* p4est
);


double
curved_element_data_compute_l2_norm_sqr_no_local
(
 p4est_t* p4est,
 double* vec,
 int local_nodes,
 dgmath_jit_dbase_t* dgmath_jit_dbase
);

double
curved_element_data_compute_l2_norm_sqr
(
 p4est_t* p4est,
 double* nodal_vec,
 int local_nodes,
 dgmath_jit_dbase_t* dgmath_jit_dbase
);

void
curved_element_data_print_node_vec
(
 p4est_t* p4est,
 double* vec
);

void
curved_element_data_destroy
(
 p4est_t* p4est
);

void
curved_element_data_copy_from_storage_to_vec
(
 p4est_t* p4est,
 double* vec
);

void
curved_element_data_store_local_estimator_in_corner_array
(
 p4est_t* p4est,
 double* est_corner
);

void
curved_element_data_copy_from_vec_to_storage(
 p4est_t* p4est,
 double* vec
);

void
curved_element_data_store_nodal_vec_in_vertex_array
(
 p4est_t* p4est,
 double* nodal_vec,
 double* corner_vec
);


void
curved_element_data_store_element_scalar_in_vertex_array
(
 p4est_t* p4est,
 double* est_corner,
 double (*get_local_scalar_fcn)(curved_element_data_t*)
);


geometric_factors_t*
geometric_factors_init
(
 p4est_t* p4est
);

void
geometric_factors_reinit
(
 p4est_t* p4est,
 geometric_factors_t* geometric_factors,
 int init_deg,
 int local_nodes,
 int local_nodes_integ,
 int local_sqr_nodes,
 int local_sqr_trace_nodes
);

void
geometric_factors_destroy
(
 geometric_factors_t* geometric_factors
);

void
curved_element_data_debug_print_node_vecs
(
 p4est_t* p4est,
 double** vecs,
 int num_vecs,
 int* elems,
 int num_elems
);

void
curved_element_data_debug_spheresym
(
 p4est_t* p4est,
 dgmath_jit_dbase_t* dgbase,
 double* vec
);

void
curved_element_data_reorient_f_p_elements_to_f_m_order
(
 curved_element_data_t** e_p,
 int face_dim,
 int f_m,
 int f_p,
 int o,
 int faces_p,
 curved_element_data_t* e_p_oriented [(P4EST_HALF)]
);

void
curved_element_data_compute_mortar_normal_and_sj_using_face_data
(
 curved_element_data_t** e,
 int num_faces_side,
 int num_faces_mortar,
 int* deg_mortar,
 int face_side,
 dxdr_method_t dxdr_method,
 int interp_to_Gauss,
 double* n [(P4EST_DIM)],
 double* sj,
 d4est_geometry_t* d4est_geom,
 dgmath_jit_dbase_t* dgmath_jit_dbase,
 double* xyz [(P4EST_DIM)]
);

void
curved_element_data_compute_surface_jacobian_and_normal
(
 double* rst_xyz [(P4EST_DIM)][(P4EST_DIM)],
 double* J,
 double* n [(P4EST_DIM)],
 double* sj,
 int dim,
 int face,
 int deg,
 dgmath_jit_dbase_t* dgmath_jit_dbase
);

int
curved_element_data_debug_find_node
(
 p4est_t* p4est,
 int node
);


void
curved_element_data_compute_mortar_normal_and_sj_using_face_data_at_Gauss_nodes
(
 curved_element_data_t** e,
 int num_faces_side,
 int num_faces_mortar,
 int* deg_mortar_Lobatto,
 int* deg_mortar_Gauss,
 int face_side,
 double* n [(P4EST_DIM)],
 double* sj,
 p4est_geometry_t* p4est_geom,
 dgmath_jit_dbase_t* dgmath_jit_dbase
);

curved_element_data_t*
curved_element_data_get_element_data
(
 p4est_t* p4est,
 int local_element_id
);

void
curved_element_data_print_element_data_debug
(
 p4est_t* p4est
);

int
curved_element_data_count_boundary_elements
(
 p4est_t* p4est
);

int
curved_element_data_global_node_to_local_node
(
 p4est_t* p4est,
 int global_node
);

void
curved_element_data_set_degrees
(
 p4est_t* p4est,
 int (*set_deg_fcn)(int, int, int)
);

/* void */
/* curved_element_data_compute_mortar_normal_and_sj_using_face_data_at_Gauss_nodes_using_interp_cheap */
/* ( */
/*  curved_element_data_t** e_m, */
/*  int num_faces_side, */
/*  int num_faces_mortar, */
/*  int* deg_mortar_Lobatto, */
/*  int* deg_mortar_Gauss, */
/*  int face_side, */
/*  double* n [(P4EST_DIM)], */
/*  double* sj, */
/*  p4est_geometry_t* p4est_geom, */
/*  dgmath_jit_dbase_t* dgmath_jit_dbase */
/* ); */


void
curved_element_data_compute_mortar_normal_and_sj_using_face_data_at_Gauss_nodes_no_interp_usingJ
(
 curved_element_data_t** e,
 int num_faces_side,
 int num_faces_mortar,
 int* deg_mortar_Gauss,
 int face_side,
 double* n [(P4EST_DIM)],
 double* sj,
 d4est_geometry_t* d4est_geom,
 dgmath_jit_dbase_t* dgmath_jit_dbase
);

void
curved_element_data_compute_mortar_normal_and_sj_using_face_data_at_Gauss_nodes_no_interp
(
 curved_element_data_t** e,
 int num_faces_side,
 int num_faces_mortar,
 int* deg_mortar_Gauss,
 int face_side,
 double* n [(P4EST_DIM)],
 double* sj,
 d4est_geometry_t* d4est_geom,
 dgmath_jit_dbase_t* dgmath_jit_dbase,
 int store_xyz,
 double* xyz_on_mortar_Gauss [(P4EST_DIM)]
);


void
curved_element_data_compute_J_and_rst_xyz
(
 double* dxyz_drst [(P4EST_DIM)][(P4EST_DIM)],
 double* jac,
 double* drst_dxyz [(P4EST_DIM)][(P4EST_DIM)],
 int volume_nodes
);

void
curved_element_data_compute_mortar_normal_and_sj_using_face_data_at_Gauss_nodes_usingJ_Lobatto
(
 curved_element_data_t** e,
 int num_faces_side,
 int num_faces_mortar,
 int* deg_mortar_Gauss,
 int face_side,
 double* n [(P4EST_DIM)],
 double* sj,
 d4est_geometry_t* d4est_geom,
 dgmath_jit_dbase_t* dgmath_jit_dbase
);


void
curved_element_data_compute_dxyz_drst
(
 dgmath_jit_dbase_t* dgmath_jit_dbase,
 p4est_qcoord_t q0 [(P4EST_DIM)],
 p4est_qcoord_t dq,
 int which_tree,
 d4est_geometry_t* d4est_geom,
 dxdr_method_t dxdr_method,
 int deg,
 int interp_to_Gauss,
 double* dxyz_drst [(P4EST_DIM)][(P4EST_DIM)],
 double* xyz [(P4EST_DIM)]
);


void
curved_element_data_compute_surface_jacobian_and_normal_from_rst_xyz
(
 double* dxyz_drst [(P4EST_DIM)][(P4EST_DIM)],
 double* n [(P4EST_DIM)],
 double* sj,
 int face,
 int deg,
 dgmath_jit_dbase_t* dgmath_jit_dbase
);


void
curved_element_data_compute_surface_jacobian_and_normal_from_rst_xyz_interp_to_Gauss
(
 double* dxyz_drst [(P4EST_DIM)][(P4EST_DIM)],
 double* n [(P4EST_DIM)],
 double* sj,
 int face,
 int deg,
 dgmath_jit_dbase_t* dgmath_jit_dbase
);

#endif