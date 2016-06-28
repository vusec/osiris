#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "debug.h"
#include "helper.h"
#include "logparse.h"

#define PATTERN_PARAM_COUNT_MAX 4

struct logfilestate {
	const struct logparse_callbacks *callbacks;
	FILE *logfile;
	const char *logpath;
	struct timeval *timestamp_last;
	int lineno;
	int c;
	char *linebuf;
	size_t linebuf_size;
};

static int next_char(struct logfilestate *state) {
	assert(state);
	state->c = fgetc(state->logfile);
	if (state->c == '\n') state->lineno++;
	return state->c;
}

static int read_char(struct logfilestate *state, char c) {
	assert(state);

	if (state->c != c) return 0;
	next_char(state);
	return 1;
}

static int read_digit(struct logfilestate *state, int *value) {

	assert(state);
	assert(value);

	if (state->c < '0' || state->c > '9') return 0;
	*value = *value * 10 + state->c - '0';
	next_char(state);
	return 1;
}

static int read_char_or_digit(struct logfilestate *state, char c, int *value) {
	assert(state);
	assert(value);

	return read_char(state, c) || read_digit(state, value);
}

static char *read_line(struct logfilestate *state) {
	size_t linelen;

	linelen = 0;
	while (state->c != '\n' && state->c != EOF) {
		if (linelen + 1 >= state->linebuf_size) {
			if (state->linebuf_size > 0) {
				state->linebuf_size *= 2;
			} else {
				state->linebuf_size = 256;
			}
			state->linebuf = REALLOC(state->linebuf, state->linebuf_size, char);
		}
		state->linebuf[linelen++] = state->c;
		next_char(state);
	}
	state->linebuf[linelen] = 0;

	return state->linebuf;
}

static void skip_line(struct logfilestate *state) {
	assert(state);

	while (state->c != '\n' && state->c != EOF) {
		next_char(state);
	}
}

static int logparse_timestamp(
	struct logfilestate *state,
	struct timeval *timestamp) {
	int year = 0, month = 0;
	int microseconds = 0;
	struct tm tm;

	assert(state);
	assert(timestamp);

	memset(timestamp, 0, sizeof(*timestamp));
	memset(&tm, 0, sizeof(tm));
	if (read_char(state, '[') &&
		read_digit(state, &year) &&
		read_digit(state, &year) &&
		read_digit(state, &year) &&
		read_digit(state, &year) &&
		read_char(state, '-') &&
		read_digit(state, &month) &&
		read_digit(state, &month) &&
		read_char(state, '-') &&
		read_digit(state, &tm.tm_mday) &&
		read_digit(state, &tm.tm_mday) &&
		read_char(state, ' ') &&
		read_char_or_digit(state, ' ', &tm.tm_hour) &&
		read_digit(state, &tm.tm_hour) &&
		read_char(state, ':') &&
		read_digit(state, &tm.tm_min) &&
		read_digit(state, &tm.tm_min) &&
		read_char(state, ':') &&
		read_digit(state, &tm.tm_sec) &&
		read_digit(state, &tm.tm_sec) &&
		read_char(state, '.') &&
		read_digit(state, &microseconds) &&
		read_digit(state, &microseconds) &&
		read_digit(state, &microseconds) &&
		read_digit(state, &microseconds) &&
		read_digit(state, &microseconds) &&
		read_digit(state, &microseconds) &&
		read_char(state, ']') &&
		read_char(state, ' ')) {
		tm.tm_year =  year - 1900;
		tm.tm_mon = month - 1;
		timestamp->tv_sec = mktime(&tm);
		timestamp->tv_usec = microseconds;
		return 1;
	} else {
		return 0;
	}
}

struct logparse_line_handler_param {
	const char *str;
	long num;
	size_t len;
};

static void logparse_line_handler_qemu_drive(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 2);
	assert(params);
	assert(!params[0].str);
	assert(params[1].str);

	if (state->callbacks->qemu_drive)
		state->callbacks->qemu_drive(cbstate,
			params[0].num,
			params[1].str, params[1].len);
}

static void logparse_line_handler_edfi_context_release(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->edfi_context_release)
		state->callbacks->edfi_context_release(cbstate,
			params[0].str, params[0].len);
}

