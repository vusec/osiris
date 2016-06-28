#include "ltckpt_types.h"
#include "ltckpt_recover.h"
#include "ltckpt_common.h"

int suicide_on_window_close = 0;
int sa_window__is_open = 0;

int inside_trusted_compute_base = 1;

window_shutter g_shutter_board[LTCKPT_MAX_WINDOW_SHUTTERS] = {};
char g_module_name[LTCKPT_MODULE_NAME_SIZE] = "none";
int wshutter_prof_curr_bb_in = 0;

// dummy value. Will be initialized to a good value by recovery.so instrumentation.
UINT64_T g_num_window_shutters = 0;
UINT64_T g_num_kernelcall_shutters = 0;

unsigned int g_recovery_bitmask = LTCKPT_RECOVERY_MASK_IDEMPOTENT;
unsigned int g_recovery_naive_mode = LTCKPT_RECOVERY_NAIVE_MODE_DEFAULT;
const char *g_recovery_bitmask_reason;
uint64_t g_recovery_bitmask_callsites[RECOVERY_BITMASK_CALLSITE_COUNT];
int g_recovery_bitmask_callsite_count;
uint64_t g_recovery_bitmask_total_count;

// inwindow / outside window counters for bbclone evaluation
 uint64_t g_counter_inwindow = 0;
 uint64_t g_counter_outsidewindow = 0;
unsigned long long g_num_window_opens = 0;

// suicide count 
uint64_t g_num_suicides = 0;

#ifdef __MINIX
#include <minix/com.h>
#include <minix/ipc.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <signal.h>
#include <stddef.h>
#include <sys/errno.h>
#include <sys/reboot.h>

int ltckpt_send_reply(message msg);

void ltckpt_dump_glo_prof_data()
{
  ltckpt_printf("Window profiling data for : %s\n", g_module_name);
  ltckpt_printf("RWINDOW: bb_in: %llu, bb_out: %llu, bb_end: %llu \n", \
		wprof.glo.rwindow.bb_in, wprof.glo.rwindow.bb_out, \
		wprof.glo.rwindow.end);
  ltckpt_printf("Num window opens: %llu\n\n", g_num_window_opens);
}

void ltckpt_dump_shutter_board()
{
	ltckpt_printf("%s :=\tDeterministic Faults enabled? %d \n", g_module_name, suicide_on_window_close);
	ltckpt_printf("Suicide switchboard status: \t (size: %llu)\n", g_num_window_shutters);

	assert(g_num_window_shutters <= LTCKPT_SHUTTER_BOARD_IPC_END);

	for (unsigned i=0; i <= LTCKPT_SHUTTER_BOARD_IPC_END + g_num_kernelcall_shutters; i++)
	{
		// skip to the start of kernelcall switches
		if( i == g_num_window_shutters)	i = LTCKPT_SHUTTER_BOARD_IPC_END + 1;

		wshutter_prof_t *p_prof = &g_shutter_board[i].prof;

		ltckpt_printf("\tswitch at site: %u\t ==> %u \t num transits: %llu ", i, g_shutter_board[i].do_suicide, p_prof->num_end);

		if (wprof_enable_stats)
		{
			ltckpt_printf("\tbb_in: %llu \t wshutter_prof_curr_bb_in: %d", p_prof->bb_in, wshutter_prof_curr_bb_in);
		}
		ltckpt_printf("%s", " \n");
	}
	ltckpt_printf("%s", " \n");
return;
}

void ltckpt_show_benboard_faults()
{
	ltckpt_printf("Number of faults injected: %llu\n", g_num_suicides);
	return;
}

int sef_fi_custom()
{
#if defined(LTRC_SUICIDE_PERIODICALLY) || defined(LTRC_SUICIDE_ONLY_ONCE)
ltckpt_printf("%s: Enabled crash-on-window-close.\n", g_module_name);
suicide_on_window_close = 1;
#endif

#ifdef LTRC_SUICIDE_ONLY_ONCE
	ltckpt_dump_shutter_board();
#endif

#ifdef TOGGLE_ENABLE_STATS
		wprof_enable_stats = wprof_enable_stats ? 0 : 1;
		if (!wprof_enable_stats) {
		    ltckpt_dump_glo_prof_data();
   		 }
#endif
	return 0;
}

