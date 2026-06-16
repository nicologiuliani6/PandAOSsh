# Makefile per le U-proc di test del Support Level (Phase 3).
#
# Per ogni programma <name>.c produce <name>.uriscv, l'immagine di un
# device flash precaricata con il load image (.aout) della U-proc, da
# mappare su un device flash nel pannello di configurazione di uRISCV.
#
# Layout flash: blocchi 0..30 = .text/.data, blocco 31 = pagina di stack.

XT_PRG_PREFIX = riscv64-unknown-elf-
CC  = $(XT_PRG_PREFIX)gcc

URISCV_SHARE = /usr/local/share/uriscv
URISCV_INC   = /usr/include

CFLAGS = -ffreestanding -nostdlib -nostartfiles -Wall -O0 -std=gnu99 \
         -march=rv32imafd -mabi=ilp32d -I$(URISCV_INC) -Ih
LDFLAGS = -nostdlib -nostartfiles -march=rv32imafd -mabi=ilp32d \
          -T $(URISCV_SHARE)/uriscvaout.ldscript

# Elenco dei programmi U-proc (un .c per programma).
# Mappatura nome -> ASID -> flash device (vedi shell.c e config macchina):
#   shell=1/flash0  echo=2/flash1  fibEleven=3/flash2
#   uname=4/flash3  sl=5/flash4    calc=6/flash5
PROGS = shell echo fibEleven uname sl calc

# Immagini flash risultanti.
FLASHES = $(PROGS:%=%.uriscv)

.PHONY: all clean
all: $(FLASHES)

# Oggetti comuni a tutte le U-proc.
crtsi.o: crtsi.S
	$(CC) $(CFLAGS) -c $< -o $@

liburiscv.o: $(URISCV_SHARE)/liburiscv.S
	$(CC) $(CFLAGS) -c $< -o $@

# Regola generica: <name>.c -> <name>.o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link della U-proc: <name>.o + crtsi + liburiscv -> ELF eseguibile
%.elf: %.o crtsi.o liburiscv.o
	$(CC) $(LDFLAGS) crtsi.o $< liburiscv.o -o $@

# Load image .aout e immagine flash
%.uriscv: %.elf
	uriscv-elf2uriscv -a $<
	uriscv-mkdev -f $@ $<.aout.uriscv

clean:
	rm -f *.o *.elf *.aout.uriscv $(FLASHES)
