#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile real legacy fleet mapping/build functions with synthetic engine reads.

No Windows/game execution: only file/registry/lock/SEH declarations are stand-ins.
The production build and URN/name mapping bodies are extracted unchanged.
SEH syntax is translated to C++ try/catch only for injected read failures; this
never executes or validates Windows SEH or actual invalid-pointer recovery.
"""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class FleetIdentityTest(unittest.TestCase):
    def test_rebuild_preserves_session_identity(self):
        source_path = pathlib.Path(os.environ.get("FLEET_IDENTITY_SOURCE", ROOT / "src/hooks.cpp"))
        source = source_path.read_text()
        globals_ = source[source.index("struct FleetShip {"):source.index("using StrCtorFn", source.index("struct FleetShip {"))]
        build = source[source.index("static int BuildFleetShips()"):source.index("static int EnsureFleetShips()")]
        mapping = source[source.index("static int SlotShipIndex("):source.index("const char* RequestShipFromAtc(")]
        retrieve = source[source.index("static void __fastcall Hook_RetrieveVehicle(uintptr_t asop, uintptr_t slot) {"):source.rindex("#endif")]
        prelude = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include <cassert>
#include <cstdarg>
#include <strings.h>
#define __fastcall
#define __try try
#define __except(x) catch (...)
#define EXCEPTION_EXECUTE_HANDLER 1
#define MAX_PATH 1024
#define _SH_DENYNO 0
#define _TRUNCATE 0
#define _stricmp strcasecmp
using SRWLOCK = int;
#define SRWLOCK_INIT 0
int shared_depth = 0, shared_acquires = 0, input_reads = 0, fail_read = 0;
uint64_t* watched_input = nullptr;
bool mutate_on_acquire = false;
void AcquireSRWLockShared(SRWLOCK*) {
    ++shared_depth; ++shared_acquires;
    if (mutate_on_acquire) { watched_input[0]=0; watched_input[1]=0; }
}
void ReleaseSRWLockShared(SRWLOCK*) { assert(shared_depth==1); --shared_depth; }
constexpr uint64_t kShipUrnMarker = 0x53434F4600000000ull;
constexpr size_t kSlotUrn = 0x1698, kEntitlementSize = 0x150, kAsopAtcId = 0x9F8;
std::string requested_class;
int forwarded = 0;
void g_origRetrieveVehicle(uintptr_t,uintptr_t) { ++forwarded; }
bool Fleet_RetrieveActive() { return false; }
uint64_t LocalPlayerId() { return 1; }
const char* RequestShipFromAtc(uint64_t,uint64_t,const char* name) { requested_class=name; return nullptr; }
constexpr uint16_t kUrnType = 0x1606;
constexpr uint8_t kUrnKindUuid = 3, kUrnIdGuid = 2;
uint64_t _byteswap_uint64(uint64_t n) { return __builtin_bswap64(n); }
template<class T> T Rd(uintptr_t p) {
    if (watched_input && (p==reinterpret_cast<uintptr_t>(watched_input) ||
                          p==reinterpret_cast<uintptr_t>(watched_input+1))) {
        assert(shared_depth==0);
        if (++input_reads==fail_read) throw 1;
    }
    T v; memcpy(&v, reinterpret_cast<void*>(p), sizeof v); return v;
}
template<size_t N> void strncpy_s(char (&out)[N], const char* in, int) { snprintf(out,N,"%s",in); }
void Log(const char*, ...) {}
std::string fixture;
bool ShipsFilePath(char* out, size_t n) { snprintf(out,n,"%s",fixture.c_str()); return true; }
FILE* _fsopen(const char* path, const char* mode, int) { return fopen(path,mode); }
uintptr_t entity_system = 1;
struct { uintptr_t* entitySystem = &entity_system; } g_tp;
template<class T> T VCall(uintptr_t, size_t) { return T(1); }
template<class T> T VCall(uintptr_t, size_t, const char* name) {
    if (!strcmp(name,"Synthetic_A")) return T(101);
    if (!strcmp(name,"Synthetic_B")) return T(102);
    if (!strcmp(name,"Synthetic_C")) return T(103);
    return T(0);
}
template<class T> T VCall(uintptr_t, size_t, uint64_t* guid) { return T(guid[0]); }
'''
        # Registry traversal is an engine seam; production compaction and mapping remain real.
        registry = r'''
static void FindClassGuids(uintptr_t, const uintptr_t* classes) {
    for (int i=0;i<g_shipCount;++i) { g_ships[i].guid[0]=classes[i]; g_ships[i].guid[1]=0; }
}
'''
        checks = r'''
void require(bool condition, const char* reason) {
    if (!condition) { fprintf(stderr,"FAIL: %s\n",reason); exit(1); }
}
void rebuild(const char* rows) {
    FILE* f=fopen(fixture.c_str(),"w"); assert(f); fputs(rows,f); fclose(f);
    free(g_fakeEntitlements); g_fakeEntitlements=nullptr;
    g_shipCount=0; BuildFleetShips(); g_shipsBuilt=true;
}
uint64_t issued(int row) { return Rd<uint64_t>(reinterpret_cast<uintptr_t>(g_fakeEntitlements)+row*kEntitlementSize+0x10); }
int resolve(uint64_t q) { uint64_t id[2]={q,0}; return FleetShipIndexOfUrnId(id); }
int main(int argc,char** argv) {
    assert(argc==3); fixture=argv[1];
    rebuild("Synthetic_A\nSynthetic_B\n");
    const auto a=issued(0), b=issued(1); const int aId=resolve(a), bId=resolve(b);
    const char* aName=FleetShipClass(aId);
    if (!strcmp(argv[2],"capacity")) {
        for (int i=g_shipHistoryCount; i<kMaxFleetHistory; ++i) {
            FleetShip synthetic={}; snprintf(synthetic.name,sizeof synthetic.name,"Synthetic_history_%d",i);
            require(RememberFleetShip(synthetic)==i,"each new history ID must be monotonic");
        }
        FleetShip excess={}; snprintf(excess.name,sizeof excess.name,"Synthetic_excess");
        require(RememberFleetShip(excess)==-1 && g_shipHistoryCount==kMaxFleetHistory,"full history must refuse without recycling");
        rebuild("Synthetic_C\nSynthetic_A\n");
        require(g_shipCount==1 && issued(0)==a,"full history must retain known IDs and refuse new class");
        require(resolve(b)==-1 && !strcmp(aName,"Synthetic_A"),"capacity refusal must preserve historical names");
    } else if (!strcmp(argv[2],"empty")) {
        rebuild("");
        require(g_shipCount==0 && resolve(a)==-1 && resolve(b)==-1,"empty list must revoke every issued ID");
        require(aName==FleetShipClass(aId) && !strcmp(aName,"Synthetic_A"),"empty list must preserve historical name");
        rebuild("Synthetic_A\n");
        require(issued(0)==a,"empty list must not reset session identity");
    } else if (!strcmp(argv[2],"malformed")) {
        uint8_t malformed[kSlotUrn+64]={}; memcpy(malformed+kSlotUrn,&kUrnType,2); malformed[kSlotUrn+0x20]=kUrnIdGuid;
        uint64_t invalid=kShipUrnMarker|0x80000000ull; memcpy(malformed+kSlotUrn+0x10,&invalid,8);
        require(SlotShipIndex(reinterpret_cast<uintptr_t>(malformed))==-1,"out-of-range slot must return exactly -1");
    } else if (!strcmp(argv[2],"snapshot")) {
        uint64_t input[2]={a,0}; watched_input=input; mutate_on_acquire=true;
        require(FleetShipIndexOfUrnId(input)==aId,"input must be copied before lock acquisition mutates engine memory");
        require(input_reads==2 && shared_depth==0,"both input qwords must be read before the balanced lock");
    } else if (!strcmp(argv[2],"read-fault")) {
        uint64_t input[2]={a,0}; watched_input=input;
        for (int fault=1; fault<=2; ++fault) {
            fail_read=fault; input_reads=0; shared_acquires=0;
            require(FleetShipIndexOfUrnId(input)==-1,"either simulated input read fault must be refused");
            require(input_reads==fault && shared_acquires==0 && shared_depth==0,"read fault must never acquire or leak shared lock");
        }
        fail_read=0;
        require(FleetShipIndexOfUrnId(input)==aId && shared_depth==0,"valid lookup must still work after simulated faults");
    } else if (!strcmp(argv[2],"null")) {
        require(FleetShipIndexOfUrnId(nullptr)==-1,"null URN must be refused");
    } else {
    require(FleetShipClass(-1)==nullptr && FleetShipClass(8192)==nullptr,"invalid history IDs must be refused");
    rebuild("Synthetic_B\nSynthetic_A\n");
    require(issued(0)==b && issued(1)==a,"reorder must preserve each class URN");
    require(!strcmp(FleetShipClass(aId),"Synthetic_A"),"stored record must not switch class");
    require(aName==FleetShipClass(aId) && !strcmp(aName,"Synthetic_A"),"published name pointer must stay immutable");
    rebuild("Synthetic_B\nSynthetic_C\n");
    require(resolve(a)==-1,"removed class must not alias another issued row");
    require(!strcmp(FleetShipClass(aId),"Synthetic_A"),"removed historical class must remain resolvable by stable ID");
    require(issued(0)==b && issued(1)!=a && issued(1)!=b,"new class must never recycle session IDs");
    uint8_t slot[kSlotUrn+64]={}; memcpy(slot+kSlotUrn,&kUrnType,2); slot[kSlotUrn+0x20]=kUrnIdGuid;
    memcpy(slot+kSlotUrn+0x10,&a,8);
    require(SlotShipIndex(reinterpret_cast<uintptr_t>(slot))==-1,"stale terminal slot must be refused");
    rebuild("Synthetic_B\nSynthetic_A\n");
    require(issued(0)==b && issued(1)==a,"readding a class must restore its original ID");
    uint64_t swapped[2]={0,_byteswap_uint64(b)};
    require(FleetShipIndexOfUrnId(swapped)==bId,"second-qword byte-swapped ID must resolve");
    memcpy(slot+kSlotUrn+0x10,&b,8);
    require(SlotShipIndex(reinterpret_cast<uintptr_t>(slot))==bId,"current terminal slot must use stable ID");
    uint8_t kiosk[kAsopAtcId+8]={};
    Hook_RetrieveVehicle(reinterpret_cast<uintptr_t>(kiosk),reinterpret_cast<uintptr_t>(slot));
    require(requested_class=="Synthetic_B" && forwarded==0,"retrieve must request stable class, not current row");
    }
    printf("PASS: %s\n",argv[2]);
    free(g_fakeEntitlements);
}
'''
        with tempfile.TemporaryDirectory(prefix=".fleet-identity-", dir=ROOT) as tmp:
            directory = pathlib.Path(tmp)
            cpp = directory / "fleet_identity.cpp"
            cpp.write_text(prelude + globals_ + registry + build + mapping + retrieve + checks)
            executable = directory / "fleet_identity"
            subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-fsanitize=address,undefined", str(cpp), "-o", str(executable)], check=True)
            sanitizer_env = dict(os.environ, UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
            for case in ("rebuild", "malformed", "null", "empty", "capacity", "snapshot", "read-fault"):
                with self.subTest(case=case):
                    result = subprocess.run([str(executable), str(directory / "ships.txt"), case], text=True, capture_output=True, env=sanitizer_env)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
