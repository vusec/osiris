#ifndef STATS_H
#define STATS_H

#define SIGNIFICANCE_TRESHOLD 0.05

#define CHI2_EXPECTED_MIN 5

struct stat_aggregate_double {
	int n;
	double sum;
	double sum2;
};

struct stat_aggregate_int {
	int n;
	long long sum;
	long long sum2;
};

#define ZSCORE_MAX 4
struct stat_aggregate_zscore {
	int bins[2 * ZSCORE_MAX + 1];
};

double chi2_1df_p(double chi2);
double student_t_2t_p(double t, double df);
void stat_aggregate_double_add(struct stat_aggregate_double *stats,
	double value);
double stat_aggregate_double_avg(const struct stat_aggregate_double *stats);
double stat_aggregate_double_stdev(const struct stat_aggregate_double *stats);
double stat_aggregate_double_var(const struct stat_aggregate_double *stats);
void stat_aggregate_int_add(struct stat_aggregate_int *stats,
	long long value);
double stat_aggregate_int_avg(const struct stat_aggregate_int *stats);
double stat_aggregate_int_stdev(const struct stat_aggregate_int *stats);
double stat_aggregate_int_var(const struct stat_aggregate_int *stats);
void stat_aggregate_zscore_add(struct stat_aggregate_zscore *stats,
	double value, const struct stat_aggregate_double *stats_golden);
int stat_aggregate_zscore_is_zero(struct stat_aggregate_zscore *stats);

#endif