static void logparse_line_handler_edfi_context_reset(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->edfi_context_reset)
		state->callbacks->edfi_context_reset(cbstate,
			params[0].str, params[0].len);
}

static void logparse_line_handler_edfi_context_set(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->edfi_context_set)
		state->callbacks->edfi_context_set(cbstate,
			params[0].str, params[0].len);
}

static void logparse_line_handler_edfi_dump_stats(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count >= 0);
	assert(param_count <= 1);
	assert(param_count < 1 || params);
	assert(param_count < 1 || params[0].str);

	if (state->callbacks->edfi_dump_stats)
		state->callbacks->edfi_dump_stats(cbstate,
			(param_count >= 1 ) ? params[0].str : NULL,
			(param_count >= 1 ) ? params[0].len : 0);
}

static int read_execcounts_from_string_internal(const char *s, execcount *bbs,
	size_t *bb_count_p) {
	size_t bb_count = 0;
	execcount count = 0;
	unsigned repeats;

	assert(s);
	assert(bb_count_p);

	/* Reads a line of run-length encoded execution counts.
	 * Each specification is either "count" or "count x repeats"
	 * (without spaces). Specifications are separated by spaces.
	 */
	while (*s) {
		while (*s == ' ') s++;
		if (*s < '0' || *s > '9') break;

		count = 0;
		while (*s >= '0' && *s <= '9') {
			count = 10 * count + *(s++) - '0';
		}
		if (*s == 'x') {
			s++;
			repeats = 0;
			while (*s >= '0' && *s <= '9') {
				repeats = 10 * repeats + *(s++) - '0';
			}
		} else {
			repeats = 1;
		}
		if (repeats <= 0) {
			*bb_count_p = 0;
			return 0;
		}
		while (repeats-- > 0) {
			if (bbs) *(bbs++) = count;
			bb_count++;
		}
	}
	if (*s) {
		*bb_count_p = 0;
		return 0;
	}
	*bb_count_p = bb_count;
	return 1;
}

static int read_execcounts_from_string(const char *s, execcount **bbs_p,
	size_t *bb_count_p) {

	assert(s);
	assert(bbs_p);
	assert(bb_count_p);

	/* count number of basic blocks */
	if (!read_execcounts_from_string_internal(s, NULL, bb_count_p)) {
		*bbs_p = NULL;
		return 0;
	}
	dbgprintf_v("number of basic blocks: %ld\n", (long) *bb_count_p);

	/* allocate result buffer and store execution counts */
	*bbs_p = MALLOC(*bb_count_p, execcount);
	read_execcounts_from_string_internal(s, *bbs_p, bb_count_p);
	return 1;
}

static void logparse_line_handler_edfi_dump_stats_module(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {
	execcount *bbs;
	size_t bb_count;

	assert(state);
	assert(param_count >= 2);
	assert(param_count <= 3);
	assert(params);
	assert(params[0].str);
	assert(params[1].str);
	assert(param_count < 3 || params[2].str);

	if (!state->callbacks->edfi_dump_stats_module) return;

	if (!read_execcounts_from_string(params[param_count - 1].str,
		&bbs, &bb_count)) {
		fprintf(stderr, "warning: invalid execution count list on "
			"line %d of log file %s\n", state->lineno,
			state->logpath);
		return;
	}
	state->callbacks->edfi_dump_stats_module(cbstate,
		params[0].str,
		params[0].len,
		(param_count >= 3) ? params[1].str : NULL,
		(param_count >= 3) ? params[1].len : 0,
		bbs, bb_count);
	FREE(bbs);
}

static void logparse_line_handler_edfi_dump_stats_module_no_context(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count >= 1);
	assert(param_count <= 2);
	assert(params);
	assert(params[0].str);
	assert(param_count < 2 || params[1].str);

	if (state->callbacks->edfi_dump_stats_module) return;
		state->callbacks->edfi_dump_stats_module(cbstate,
			params[0].str,
			params[0].len,
			(param_count >= 2) ? params[1].str : NULL,
			(param_count >= 2) ? params[1].len : 0,
			NULL,
			0);
}

