#include <asm/processor.h>
#include "pgtable.h"
#include "../string.h"

/*
 * __force_order is used by special_insns.h asm code to force instruction
 * serialization.
 *
 * It is not referenced from the code, but GCC < 5 with -fPIE would fail
 * due to an undefined symbol. Define it to make these ancient GCCs work.
 */
unsigned long __force_order;

#define BIOS_START_MIN		0x20000U	/* 128K, less than this is insane */
#define BIOS_START_MAX		0x9f000U	/* 640K, absolute maximum */

struct paging_config {
	unsigned long trampoline_start;
	unsigned long l5_required;
};

extern void *trampoline_save;
extern void *pgtable_trampoline;

struct paging_config paging_prepare(void)
{
	struct paging_config paging_config = {};
	unsigned long bios_start, ebda_start, *trampoline;

	/* Check if LA57 is desired and supported */
	if (IS_ENABLED(CONFIG_X86_5LEVEL) && native_cpuid_eax(0) >= 7 &&
			(native_cpuid_ecx(7) & (1 << (X86_FEATURE_LA57 & 31))))
		paging_config.l5_required = 1;

	/*
	 * Find a suitable spot for the trampoline.
	 * This code is based on reserve_bios_regions().
	 */

	ebda_start = *(unsigned short *)0x40e << 4;
	bios_start = *(unsigned short *)0x413 << 10;

	if (bios_start < BIOS_START_MIN || bios_start > BIOS_START_MAX)
		bios_start = BIOS_START_MAX;

	if (ebda_start > BIOS_START_MIN && ebda_start < bios_start)
		bios_start = ebda_start;

	/* Place the trampoline just below the end of low memory, aligned to 4k */
	paging_config.trampoline_start = bios_start - TRAMPOLINE_32BIT_SIZE;
	paging_config.trampoline_start = round_down(paging_config.trampoline_start, PAGE_SIZE);

	trampoline = (unsigned long *)paging_config.trampoline_start;

	/* Preserve trampoline memory */
	memcpy(trampoline_save, trampoline, TRAMPOLINE_32BIT_SIZE);

	/* Clear trampoline memory first */
	memset(trampoline, 0, TRAMPOLINE_32BIT_SIZE);

	/* Copy trampoline code in place */
	memcpy(trampoline + TRAMPOLINE_32BIT_CODE_OFFSET / sizeof(unsigned long),
			&trampoline_32bit_src, TRAMPOLINE_32BIT_CODE_SIZE);

	/*
	 * Set up a new page table that will be used for switching from 4-
	 * to 5-level paging or vice versa. In other cases trampoline
	 * wouldn't touch CR3.
	 *
	 * For 4- to 5-level paging transition, set up current CR3 as the
	 * first and the only entry in a new top-level page table.
	 *
	 * For 5- to 4-level paging transition, copy page table pointed by
	 * first entry in the current top-level page table as our new
	 * top-level page table. We just cannot point to the page table
	 * from trampoline as it may be above 4G.
	 */
	if (paging_config.l5_required) {
		trampoline[TRAMPOLINE_32BIT_PGTABLE_OFFSET] = __native_read_cr3() + _PAGE_TABLE_NOENC;
	} else if (native_read_cr4() & X86_CR4_LA57) {
		unsigned long src;

		src = *(unsigned long *)__native_read_cr3() & PAGE_MASK;
		memcpy(trampoline + TRAMPOLINE_32BIT_PGTABLE_OFFSET / sizeof(unsigned long),
		       (void *)src, PAGE_SIZE);
	}

	return paging_config;
}

void cleanup_trampoline(void *trampoline)
{
	void *cr3 = (void *)__native_read_cr3();

	/*
	 * Move the top level page table out of trampoline memory,
	 * if it's there.
	 */
	if (cr3 == trampoline + TRAMPOLINE_32BIT_PGTABLE_OFFSET) {
		memcpy(pgtable_trampoline, trampoline + TRAMPOLINE_32BIT_PGTABLE_OFFSET, PAGE_SIZE);
		native_write_cr3((unsigned long)pgtable_trampoline);
	}

	/* Restore trampoline memory */
	memcpy(trampoline, trampoline_save, TRAMPOLINE_32BIT_SIZE);
}
