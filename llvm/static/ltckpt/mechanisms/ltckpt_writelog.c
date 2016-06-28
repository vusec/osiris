#define LTCKPT_CHECKPOINT_METHOD undolog
#include "../ltckpt_local.h"
#include "../ltckpt_recover.h"
LTCKPT_CHECKPOINT_METHOD_ONCE();

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>



#ifndef __MINIX
#define WRITELOG_START       0
#define WRITELOG_FLAGS       0
#ifdef LTCKPT_X86_64
#define WRITELOG_MAXLEN      (1024*1024*1024*4L)
#else  /* !LTCKPT_X86_64 */
#define WRITELOG_MAXLEN      (1024*1024*128L)
#endif /* LTCKPT_X86_64 */
#define LTCKPT_RESTARTING()  0
#define LTCKPT_IS_VM()       0
#define LTCKPT_IS_RS()       0

#else /* __MINIX */
#include <minix/sef.h>
#include <minix/sysutil.h>
#include <minix/endpoint.h>
#include <minix/syslib.h>
#define WRITELOG_START       (1536*1024*1024)
#define WRITELOG_FLAGS       LTCKPT_MAP_FIXED
#define WRITELOG_MAXLEN      (1024*1024*2)

static endpoint_t i_am_ep;
static char i_am_name[8];


extern __attribute__((weak)) int   vm_is_hole(uint32_t addr, int pages);
extern __attribute__((weak)) void *vm_allocpages_at(uint32_t addr, size_t size);
extern __attribute__((weak)) int copy_rs_start(void *, ...);
#define LTCKPT_IS_VM() (vm_is_hole)
#define LTCKPT_IS_RS() (copy_rs_start)
#endif /* __MINIX */


#define WRITELOG_GRANULARITY         (8) /* the sizeof writelog entries in bytes */
#define WRITELOG_BYTES          (WRITELOG_MAXLEN*(WRITELOG_GRANULARITY+sizeof(void*)))

#ifdef WRITELOG_PER_THREAD

static __thread char *wl_data = NULL;
static __thread unsigned long wl_position;

LTCKPT_DECLARE_ATPTHREAD_CREATE_CHILD_HOOK()
{
	ltckpt_init_writelog();
}

#else

static char *wl_data = NULL;
static unsigned long wl_position;
static unsigned long wl_position_high_watermark;

#endif

#ifdef LTCKPT_ALWAYS_ON
#define LTCKPT_WRITELOG_ALWAYS_ON LTCKPT_ALWAYS_ON
#endif

#ifndef LTCKPT_WRITELOG_ALWAYS_ON
#define LTCKPT_WRITELOG_ALWAYS_ON 0
#endif

#if LTCKPT_WRITELOG_ALWAYS_ON
static const int ltckpt_writelog_enabled = 1;
#else
static int ltckpt_writelog_enabled = 0;
#endif


typedef struct region_t {
	char data[WRITELOG_GRANULARITY];
} region_t;

#ifdef __MINIX
static int LTCKPT_RESTARTING()
{
    int r;
    int priv_flags;
    int init_flags;

    r = sys_whoami(&i_am_ep, i_am_name, 8,
            &priv_flags, &init_flags);

	lt_printf("restarting: wl_data: %x.\n", (unsigned int) wl_data);

    if (ltckpt_is_mapped((void*)WRITELOG_START)) {
	lt_printf("yes mapped: wl_data: %x.\n", (unsigned int) wl_data);
	wl_data = (char *) WRITELOG_START; /* ??? - this should probably be assigned already? */
        return 1;
    }

	lt_printf("not mapped: wl_data: %x.\n", (unsigned int) wl_data);

    /* we have to special case RS here */
    if ( r != OK) {
        panic("LTCKPT_RESTARTING: sys_whoami failed: %d\n", r);
    }

    return ((priv_flags & ROOT_SYS_PROC) && i_am_ep != RS_PROC_NR);
}
#endif

