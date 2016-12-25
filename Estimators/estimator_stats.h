#ifndef ESTIMATOR_STATS_H
#define ESTIMATOR_STATS_H 

#include <pXest.h>

typedef struct {
  double total;
  double mean;
  double std;
  double max;
  double min;
  double median;
  double Q1;
  double Q3;
  double IQR;
  double customQ;
  double p5;
  double p10;
  double p15;
  double p20;
  
  /* bin size from scott's rule */
  double bin_size_scott;
  double bin_size_freedman;
  int num_bins_scott;
  int num_bins_freedman;
  int sample_size;
  
} estimator_stats_t;

/* This file was automatically generated.  Do not edit! */
void estimator_stats_write_to_file(p4est_t *p4est,estimator_stats_t *stats,const char *file_name_stats,const char *file_name_estimator_prefix,int curved,int mpi_rank,int print_gnuplot);
void estimator_stats_print(estimator_stats_t *stats,int mpi_rank);
double estimator_stats_get_percentile(estimator_stats_t *stats,int percentile);
void estimator_stats_compute(p4est_t *p4est,estimator_stats_t *stats,int curved);

#endif