static void logparse_line_handler_edfi_faultindex_get(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count >= 1);
	assert(param_count <= 2);
	assert(params);
	assert(params[0].str);
	assert(!params[1].str);

	if (state->callbacks->edfi_faultindex_get)
		state->callbacks->edfi_faultindex_get(cbstate,
			params[0].str, params[0].len,
			(param_count < 2) ? -1 : params[1].num);
}

static void logparse_line_handler_fault(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count >= 2);
	assert(param_count <= 3);
	assert(params);
	assert(params[0].str);
	assert(!params[1].str);
	assert(param_count < 3 || !params[2].str);

	if (state->callbacks->fault)
		state->callbacks->fault(cbstate,
			params[0].str, params[0].len,
			params[1].num,
			(param_count < 3) ? 1 : params[2].num);
}

static void logparse_line_handler_hypermem_error(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->hypermem_error)
		state->callbacks->hypermem_error(cbstate,
			params[0].str, params[0].len);
	else
		fprintf(stderr, "hypermem error: %.*s\n",
			(int) params[0].len, params[0].str);
}

static void logparse_line_handler_hypermem_faultspec(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->hypermem_faultspec)
		state->callbacks->hypermem_faultspec(cbstate,
			params[0].str, params[0].len);
}

static void logparse_line_handler_hypermem_flushlog_false(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->hypermem_flushlog)
		state->callbacks->hypermem_flushlog(cbstate, 0);
}

static void logparse_line_handler_hypermem_flushlog_true(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->hypermem_flushlog)
		state->callbacks->hypermem_flushlog(cbstate, 1);
}

static void logparse_line_handler_hypermem_logpath(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->hypermem_logpath)
		state->callbacks->hypermem_logpath(cbstate,
			params[0].str, params[0].len);
}

static void logparse_line_handler_hypermem_warning(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->hypermem_warning)
		state->callbacks->hypermem_warning(cbstate,
			params[0].str, params[0].len);
	else
		fprintf(stderr, "hypermem warning: %.*s\n",
			(int) params[0].len, params[0].str);
}

static void logparse_line_handler_nop(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->nop)
		state->callbacks->nop(cbstate);
}

static void logparse_line_handler_print_abort_after_restart(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 0);

	if (state->callbacks->print_abort_after_restart)
		state->callbacks->print_abort_after_restart(cbstate);
}

static void logparse_line_handler_print_debug(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_debug)
		state->callbacks->print_debug(cbstate,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_end(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 0);

	if (state->callbacks->print_end)
		state->callbacks->print_end(cbstate);
}

static void logparse_line_handler_print_ltckpt_closed_ipc(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt_closed)
		state->callbacks->print_ltckpt_closed(cbstate, lcr_ipc,
			params[0].str, params[0].len);
}

static void parse_ipc_callsites(
	const struct logfilestate *state,
	const char *str,
	size_t strlen,
	uint64_t **callsites_p,
	size_t *callsitecount_p) {
	uint64_t id;
	const char *strend;
	uint64_t *callsites;
	size_t callsitecount;

	assert(strlen == 0 || str);
	assert(callsites_p);
	assert(callsitecount_p);

	callsites = *callsites_p = CALLOC((strlen + 1) / 2, uint64_t);
	callsitecount = 0;

	strend = str + strlen;
	for (;;) {
		/* skip whitespace */
		while (str < strend && *str == ' ') str++;

		/* parse id */
		if (str >= strend || *str < '0' || *str > '9') {
			fprintf(stderr, "warning: no callsite id on line %d "
				"of log file %s\n", state->lineno, state->logpath);
			break;
		}
		id = 0;
		do {
			id = id * 10 + (*str - '0');
			str++;
		} while (str < strend && *str >= '0' && *str <= '9');
		callsites[callsitecount++] = id;

		/* skip whitespace */
		while (str < strend && *str == ' ') str++;

		/* if there's a comma, more are coming */
		if (str >= strend) break;
		if (*str != ',') {
			fprintf(stderr, "warning: missing comma in callsite id "
				"list on line %d of log file %s\n",
				state->lineno, state->logpath);
			break;
		}
		str++;
	}

	*callsitecount_p = callsitecount;
}

