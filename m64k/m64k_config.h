#ifndef M64K_CONFIG_H
#define M64K_CONFIG_H

// Base address of the M68K memory space within the N64 memory map
#define M64K_CONFIG_MEMORY_BASE    0xFF000000

// Configure the require level of timing accuracy:
//   <0: very coarse timing. All opcodes takes this amount of cycles,
//       irrespective of their actual execution time. This is the fastest
//       option and might be ok for some arcade games.
//    0: approximate timing. The execution time of each opcode is close to
//       the real time, but several shortcuts are taken to avoid calculations
//       that increase execution time or code size too much. Expected error is
//       within ~3% but it will depend on the actual ROM.
//    1: accurate timing. The execution time of each opcode is tested to be
//       exact against known behaviour of the hardware (via the testsuite).
//       This is the slowest configuration.
#define M64K_CONFIG_TIMING_ACCURACY  0

// Set to 1 to emulate address exceptions. Address exceptions impact both
// codesize and runtime, as all memory accesses need to be verified beforehand.
// NOTE: exact timing of address errors is not emulated.
#define M64K_CONFIG_ADDRERR        1

// Set to 1 to emulate privilege violations. This exceptions are raised when
// a privileged instruction (accessing SR) is executed in non-supervisor mode.
#define M64K_CONFIG_PRIVERR        1

// Set to 1 to emulate division by zero exception.
#define M64K_CONFIG_DIVBYZERO      1

// Set to 1 to emulate instructions that access memory across the
// address space boundary (e.g. 0x00FFFFFF -> 0x00000000). The implementation
// is not complete: specifically, it doesn't still handle single 32-bit 
// accesses across the address space wrap-around (eg: 32-bit read at 0x00FFFFFE).
#define M64K_CONFIG_ADDR_WRAP      1

// Set to 1 to emulate the TAS instruction without writeback.
// This is the actual behaviour on Sega Megadrive because of a hardware bug
// in the board (the TAS memory transaction is not correctly supported), so
// define to 1 if you want to emulate the Megadrive or a similarly broken hardware.
#define M64K_CONFIG_BROKEN_TAS     0

// Set to 1 if you want m64k to log (via debugf) all unusual exceptions happening
// during emulation: address errors, privilege violations, divisions by zero, etc.
// This might be useful during the development phase.
#define M64K_CONFIG_LOG_EXCEPTIONS 0

#endif // M64K_CONFIG_H
