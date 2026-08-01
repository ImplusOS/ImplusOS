#include <stdint.h>

__thread int base_tls_counter = 100;
static __thread long base_tls_slot = 0x1122334455667788LL;

int libbase_tls_get(void) { return base_tls_counter; }
void libbase_tls_set(int value) { base_tls_counter = value; }
long libbase_tls_slot_get(void) { return base_tls_slot; }
void libbase_tls_slot_set(long value) { base_tls_slot = value; }
int libbase_add(int a, int b) { return a + b; }