static void logparse_line_handler_print_ltckpt_closed_ipc_callsites(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {
	uint64_t *callsites;
	size_t callsitecount;

	assert(state);
	assert(cbstate);
	assert(param_count == 4);
	assert(params);
	assert(!params[0].str);
	assert(!params[1].str);
	assert(params[2].str);
	assert(params[3].str);

	if (!state->callbacks->print_ltckpt_closed_ipc_callsites) return;

	parse_ipc_callsites(state, params[2].str, params[2].len,
		&callsites, &callsitecount);
	state->callbacks->print_ltckpt_closed_ipc_callsites(cbstate,
		params[3].str, params[3].len,
		params[0].num, params[1].num,
		callsites, callsitecount);
	FREE(callsites);
}

static void logparse_line_handler_print_ltckpt_closed_unknown(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt_closed)
		state->callbacks->print_ltckpt_closed(cbstate, lcr_unknown,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_closed_writelog_full(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt_closed)
		state->callbacks->print_ltckpt_closed(cbstate, lcr_writelog_full,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_failed(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_failure, lm_generic,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_failed_message(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 2);
	assert(params);
	assert(!params[0].str);
	assert(params[1].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_failure, lm_generic,
			params[1].str, params[1].len);
}

static void logparse_line_handler_print_ltckpt_failed_idempotent(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_failure, lm_idempotent,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_failed_process_specific(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_failure, lm_process_specific,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_failed_writelog(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_failure,
			lm_writelog, params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_shutdown(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_shutdown, lm_generic,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_shutdown_failstop(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_shutdown, lm_failstop,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_start(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_start, lm_generic,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_start_writelog(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_start, lm_writelog,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_success_idempotent(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_success, lm_idempotent,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_success_process_specific(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_success, lm_process_specific,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_success_service_fi(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_success, lm_service_fi,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_ltckpt_success_writelog(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(cbstate);
	assert(param_count == 3);
	assert(params);
	assert(!params[0].str);
	assert(!params[1].str);
	assert(params[2].str);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(cbstate, ls_success,
			lm_writelog, params[2].str, params[2].len);
}

static void logparse_line_handler_print_rs_crash(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 2);
	assert(params);
	assert(!params[0].str);
	assert(params[1].str);

	if (state->callbacks->print_rs_crash)
		state->callbacks->print_rs_crash(cbstate,
			params[1].str, params[1].len,
			params[0].num);
}

static void logparse_line_handler_print_rs_normal(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_rs_normal)
		state->callbacks->print_rs_normal(cbstate,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_rs_shutdown_deadlock(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->print_ltckpt)
		state->callbacks->print_ltckpt(
			cbstate, ls_shutdown, lm_deadlock, "rs", 2);
}

static void logparse_line_handler_print_sanity_pass_pre(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 2);
	assert(params);
	assert(params[0].str);
	assert(params[1].str);

	if (state->callbacks->print_sanity)
		state->callbacks->print_sanity(cbstate, 1, 0);
}

static void logparse_line_handler_print_sanity_pass_post(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 2);
	assert(params);
	assert(params[0].str);
	assert(params[1].str);

	if (state->callbacks->print_sanity)
		state->callbacks->print_sanity(cbstate, 1, 1);
}

static void logparse_line_handler_print_sanity_fail_pre(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 2);
	assert(params);
	assert(params[0].str);
	assert(params[1].str);

	if (state->callbacks->print_sanity)
		state->callbacks->print_sanity(cbstate, 0, 0);
}

static void logparse_line_handler_print_sanity_fail_post(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 2);
	assert(params);
	assert(params[0].str);
	assert(params[1].str);

	if (state->callbacks->print_sanity)
		state->callbacks->print_sanity(cbstate, 0, 1);
}

static void logparse_line_handler_print_test_failed(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_test)
		state->callbacks->print_test(cbstate,
			params[0].str, params[0].len, mts_failed);
}

static void logparse_line_handler_print_test_output(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 2);
	assert(params);
	assert(params[0].str);
	assert(params[1].str);

	if (state->callbacks->print_test_output)
		state->callbacks->print_test_output(cbstate,
			params[0].str, params[0].len,
			params[1].str, params[1].len);
}

static void logparse_line_handler_print_test_passed(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_test)
		state->callbacks->print_test(cbstate,
			params[0].str, params[0].len, mts_passed);
}

static void logparse_line_handler_print_test_running(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_test)
		state->callbacks->print_test(cbstate,
			params[0].str, params[0].len, mts_running);
}

static void logparse_line_handler_print_tests_completed(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->print_tests_completed)
		state->callbacks->print_tests_completed(cbstate, NULL, 0);
}

static void logparse_line_handler_print_tests_completed_failed(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_tests_completed)
		state->callbacks->print_tests_completed(cbstate,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_tests_starting(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_tests_starting)
		state->callbacks->print_tests_starting(cbstate,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_start(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->print_start)
		state->callbacks->print_start(cbstate);
}

static void logparse_line_handler_print_startup(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_startup)
		state->callbacks->print_startup(cbstate,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_workload(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(params[0].str);

	if (state->callbacks->print_workload)
		state->callbacks->print_workload(cbstate,
			params[0].str, params[0].len);
}

static void logparse_line_handler_print_workload_completed(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->print_workload_completed)
		state->callbacks->print_workload_completed(cbstate);
}

static void logparse_line_handler_qemu_exit(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->qemu_exit)
		state->callbacks->qemu_exit(cbstate);
}

static void logparse_line_handler_qemu_hypermem_reset(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->qemu_hypermem_reset)
		state->callbacks->qemu_hypermem_reset(cbstate);
}

static void logparse_line_handler_qemu_interrupt(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(!params[0].str);

	if (state->callbacks->qemu_interrupt)
		state->callbacks->qemu_interrupt(cbstate, params[0].num);
}

static void logparse_line_handler_qemu_powerdown(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->qemu_powerdown)
		state->callbacks->qemu_powerdown(cbstate);
}

static void logparse_line_handler_qemu_reset(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->qemu_reset)
		state->callbacks->qemu_reset(cbstate);
}

