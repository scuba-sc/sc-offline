// SPDX-License-Identifier: GPL-3.0-only
#if defined(SCO_ASOP_SELECTION_COMPOSITION) && !__has_include(<sco/game/asop_selection.h>)
#error "Selected development composition requires its matching Core headers"
#endif
#if __has_include(<sco/game/asop_selection.h>)
// Host-side fixed document adapter. No engine or plugin pointers enter this code.
#include "asop_selection_file.h"
#include <sc_asop_selection.h>
#include <sco/net/sha2.h>
#include <windows.h>
#include <atomic>
#include <cstring>
#include <limits>

bool DataFilePath(char*, DWORD, const char*);
#ifdef SCO_ASOP_SELECTION_TESTING
// Only the standalone test executable links this deterministic crash/race seam.
bool AsopSelectionTestPoint(const char* stage);
#endif
namespace offline::asop_selection {
namespace S = sco::game::asop_selection;
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const { return value != INVALID_HANDLE_VALUE && value != nullptr; }
};
struct Identity {
    DWORD volume, index_high, index_low, size_high, size_low, write_high, write_low;
};
bool Info(HANDLE h, Identity& id) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    id={info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow,info.nFileSizeHigh,info.nFileSizeLow,
        info.ftLastWriteTime.dwHighDateTime,info.ftLastWriteTime.dwLowDateTime};
    return true;
}
S::FileResult ReadOnce(const std::wstring& path, S::Document& doc) {
    Handle f(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!f.valid()) {
        const DWORD error=GetLastError();
        return error==ERROR_FILE_NOT_FOUND ? S::FileResult::NotFound : S::FileResult::Error;
    }
    Identity before{}, after{};
    if (!Info(f.value,before)) return S::FileResult::Error;
    if (before.size_high || before.size_low>SC_ASOP_SELECTION_MAX_FILE_BYTES) return S::FileResult::Invalid;
    std::string bytes(before.size_low,'\0'); DWORD got=0;
    if (!bytes.empty() && (!ReadFile(f.value,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr) || got!=bytes.size())) return S::FileResult::Error;
    if (!Info(f.value,after) || std::memcmp(&before,&after,sizeof(before))) return S::FileResult::Busy;
    doc={};doc.present=true;doc.bytes=std::move(bytes);
    sco_sha256_digest(&after,sizeof(after),doc.identity.data());
    return S::FileResult::Ok;
}
bool Same(const S::Document& a,const S::Document& b) {
    return a.present==b.present && a.bytes==b.bytes && a.identity==b.identity;
}
S::FileResult StableRead(const std::wstring& path,S::Document& out) {
    for (int attempt=0;attempt<3;++attempt) {
        S::Document a,b;
        const auto ra=ReadOnce(path,a), rb=ReadOnce(path,b);
        if (ra==S::FileResult::NotFound && rb==ra) { out={}; return ra; }
        if (ra==S::FileResult::Invalid && rb==ra) return ra;
        if (ra==S::FileResult::Ok && rb==ra && Same(a,b)) {out=std::move(b);return ra;}
        if (ra==S::FileResult::Error || rb==S::FileResult::Error) return S::FileResult::Error;
    }
    return S::FileResult::Busy;
}
bool WriteNew(const std::wstring& path,const void* data,DWORD length,bool& created,const char* kind) {
    created=false;
#ifdef SCO_ASOP_SELECTION_TESTING
    if(!AsopSelectionTestPoint((std::string(kind)+".create").c_str()))return false;
#else
    (void)kind;
#endif
    Handle f(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr));
    if (!f.valid()) return false;
    created=true;
    DWORD n=0;
    bool ok=true;
#ifdef SCO_ASOP_SELECTION_TESTING
    ok=AsopSelectionTestPoint((std::string(kind)+".write").c_str());
#endif
    if(ok)ok=!length || (WriteFile(f.value,data,length,&n,nullptr) && n==length);
#ifdef SCO_ASOP_SELECTION_TESTING
    if(ok)ok=AsopSelectionTestPoint((std::string(kind)+".flush").c_str());
