ENTRY(main)

MEMORY {
    FLASH (rx) : ORIGIN = 0x88000000, LENGTH = 0x01000000  /* 16 MB FLASH */
    RAM   (xrw): ORIGIN = 0x88100000, LENGTH = 0x08000000  /* 128 MB RAM */
}

SECTIONS {
    /* Program code & read-only data go into FLASH */
    .text : {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        *(.srodata)
        *(.srodata*)
        . = ALIGN(4);
        _etext   = .;
        _sidata  = _etext;
    } > FLASH

    /* Initialized data is copied from FLASH to RAM */
    .data : AT(_sidata) {
        . = ALIGN(4);
        _sdata     = .;
        _ram_start = .;
        . = ALIGN(4);
        *(.data)
        *(.data*)
        *(.sdata)
        *(.sdata*)
        . = ALIGN(4);
        _edata     = .;
    } > RAM

    /* Zero-initialized data */
    .bss : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(.sbss)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* Heap start marker */
    .heap : {
        . = ALIGN(4);
        _heap_start = .;
    } > RAM
}