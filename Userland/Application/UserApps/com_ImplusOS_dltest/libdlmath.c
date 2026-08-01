#include <stdint.h>

extern __thread int base_tls_counter;
extern int libbase_add(int a, int b);
extern int libbase_tls_get(void);

__thread int math_tls_round = 0;

int libmath_init_tls(int value) { math_tls_round = value; return math_tls_round; }
int libmath_get_round(void) { return math_tls_round; }
int libmath_read_base_counter(void) { return base_tls_counter; }
int libmath_call_base(int a, int b) { return libbase_add(a, b) + libbase_tls_get(); }