static inline void ltckpt_write_wl(void * addr)
{
	/* align address to region boundary */
	addr = (void *)(LTCKPT_PTR_TO_VA(addr) & ~(WRITELOG_GRANULARITY-1));
	char *wl_data_start = (char *) LTCKPT_PTR_TO_VA(wl_data);
	char *wl_data_end = (char *) LTCKPT_PTR_TO_VA(wl_data) + WRITELOG_BYTES;

	lt_assert(wl_data);

	if (!sa_window__is_open) {
		return;
	}

#if 0
	lt_kputs("WL:");
	lt_putx((unsigned int) wl_data);
#endif

	if ( (char *) LTCKPT_PTR_TO_VA(addr) >= wl_data_start &&
			(char *) LTCKPT_PTR_TO_VA(addr) < wl_data_end)
	{
		lt_panic("Try to write into writelog!");
	}

	void **addr_slot  =    (void **) &wl_data[wl_position];
	region_t  *region = (region_t *) &wl_data[wl_position + sizeof(void*)];

	*addr_slot   = addr;
	*region      = *((region_t*) addr);

	wl_position +=  (sizeof(void*) + sizeof(region_t));

	if (wl_position >= WRITELOG_BYTES)
	{
		lt_printf("Writelog overflow at position: %lx.\n", wl_position);
		/* lt_panic("Writelog overflow."); */
		lt_printf("Closing window\n");
		sa_window__is_open = 0;
		g_recovery_bitmask |= LTCKPT_RECOVERY_MASK_FAIL_STOP;
		if (!g_recovery_bitmask_reason) {
			g_recovery_bitmask_reason = "ltckpt recovery: window closed: writelog full";
		}
	}
#ifndef __MINIX
	ltckpt_debug_print("%lx of %lx\n", wl_position, WRITELOG_BYTES);
#endif
}

LTCKPT_DECLARE_STORE_HOOK()
{
	if (ltckpt_writelog_enabled)
		ltckpt_write_wl(addr);
}

LTCKPT_DECLARE_MEMCPY_HOOK()
{
	if (!ltckpt_writelog_enabled)
		return;
	char *end = addr + size;
	region_t *reg = (region_t *)(((ltckpt_va_t)(addr)) & ~0x3);
	while ((char *)reg < end) {
		ltckpt_write_wl(reg);
		reg++;
	}
}

LTCKPT_DECLARE_TOP_OF_THE_LOOP_HOOK()
{
	CTX_NEW_LOG_SIZE(wl_position);
	CTX_NEW_TOL_OR_RETURN();

	if(wl_position > wl_position_high_watermark) {
#ifdef __MINIX
		if(!i_am_name[0]) {
			int priv_flags;
    			int init_flags;
			lt_printf("ltckpt: retrying retrieving self nmae\n");
			sys_whoami(&i_am_ep, i_am_name, 8,
		            &priv_flags, &init_flags);
		}

		wl_position_high_watermark = wl_position;
#if LTRC_LOG_WATERMARK
		lt_printf("ltckpt: %s:%d: undolog high watermark: %ld kB (%ld bytes)\n", i_am_name, i_am_ep, wl_position_high_watermark/1024, wl_position_high_watermark);
#endif
#endif
	}

	wl_position=0;
#if !LTCKPT_WRITELOG_ALWAYS_ON
	ltckpt_writelog_enabled = 1;
#endif

	CTX_NEW_CHECKPOINT();
}


static void  ltckpt_init_writelog()
{
	unsigned long flags = LTCKPT_MAP_PRIVATE | LTCKPT_MAP_NORESERVE | WRITELOG_FLAGS;
#ifdef __MINIX
	if (LTCKPT_IS_RS()) {
		/* rs may not pagefault during VM recovery */
		printf("preallocating log for RS\n");
		flags |= LTCKPT_MAP_POPULATE;
	}
#endif
	lt_printf("%s", "doing mmap..\n");
	ltckpt_va_t ret =  ltckpt_mmap(LTCKPT_PTR_TO_VA(WRITELOG_START),
	                 WRITELOG_BYTES,
	                 LTCKPT_PROT_W | LTCKPT_PROT_R, flags);
	if (ret == LTCKPT_MAP_FAILED) {
		ltckpt_panic("%s", "ltckpt: could not allocate writelog data\n");
	}
	if (ret != LTCKPT_PTR_TO_VA(WRITELOG_START)) {
		ltckpt_panic("%s", "ltckpt: wrong address returned?!\n");
	}
	lt_printf("writelog: %x.\n", ret);
	wl_data = LTCKPT_VA_TO_PTR(ret);
}