void inc_counter_inwindow()
{
  // assuming no multi-threaded use
  g_counter_inwindow++;
}

void inc_counter_outsidewindow()
{
  // assuming no multi-threaded use
  g_counter_outsidewindow++;
}

/*+++ Window managers +++*/

// Would be hooked in the sef_handle_message() function
// NOTE: Never call this function by hand.
void ltckpt_detect_start_of_window_hook()
{
	extern int have_handled_message;
	if (!prior_have_handled_message && have_handled_message)
	{
		// entering the window
		sa_window__is_open = 1;
		g_recovery_bitmask = LTCKPT_RECOVERY_MASK_IDEMPOTENT;
		g_recovery_bitmask_reason = NULL;
		g_recovery_bitmask_callsite_count = 0;
		g_recovery_bitmask_total_count = 0;

		inside_trusted_compute_base = 0;

		if(wprof_enable_stats)
		{
			wshutter_prof_curr_bb_in = 0;
			g_num_window_opens++;
		}
	}
	else if(prior_have_handled_message && !have_handled_message)
	{
		//leaving the window
		sa_window__is_open = 0;
	}

	prior_have_handled_message = have_handled_message;
	return;
}

__attribute__((always_inline))
int ltckpt_close_r_window(UINT64_T site_id)
{
	extern int have_handled_message;
	int sa_have_handled_message = have_handled_message;
	if (sa_have_handled_message && sa_window__is_open)
	{
		// currently window is open, so we can close the window.
		sa_window__is_open = 0;

		if(wprof_enable_stats)
		{
			wprof.glo.rwindow.end++;
		}
		return 1; // successfully closed the window.
	}
	return 0; // not closing the window.
}

__attribute__((always_inline))
int ltckpt_transit_window(UINT64_T site_id)
{
	CHECK_SHUTTER_BOARD_SIZE(site_id, 0)
	g_shutter_board[(unsigned)site_id].prof.num_end++;
	if (wprof_enable_stats)
	{
		if (0 == g_shutter_board[(unsigned)site_id].prof.bb_in)
		{
			g_shutter_board[(unsigned)site_id].prof.bb_in = wshutter_prof_curr_bb_in;
		}
	}
	// ltckpt_printf("Window transition @ site %llu \n", site_id);
	return 0;
}

/*++++++++++++++++++++++++++++++
 Deterministic fault injection
 ---------------------------- */

extern int sef_init_done, have_handled_message;


void ltckpt_do_suicide()
{

#ifdef LTRC_SUICIDE_ONLY_ONCE
	suicide_on_window_close = 0;
#endif

	if (inside_trusted_compute_base)
	{
		return;
	}

	if(g_recovery_bitmask != LTCKPT_RECOVERY_MASK_IDEMPOTENT) {
		ltckpt_printf("%s: %s: Skipping crash - not within recovery window.\n", g_module_name, __func__);
		return;
	}

  ltckpt_printf("%s: !!SUICIDE!!\n", g_module_name);
	ltckpt_printf("\t%s: suicide replyable: %d\n", __func__, ltckpt_is_message_replyable(NULL, NULL));
	ltckpt_printf("\t%s: have handled message: %d\n", __func__, have_handled_message);
	ltckpt_printf("\t%s: suicide sef_init: %d\n", __func__, sef_init_done);
	ltckpt_printf("\t%s: in tcb: %d\n", __func__, inside_trusted_compute_base);
	g_num_suicides++;

	volatile char *p = 0;
	(*p)++;
}

#if LTRC_SUICIDE_PERIODICALLY
static struct bens_shutter_board {
	int transits;
	int period;
#define BEN_BOARD 1000
} board[BEN_BOARD];
#endif

