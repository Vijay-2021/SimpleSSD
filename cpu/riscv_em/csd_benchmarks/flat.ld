ENTRY(main)

SECTIONS {
  .text : { *(.text.main) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
  _heap_start = .;
  /DISCARD/ : { *(.comment*) *(.note*) }
}