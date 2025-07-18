extern "C" void __cxa_pure_virtual() {
    // Infinite loop or trap so you catch errors in simulation
    while (1) {
        // Optionally add debug breakpoint or signal
        // __asm__ volatile("ebreak"); // RISC-V
    }
}