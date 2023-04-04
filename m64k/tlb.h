#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

static void tlb_init(void) {
	C0_WRITE_ENTRYHI(0xFFFFFFFF);
	C0_WRITE_ENTRYLO0(0);
	C0_WRITE_ENTRYLO1(0);
	C0_WRITE_PAGEMASK(0);
	for (int i=0;i<32;i++) {
		C0_WRITE_INDEX(i);
		C0_TLBWI();
	}
}

static void tlb_map_area_internal(unsigned int idx, uint32_t virt, uint32_t vmask, uint32_t phys, bool readwrite, uint32_t vpn2)
{
	bool low_half = !(virt & (vmask+1));

	// Check if there's already an entry at this address
	C0_WRITE_ENTRYHI(vpn2);
	C0_TLBP();
	uint32_t existing = C0_INDEX();
	uint32_t exentry0 = C0_ENTRYLO0();
	uint32_t exentry1 = C0_ENTRYLO1();
	if (!(existing & 0x80000000)) {
		// An entry was found. Check if the slot we need (lower or upper) is free.
		// In fact, there could an entry mapping for instance only the lower half,
		// but that is fine if we need to map the upper half instead.
		if (low_half) {
			assertf((exentry0 & 2) == 0, "Duplicated TLB entry with vaddr %08lx (%lx/%lx)", vpn2, exentry0, exentry1);
		} else {
			assertf((exentry1 & 2) == 0, "Duplicated TLB entry with vaddr %08lx (%lx/%lx)", vpn2, exentry0, exentry1);			
		}
		// Reuse the same TLB index.
		idx = existing;
	} else {
		exentry0 = exentry1 = 1<<0;
	}

	// Create the new entry
	uint32_t entry = (phys & 0x3FFFF000) >> 6;
	if (readwrite)
		entry |= (1<<2); // dirty bit
	entry |= 1<<1; // valid bit
	entry |= 1<<0; // global bit

	// Write it into the correct slot, keeping the other slot intact in case
	// it contains other data.
	if (low_half) {
		C0_WRITE_ENTRYLO0(entry);
		C0_WRITE_ENTRYLO1(exentry1);
	} else {
		C0_WRITE_ENTRYLO0(exentry0);
		C0_WRITE_ENTRYLO1(entry);
	}

	// Write the TLB
	C0_WRITE_INDEX(idx);
	C0_TLBWI();
}

static void tlb_map_area(unsigned int idx, void *virt, uint32_t vmask, uint32_t phys, bool readwrite)
{
	uint32_t vaddr = (uint32_t)virt;
	assert(idx < 32);

	// Check if the mask is valid, and if it requires usage of both slots of an entry
	// (dbl=true), or only one.
	bool dbl;
	switch (vmask) {
	case 0x000FFF: C0_WRITE_PAGEMASK(0x00 << 13); dbl=false; break;
	case 0x001FFF: C0_WRITE_PAGEMASK(0x00 << 13); dbl=true;  break;
	case 0x003FFF: C0_WRITE_PAGEMASK(0x03 << 13); dbl=false; break;
	case 0x007FFF: C0_WRITE_PAGEMASK(0x03 << 13); dbl=true;  break;
	case 0x00FFFF: C0_WRITE_PAGEMASK(0x0F << 13); dbl=false; break;
	case 0x01FFFF: C0_WRITE_PAGEMASK(0x0F << 13); dbl=true;  break;
	case 0x03FFFF: C0_WRITE_PAGEMASK(0x3F << 13); dbl=false; break;
	case 0x07FFFF: C0_WRITE_PAGEMASK(0x3F << 13); dbl=true;  break;
	case 0x0FFFFF: C0_WRITE_PAGEMASK(0xFF << 13); dbl=false; break;
	case 0x1FFFFF: C0_WRITE_PAGEMASK(0xFF << 13); dbl=true;  break;
	default: assertf(0, "invalid vmask %08lx", vmask); break;
    }

	// Verify that the addresses are aligned to the needed vmask.
	assert(((uint32_t)phys & vmask) == 0);
	assert((vaddr & vmask) == 0);
	if (vmask == 0x000FFF) {
		// Unfortunately, data cache is indexed using bits 12:4, so a TLB-mapped
		// 4k page might use different cachelines compared to using the 0x8000_0000
		// segment to access the same physical address.
		// This is not forbidden per se, but it can create nasty cache coherency
		// issues, so better forbid it altogether.
		assertf((vaddr & (vmask+1)) == (phys & (vmask+1)),
			"cached 4K pages should be created with virtual addresses having the same 8K alignment of the physical address.\n"
			"vaddr=0x%08lx (8K alignment: 0x%08lx)\n"
			"paddr=0x%08lx (8K alignment: 0x%08lx)\n", vaddr, vaddr & (vmask+1), phys, phys & (vmask+1));
	}

	// If this is a double mapping, we are going to write two slots, and each
	// one has half the vmask.
	if (dbl) vmask >>= 1;

	uint32_t vpn2 = vaddr;
	if (!dbl) vpn2 &= ~(vmask+1); // ????

	// Map the entries
	tlb_map_area_internal(idx, vaddr, vmask, phys,         readwrite, vpn2);
	if (dbl)
		tlb_map_area_internal(idx, vaddr+vmask+1, vmask, phys+vmask+1, readwrite, vpn2);
}
