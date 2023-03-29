#include <stdint.h>

extern void m64k_run_internal(void);

void m64k_run(void)
{
    m64k_run_internal();
}
