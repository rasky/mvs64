#ifndef M64K_CONFIG_H
#define M64K_CONFIG_H

// Base address of the M68K memory space within the N64 memory map
#define M68K_CONFIG_MEMORY_BASE    0xFF000000

// Set to 1 to emulate address exceptions
#define M64K_CONFIG_ADDRERR        1

// Set to 1 to emulate privilege violations
#define M64K_CONFIG_PRIVERR        1


#endif // M64K_CONFIG_H