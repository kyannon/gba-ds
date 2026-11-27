ENTRY(_start)
MEMORY {
	ROM (rx) : ORIGIN = 0x08000000, LENGTH = 32M
	IWRAM (rw) : ORIGIN = 0x03000000, LENGTH = 31K
}
SECTIONS {
	.crt0 0x08000000 : { KEEP(*(.crt0)) } > ROM
	/* the widescreen marker's place: 32 bytes of 0xFF a patch fills (docs/WIDESCREEN.md) */
	.widemarker 0x08000200 : { KEEP(*(.widemarker)) } > ROM
	.text : { *(.text*) *(.rodata*) } > ROM
	.bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > IWRAM
	/DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
