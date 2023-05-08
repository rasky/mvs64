# m64k: an optimized Motorola 68000 core for Nintendo 64

m64k is an attempt at finally "cracking" 68000 on Nintendo 64, that is finding
a viable way to emulate it. This will hopefully open the door to emulating
many hardware platforms (both consoles and arcade) based on this popular
processor.

The emulator is meant to run within a [libdragon](https://github.com/DragonMinded/libdragon) application. It is not compatible with other Nintendo 64 development
environments.

## Features

The goal is to have a very fast core with perfect opcode accuracy, though not
cycle-level accuracy. In particular:

* All 68000 opcodes are emulated in their complete behavior (including
  undocumented flags).
* Memory accesses are performed via TLB mapping for maximum speed. MMIO accesses
  can be emulated via TLB miss exceptions. The m64k API allows to easily
  implement MMIO handlers either in C or Assembly.
* Opcode cycle counting can be emulated at three accuracy levels:
  * `M64K_CONFIG_TIMING_ACCURACY = 0` (default): this is a reasonable compromise
    between accuracy and emulation speed. The emulated cycle counts should be
    quite close to the original hardware, with an expected error within 3%.
  * `M64K_CONFIG_TIMING_ACCURACY = 1`. This is the full accuracy option: all
    opcodes are expected to emulate the exact cycle count as real hardware. 
    Activating this mode will provide the maximum accuracy at the expense of
    code size and some emulation speed.
  * `M64K_CONFIG_TIMING_ACCURACY < 0`. This is a "quick hack" mode: all opcodes
    are emulated with a fixed amount of cycles: the number configured in the
    preprocessor macro. For instance, by setting `64K_CONFIG_TIMING_ACCURACY = -10`
    all emulated opcodes will consume exactly 10 cycles. This is the fastest
    option, but obviously is very inaccurate. Still, it can be enough to run
    for instance 68000-based arcade machines.
* Address errors can optionally be enabled via `M64K_CONFIG_ADDRERR`. They
  cause a bit of overhead and a measurable increase of code size.
* Privilege errors can be enabled via `M64K_CONFIG_PRIVERR`. This has little
  impact on codesize or speed.

## How to use m64k in your emulator

### Tutorial


### Memory accesses

The 68000 CPU uses 32-bit registers to address memory, though the physical
address bus is only 24-bit. This means that the top 8-bit of the address
register are actually ignored when accessing the bus.

The m64k emulator is designed to use N64 TLBs (sort of a MMU) to emulate
memory accesses with high-speed. The assumption is that most accesses will
either be to ROM or RAM and thus to a memory-like area where no further action
must be taken by the emulator beyond actually reading / writing the memory.
Thus, m64k requests that all memory banks are actually TLB-mapped. To do so,
it offers a helper function in its API (m64k_map_memory). 

There are a few constraints to be aware:

 * In general, the mapped area must have the same alignment of its size. For
   instance a 16 KiB memory area must be aligned to 16 Kib. You can use
   `memalign` to allocate it dynamically from the heap, or `alignas()` to
   force compile-time alignment of a static buffer.
 * The minimum mappable area is 4 KiB. If you absolutely need to map less
   than 4 KiB, your only option is to treat it as MMIO (see later). This will
   unfortunately have a speed impact if the 68000 does frequent accesses there.

```C
  // Allocate 64 KiB of memory, aligned to a 64 KiB address. In general,
  // TLB-mapped areas must have the same alignment of their size.
  uint8_t *work_ram = memalign(64*1024, 64*1024);

  // Map it to address 0x040000 in 68000 memory map
  m64k_map_memory(&m64k, 0x040000, 64*1024, work_ram, true);
```

m64k will reserves an area of the N64 memory space for the 68000 memory map.
By default, this area is `0xFF000000 - 0xFFFFFFFF` (`M64K_CONFIG_MEMORY_BASE`
is `0xFF000000`). So in the above example, after running the `m64k_map_memory`
function, the buffer will also be accessible with a pointer at `0xFF040000`.

**NOTE:** the TLB mapping is currently permanent, but this is an implementation
detail. We plan to eventually make the TLB mapping active only while the
m64k core is running (that is, within the `m64k_run` function). So do not
access the memory via the TLB-mapped address: if you need to access the work
RAM in your emulator, just use the pointer you allocated yourself.

## MMIO accesses

All accesses to addresses not mapped via `m64k_map_memory` are considered to
be MMIO by m64k. To implement them, there are two possible callbacks that
can be registered:

 * Fast Assembly handlers: register them via `m64k_set_mmio_fast_handlers`
 * Slow C handlers: register them via `m64k_set_mmio_handlers`

Assembly handlers are faster not because writing them in assembly is more
performant (that depends and in the general case C is indeed good enough); the
difference comes from the fact that calling a C callback from within the m64k
core is very expensive. In fact, the achieve maximum speed, the m64k core uses
most of the 32 MIPS registers, many of them with fixed values. Whenever it needs
to call a C function, it has to save and restore most of them around the
function call. This is quite expensive but it is an unfortunate reality of the
MIPS ABI.

On the contrary, we designed our own ABI for MIPS assembly handlers with minimal
register usage. Obviously if the assembly handler requires more registers, it
would have to save and restore them, but at least the cost is only paid when
absolutely necessary.

MMIO handlers are passed the 68000 address, and the size of the access (either
8-bit or 16-bit). Write handlers also receive the value being written (which is
the value put on the bus), while read handlers must return the value being read.

Notice that 32-bit MMIO handlers are not supported to improve accuracy. In fact,
accesses performed by 68000 to the bus are logically either 8-bit or 16-bit.
Opcodes that do 32-bit accesses like `move.l` are internally microcoded to
perform two subsequent 16-bit accesses; the order is normally high-word first,
though it actually does depend on each opcode. The actual access order does not
affect memory like RAM or ROM, but can have an effect on MMIOs, so we designed
m64k to perform two subsequent MMIO handler calls for 32-bit accesses, in the
correct order, to better mimic what happens at the hardware level.

### MMIO: C handler example

This is an example of a simple MMIO read/write handler that perform reads/writes
to a VRAM bank which is not memory mapped into the 68000. This assumes one address
for reading/writing the VRAM offset, and another address to read/write the
actual data:

```C
uint16_t VRAM[0x4000];   // 16-bit VRAM
uint16_t vram_ptr;

uint32_t my_read_handler(uint32_t address, int sz) {
  switch (address) {
    case 0xE00000:    // VRAM offset
      return vram_ptr;
    case 0xE00002: {  // VRAM data
      uint16_t value = VRAM[vram_ptr];
      vram_ptr++;
      vram_ptr &= 0x3FFF;
      return value;
    }
    default:
      debugf("[MMIO] unknown read: %06lx (%c)\n", address, sz == 1 ? 'b' : 'w');
      return 0xFFFF;
  }
}

void my_write_handler(uint32_t address, uint16_t value, int sz) {
  switch (address) {
    case 0xE00000:    // VRAM offset
      vram_ptr = value & 0x3FFF;
      return;
    case 0xE00002: {  // VRAM data
      VRAM[vram_ptr] = value;
      vram_ptr++;
      vram_ptr &= 0x3FFF;
      return;
    } 
    default:
      debugf("[MMIO] unknown write: %06lx (%c)\n", address, sz == 1 ? 'b' : 'w');
      return 0xFFFF;
  }
}

[....]

// Register MMIO handlers into the m64k core
m64k_set_mmio_handlers(&m64k, my_read_handler, my_write_handler);
```

As you can see, the m64k core does not perform any attempt at address banking
or decoding: all accesses are forward to the handlers. If you want to further
split work across functions for different logical ares, you will have to handle
this yourself in your code, as you will probably know the better way to split
the address into optimal sub-ranges.

The above example ignores the access size. This is not necessarily a bug but
it might be an accurate emulation of the hardware. In fact, 8-bit accesses
on hardware are always performed putting the 16-bit (aligned) address on the
bus, and the signaling via two signals (/LDS and /UDS) whether the access is
happening on either the lower byte only or the upper byte only. This means
that it is quite cheap for devices talking to 68k to simply ignore the /LDS and
/UDS lines (and thus 8-bit accesses) and just pretend they are always 16-bit.
m64k does handle this behavior accurately. If instead the device attached to
the bus does interpret the /UDS or /LDS lines somehow, then you would have
to handle that by changing behavior depending on `sz`.

### MMIO: Assembly handler example

To show the same example of above but using assembly instead, first let's
document the expected assembly ABI.

For read handlers:

| Register | Description |
| ---------| ----------- |
| `k1` (input) | 24-bit 68000 address |
| `k0` (input) | 0 if 8-bit access, 1 if 16-bit access |
| `t6` (output) | value read |
| `k0` (output) | non-zero if handled, 0 if not handled (fallback to C handler) |

For write handlers:

| Register | Description |
| ---------| ----------- |
| `k1` (input) | 24-bit 68000 address |
| `k0` (input) | 0 if 8-bit access, 1 if 16-bit access |
| `t6` (input) | value being written |
| `k0` (output) | non-zero if handled, 0 if not handled (fallback to C handler) |

The registers freely available in the assembly handlers are k0, k1, t6, at. 
All other registers must be preserved.

This is how you would implement the VRAM handlers defined in C above:

```mips
  .set reorder     # reorder delay slots automatically
  .set at          # allow assembly macros, since at is available

my_read_handler:
  beq k1, 0xE00000, asm_vram_offset_r
  beq k1, 0xE00002, asm_vram_data_r
  li k0, 0               # MMIO not handled, fallback to C
  jr ra

my_write_handler:
  beq k1, 0xE00000, asm_vram_offset_w
  beq k1, 0xE00002, asm_vram_data_w
  li k0, 0               # MMIO not handled, fallback to C
  jr ra

asm_vram_offset_r:
  lhu t6, vram_offset    # read the uint16_t vram_offset (defined in C)
  li k0, 1               # MMIO handled correctly
  jr ra

asm_vram_offset_w:
  andi k0, 0x3FFF        # wrap the offset
  sh t6, vram_offset     # write the offset
  li k0, 1               # MMIO handled correctly
  jr ra

asm_vram_data_r:
  lhu k1, vram_offset    # read the uint16_t vram_offset (defined in C)
  addiu k0, k1, 1        # increment the offset
  andi k0, 0x3FFF        # wrap the offset
  sh k0, vram_offset     # write back offset after increment
  add k1, k1             # multiply offset by two (as VRAM is a 16-bit area)
  lhu t6, VRAM(k0)       # fetch from within the VRAM area (defined in C)
  li k0, 1               # MMIO handled correctly
  jr ra

asm_vram_data_w:
  lhu k1, vram_offset    # read the uint16_t vram_offset (defined in C)
  addiu k0, k1, 1        # increment the offset
  andi k0, 0x3FFF        # wrap the offset
  sh k0, vram_offset     # write back offset after increment
  add k1, k1             # multiply offset by two (as VRAM is a 16-bit area)
  sh t6, VRAM(k0)        # store to the VRAM area (defined in C)
  li k0, 1               # MMIO handled correctly
  jr ra
```

After defining this in an assembly source file, you can simply register the
handlers like this:


```C
extern uint8_t my_read_handler[], my_write_handler[]; // assembly functions

m64k_set_mmio_fast_handlers(&m64ks, 
  my_read_handler, my_write_handler, my_read_handler, my_write_handler, );
```




## Testing

m64k was developed against the only available 68000 testsuite available online:
https://github.com/TomHarte/ProcessorTests/tree/main/680x0/68000/v1

Luckily, the testsuite seems to be pretty comprehensive and quite accurate, even
if it was not hardware verified yet.

## Implementation notes

### Code size

The core is implemented in assembly. While the source code seems massive
(~3000 lines of dense code), it actually assembles to only 8 KiB bytes of code,
and 0.5 KiB of tables. This means that the code fully fits within 16 KiB
instruction cache present in the VR4300 processor, and while the emulator
is running, the interpreter core should rarely pay the ~40 cycles of penalty
hit for each cache miss.

This is achieved by manually size-optimizing the core, reusing most of the
implementation for similar opcodes, and using a ad-hoc decoder for opcodes.
The 68000 is notoriously hard to decode as opcodes are stuffed in empty/invalid
bit space left by other opcodes, so most PC emulators end up using a 64 KiB
table of function pointers for decoding. That makes sense on modern PCs with
their giant 3-level caches, but it does kill performance on N64 because of
the amount of cache trashing it will incur on.

You can compare it with Musashi, a common 68000 PC interpreter that uses code
generation, and uses 264 KiB of code plus 320 KiB of tables. Many opcodes will
likely incur in at least two cache misses: one for fetching the 256 KiB function pointers table, and another one to fetch the 64 KiB instruction cycle count table,
causing ~80 cycles of cost. In addition to that, most opcodes will also incur
in multiple cache misses for fetching the code of the opcode itself, as Musashi
code-generates one different function for most of the 64K variants.

### Flag handling

Flag handling is handled through three 64-bit registers: `flag_nv`, `flag_zc`,
and `flag_x` (which is more rarely affected by opcodes). They are stored in
fixed registers in VR4300, and defined like this:

```C
#define flag_nv     s6    // bit 31: N; bit 63: N^V
#define flag_zc     s7    // bit 0..31: !Z; bit 32: C
#define flag_x      s8    // bit 0: X
```

This definition might look complex at first but it actually allows for very
efficient flag calculations on MIPS64 ISA. Specifically, `flag_nv` takes
advantage of the fact that most 32-bit opcodes will sign-extend the result
into the destination 64-bit register, and is thus very easy to both reset
the V flag (by sign-extending), or setting it to the proper expected value
by performing a 64-bit operation.

For instance, this the actual implementation (after the decoding) of
the `or` opcode, valid for 8, 16, and 32-bit variants:

```mips
    # Calculate result
    or result, t0, t1

    # Left-align result to the lower 32-bit. rmw_bitsize has been set to
    # either 24, 16 or 0 for 8-bit, 16-bit and 32-bit operation respectively.
    # This both sets flag N according the result, and automatically clears V
    # because of the sign extension.
    sllv flag_nv, result, rmw_bitsize

    # zx64 is a one-opcode macro that does a 32-bit zero extension into
    # the 64-bit destination (using a fixed register with a mask).
    # This basically copies the result into the lower 32-bit of flag_zc
    # (thus correctly setting the Z flag), and clears C through the zero extension.
    zx64 flag_zc, flag_nv
```

Let's look now for the `add` opcode that needs to calculate the proper overflow
flag. Again the implementation is valid for 8, 16 and 32-bit variants:

```mips
    # Calculate result with a 64-bit addition, to preserve the carry even for
    # 32-bit operands. Notice that operands are guaranteed to be zero-extended
    # at this point.
    daddu result, t0, t1

    # Left-align result to the lower 32-bit. rmw_bitsize has been set to
    # either 24, 16 or 0 for 8-bit, 16-bit and 32-bit operation respectively.
    # This means that the carry ends up on bit 33, correctly affecting the C
    # flag, and the result goes in the lower 32-bits affecting the Z flag.
    dsll flag_zc, result, rmw_bitsize 

    # Copy C flag (bit 32) into flag X (bit 0)
    dsrl flag_x, flag_zc, 32

    # We now need to perform a second addition with sign-extended operands
    # to calculate the overflow flag. So first left-align the operands using
    # 32-bit shifts, that will sign-extends the operands.
    sllv t0, t0, rmw_bitsize 
    sllv t1, t1, rmw_bitsize 

    # Do the addition of sign-extended operands. This will create the correct
    # left-aligned result in the lower 32-bit bits of flag_nv, correctly
    # affecting the N flag. Moreover, any overflow will cause bit 63 to change
    # sign compared to bit 31, correctly affecting the V flag.
    daddu flag_nv, t0, t1
```

Thanks to the chosen flag definition optimized for the ISA, it was possible
to emulate the `add` opcode (after decoding it) in only 6 cycles, correctly
calculating all 5 flags at the same time. Moreover, the same implementation
is valid for the 3 variants (byte, word, long), containing code size.


### Memory accesses

As



## Credits

I'd like to thank calc84maniac who came up with the initial idea for the smart
flag handling, and later helped me implementing the most complex opcodes with
their witty optimization tricks. 

Also thanks to Mast of Destiny for general 68000 advices and guidance.
