cmake_minimum_required(VERSION 3.25)
project(MultiPandOS LANGUAGES C ASM)

set(XT_PRG_PREFIX riscv64-unknown-elf-)
set(URISCV_SRC /usr/local/share/uriscv)
set(URISCV_INC /usr/include)

set(CMAKE_C_COMPILER   ${XT_PRG_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${XT_PRG_PREFIX}gcc)
set(CMAKE_C_LINKER     ${XT_PRG_PREFIX}ld)

add_link_options(-nostdlib -T ${URISCV_SRC}/uriscvcore.ldscript -march=rv32imfd)
add_compile_options(
    -ffreestanding -static -nostartfiles -nostdlib
    -I${URISCV_INC}
    -ggdb -Wall -O0 -std=gnu99
    -march=rv32imafd -mabi=ilp32d
)

# -----------------------------------------------------------------------
# PHASE 1 - testa PCB e ASL
# Produce MultiPandOS.core.uriscv (compatibile con config_machine.json)
# Uso: make phase1
# -----------------------------------------------------------------------
add_executable(MultiPandOS_phase1 EXCLUDE_FROM_ALL
    ./klog.c
    phase1/pcb.c
    phase1/asl.c
    phase1/p1test.c
    ${URISCV_SRC}/crtso.S
    ${URISCV_SRC}/liburiscv.S
)

add_custom_target(phase1
    # Copia l'eseguibile come MultiPandOS così elf2uriscv produce
    # MultiPandOS.core.uriscv e MultiPandOS.stab.uriscv
    COMMAND ${CMAKE_COMMAND} -E copy
        ${PROJECT_BINARY_DIR}/MultiPandOS_phase1
        ${PROJECT_BINARY_DIR}/MultiPandOS
    COMMAND uriscv-elf2uriscv -k ${PROJECT_BINARY_DIR}/MultiPandOS
    BYPRODUCTS MultiPandOS.core.uriscv MultiPandOS.stab.uriscv
    DEPENDS MultiPandOS_phase1
    COMMENT ">>> Build Phase 1 - carica config_machine.json nell'emulatore"
)

# -----------------------------------------------------------------------
# PHASE 2 - testa il Nucleo
# Produce MultiPandOS.core.uriscv (compatibile con config_machine.json)
# Uso: make phase2
# -----------------------------------------------------------------------
add_executable(MultiPandOS_phase2 EXCLUDE_FROM_ALL
    ./klog.c
    phase1/pcb.c
    phase1/asl.c
    phase2/initial.c
    phase2/scheduler.c
    phase2/exceptions.c
    phase2/interrupts.c
    phase2/p2test.c
    ${URISCV_SRC}/crtso.S
    ${URISCV_SRC}/liburiscv.S
)

add_custom_target(phase2
    COMMAND ${CMAKE_COMMAND} -E copy
        ${PROJECT_BINARY_DIR}/MultiPandOS_phase2
        ${PROJECT_BINARY_DIR}/MultiPandOS
    COMMAND uriscv-elf2uriscv -k ${PROJECT_BINARY_DIR}/MultiPandOS
    BYPRODUCTS MultiPandOS.core.uriscv MultiPandOS.stab.uriscv
    DEPENDS MultiPandOS_phase2
    COMMENT ">>> Build Phase 2 - carica config_machine.json nell'emulatore"
)