static void logparse_line_handler_qemu_shutdown(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->qemu_shutdown)
		state->callbacks->qemu_shutdown(cbstate);
}

static void logparse_line_handler_qemu_signal(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 1);
	assert(params);
	assert(!params[0].str);

	if (state->callbacks->qemu_signal)
		state->callbacks->qemu_signal(cbstate, params[0].num);
}

static void logparse_line_handler_quit(
	const struct logfilestate *state,
	const struct logparse_callback_state *cbstate,
	const struct logparse_line_handler_param *params,
	size_t param_count) {

	assert(state);
	assert(param_count == 0);

	if (state->callbacks->quit)
		state->callbacks->quit(cbstate);
}

static const struct logparse_line_pattern {
	void (* handler)(
		const struct logfilestate *state,
		const struct logparse_callback_state *cbstate,
		const struct logparse_line_handler_param *params,
		size_t param_count);
	const char *pattern;
} logparse_line_patterns[] = {
	/* output directly from QEMU */
	{ logparse_line_handler_edfi_context_release, "EDFI context release module=*" },
	{ logparse_line_handler_edfi_context_reset, "EDFI context reset module=*" },
	{ logparse_line_handler_edfi_context_set, "EDFI context set module=*" },
	{ logparse_line_handler_edfi_dump_stats, "edfi_dump_stats msg=*" },
	{ logparse_line_handler_edfi_dump_stats, "edfi_dump_stats" },
	{ logparse_line_handler_edfi_dump_stats_module_no_context, "edfi_dump_stats_module name=* msg=* no context known" },
	{ logparse_line_handler_edfi_dump_stats_module_no_context, "edfi_dump_stats_module name=* no context known" },
	{ logparse_line_handler_edfi_dump_stats_module, "edfi_dump_stats_module name=* msg=* bbs=*" },
	{ logparse_line_handler_edfi_dump_stats_module, "edfi_dump_stats_module name=* *" },
	{ logparse_line_handler_edfi_faultindex_get, "edfi_faultindex_get name=* bbindex=#" },
	{ logparse_line_handler_edfi_faultindex_get, "edfi_faultindex_get name=* fault injection disabled" },
	{ logparse_line_handler_edfi_faultindex_get, "edfi_faultindex_get name=* not selected for fault injection" },
	{ logparse_line_handler_fault, "fault name=* bbindex=# count=#" },
	{ logparse_line_handler_fault, "fault name=* bbindex=#" },
	{ logparse_line_handler_hypermem_error, "error: *" },
	{ logparse_line_handler_hypermem_faultspec, "hypermem-faultspec=\"*\"" },
	{ logparse_line_handler_hypermem_flushlog_false, "hypermem-flushlog=false" },
	{ logparse_line_handler_hypermem_flushlog_true, "hypermem-flushlog=true" },
	{ logparse_line_handler_hypermem_logpath, "hypermem-logpath=\"*\"" },
	{ logparse_line_handler_hypermem_warning, "warning: *" },
	{ logparse_line_handler_nop, "nop" },
	{ logparse_line_handler_qemu_drive, "drive[#]={ file: \"*\" }" },
	{ logparse_line_handler_qemu_exit, "QEMU exiting" },
	{ logparse_line_handler_qemu_hypermem_reset, "QEMU hypermem reset" },
	{ logparse_line_handler_qemu_interrupt, "Interrupt #" },
	{ logparse_line_handler_qemu_powerdown, "QEMU powerdown" },
	{ logparse_line_handler_qemu_reset, "QEMU reset" },
	{ logparse_line_handler_qemu_shutdown, "QEMU shutdown" },
	{ logparse_line_handler_qemu_signal, "QEMU signal #" },
	{ logparse_line_handler_quit, "quitting QEMU" },

	/* output from MINIX (SEF) */
	{ logparse_line_handler_print_debug, "print DEBUG: *" },
	{ logparse_line_handler_print_rs_crash, "print rs-CRASH: signal [#] sent for *" },
	{ logparse_line_handler_print_rs_normal, "print rs-NORMAL: down sent for *" },
	{ logparse_line_handler_print_rs_shutdown_deadlock, "print RS: shutdown due to deadlock" },
	{ logparse_line_handler_print_startup, "print <EDFI> startup *" },

	/* output from llvm/tools/edfi/qemu_scripts */
	{ logparse_line_handler_print_end, "print <END>" }, /* workload.sh */
	{ logparse_line_handler_print_sanity_fail_post, "print Sanity Failed (*:POSTTEST) *" }, /* sanity.sh */
	{ logparse_line_handler_print_sanity_fail_pre, "print Sanity Failed (*:PRETEST) *" }, /* sanity.sh */
	{ logparse_line_handler_print_sanity_pass_post, "print Sanity Passed (*:POSTTEST) *" }, /* sanity.sh */
	{ logparse_line_handler_print_sanity_pass_pre, "print Sanity Passed (*:PRETEST) *" }, /* sanity.sh */
	{ logparse_line_handler_print_start, "print <START>" }, /* workload.sh */
	{ logparse_line_handler_print_workload, "print (*) Workload" }, /* run.sh for individual workloads */
	{ logparse_line_handler_print_workload_completed, "print Workload Completed" }, /* workload.sh */
	{ logparse_line_handler_print_workload_completed, "print Workload Completed *" }, /* workload.sh */

	/* output from prun-scripts/ltckpt-edfi-boot.sh */
	{ logparse_line_handler_print_abort_after_restart, "print Aborting tests after previous restart" },

	/* output from MINIX test set */
	{ logparse_line_handler_print_test_failed, "print Test *: failed" },
	{ logparse_line_handler_print_test_output, "print Test *: output line: *" },
	{ logparse_line_handler_print_test_passed, "print Test *: passed" },
	{ logparse_line_handler_print_test_running, "print Test *: running" },
	{ logparse_line_handler_print_tests_completed, "print Completed tests" },
	{ logparse_line_handler_print_tests_completed_failed, "print Completed tests, these failed: *" },
	{ logparse_line_handler_print_tests_starting, "print Starting tests: *" },

	/* output from LTCKPT */
	{ logparse_line_handler_print_ltckpt_closed_ipc, "print ltckpt recovery: window closed: ltckpt_set_fail_stop_recovery; module=*" },
	{ logparse_line_handler_print_ltckpt_closed_ipc_callsites, "print ltckpt recovery: window closed # times from # callsite(s): *; module=*" },
	{ logparse_line_handler_print_ltckpt_closed_unknown, "print ltckpt recovery: window closed: reason unknown; module=*" },
	{ logparse_line_handler_print_ltckpt_closed_writelog_full, "print ltckpt recovery: window closed: writelog full; module=*" },
	{ logparse_line_handler_print_ltckpt_failed, "print ltckpt recovery: failed: bad ltckpt_is_message_replyable result; module=*" },
	{ logparse_line_handler_print_ltckpt_failed, "print ltckpt recovery: failed: havent handled message; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_idempotent, "print ltckpt recovery: failed: idempotent: couldn't send error message; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_idempotent, "print ltckpt recovery: failed: idempotent: sef_get_current_message() not available; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_idempotent, "print ltckpt recovery: failed: idempotent: unable to fetch last handled message; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_idempotent, "print ltckpt recovery: failed: idempotent: unreplyable request; module=*" },
	{ logparse_line_handler_print_ltckpt_failed, "print ltckpt recovery: failed: invalid g_recovery_bitmask bitmask; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_message, "print ltckpt recovery: failed: non-replyable message: #; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_process_specific, "print ltckpt recovery: failed: process_specific: error in figuring out whether source was user process; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_process_specific, "print ltckpt recovery: failed: process_specific: unable to fetch last handled message; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_process_specific, "print ltckpt recovery: failed: process_specific: unable to find necessary sef_llvm method; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_process_specific, "print ltckpt recovery: failed: process_specific: unable to kill user process; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_process_specific, "print ltckpt recovery: failed: process_specific: unable to send reply; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_writelog, "print ltckpt recovery: failed: writelog: identity state transfer failed; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_writelog, "print ltckpt recovery: failed: writelog: sys_safecopyfrom addr failed; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_writelog, "print ltckpt recovery: failed: writelog: sys_safecopyfrom region failed; module=*" },
	{ logparse_line_handler_print_ltckpt_failed_writelog, "print ltckpt recovery: failed: writelog: sys_safecopyto failed; module=*" },
	{ logparse_line_handler_print_ltckpt_shutdown, "print ltckpt recovery: shutdown; module=*" },
	{ logparse_line_handler_print_ltckpt_shutdown_failstop, "print ltckpt recovery: shutdown: fail_stop; module=*" },
	{ logparse_line_handler_print_ltckpt_start, "print ltckpt recovery: start recovery attempt; module=*" },
	{ logparse_line_handler_print_ltckpt_start_writelog, "print ltckpt recovery: start: writelog; module=*" },
	{ logparse_line_handler_print_ltckpt_success_idempotent, "print ltckpt recovery: success: idempotent; module=*" },
	{ logparse_line_handler_print_ltckpt_success_process_specific, "print ltckpt recovery: success: killed user process; module=*" },
	{ logparse_line_handler_print_ltckpt_success_service_fi, "print ltckpt recovery: success: RS_FI_CUSTOM; module=*" },
	{ logparse_line_handler_print_ltckpt_success_process_specific, "print ltckpt recovery: success: user process is invalid; module=*" },
	{ logparse_line_handler_print_ltckpt_success_writelog, "print ltckpt recovery: success: writelog: # log entries, # stack entries; module=*" },

	{ },
};

static int convertnumchar(char c, int base, int *value) {

	assert(base >= 2);
	assert(base <= 36);
	assert(value);

	if (c >= '0' && c <= '9') {
		*value = c - '0';
	} else if (c >= 'A' && c <= 'Z') {
		*value = c - 'A' + 10;
	} else if (c >= 'a' && c <= 'z') {
		*value = c - 'a' + 10;
	} else {
		*value = -1;
		return 0;
	}
	return *value < base;
}

static int pattern_match(const char *line, const char *pattern,
	struct logparse_line_handler_param *params, size_t *param_count) {
	int base, neg;
	const char *line_curr = line;
	size_t param_index = 0;
	int value;

	assert(line);
	assert(pattern);
	assert(params);
	assert(param_count);

	memset(params, 0, *param_count * sizeof(*params));
	for (;;) {
		if (*pattern == '*') {
			/* match parameter */
			assert(param_index < *param_count);
			params[param_index].str = line_curr;
			while (*line_curr && *line_curr != pattern[1]) {
				line_curr++;
			}
			params[param_index].len = line_curr - params[param_index].str;
			param_index++;
			pattern++;
		} else if (*pattern == '#') {
			/* match number */
			assert(param_index < *param_count);
			neg = (line_curr[0] == '-');
			if (neg) line_curr++;
			if (line_curr[0] == '0' &&
				(line_curr[1] == 'x' || line_curr[1] == 'X')) {
				base = 16;
				line_curr += 2;
			} else {
				base = 10;
			}
			if (!convertnumchar(*line_curr, base, &value)) return 0;

			params[param_index].num = 0;
			do {
				params[param_index].num =
					params[param_index].num * base +
					value;
				params[param_index].len++;
				line_curr++;
			} while (convertnumchar(*line_curr, base, &value));
			if (neg) params[param_index].num = -params[param_index].num;
			param_index++;
			pattern++;
		} else if (*pattern == *line_curr) {
			/* match char */
			if (!*pattern) {
				*param_count = param_index;
				return 1;
			}
			line_curr++;
			pattern++;
		} else {
			/* no match */
			return 0;
		}
	}
}

static const struct logparse_line_pattern *patterns_match(
	struct logfilestate *state,
	const char *line,
	struct logparse_line_handler_param *params,
	size_t *param_count) {
	const struct logparse_line_pattern *pattern;

	assert(line);
	assert(params);
	assert(param_count);

	pattern = logparse_line_patterns;
	for (;;) {
		if (!pattern->handler && !pattern->pattern) {
			fprintf(stderr, "warning: line %d of log file %s "
				"not recognized: %.64s\n",
				state->lineno, state->logpath, line);
			*param_count = 0;
			return NULL;
		}
		assert(pattern->handler);
		assert(pattern->pattern);
		*param_count = PATTERN_PARAM_COUNT_MAX;
		if (pattern_match(line, pattern->pattern, params,
			param_count)) {
			return pattern;
		}
		pattern++;
	}
}

static int logparse_from_file_line(struct logfilestate *state) {
	struct logparse_callback_state cbstate;
	char *line;
	const struct logparse_line_pattern *pattern;
	struct logparse_line_handler_param params[PATTERN_PARAM_COUNT_MAX];
	size_t param_count;

	assert(state);
	assert(state->callbacks);
	assert(state->logpath);

	/* check for EOF or empty line */
	switch (state->c) {
	case EOF: return 0;
	case '\n': next_char(state); return 1;
	}

	/* initialize data for callback */
	memset(&cbstate, 0, sizeof(cbstate));
	cbstate.logpath = state->logpath;
	cbstate.lineno = state->lineno;
	cbstate.arg = state->callbacks->arg;

	/* read timestamp */
	if (!logparse_timestamp(state, &cbstate.timestamp)) {
		fprintf(stderr, "warning: invalid timestamp on line %d "
			"of log file %s\n", state->lineno, state->logpath);
		skip_line(state);
		return 1;
	}
	if (state->timestamp_last) *state->timestamp_last = cbstate.timestamp;

	/* read entire line into buffer for convenience */
	line = read_line(state);

	/* match patterns */
	pattern = patterns_match(state, line, params, &param_count);
	if (pattern) pattern->handler(state, &cbstate, params, param_count);

	return state->c != EOF;
}

static void logparse_from_file(
	const char *logpath,
	FILE *logfile,
	const struct logparse_callbacks *callbacks,
	struct timeval *timestamp_last) {
	struct logfilestate state = {
		.callbacks      = callbacks,
		.logfile        = logfile,
		.logpath        = logpath,
		.timestamp_last = timestamp_last,
		.lineno         = 1,
	};

	assert(logpath);
	assert(logfile);
	assert(callbacks);

	dbgprintf("loading log file %s\n", logpath);

	/* parse line by line */
	next_char(&state);
	while (logparse_from_file_line(&state));

	if (state.linebuf) FREE(state.linebuf);
}

void logparse_from_path(
	const char *logpath,
	const struct logparse_callbacks *callbacks,
	struct timeval *timestamp_last) {
	FILE *logfile;

	assert(logpath);
	assert(callbacks);

	if (timestamp_last) memset(timestamp_last, 0, sizeof(*timestamp_last));

	/* open log file */
	logfile = fopen(logpath, "r");
	if (!logfile) {
		fprintf(stderr, "error: could not open log file %s: %s\n",
			logpath, strerror(errno));
		exit(-1);
	}

	/* load info from open file */
	logparse_from_file(logpath, logfile, callbacks, timestamp_last);

	/* close log file */
	fclose(logfile);
}
