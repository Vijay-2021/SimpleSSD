cmake_minimum_required(VERSION 3.12)

project (riscv_em)
set(CMAKE_BUILD_TYPE Release)
add_definitions ("-Wall -Werror -Wextra -Wpedantic")

OPTION(RV_ARCH "RISC-V Arch" "64")
if(RV_ARCH STREQUAL "64")
    add_compile_definitions(RV64)
endif()

OPTION(RISCV_EM_DEBUG "RISC-V Debug Enable" "1")
if(RISCV_EM_DEBUG STREQUAL "1")
    add_compile_definitions(RISCV_EM_DEBUG)
else()
    add_compile_definitions(USE_SIMPLE_UART)
endif()

set(SRC_HELPER 
    src/helpers/fifo.c
    src/helpers/file_helper.c
)

set(SRC_CORE 
    src/core/core.c
    src/core/csr/csr.c
    src/core/pmp/pmp.c
    src/core/trap/trap.c
    src/core/mmu/mmu.c
)

set(SRC_PERIPH
    src/peripherals/clint/clint.c
    src/peripherals/plic/plic.c
    src/peripherals/uart/simple_uart.c
    src/peripherals/uart/uart_8250.c
)


set(SRC_SOC 
    src/soc/riscv_example_soc.c
)



add_executable(riscv_em ${SRC_HELPER} ${SRC_CORE} ${SRC_PERIPH} ${SRC_SOC} src/main.c)
add_library(riscv_em_lib STATIC ${SRC_HELPER} ${SRC_CORE} ${SRC_PERIPH} ${SRC_SOC})

target_include_directories(riscv_em PUBLIC ${PROJECT_SOURCE_DIR})
target_include_directories(riscv_em_lib PUBLIC ${PROJECT_SOURCE_DIR})

target_compile_options(riscv_em PRIVATE -Wall -Wextra -pedantic -Werror)
target_compile_options(riscv_em_lib PRIVATE -Wall -Wextra -pedantic -Werror)

target_link_libraries(riscv_em pthread)
target_link_libraries(riscv_em_lib pthread)

install (TARGETS riscv_em
         ARCHIVE DESTINATION lib
         LIBRARY DESTINATION lib
         RUNTIME DESTINATION bin)

install (TARGETS riscv_em_lib
         ARCHIVE DESTINATION lib
         LIBRARY DESTINATION lib
         RUNTIME DESTINATION bin)

### Debian Package generation
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "https://github.com/franzflasch")
include(CPack)
