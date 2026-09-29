// Persistent storage of the MAX_PROGRAMS temperature programs on LittleFS
// (one small file per program, only the used steps are written).
#pragma once
#include "program.h"

// Mounts LittleFS (formats it on first use) and creates /duo. Safe to call
// more than once. False = no file system (settings and programs not persistent).
bool storageBegin();

// Mount the file system and load all programs into `progs`. Missing files
// become empty programs named "Program N". If no program exists at all,
// `firstBoot` (if given) becomes program 1.
void programStoreBegin(Program* progs, const Program* firstBoot);
bool programStoreSave(int idx, const Program& p);
void programExample(Program& p);  // demo program used on first boot
