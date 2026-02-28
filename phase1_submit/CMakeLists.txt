cmake_minimum_required(VERSION 3.25)
project(MultiPandOS LANGUAGES C ASM)

set(XT_PRG_PREFIX riscv64-unknown-elf-)
set(URISCV_SRC /usr/local/share/uriscv)
set(URISCV_INC /usr/include)

set(CMAKE_C_COMPILER ${XT_PRG_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${XT_PRG_PREFIX}gcc)
set(CMAKE_C_LINKER ${XT_PRG_PREFIX}ld)

add_link_options(-nostdlib -T ${URISCV_SRC}/uriscvcore.ldscript -march=rv32imfd)
add_compile_options(-ffreestanding -static -nostartfiles -nostdlib -I${URISCV_INC} -ggdb -Wall -O0 -std=gnu99 -march=rv32imafd -mabi=ilp32d)

add_executable(MultiPandOS
    ./klog.c phase1/pcb.c phase1/asl.c phase1/p1test.c
    ${URISCV_SRC}/crtso.S ${URISCV_SRC}/liburiscv.S)

add_custom_target(
	MultiPandOSuRISCV ALL
	COMMAND uriscv-elf2uriscv -k ${PROJECT_BINARY_DIR}/MultiPandOS
	BYPRODUCTS MultiPandOS.core.uriscv MultiPandOS.stab.uriscv
	DEPENDS MultiPandOS
)