int ltckpt_do_suicide_per_site(UINT64_T site_id)
{
       message last_handled_msg;
       int replyable;
	CHECK_SHUTTER_BOARD_SIZE(site_id, 0)
	unsigned index = (unsigned) site_id;

        extern int __attribute__((weak)) SEF_GET_CURRENT_MESSAGE(message *mptr, int *replyable);

	if (inside_trusted_compute_base)
	{
		return 0;
	}

	if(!sef_init_done)
	{
		ltckpt_printf("\tSkipping suicide (SEF init not done) (forcing TCB to 1!).\n");
		inside_trusted_compute_base = 1;
		return 0;
	}

        if (!SEF_GET_CURRENT_MESSAGE)
        {
        	// ltckpt_printf("ltckpt_do_suicide_per_site: sef_get_current_message() not available.\n");
        	return 0;
        }

	if (sa_window__is_open && g_shutter_board[index].do_suicide > 0 && suicide_on_window_close)

	{
	        if (0 == SEF_GET_CURRENT_MESSAGE(&last_handled_msg, &replyable))
		{
			// ltckpt_printf("ltckpt_do_suicide_per_site: Cannot crash. Unable to fetch last handled message.\n");
			return 0;
		}

		if(last_handled_msg.m_source == RS_PROC_NR) {
			// ltckpt_printf("ltckpt_do_suicide_per_site: Cannot crash. Handling a message from RS.\n");
			return 0;
		}

		// ltckpt_printf("ltckpt_do_suicide_per_site: RS ready: %d.\n", sys_privctl_get_rs_ready());

		if (!sys_privctl_get_rs_ready())
		{
			// ltckpt_printf("ltckpt_do_suicide_per_site: Cannot crash. RS not ready.\n");
			return 0;
		}

#ifdef LTRC_SUICIDE_WHEN_RECOVERABLE
		if (1 != ltckpt_is_message_replyable(NULL, NULL))
		{
			// ltckpt_printf("\tsite_id: %s:%llu\t unrecoverable suicide point (unreplyable, 0x%x from %d). Skipping.\n", g_module_name, site_id, last_handled_msg.m_type, last_handled_msg.m_source);
			return 0;
		}
#endif
//		g_shutter_board[index].do_suicide--;

#if LTRC_SUICIDE_PERIODICALLY
		/* Suicide periodically. */
		board[index].transits++;

		if(board[index].period < 1) {
			board[index].period = 1;
		}

		if(board[index].transits % board[index].period) {
			// ltckpt_printf("ltckpt_do_suicide_per_site: Skipping crash, transit %d out of %d\n",
			// 	board[index].transits, board[index].period);
			return 0;
		}

		ltckpt_printf("\n%s: site_id: %llu\t committing suicide! [transits: %d period: %d]\n",
			g_module_name, site_id,
			board[index].transits, board[index].period);
		board[index].period++;
		board[index].period *= 3;
		board[index].period /= 2;
		board[index].transits = 0;
#else
		/* Only suicide once. */
		g_shutter_board[index].do_suicide = 0;
#endif
		ltckpt_do_suicide();
	}
	return 0;
}

/*++++++++++++++++++++++++++++++
 hooks for recovery policies
 ---------------------------- */

void ltckpt_set_idempotent_recovery(UINT64_T site_id)
{
	if (inside_trusted_compute_base)
		return;
	// We shouldn't close the window here!
	ltckpt_transit_window(site_id);
	g_recovery_bitmask |= LTCKPT_RECOVERY_MASK_IDEMPOTENT;
	return;
}

void ltckpt_set_request_specific_recovery(UINT64_T site_id)
{
	if (inside_trusted_compute_base)
		return;
	// We shouldn't close the window here!
	ltckpt_transit_window(site_id);
	g_recovery_bitmask |= LTCKPT_RECOVERY_MASK_IDEMPOTENT;
	return;
}

void ltckpt_set_process_local_recovery(UINT64_T site_id)
{
	if (inside_trusted_compute_base)
		return;
	// We shouldn't close the window here!
	ltckpt_transit_window(site_id);
	g_recovery_bitmask |= LTCKPT_RECOVERY_MASK_PROCESS_SPECIFIC;
	return;
}

void ltckpt_add_site_id(UINT64_T site_id)
{
	int i;

	for (i = 0; i < g_recovery_bitmask_callsite_count; i++) {
		if (g_recovery_bitmask_callsites[i] == site_id) return;
	}
	if (g_recovery_bitmask_callsite_count < RECOVERY_BITMASK_CALLSITE_COUNT) {
		g_recovery_bitmask_callsites[g_recovery_bitmask_callsite_count++] = site_id;
	}
}

