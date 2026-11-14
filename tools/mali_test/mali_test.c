/*
 * mali_test: the J7 Prime's Mali-T830 from a program, through HakiosMali (xnu patch 0193, SweetFramebuffer.cpp),
 * without Mesa: one request at a time, so a boot says whether the kernel's half works for a user process before
 * Mesa's panfrost is asked to draw through it. After the a50's core/userland/src/mali_test.c (same selectors and
 * structures); here it also says everything in the kernel log ("USB> on7wifid: mt: ...", kern.on7xelte_wifi
 * "log TEXT", root only, 80-byte pieces) because under launchd its stdout goes nowhere.
 *
 *   1  open HakiosMali (registered once the kernel's step 8 passed: boot-args on7mali=1 on7malistep>=8);
 *      at boot it waits up to 90 s for it
 *   2  its parameters (GET_PARAM), as panfrost asks for them
 *   3  a buffer (CREATE_BO): handle, GPU address; mapped here (MMAP_BO + IOConnectMapMemory64, type = handle)
 *   4  WRITE_VALUE jobs written into it (job type 2, 64-bit descriptor, target at +0x20, value type at +0x28):
 *      first each value type 1-7 once (4t lines), then the GPU timestamp (type 2) over a filled target,
 *      SUBMIT, wait by number (0x7d) and by buffer (WAIT_BO), the target checked; then ROUNDS more jobs
 *      (default 1000) for the round-trip time
 *   5  the buffer given back (GEM_CLOSE)
 *   6  /usr/local/bin/gl_test (tools/gl_test: Mesa's panfrost through the same service), when it is
 *      installed, run and waited for; its exit status logged
 *
 *   mali_test [ROUNDS]     (launchd job com.on7xelte.malitest runs it once at boot)
 */
#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
	kDrmGemClose = 0x09, kSubmit = 0x40, kWaitBO = 0x41, kCreateBO = 0x42, kMmapBO = 0x43, kGetParam = 0x44,
	kGetBOOffset = 0x45, kWaitSeq = 0x7d,
};

struct gem_close { uint32_t handle, pad; };
struct submit { uint64_t jc, in_syncs; uint32_t in_sync_count, out_sync; uint64_t bo_handles; uint32_t bo_handle_count, requirements; };
struct wait_bo { uint32_t handle, pad; int64_t timeout_ns; };
struct create_bo { uint32_t size, flags, handle, pad; uint64_t offset; };
struct mmap_bo { uint32_t handle, flags; uint64_t offset; };
struct get_param { uint32_t param, pad; uint64_t value; };
struct wait_seq { uint64_t seq; int64_t timeout_ns; };

static io_connect_t gpu;

static void
say(const char *fmt, ...)
{
	char line[200], msg[96];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	printf("mali_test: %s\n", line);
	fflush(stdout);
	/* the kernel takes at most 95 bytes per write (on7xelte_wifi.c buf[96]): 80-byte pieces, "mt+" continues */
	for (size_t at = 0, len = strlen(line); at < len || at == 0; at += 80) {
		snprintf(msg, sizeof(msg), "log %s %.80s", at ? "mt+" : "mt:", line + at);
		(void)sysctlbyname("kern.on7xelte_wifi", NULL, NULL, msg, strlen(msg) + 1);
		if (len == 0) {
			break;
		}
	}
}

static kern_return_t
request(uint32_t selector, void *structure, size_t size)
{
	size_t out = size;

	return IOConnectCallStructMethod(gpu, selector, structure, size, structure, &out);
}

static double
us_since(uint64_t then)
{
	static mach_timebase_info_data_t tb;

	if (tb.denom == 0) {
		mach_timebase_info(&tb);
	}
	return (double)(mach_absolute_time() - then) * tb.numer / tb.denom / 1e3;
}

