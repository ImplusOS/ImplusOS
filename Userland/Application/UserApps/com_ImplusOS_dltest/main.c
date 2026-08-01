#include <dlfcn.h>
#include <pthread.h>
#include <Process.h>
#include <stdint.h>
#include <stdio.h>

static int g_failures = 0;

int main(void);

void _start(void)
{
    int32_t status = (int32_t)main();
    process_exit(status);
    for (;;) { process_yield(); }
}

static void check(int ok, const char* what)
{
    if (!ok) {
        g_failures++;
        printf("[FAIL] %s\n", what);
    } else {
        printf("[OK]   %s\n", what);
    }
}

static __thread int main_tls_value = 777;
static __thread long main_tls_slot = 0x0102030405060708LL;

static int main_tls_read(void) { return main_tls_value; }
static void main_tls_write(int v) { main_tls_value = v; }
static long main_tls_slot_read(void) { return main_tls_slot; }
static void main_tls_slot_write(long v) { main_tls_slot = v; }

typedef int (*fn_i_void)(void);
typedef int (*fn_i_i)(int);
typedef void (*fn_v_i)(int);
typedef int (*fn_i_ii)(int, int);

typedef struct {
    void* handle;
    int round;
    int ok;
} worker_arg_t;

static void* worker_main(void* arg)
{
    worker_arg_t* wa = (worker_arg_t*)arg;
    fn_v_i base_set = (fn_v_i)dlsym(wa->handle, "libbase_tls_set");
    fn_i_void base_get = (fn_i_void)dlsym(wa->handle, "libbase_tls_get");
    fn_i_i math_init = (fn_i_i)dlsym(wa->handle, "libmath_init_tls");
    fn_i_void math_get = (fn_i_void)dlsym(wa->handle, "libmath_get_round");
    fn_i_void base_counter = (fn_i_void)dlsym(wa->handle, "libmath_read_base_counter");

    if (!base_set || !base_get || !math_init || !math_get || !base_counter) {
        printf("thread %d: dlsym failed: %s\n", wa->round, dlerror());
        wa->ok = 0;
        return NULL;
    }

    int expected_base = 2000 + wa->round;
    int expected_round = 100 + wa->round;
    base_set(expected_base);
    math_init(expected_round);

    int ok = base_get() == expected_base &&
             math_get() == expected_round &&
             base_counter() == expected_base;
    printf("thread %d: base=%d round=%d counter=%d %s\n",
           wa->round, base_get(), math_get(), base_counter(),
           ok ? "OK" : "MISMATCH");
    wa->ok = ok;
    return NULL;
}

int main(void)
{
    printf("dltest: ImplusOS dynamic linker test\n");

    void* h_base = dlopen("/Userland/UserApps/com_ImplusOS_dltest/libdlbase.so", RTLD_NOW | RTLD_GLOBAL);
    check(h_base != NULL, "dlopen libdlbase.so");
    if (!h_base) {
        printf("dlerror: %s\n", dlerror());
        return 1;
    }

    printf("main TLS initial: %d slot=%lx\n", main_tls_read(), main_tls_slot_read());
    main_tls_write(888);
    main_tls_slot_write(0xAABBCCDD00112233LL);
    check(main_tls_read() == 888 && main_tls_slot_read() == 0xAABBCCDD00112233LL,
          "main exe __thread write/read");

    void* h_math = dlopen("/Userland/UserApps/com_ImplusOS_dltest/libdlmath.so", RTLD_NOW | RTLD_LOCAL);
    check(h_math != NULL, "dlopen libdlmath.so (DT_NEEDED libdlbase.so)");
    if (!h_math) {
        printf("dlerror: %s\n", dlerror());
        return 1;
    }

    fn_i_void math_get = (fn_i_void)dlsym(h_math, "libmath_get_round");
    int m1 = math_get();
    check(math_get != NULL && m1 == 0, "libmath TLS initialized to 0 (IE)");

    fn_i_void base_get = (fn_i_void)dlsym(h_math, "libbase_tls_get");
    check(base_get != NULL && base_get() == 100, "libbase TLS initialized to 100 (GD)");

    fn_i_ii call_base = (fn_i_ii)dlsym(h_math, "libmath_call_base");
    check(call_base != NULL && call_base(3, 4) == 107, "cross-DSO call + TLS read = 107");

    fn_i_void read_counter = (fn_i_void)dlsym(h_math, "libmath_read_base_counter");
    check(read_counter != NULL && read_counter() == 100, "cross-DSO GD TLS read (DTPMOD/DTPOFF)");

    fn_v_i base_set = (fn_v_i)dlsym(h_math, "libbase_tls_set");
    base_set(5);
    check(base_get() == 5 && read_counter() == 5, "TLS write visible across DSOs");

    fn_i_i math_init = (fn_i_i)dlsym(h_math, "libmath_init_tls");
    check(math_init != NULL && math_init(42) == 42 && math_get() == 42, "IE TLS write");

    fn_i_ii base_add = (fn_i_ii)dlsym(RTLD_DEFAULT, "libbase_add");
    check(base_add != NULL && base_add(10, 20) == 30, "dlsym RTLD_DEFAULT (global scope)");

    check(dlsym(h_math, "no_such_symbol_xyz") == NULL, "dlsym missing symbol -> NULL");
    const char* err = dlerror();
    printf("  dlerror: %s\n", err ? err : "(null)");
    check(err != NULL, "dlerror after failed dlsym");
    check(dlerror() == NULL, "dlerror clears after read");

    check(dlopen("/Userland/UserApps/com_ImplusOS_dltest/no_such.so", RTLD_NOW) == NULL,
          "dlopen missing file -> NULL");
    printf("  dlerror: %s\n", dlerror());

    pthread_t threads[3];
    worker_arg_t args[3];
    for (int i = 0; i < 3; i++) {
        args[i].handle = h_math;
        args[i].round = i;
        args[i].ok = 0;
        check(pthread_create(&threads[i], NULL, worker_main, &args[i]) == 0, "pthread_create");
    }
    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
        if (!args[i].ok) g_failures++;
    }

    check(base_get() == 5 && math_get() == 42 && main_tls_read() == 888,
          "main thread TLS unchanged after workers");

    void* h_plain = dlopen("/Userland/UserApps/com_ImplusOS_dltest/libdlplain.so", RTLD_NOW | RTLD_LOCAL);
    check(h_plain != NULL, "dlopen libdlplain.so");
    fn_i_i plain_triple = (fn_i_i)dlsym(h_plain, "plain_triple");
    check(plain_triple != NULL && plain_triple(5) == 15, "dlsym plain_triple");
    check(dlsym(RTLD_DEFAULT, "plain_triple") == NULL, "RTLD_LOCAL symbol hidden from global scope");

    check(dlclose(h_plain) == 0, "dlclose libdlplain.so (unload)");
    void* h_plain2 = dlopen("/Userland/UserApps/com_ImplusOS_dltest/libdlplain.so", RTLD_NOW | RTLD_LOCAL);
    check(h_plain2 != NULL, "re-dlopen after unload");
    if (h_plain2) {
        check(dlsym(h_plain2, "plain_triple") != NULL, "re-loaded lib callable");
        dlclose(h_plain2);
    }

    check(dlclose(h_math) == 0, "dlclose libdlmath.so (TLS lib kept)");
    check(dlclose(h_base) == 0, "dlclose libdlbase.so (global lib kept)");
    check(base_get() == 5, "lib functions still callable after dlclose (kept)");

    printf(g_failures == 0 ? "\ndltest: ALL PASSED\n" : "\ndltest: %d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
