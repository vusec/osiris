#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "stats.h"
#include "stats-chi2.h"
#include "stats-student-t.h"

#define LENGTH(array) (sizeof((array)) / sizeof((array)[0]))

double chi2_1df_p(double chi2) {
	int imax, imid, imin;
	struct chi2_1df_table *tmax, *tmin;

	/* binary search */
	imin = 0;
	imax = LENGTH(chi2_1df_table) - 1;
	if (chi2 < chi2_1df_table[imin].chi2) return chi2_1df_table[imin].p;
	if (chi2 > chi2_1df_table[imax].chi2) return chi2_1df_table[imax].p;
	
	while (imin + 1 < imax) {
		imid = (imin + imax) / 2;
		if (chi2_1df_table[imid].chi2 < chi2) {
			imin = imid;
		} else {
			imax = imid;
		}
	}

	/* linear interpolation */
	tmin = &chi2_1df_table[imin];
	tmax = &chi2_1df_table[imax];
	if (tmax->chi2 == tmin->chi2) return tmin->p;
	return tmin->p + (chi2 - tmin->chi2) * (tmax->p - tmin->p) /
		(tmax->chi2 - tmin->chi2);
}

double student_t_2t_p(double t, double df) {
	int inmax, inmid, inmin;
	int itmax, itmid, itmin;
	double nfac, tfac;
	int nmax, nmin;
	struct student_t_table *tmax, *tmin;

	assert(df >= 1);

	/* two-tailed */
	if (t < 0) t = -t;

	/* binary search for n */
	inmin = 0;
	inmax = LENGTH(student_t_table_df) - 1;
	while (inmin + 1 < inmax) {
		inmid = (inmin + inmax) / 2;
		if (student_t_table_df[inmid] < df) {
			inmin = inmid;
		} else {
			inmax = inmid;
		}
	}

	/* binary search for t */
	itmin = 0;
	itmax = LENGTH(student_t_table) - 1;	
	while (itmin + 1 < itmax) {
		itmid = (itmin + itmax) / 2;
		if (student_t_table[itmid].t < t) {
			itmin = itmid;
		} else {
			itmax = itmid;
		}
	}

	/* linear interpolation */
	nmin = student_t_table_df[inmin];
	nmax = student_t_table_df[inmax];
	nfac = (nmax == nmin) ? 0 : (df - nmin) / (nmax - nmin);
	tmin = &student_t_table[itmin];
	tmax = &student_t_table[itmax];
	tfac = (tmax->t == tmin->t) ? 0 : (t - tmin->t) / (tmax->t - tmin->t);
	return (1 - nfac) * (1 - tfac) * tmin->p[inmin] +
		(1 - nfac) * tfac * tmax->p[inmin] +
		nfac * (1 - tfac) * tmin->p[inmax] +
		nfac * tfac * tmax->p[inmax];
}

void stat_aggregate_double_add(struct stat_aggregate_double *stats,
	double value) {
	assert(stats);
	stats->n++;
	stats->sum += value;
	stats->sum2 += value * value;
}

double stat_aggregate_double_avg(const struct stat_aggregate_double *stats) {
	assert(stats);
	if (stats->n > 0) {
		return stats->sum / stats->n;
	} else {
		return NAN;
	}
}

double stat_aggregate_double_stdev(const struct stat_aggregate_double *stats) {
	assert(stats);
	return sqrt(stat_aggregate_double_var(stats));
}

double stat_aggregate_double_var(const struct stat_aggregate_double *stats) {
	assert(stats);
	if (stats->n > 1) {
		return (stats->sum2 - stats->sum * stats->sum / stats->n) /
			(stats->n - 1);
	} else {
		return NAN;
	}
}
	
void stat_aggregate_int_add(struct stat_aggregate_int *stats,
	long long value) {
	assert(stats);
	stats->n++;
	stats->sum += value;
	stats->sum2 += value * value;
}

double stat_aggregate_int_avg(const struct stat_aggregate_int *stats) {
	assert(stats);
	if (stats->n > 0) {
		return (double) stats->sum / stats->n;
	} else {
		return NAN;
	}
}

double stat_aggregate_int_stdev(const struct stat_aggregate_int *stats) {
	assert(stats);
	return sqrt(stat_aggregate_int_var(stats));
}

double stat_aggregate_int_var(const struct stat_aggregate_int *stats) {
	assert(stats);
	if (stats->n > 1) {
		return (stats->sum2 - stats->sum * stats->sum /
			(double) stats->n) / (stats->n - 1);
	} else {
		return NAN;
	}
}

void stat_aggregate_zscore_add(struct stat_aggregate_zscore *stats,
	double value, const struct stat_aggregate_double *stats_golden) {
	double stdev, zscore;
	int zscore_int;

	assert(stats);
	assert(stats_golden);

	if (stats_golden->n <= 1) {
		fprintf(stderr, "warning: not enough golden runs to compare "
			"against for Z-score\n");
		return;
	}

	stdev = stat_aggregate_double_stdev(stats_golden);
	if (stdev == 0) {
		fprintf(stderr, "warning: cannot compute Z-score "
			"with golden run stdev=0\n");
		return;
	}

	zscore = (value - stat_aggregate_double_avg(stats_golden)) / stdev;
	if (zscore < 0) {
		zscore_int = (int) ceil(zscore);
	} else {
		zscore_int = (int) floor(zscore);
	}
	if (zscore_int < -ZSCORE_MAX) zscore_int = -ZSCORE_MAX;
	if (zscore_int > ZSCORE_MAX) zscore_int = ZSCORE_MAX;
	stats->bins[zscore_int + ZSCORE_MAX]++;
}

int stat_aggregate_zscore_is_zero(struct stat_aggregate_zscore *stats) {
	int z;

	assert(stats);

	for (z = -ZSCORE_MAX; z <= ZSCORE_MAX; z++) {
		if (stats->bins[z + ZSCORE_MAX]) return 0;
	}
	return 1;
}

