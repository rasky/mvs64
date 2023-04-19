#ifndef M64K_CONFIG_H
#define M64K_CONFIG_H

// Base address of the M68K memory space within the N64 memory map
#define M64K_CONFIG_MEMORY_BASE    0xFF000000

// Set to 1 to emulate address exceptions
#define M64K_CONFIG_ADDRERR        1

// Set to 1 to emulate privilege violations
#define M64K_CONFIG_PRIVERR        1

// Set to 1 to emulate division by zero
#define M64K_CONFIG_DIVBYZERO      1

// Set to 1 to emulate instructions that access memory across the
// address space boundary (e.g. 0x00FFFFFF -> 0x00000000). The implementation
// is not complete: specifically, it doesn't still handle single 32-bit 
// accesses across the address space wrap-around (eg: 32-bit read at 0x00FFFFFE).
#define M64K_CONFIG_ADDR_WRAP      1

#endif // M64K_CONFIG_H