LTCKPT_DECLARE_LATE_INIT_HOOK()
{
	if (LTCKPT_IS_VM()) {
		ltckpt_debug_print("ltckpt: VM detected. Skipping generic late init hook.\n");
		return;
	}

	if (LTCKPT_RESTARTING()) {
		ltckpt_debug_print("ltckpt: We are restarting now, skipping late init hook\n");
		return;
	}
	ltckpt_init_writelog();
}

#ifdef __MINIX
void ltckpt_undolog_vm_late_init()
{
	void *ret;
	printf("ltckpt: running vm late init WML: %d entries, 0x%x bytes\n",
			WRITELOG_MAXLEN, WRITELOG_BYTES);
	if (!vm_is_hole(WRITELOG_START, 1)) {
		lt_printf("ltckpt: We are restarting now, skipping late init hook\n");
		return;
	}
	if ( (ret = vm_allocpages_at(WRITELOG_START, WRITELOG_BYTES)) != NULL ) {
		wl_data = ret;
	} else {
		lt_panic("ltckpt: vm: could not allocate writelog");
	}
}
#endif

extern int inside_trusted_compute_base, have_handled_message;

#ifdef __MINIX
static int ltckpt_overlaps(const void *p1, size_t s1, const void *p2, size_t s2) {
	const char *ps1 = p1, *pe1 = ps1 + s1;
	const char *ps2 = p2, *pe2 = ps2 + s2;
	return (ps1 <= ps2 && pe1 > ps2) || (ps2 <= ps1 && pe2 > ps1);
}

static int ltckpt_can_restore(const void *addr) {
	return !ltckpt_overlaps(addr, sizeof(region_t), wl_data, WRITELOG_BYTES) &&
		!ltckpt_overlaps(addr, sizeof(region_t), &wl_data, sizeof(wl_data)) &&
		!ltckpt_overlaps(addr, sizeof(region_t), &wl_position, sizeof(wl_position)) &&
		!ltckpt_overlaps(addr, sizeof(region_t), &ltckpt_writelog_enabled, sizeof(ltckpt_writelog_enabled));
}