#endif
    if(ok)ok=FlushFileBuffers(f.value)!=0;
    const bool closed=CloseHandle(f.value)!=0;f.value=INVALID_HANDLE_VALUE;
#ifdef SCO_ASOP_SELECTION_TESTING
    if(ok)ok=AsopSelectionTestPoint((std::string(kind)+".close").c_str());
#endif
    return ok && closed;
}
bool Remove(const std::wstring& path) { return DeleteFileW(path.c_str()) || GetLastError()==ERROR_FILE_NOT_FOUND; }
struct DocumentLock {
    Handle mutex;
    bool locked=false;
    explicit DocumentLock(const std::wstring& path) {
        std::wstring normalized=path;
        for(auto& c:normalized){if(c==L'/')c=L'\\';else if(c>=L'a' && c<=L'z')c=static_cast<wchar_t>(c-(L'a'-L'A'));}
        uint8_t hash[32];sco_sha256_digest(normalized.data(),normalized.size()*sizeof(wchar_t),hash);
        static constexpr wchar_t hex[]=L"0123456789abcdef";
        std::wstring name=L"Local\\SCO-ASOP-";
        for (uint8_t c:hash) {name.push_back(hex[c>>4]);name.push_back(hex[c&15]);}
        mutex.value=CreateMutexW(nullptr,FALSE,name.c_str());
        if (mutex.valid()) {const DWORD result=WaitForSingleObject(mutex.value,0);locked=result==WAIT_OBJECT_0 || result==WAIT_ABANDONED;}
    }
    ~DocumentLock() {if(locked)ReleaseMutex(mutex.value);}
};
bool Pending(const std::wstring& path,std::wstring& diagnostic, std::wstring& marker) {
    WIN32_FIND_DATAW entry{};
    Handle search(FindFirstFileW((path+L".sco-*.pending").c_str(),&entry));
    if (search.value==INVALID_HANDLE_VALUE) {
        if (GetLastError()==ERROR_FILE_NOT_FOUND) return false;
        diagnostic=L"Unable to inspect ASOP recovery markers";return true;
    }
    const auto first=std::wstring(entry.cFileName);
    const bool multiple=FindNextFileW(search.value,&entry)!=0;
    const DWORD next_error=multiple?ERROR_SUCCESS:GetLastError();
    // Find handles require FindClose, not CloseHandle.
    FindClose(search.value);search.value=INVALID_HANDLE_VALUE;
    const auto slash=path.find_last_of(L"\\/");
    marker=path.substr(0,slash==std::wstring::npos?0:slash+1)+first;
    diagnostic=L"ASOP recovery required: "+marker;
    if (multiple || next_error!=ERROR_NO_MORE_FILES) marker.clear();
    return true;
}
struct Marker {
    char magic[8];
    uint32_t expected_present;
    uint8_t expected_digest[32],expected_identity[32],candidate_digest[32];
};
std::atomic<unsigned long> serial{0};
bool HasDigest(const std::string& bytes,const uint8_t expected[32]) {
    uint8_t digest[32];sco_sha256_digest(bytes.data(),bytes.size(),digest);
    return std::memcmp(digest,expected,32)==0;
}
// A marker is cleared only when the file and artifacts prove a completed or
// never-started replacement. Ambiguous and divergent states stay recoverable.
bool Recover(const std::wstring& path,const std::wstring& marker) {
    if (marker.size()<8 || marker.substr(marker.size()-8)!=L".pending") return false;
    const std::wstring stem=marker.substr(0,marker.size()-8);
    S::Document m,candidate,current,temp,backup;
    if (StableRead(marker,m)!=S::FileResult::Ok || m.bytes.size()!=sizeof(Marker)) return false;
    Marker record{};std::memcpy(&record,m.bytes.data(),sizeof(record));
    if (std::memcmp(record.magic,"SCOASOP1",8) || record.expected_present>1 ||
        StableRead(stem+L".candidate",candidate)!=S::FileResult::Ok || !HasDigest(candidate.bytes,record.candidate_digest)) return false;
    const auto target_result=StableRead(path,current);
    if (target_result!=S::FileResult::Ok && target_result!=S::FileResult::NotFound) return false;
    const auto backup_result=StableRead(stem+L".bak",backup);
    const auto temp_result=StableRead(stem+L".tmp",temp);
    const bool base=record.expected_present ? target_result==S::FileResult::Ok &&
        HasDigest(current.bytes,record.expected_digest) && std::memcmp(current.identity.data(),record.expected_identity,32)==0
        : target_result==S::FileResult::NotFound;
    const bool unstarted=base && backup_result==S::FileResult::NotFound && temp_result==S::FileResult::Ok &&
        HasDigest(temp.bytes,record.candidate_digest);
    const bool displaced=record.expected_present ? backup_result==S::FileResult::Ok &&
        HasDigest(backup.bytes,record.expected_digest) && std::memcmp(backup.identity.data(),record.expected_identity,32)==0
        : backup_result==S::FileResult::NotFound;
    const bool committed=target_result==S::FileResult::Ok && HasDigest(current.bytes,record.candidate_digest) &&
        displaced && temp_result==S::FileResult::NotFound;
    if (!unstarted && !committed) return false;
    return Remove(stem+L".tmp") && Remove(stem+L".bak") && Remove(stem+L".candidate") && Remove(marker);
}
}
bool FixedFile::Initialize() {
    path_.clear();diagnostic_.clear();char path[32768]{};
    if (!DataFilePath(path,static_cast<DWORD>(sizeof(path)),"asop_ships.txt") || !path[0]) {
        diagnostic_=L"Configured ships-file sibling unavailable";return false;
    }
    const int length=MultiByteToWideChar(CP_ACP,0,path,-1,nullptr,0);
    if (length<=1) { diagnostic_=L"Configured selection path is not representable";return false; }
    std::wstring wide(static_cast<size_t>(length),L'\0');
    if (!MultiByteToWideChar(CP_ACP,0,path,-1,wide.data(),length)) {diagnostic_=L"Configured selection path conversion failed";return false;}
    wide.pop_back();
    const bool drive=wide.size()>=3 && ((wide[0]>=L'A' && wide[0]<=L'Z') || (wide[0]>=L'a' && wide[0]<=L'z')) &&
                     wide[1]==L':' && (wide[2]==L'\\' || wide[2]==L'/');
    const bool unc=wide.size()>=3 && wide[0]==L'\\' && wide[1]==L'\\';
    if (!drive && !unc) {diagnostic_=L"Configured selection root must be absolute";return false;}
    for (wchar_t c:wide) if(c<L' ' || c==L'*' || c==L'?') {
        diagnostic_=L"Configured selection path contains unsupported characters";return false;
    }
    const DWORD needed=GetFullPathNameW(wide.c_str(),0,nullptr,nullptr);
    if (!needed || needed>32768) {diagnostic_=L"Configured selection path cannot be normalized";return false;}
    std::wstring full(needed,L'\0');
    const DWORD written=GetFullPathNameW(wide.c_str(),needed,full.data(),nullptr);
    if (!written || written>=needed) {diagnostic_=L"Configured selection path cannot be normalized";return false;}
    full.resize(written);path_=std::move(full);return true;
}
S::FileAdapter FixedFile::Adapter() { return {this,Read,Replace}; }
S::FileResult FixedFile::Read(void* context,S::Document& out) {
    auto& self=*static_cast<FixedFile*>(context);
    if (self.path_.empty()) return S::FileResult::Error;
    self.diagnostic_.clear();
    DocumentLock lock(self.path_);if(!lock.locked)return S::FileResult::Busy;
    std::wstring marker;
    if (Pending(self.path_,self.diagnostic_,marker)) {
        if (marker.empty() || !Recover(self.path_,marker)) return S::FileResult::OutcomeUnknown;
        self.diagnostic_.clear();
    }
    const auto result=StableRead(self.path_,out);
    if (result!=S::FileResult::Ok && result!=S::FileResult::NotFound) self.diagnostic_=L"Unable to read a stable ASOP selection file";
    return result;
}
S::FileResult FixedFile::Replace(void* context,const S::Document& expected,std::string_view bytes,S::Document& out,const S::CommitPermit& permit) {
    auto& self=*static_cast<FixedFile*>(context);
    if (self.path_.empty() || !permit.begin || bytes.size()>SC_ASOP_SELECTION_MAX_FILE_BYTES) return S::FileResult::Error;
    self.diagnostic_.clear();
    DocumentLock lock(self.path_);if(!lock.locked)return S::FileResult::Busy;
    std::wstring pending;
    if (Pending(self.path_,self.diagnostic_,pending)) return S::FileResult::OutcomeUnknown;
    S::Document now;
    auto read=StableRead(self.path_,now);
    if (read!=S::FileResult::Ok && read!=S::FileResult::NotFound) return S::FileResult::Error;
    if (!Same(now,expected)) return S::FileResult::Conflict;
    // Each transaction retains a distinct candidate copy, temp, backup and marker.
    const auto suffix=L".sco-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial);
    const auto stem=self.path_+suffix, temp=stem+L".tmp", candidate=stem+L".candidate", backup=stem+L".bak", marker=stem+L".pending";
    const DWORD backup_attributes=GetFileAttributesW(backup.c_str());
    if (backup_attributes!=INVALID_FILE_ATTRIBUTES || GetLastError()!=ERROR_FILE_NOT_FOUND) return S::FileResult::Error;
    Marker record{};std::memcpy(record.magic,"SCOASOP1",8);record.expected_present=expected.present?1u:0u;
    sco_sha256_digest(expected.bytes.data(),expected.bytes.size(),record.expected_digest);
    std::memcpy(record.expected_identity,expected.identity.data(),32);
    sco_sha256_digest(bytes.data(),bytes.size(),record.candidate_digest);
    bool made_temp=false,made_candidate=false,made_marker=false;
    if (!WriteNew(temp,bytes.data(),static_cast<DWORD>(bytes.size()),made_temp,"temp")) {if(made_temp)Remove(temp);return S::FileResult::Error;}
    if (!WriteNew(candidate,bytes.data(),static_cast<DWORD>(bytes.size()),made_candidate,"candidate")) {Remove(temp);if(made_candidate)Remove(candidate);return S::FileResult::Error;}
    if (!WriteNew(marker,&record,sizeof(record),made_marker,"marker")) {Remove(temp);Remove(candidate);if(made_marker)Remove(marker);return S::FileResult::Error;}
    self.diagnostic_=L"ASOP recovery artifacts: "+stem;
#ifdef SCO_ASOP_SELECTION_TESTING
    if (!AsopSelectionTestPoint("prepared")) return S::FileResult::OutcomeUnknown;
#endif
    if (!permit.begin(permit.context)) {
        if (Remove(temp) && Remove(candidate) && Remove(marker)) {self.diagnostic_.clear();return S::FileResult::Cancelled;}
        return S::FileResult::OutcomeUnknown;
    }
    const BOOL replaced=expected.present ? ReplaceFileW(self.path_.c_str(),temp.c_str(),backup.c_str(),REPLACEFILE_WRITE_THROUGH,nullptr,nullptr)
                                         : MoveFileExW(temp.c_str(),self.path_.c_str(),MOVEFILE_WRITE_THROUGH);
    if (!replaced) return S::FileResult::OutcomeUnknown;
#ifdef SCO_ASOP_SELECTION_TESTING
    if (!AsopSelectionTestPoint("replaced")) return S::FileResult::OutcomeUnknown;
#endif
    S::Document saved, displaced;
    const auto saved_result=StableRead(self.path_,saved);
    const auto displaced_result=expected.present?StableRead(backup,displaced):S::FileResult::NotFound;
    if (saved_result!=S::FileResult::Ok || saved.bytes!=bytes ||
        (expected.present && (displaced_result!=S::FileResult::Ok || !Same(displaced,expected)))) return S::FileResult::Conflict;
    // Keep the marker until all other cleanup succeeds: interrupted cleanup fails closed.
    if (!Remove(backup) || !Remove(temp) || !Remove(candidate) || !Remove(marker)) return S::FileResult::OutcomeUnknown;
    self.diagnostic_.clear();out=std::move(saved);return S::FileResult::Ok;
}
}

#endif // matching development Core header is available