void ltckpt_set_fail_stop_recovery(UINT64_T site_id)
{
	if (inside_trusted_compute_base)
		return;
	ltckpt_transit_window(site_id);
	ltckpt_close_r_window(site_id);
	g_recovery_bitmask |= LTCKPT_RECOVERY_MASK_FAIL_STOP;
	g_recovery_bitmask_reason = "ltckpt recovery: window closed: ltckpt_set_fail_stop_recovery";
	ltckpt_add_site_id(site_id);
	g_recovery_bitmask_total_count++;
	return;
}

void ltckpt_set_naive_recovery(uint32_t mode)
{
	g_recovery_naive_mode = mode;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++
 hooks for recovery dispatcher and recovery actions
 ----------------------------------------------- */

char *hypermem_cat(char *p, char *pend, const char *s)
{
	ptrdiff_t avail;
	size_t len;

	if (pend <= p) return p;
	avail = pend - p;

	len = strlen(s);
	if (len >= avail) len = avail - 1;

	memcpy(p, s, len);
	p += len;
	*p = 0;
	return p;
}

char *hypermem_cat_num(char *p, char *pend, long long n)
{
	char buf[20], *s = buf + sizeof(buf);
	int neg = n < 0;

	if (neg) n = -n;

	*--s = 0;
	do {
		*--s = (n % 10) + '0';
		n /= 10;
	} while (n > 0);

	if (neg) *--s = '-';

	return hypermem_cat(p, pend, s);
}

void hypermem_log(const char *msg)
{
	char buf[1024];
	char *p = buf;
	char *pend = buf + sizeof(buf);

	p = hypermem_cat(p, pend, msg);
	p = hypermem_cat(p, pend, "; module=");
	p = hypermem_cat(p, pend, g_module_name);
	hypermem_printstr(buf);
}

static void hypermem_log_callsites(void) {
	char buf[1024];
	int i;
	char *p = buf;
	char *pend = buf + sizeof(buf);

	p = hypermem_cat(p, pend, "ltckpt recovery: window closed ");
	p = hypermem_cat_num(p, pend, g_recovery_bitmask_total_count);
	p = hypermem_cat(p, pend, " times from ");
	p = hypermem_cat_num(p, pend, g_recovery_bitmask_callsite_count);
	p = hypermem_cat(p, pend, " callsite(s): ");
	for (i = 0; i < g_recovery_bitmask_callsite_count; i++) {
		if (i > 0) p = hypermem_cat(p, pend, ", ");
		p = hypermem_cat_num(p, pend, g_recovery_bitmask_callsites[i]);
	}
	hypermem_log(buf);
}

static void hypermem_log_nonreplyable(int msgtype, int r) {
	char buf[1024];
	char *p = buf;
	char *pend = buf + sizeof(buf);

	p = hypermem_cat(p, pend, "ltckpt recovery: ");
	p = hypermem_cat(p, pend, (r == LTCKPT_RECOVERY_SUCCESS) ? "success" : "failed");
	p = hypermem_cat(p, pend, ": non-replyable message: ");
	p = hypermem_cat_num(p, pend, msgtype);
	hypermem_log(buf);
}

int ltckpt_recovery_dispatcher(void *info)
{
	int r = LTCKPT_RECOVERY_FAILURE;
	int replyable;
	int rs_fi_flag = 0;
	int msgtype = 0;

	hypermem_log("ltckpt recovery: start recovery attempt");
	ltckpt_printf("%s: ltckpt_recovery_dispatcher\n", g_module_name);

	inside_trusted_compute_base = 1;

	ltckpt_printf("\t%s: bitmask value: 0x%x\n", "recovery dispatcher", g_recovery_bitmask);
	ltckpt_printf("\t%s: naive mode: %u\n", "recovery dispatcher", g_recovery_naive_mode);
        ltckpt_printf("\t%s: suicide replyable: %d\n", "recovery dispatcher", ltckpt_is_message_replyable(NULL, NULL));
        ltckpt_printf("\t%s: in tcb: %d\n", "recovery dispatcher", inside_trusted_compute_base);
	ltckpt_printf("\t%s: have handled message: %d\n", "recovery dispatcher", have_handled_message);

	replyable = ltckpt_is_message_replyable(&rs_fi_flag, &msgtype);
#ifdef LTRC_NO_RECOVERY_ON_SVC_FI
	if (rs_fi_flag)
	{
		hypermem_log("ltckpt recovery: success: RS_FI_CUSTOM");
		ltckpt_printf("RS_FI_CUSTOM request. No recovery is necessary\n");
		return LTCKPT_RECOVERY_SUCCESS;
	}
#endif
	switch (g_recovery_naive_mode) {
	case LTCKPT_RECOVERY_NAIVE_MODE_NEVER_REPLY: replyable = 0; break;
	case LTCKPT_RECOVERY_NAIVE_MODE_ALWAYS_REPLY: replyable = 1; break;
	}
	switch(replyable)
	{
		case 1:
			if (g_recovery_bitmask & LTCKPT_RECOVERY_MASK_FAIL_STOP)
			{
				hypermem_log(g_recovery_bitmask_reason ? : "ltckpt recovery: window closed: reason unknown");
				hypermem_log_callsites();
				r = ltckpt_recovery_fail_stop(info);
			}
			else if (g_recovery_bitmask & LTCKPT_RECOVERY_MASK_PROCESS_SPECIFIC)
			{
				r = ltckpt_recovery_process_specific(info);
			}
			else if (g_recovery_bitmask & LTCKPT_RECOVERY_MASK_REQUEST_SPECIFIC)
			{
				r = ltckpt_recovery_request_specific(info);
			}
			else if (g_recovery_bitmask & LTCKPT_RECOVERY_MASK_IDEMPOTENT)
			{
				r = ltckpt_recovery_idempotent(info);
			}
			else
			{
				// we shouldn't ever get here.
				hypermem_log("ltckpt recovery: failed: invalid g_recovery_bitmask bitmask");
				ltckpt_printf("%s : Error!! Ran out of options in recovery_dispatcher.\n", "recovery dispatcher");
			}
			break;

		case 0:
			hypermem_log_nonreplyable(msgtype, r);
			ltckpt_printf("%s : non-replyable message\n", "recovery dispatcher");
			break;

		case -1:
			hypermem_log("ltckpt recovery: failed: havent handled message");
			ltckpt_printf("%s : havent handled message \n", "recovery dispatcher");
			break;

		default:
			hypermem_log("ltckpt recovery: failed: bad ltckpt_is_message_replyable result");
			break;
	}
	if (g_recovery_naive_mode != LTCKPT_RECOVERY_NAIVE_MODE_DEFAULT) {
		r = LTCKPT_RECOVERY_SUCCESS;
	}
	if (r == LTCKPT_RECOVERY_FAILURE)
	{
		hypermem_log("ltckpt recovery: shutdown");
		ltckpt_printf_error("Recovery failed. Resetting the system!\n");
		sys_abort(RB_NOSYNC);
	}
	return r;
}

int ltckpt_recovery_idempotent(void *info)
{
  ltckpt_printf("\tidempotent recovery\n");

  	message last_handled_msg;
	int replyable;
	extern int __attribute__((weak)) SEF_GET_CURRENT_MESSAGE(message *mptr, int *replyable);

	if (!SEF_GET_CURRENT_MESSAGE)
	{
	    hypermem_log("ltckpt recovery: failed: idempotent: sef_get_current_message() not available");
		ltckpt_printf("%s : sef_get_current_message() not available.\n", "ltckpt_recovery_idempotent");
		return LTCKPT_RECOVERY_FAILURE;
	}
	if (0 == SEF_GET_CURRENT_MESSAGE(&last_handled_msg, &replyable))
	{
		hypermem_log("ltckpt recovery: failed: idempotent: unable to fetch last handled message");
		ltckpt_printf("%s : Cannot recover. Unable to fetch last handled message.\n", "ltckpt_recovery actions");
		return LTCKPT_RECOVERY_FAILURE; // We cannot do recovery
	}

 #ifdef LTRC_REHANDLE_REQUEST

  if ((RS_PROC_NR == last_handled_msg.m_source) && (last_handled_msg.m_type == COMMON_REQ_FI_CTL))
  {
  	ltckpt_printf("%s: %s : %s => %s\n", g_module_name, "ltckpt_recovery actions", "idempotent", "service fi - not rehandling the request.");
  	return LTCKPT_RECOVERY_SUCCESS;
  }

  ltckpt_printf("%s: %s : %s => %s\n", g_module_name, "ltckpt_recovery actions", "idempotent", "Re-processing the last received message.");
  ltckpt_activate_req_rehandling();

 #else


        if(!replyable && g_recovery_naive_mode != LTCKPT_RECOVERY_NAIVE_MODE_ALWAYS_REPLY) {
		hypermem_log("ltckpt recovery: failed: idempotent: unreplyable request");
        	ltckpt_printf_error("%s : unreplyable request from %d\n", "ltckpt_recovery_idempotent", last_handled_msg.m_source);
        	return LTCKPT_RECOVERY_FAILURE;
        }

    ltckpt_printf("%s: %s : %s => %s\n", g_module_name, "ltckpt_recovery actions", "idempotent", "Send error to requestor.");
    if (NOT_OK == ltckpt_send_reply(last_handled_msg))
    {
	hypermem_log("ltckpt recovery: failed: idempotent: couldn't send error message");
    	ltckpt_printf_error("%s: %s : Couldn't send error message to source: %d\n", g_module_name, "ltckpt_recovery_idempotent", last_handled_msg.m_source);
    	return LTCKPT_RECOVERY_FAILURE;
    }
 #endif

  hypermem_log("ltckpt recovery: success: idempotent");
  return LTCKPT_RECOVERY_SUCCESS;
}

int ltckpt_recovery_request_specific(void *info)
{
	ltckpt_printf("%s : Request specific recovery - same as idempotent recovery.\n", "ltckpt_recovery_request_specific");
 	return ltckpt_recovery_idempotent(info);
}

int ltckpt_recovery_process_specific(void *info)
{
	int replyable = 0;	// just a placeholder; not used here
	endpoint_t source;
	message last_handled_msg;

	ltckpt_printf("%s : %s\n", "ltckpt_recovery actions", "process specific");

	extern int __attribute__((weak)) SEF_LLVM_IS_USR_ENDPOINT(endpoint_t);
	extern int __attribute__((weak)) SEF_GET_CURRENT_MESSAGE(message *mptr, int *replyable);

	if ((!SEF_GET_CURRENT_MESSAGE) || (!SEF_LLVM_IS_USR_ENDPOINT))
	{
		hypermem_log("ltckpt recovery: failed: process_specific: unable to find necessary sef_llvm method");
		ltckpt_printf_error("%s : Unable to find necessary sef_llvm method.\n", "ltckpt_recovery actions");
		return LTCKPT_RECOVERY_FAILURE;
	}
	if (0 == SEF_GET_CURRENT_MESSAGE(&last_handled_msg, &replyable))
	{
		hypermem_log("ltckpt recovery: failed: process_specific: unable to fetch last handled message");
		ltckpt_printf("%s : Cannot recover. Unable to fetch last handled message.\n", "ltckpt_recovery actions");
		return LTCKPT_RECOVERY_FAILURE; // We cannot do recovery
	}

	source = last_handled_msg.m_source;
	int ret = SEF_LLVM_IS_USR_ENDPOINT(source);
	if (1 == ret)
	{
		// kill the user process
		int r = sys_kill(source, SIGKILL);
		if (r == OK)
		{
			hypermem_log("ltckpt recovery: success: killed user process");
			ltckpt_printf("%s : Killed user process: %d\n", "ltckpt_recovery actions", NR_TASKS + _ENDPOINT_P(source));
		}
		else if (r == EINVAL)
		{
			hypermem_log("ltckpt recovery: success: user process is invalid");
			ltckpt_printf("%s : User process: %d is invalid.\n", "ltckpt_recovery actions", NR_TASKS + _ENDPOINT_P(source));
		}
		else
		{
			hypermem_log("ltckpt recovery: failed: process_specific: unable to kill user process");
			ltckpt_printf_error("%s : Unable to kill user process: %d [return code: %d]\n", "ltckpt_recovery actions", source, r);
			return LTCKPT_RECOVERY_FAILURE;
		}
	}
	else if (0 == ret)
	{
		// send back a reply to the source to indicate an error condition.
		if (NOT_OK == ltckpt_send_reply(last_handled_msg))
		{
			hypermem_log("ltckpt recovery: failed: process_specific: unable to send reply");
			ltckpt_printf_error("%s : unable to send reply to %d\n", "ltckpt_recovery actions", last_handled_msg.m_source);
			return LTCKPT_RECOVERY_FAILURE;
		}
	}
	else
	{
		hypermem_log("ltckpt recovery: failed: process_specific: error in figuring out whether source was user process");
		ltckpt_printf_error("%s : Error in figuring out whether source (%d) was user process.\n", "ltckpt_recovery actions", source);
		return LTCKPT_RECOVERY_FAILURE;
	}

  return LTCKPT_RECOVERY_SUCCESS;
}

int ltckpt_recovery_fail_stop(void *info)
{
	hypermem_log("ltckpt recovery: shutdown: fail_stop");
	ltckpt_printf("%s : %s\n", "ltckpt_recovery actions", "fail stop");
	ltckpt_printf("%s : %s\n", "ltckpt_recovery actions", "Consistent recovery not possible. Killing the system.");
 	int r = sys_abort(RB_NOSYNC); // reset the machine.
	if (r != OK)
	{
		ltckpt_printf_error("%s : WARNING!!! System is inconsistent and abort failed!\n", "ltckpt_recovery actions");
		return LTCKPT_RECOVERY_FAILURE;
	}
	return LTCKPT_RECOVERY_SUCCESS; // unreachable code.
}

__attribute__((always_inline))
int ltckpt_is_req_rehandling()
{
	return g_recovery_bitmask & LTCKPT_RECOVERY_REPROCESSING_REQ;
}

__attribute__((always_inline))
void ltckpt_activate_req_rehandling()
{
	ltckpt_printf("exec: ltckpt_activate_req_rehandling ON\n");
	g_recovery_bitmask |= LTCKPT_RECOVERY_REPROCESSING_REQ;
}

__attribute__((always_inline))
void ltckpt_deactivate_req_rehandling()
{
	ltckpt_printf("in ltckpt_deactivate_req_rehandling()\n");
	g_recovery_bitmask &= ~LTCKPT_RECOVERY_REPROCESSING_REQ;
}

__attribute__((always_inline))
int ltckpt_is_message_replyable(int *rs_fi_flag, int *type)
{
	message last_handled_msg;
	int replyable;
	extern int __attribute__((weak)) SEF_GET_CURRENT_MESSAGE(message *mptr, int *replyable);

	if (!SEF_GET_CURRENT_MESSAGE)
	{
		return NOT_OK;
	}
	if (0 == SEF_GET_CURRENT_MESSAGE(&last_handled_msg, &replyable))
	{
		return NOT_OK;
	}
	if ((NULL != rs_fi_flag)
		&& (RS_PROC_NR == last_handled_msg.m_source)
		&& (last_handled_msg.m_type == COMMON_REQ_FI_CTL))
	{
		*rs_fi_flag = 1;
	}
	if (type) *type = last_handled_msg.m_type;
	return replyable;
}

__attribute__((always_inline))
int ltckpt_send_reply(message msg)
{
	msg.m_type = ERESTART;
	assert(msg.m_type < 0);

	ltckpt_printf("Sending reply to : %d\n", msg.m_source);
	// int r = ipc_sendnb(msg.m_source, &msg);
	// use asynsend3 instead as discussed on 31-Jul-2015
	int r = asynsend(msg.m_source, &msg); // Don't set AMF_NOREPLY as this is a reply message
	if (r != OK)
	{
		ltckpt_printf_error("%s : unable to send reply to %d: %d\n", "ltckpt_send_reply", msg.m_source, r);
		return NOT_OK;
	}
	return OK;
}
#endif
