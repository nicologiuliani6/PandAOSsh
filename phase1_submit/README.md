# PandOSsh – Phase 1  
**Progetto di Laboratorio – A.A. 2025/2026**

## Introduzione

Questo progetto è stato sviluppato nell’ambito del corso di **Sistemi Operativi** per l’anno accademico **2025/2026**.  
L’obiettivo del progetto è l’implementazione di un sistema operativo didattico denominato **PandOSsh**.

Il progetto viene sviluppato utilizzando l’emulatore **µRISCV**, una macchina virtuale per architettura **RISC-V**.

---

## Descrizione del progetto

PandOSsh è un sistema operativo didattico progettato per essere eseguito su architettura **RISC-V** tramite l’emulatore **µRISCV**.

Il progetto è suddiviso in più fasi, ciascuna delle quali estende la precedente con nuove funzionalità del kernel.

### Phase 1

La **Phase 1** prevede l’implementazione delle principali strutture dati di base del kernel, in particolare:

- Process Control Block (PCB)
- Gestione delle code di processi
- Gestione dell’albero dei processi
- Active Semaphore List (ASL)

L’implementazione segue le specifiche fornite e non utilizza allocazioni dinamiche.

---

## Emulatore µRISCV

Il progetto utilizza l’emulatore **µRISCV**, sviluppato dal gruppo *VirtualSquare*.

Il codice sorgente dell’emulatore, insieme alla documentazione e alle istruzioni di installazione, è disponibile nel repository ufficiale:

https://github.com/virtualsquare/uriscv


---

## Ambiente di sviluppo

All’interno della directory del progetto sono forniti:
- le specifiche ufficiali della Phase 1;
- il materiale di supporto;
- i programmi di test per la verifica dell’implementazione.

Il progetto è pensato per essere compilato ed eseguito in ambiente **GNU/Linux**.

---

## Compilazione

Il progetto utilizza **CMake** come sistema di build.

Per compilare il codice sorgente della phase 1:
```bash
mkdir build
cd build
cmake ..
make
```

Aprire **µRISC-V**, caricare **config_machine.json** dentro l'emulatore e avviarlo. Per vedere il terminale andare su Windows -> Terminal 0.