LTCKPT_DECLARE_RESTART_HOOK()
{
	void *addr, *datamax;
	char buf[1024], *p = buf, *pend = buf + sizeof(buf);
	region_t *region;
	unsigned long entry_size = sizeof(void*) + sizeof(region_t);
	unsigned long i, num_entries, stack_entries;
	int r;

//        printf("%s:%d: suicide replyable: %d\n", __FILE__, __LINE__, ltckpt_is_message_replyable());
	hypermem_log("ltckpt recovery: start: writelog");
        // printf("%s:%d: have handled message: %d\n", __FILE__, __LINE__, have_handled_message);
        // printf("%s:%d: in tcb: %d\n", __FILE__, __LINE__, inside_trusted_compute_base);

	inside_trusted_compute_base = 1;

        // printf("%s:%d: in tcb: %d\n", __FILE__, __LINE__, inside_trusted_compute_base);
        // printf("%s:%d: have handled message: %d\n", __FILE__, __LINE__, have_handled_message);

#ifdef __MINIX
	/* Disable temporarily, due to the instrumented printf() below. */
	sef_init_info_t *info = (sef_init_info_t *) arg;

#if LTCKPT_RESTART_DEBUG
	printf("ltckpt_restart: disabling the log in the old process\n");
#endif
	ltckpt_writelog_enabled=0;
	if((r = sys_safecopyto(info->old_endpoint, SEF_STATE_TRANSFER_GID, (vir_bytes) &ltckpt_writelog_enabled,
			(vir_bytes) &ltckpt_writelog_enabled, sizeof(ltckpt_writelog_enabled))) != OK) {
			hypermem_log("ltckpt recovery: failed: writelog: sys_safecopyto failed");
			printf("sef_copy_state_region: sys_safecopyto failed\n");
			return r;
	}
	ltckpt_writelog_enabled=0;

#if LTCKPT_RESTART_DEBUG
	printf("ltckpt_restart: performing identity state transfer\n");
#endif

	r = sef_cb_init_identity_state_transfer(SEF_INIT_RESTART, info);
	if(r != OK) {
		hypermem_log("ltckpt recovery: failed: writelog: identity state transfer failed");
		printf("ltckpt_restart: identity state transfer failed: %d\n", r);
		return r;
	}
#endif
	num_entries = wl_position/entry_size;

#if LTCKPT_RESTART_DEBUG
	printf("ltckpt_restart: about to restore up to %lu log entries\n", num_entries);
#endif

	/* Walk the log in reverse order and restore entries. */
	i=0;
	stack_entries=0;
	assert(wl_position % entry_size == 0);
	assert(num_entries == wl_position/entry_size);
	datamax = (char*)&num_entries - 4096;
	while(wl_position >= entry_size) {
		i++;
		wl_position -= entry_size;
		inside_trusted_compute_base = 1;
		if((r = sys_safecopyfrom(info->old_endpoint, SEF_STATE_TRANSFER_GID, (vir_bytes) &wl_data[wl_position],
			(vir_bytes) &addr, sizeof(addr))) != OK) {
			hypermem_log("ltckpt recovery: failed: writelog: sys_safecopyfrom addr failed");
			printf("sef_copy_state_region: sys_safecopyfrom addr failed\n");
			return r;
		}
		if (!ltckpt_can_restore(addr)) {
			printf("sef_copy_state_region: cannot restore address 0x%p from write log\n", addr);
			continue;
		}

		region = (region_t *) &wl_data[wl_position + sizeof(void*)];
		if (addr > datamax) {
			stack_entries++;
			continue;
		}
#if LTCKPT_RESTART_DEBUG && 0
		printf("ltckpt_restart: restoring entry %lu @0x%08lx \n",
			i, (unsigned long)addr);
		printf("ltckpt_restart: region at %p \n", region);
#endif
		if (addr) {
			inside_trusted_compute_base = 1;
			if((r = sys_safecopyfrom(info->old_endpoint, SEF_STATE_TRANSFER_GID, (vir_bytes) region,
				(vir_bytes) addr, sizeof(region_t))) != OK) {
				hypermem_log("ltckpt recovery: failed: writelog: sys_safecopyfrom region failed");
				printf("sef_copy_state_region: sys_safecopyfrom region failed\n");
				return r;
			}
		}
	}
	assert(i == num_entries);
	assert(wl_position == 0);

#if LTCKPT_RESTART_DEBUG
	printf("ltckpt_restart: restored %lu log entries (%lu stack entries skipped)\n",
		num_entries-stack_entries, stack_entries);
#endif
	//ltckpt_recovery_cleanup();

	inside_trusted_compute_base = 1;
//        printf("%s:%d: suicide replyable: %d\n", __FILE__, __LINE__, ltckpt_is_message_replyable());
        // printf("%s:%d: have handled message: %d\n", __FILE__, __LINE__, have_handled_message);
        // printf("%s:%d: in tcb: %d\n", __FILE__, __LINE__, inside_trusted_compute_base);

	p = hypermem_cat(p, pend, "ltckpt recovery: success: writelog: ");
	p = hypermem_cat_num(p, pend, num_entries - stack_entries);
	p = hypermem_cat(p, pend, " log entries, ");
	p = hypermem_cat_num(p, pend, stack_entries);
	p = hypermem_cat(p, pend, " stack entries");
	hypermem_log(buf);

	return 0;
}
#endif
