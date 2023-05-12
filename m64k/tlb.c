#include "tlb.h"
#include <libdragon.h>
#include <stdlib.h>

static int tlb_map_area_internal(uint32_t virt, uint32_t vmask, uint32_t phys, int flags, uint32_t vpn2)
{
	bool low_half = !(virt & (vmask+1));
	int idx;

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
		if (!(flags & TLBF_OVERWRITE)) {
			if (low_half) {
				assertf((exentry0 & 2) == 0, "Duplicated TLB entry with vaddr %08lx (%lx/%lx)", vpn2, exentry0, exentry1);
			} else {
				assertf((exentry1 & 2) == 0, "Duplicated TLB entry with vaddr %08lx (%lx/%lx)", vpn2, exentry0, exentry1);			
			}
		}
		// Reuse the same TLB index.
		idx = existing;
	} else {
		exentry0 = exentry1 = 1<<0;
		idx = C0_WIRED();
		assertf(idx < 32, "TLB exhausted: too many fixed TLB entries");
		C0_WRITE_WIRED(idx+1);
	}

	// Create the new entry
	uint32_t entry = (phys & 0x3FFFF000) >> 6;
	if (!(flags & TLBF_READONLY))
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
    return idx;
}

uint32_t __m64k_tlb_add(void *virt, uint32_t vmask, uint32_t phys, int flags)
{
	uint32_t vaddr = (uint32_t)virt;

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
	default: assertf(0, "invalid vmask %08lx", vmask); abort(); 
    }

	// If this is a double mapping, we are going to write two slots, and each
	// one has half the vmask, and require half the alignment.
	if (dbl) vmask >>= 1;

	// Verify that the addresses are aligned to the needed vmask.
	assertf((phys & vmask) == 0, "physical address %08lx is not aligned to vmask %08lx", phys, vmask);
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

	uint32_t vpn2 = vaddr;
	if (!dbl) vpn2 &= ~(vmask+1); // ????

	// Map the entries
	int idx = tlb_map_area_internal(vaddr, vmask, phys, flags, vpn2);
	if (dbl)
		tlb_map_area_internal(vaddr+vmask+1, vmask, phys+vmask+1, flags, vpn2);

	uint32_t mid = idx<<2;
	bool low_half = !(vaddr & (vmask+1));
	if (dbl)
		mid |= 3;
	else if (low_half)
		mid |= 1;
	else
		mid |= 2;
	return mid;
}

void __m64k_tlb_change(uint32_t mid, uint32_t phys, int flags)
{
	int idx = mid >> 2;
	assertf(idx < 32, "invalid TLB index %d", idx);

	// Read the existing entry
	C0_WRITE_INDEX(idx);
	C0_TLBR();
	
	// Create the new entry
	uint32_t entry = (phys & 0x3FFFF000) >> 6;
	if (!(flags & TLBF_READONLY))
		entry |= (1<<2); // dirty bit
	entry |= 1<<1; // valid bit
	entry |= 1<<0; // global bit

	if (mid & (1<<0))
		C0_WRITE_ENTRYLO0(entry);
	if (mid & (1<<1))
		C0_WRITE_ENTRYLO1(entry);

	// Write the TLB
	C0_TLBWI();
}

void __m64k_tlb_rem(uint32_t mid)
{
	int idx = mid >> 2;
	assertf(idx < 32, "invalid TLB index %d", idx);

	C0_WRITE_INDEX(idx);
	if (mid & 3) {
		C0_WRITE_ENTRYHI(0xFFFFFFFF);
		C0_WRITE_ENTRYLO0(0);
		C0_WRITE_ENTRYLO1(0);
		C0_WRITE_PAGEMASK(0);
		C0_TLBWI();
		return;
	}
	
	C0_TLBR();
	if (mid & 1)
		C0_WRITE_ENTRYLO0(0);
	if (mid & 2)
		C0_WRITE_ENTRYLO1(0);
	C0_TLBWI();
}

__attribute__((constructor))
void __m64k_tlb_reset(void) {
	C0_WRITE_ENTRYHI(0xFFFFFFFF);
	C0_WRITE_ENTRYLO0(0);
	C0_WRITE_ENTRYLO1(0);
	C0_WRITE_PAGEMASK(0);
	for (int i=0;i<32;i++) {
		C0_WRITE_INDEX(i);
		C0_TLBWI();
	}
	C0_WRITE_WIRED(0);
}