/* one WRITE_VALUE job of TYPE at the start of the buffer, target +0x800 (pre-filled with FILL); its status */
static uint32_t
typed_job(mach_vm_address_t base, const struct create_bo *bo, uint32_t type, uint64_t imm, uint32_t fill)
{
	volatile uint32_t *job = (volatile uint32_t *)base, *word = (volatile uint32_t *)(base + 0x800);
	struct submit s = { .jc = bo->offset, .bo_handles = (uint64_t)(uintptr_t)&bo->handle, .bo_handle_count = 1 };
	struct wait_bo wb = { .handle = bo->handle, .timeout_ns = 1000000000 };
	struct wait_seq w;
	kern_return_t kr;

	memset((void *)base, 0, 0x1000);        /* a fresh descriptor every time: a DONE one fails (0x58, xnu87) */
	word[0] = fill;
	word[1] = fill;
	job[4] = (2u << 1) | 1u | (1u << 16);   /* write value; 64-bit descriptor; job index 1 */
	job[8] = (uint32_t)(bo->offset + 0x800);
	job[9] = (uint32_t)((bo->offset + 0x800) >> 32);
	job[10] = type;
	job[12] = (uint32_t)imm;
	job[13] = (uint32_t)(imm >> 32);
	__asm__ volatile ("dsb sy" ::: "memory");
	if ((kr = request(kSubmit, &s, sizeof(s))) != KERN_SUCCESS || s.jc == 0) {
		return 0xdead0000u | (kr & 0xffff);
	}
	w = (struct wait_seq){ .seq = s.jc, .timeout_ns = 1000000000 };
	(void)request(kWaitSeq, &w, sizeof(w));
	(void)request(kWaitBO, &wb, sizeof(wb));
	return job[0];
}

/* which WRITE_VALUE types this GPU carries out from a user buffer (xnu89: type 6 DONE but nothing written) */
static void
type_matrix(mach_vm_address_t base, const struct create_bo *bo)
{
	static const struct { uint32_t type; const char *name; } types[] = {
		{ 1, "cycles" }, { 2, "timestamp" }, { 3, "zero" }, { 4, "imm8" }, { 5, "imm16" }, { 6, "imm32" },
		{ 7, "imm64" },
	};
	volatile uint32_t *word = (volatile uint32_t *)(base + 0x800);

	for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		uint32_t st = typed_job(base, bo, types[i].type, 0x1122334455667788ull, 0xa5a5a5a5u);

		say("4t: type %u %-9s status %#x, words %08x %08x (fill a5a5a5a5)", types[i].type, types[i].name, st,
		    word[0], word[1]);
	}
}

/*
 * one WRITE_VALUE job at the start of the buffer: the GPU's timestamp (type 2) into +0x800, pre-filled with
 * VALUE; 1 if the GPU wrote it. Not an immediate: on the T830 (xnu90 type matrix) types 1/2/3 write cycles,
 * timestamp and zero, but the immediate types 4-7 write a 64-bit 0, whatever the descriptor holds.
 */
static int
one_job(mach_vm_address_t base, const struct create_bo *bo, uint32_t value, int verbose)
{
	volatile uint32_t *job = (volatile uint32_t *)base, *word = (volatile uint32_t *)(base + 0x800);
	struct submit s = { .jc = bo->offset, .bo_handles = (uint64_t)(uintptr_t)&bo->handle, .bo_handle_count = 1 };
	struct wait_bo wb = { .handle = bo->handle, .timeout_ns = 1000000000 };
	struct wait_seq w;
	kern_return_t kr, kb;

	memset((void *)base, 0, 0x1000);        /* a fresh descriptor every time: a DONE one fails (0x58, xnu87) */
	word[0] = value;
	word[1] = value;
	job[4] = (2u << 1) | 1u | (1u << 16);   /* write value; 64-bit descriptor; job index 1 */
	job[8] = (uint32_t)(bo->offset + 0x800);
	job[9] = (uint32_t)((bo->offset + 0x800) >> 32);
	job[10] = 2;                            /* the system timestamp */
	__asm__ volatile ("dsb sy" ::: "memory");

	if ((kr = request(kSubmit, &s, sizeof(s))) != KERN_SUCCESS || s.jc == 0) {
		say("4: SUBMIT refused: %#x (job number %llu)", kr, (unsigned long long)s.jc);
		return 0;
	}
	w = (struct wait_seq){ .seq = s.jc, .timeout_ns = 1000000000 };
	kr = request(kWaitSeq, &w, sizeof(w));
	kb = request(kWaitBO, &wb, sizeof(wb));
	int written = job[0] == 1 && (word[0] != value || word[1] != value);
	if (verbose || !written) {
		say("4: job %llu: wait %#x, WAIT_BO %#x, status %#x, timestamp %08x%08x%s", (unsigned long long)s.jc, kr, kb,
		    job[0], word[1], word[0], written ? " -- THE GPU WROTE IT" : " -- not written");
	}
	return written;
}

