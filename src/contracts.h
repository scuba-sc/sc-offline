#pragma once
#include "common.h"

void ResolveContractsApi(const Section& text, const Section& rdata);
void ProcessContracts();

// For the contracts built-in. Game thread.
bool ContractsReady();   // the mission system and the contract broker's reply were found
void RecordContractDeath(uint64_t id); // all-actor death event; unknown id 0 is ignored
struct ContractsStatus {
    int known = 0;            // contract definitions read (0 until they're listed, 15 s after you spawn)
    int listed = 0;           // of those, offered in the mobiGlas
    int running = 0;          // accepted contracts running now
    int starting = 0;         // accepted, mission still being built
    long long wallet = -1;    // your aUEC, or -1 when the wallet can't be read
};
ContractsStatus ReadContractsStatus();