/* 6: Mesa's turn, in a process of its own (it opens HakiosMali itself), once this one has let go */
static void
run_gl_test(void)
{
	static const char path[] = "/usr/local/bin/gl_test";
	char *const args[] = { (char *)path, NULL };
	extern char **environ;
	pid_t pid;
	int status = 0, err;

	if (access(path, X_OK) != 0) {
		say("6: no %s", path);
		return;
	}
	if ((err = posix_spawn(&pid, path, NULL, NULL, args, environ)) != 0) {
		say("6: %s would not start: errno %d", path, err);
		return;
	}
	say("6: %s started, pid %d", path, pid);
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
	}
	if (WIFSIGNALED(status)) {
		say("6: gl_test killed by signal %d", WTERMSIG(status));
	} else {
		say("6: gl_test exit %d", WEXITSTATUS(status));
	}
}

int
main(int argc, char **argv)
{
	static const struct { uint32_t param; const char *name; } params[] = {
		{ 0, "GPU_PROD_ID" }, { 1, "GPU_REVISION" }, { 2, "SHADER_PRESENT" }, { 3, "TILER_PRESENT" }, { 4, "L2_PRESENT" },
		{ 6, "AS_PRESENT" }, { 7, "JS_PRESENT" }, { 12, "MMU_FEATURES" }, { 17, "COHERENCY_FEATURES" },
		{ 38, "NR_CORE_GROUPS" }, { 39, "THREAD_TLS_ALLOC" },
	};
	unsigned rounds = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 1000, good = 0, done = 0;
	struct create_bo bo = { .size = 16384 };
	mach_vm_address_t base = 0;
	mach_vm_size_t mapped = 0;
	io_service_t service = 0;
	kern_return_t kr;

	say("start (pid %d, uid %d), %u rounds after the first job", getpid(), getuid(), rounds);
	for (int i = 0; i < 90 && service == 0; i++) {
		service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("HakiosMali"));
		if (service == 0) {
			sleep(1);
		}
	}
	if (service == 0) {
		say("1: no HakiosMali after 90 s (boot-args on7mali=1 on7malistep=8 or 9?)");
		return 0;
	}
	if ((kr = IOServiceOpen(service, mach_task_self(), 0, &gpu)) != KERN_SUCCESS) {
		say("1: HakiosMali would not open: %#x", kr);
		return 1;
	}
	say("1: HakiosMali open");

	for (unsigned i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
		struct get_param p = { .param = params[i].param };

		kr = request(kGetParam, &p, sizeof(p));
		say("2: %-18s %s %#llx", params[i].name, kr ? "refused," : "=", kr ? (unsigned long long)kr :
		    (unsigned long long)p.value);
	}

	if ((kr = request(kCreateBO, &bo, sizeof(bo))) != KERN_SUCCESS || bo.handle == 0) {
		say("3: CREATE_BO refused: %#x", kr);
		return 1;
	}
	struct mmap_bo mm = { .handle = bo.handle };
	kr = request(kMmapBO, &mm, sizeof(mm));
	if (kr == KERN_SUCCESS) {
		kr = IOConnectMapMemory64(gpu, bo.handle, mach_task_self(), &base, &mapped, kIOMapAnywhere);
	}
	say("3: buffer %u, %u bytes, GPU %#llx (%s), mapped here at %#llx (%llu bytes): %#x", bo.handle, bo.size,
	    (unsigned long long)bo.offset, bo.offset >= 0x2000000ull && bo.offset + bo.size <= 0x100000000ull ?
	    "in 32 MB..4 GB" : "OUTSIDE 32 MB..4 GB", (unsigned long long)base, (unsigned long long)mapped, kr);
	if (kr != KERN_SUCCESS || mapped < 0x1000) {
		return 1;
	}

	type_matrix(base, &bo);
	if (one_job(base, &bo, 0xc0ffee01u, 1)) {
		uint64_t started = mach_absolute_time();

		for (done = 0; done < rounds; done++) {
			if (!one_job(base, &bo, 0xc0ffee02u + done, 0)) {
				break;
			}
			good++;
		}
		say("4: %u of %u more jobs written, %.1f us a job (request to answer)", good, rounds,
		    good ? us_since(started) / good : 0.0);
	}

	struct gem_close gc = { .handle = bo.handle };
	(void)IOConnectUnmapMemory64(gpu, bo.handle, mach_task_self(), base);
	kr = request(kDrmGemClose, &gc, sizeof(gc));
	say("5: buffer given back: %#x; %s", kr, good == rounds ? "ALL OK" : "FAILED");
	IOServiceClose(gpu);
	run_gl_test();
	return good == rounds ? 0 : 1;
}
