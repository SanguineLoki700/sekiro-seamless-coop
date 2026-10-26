// Explicit protected integration launch; this is not multiplayer gameplay.
#include "protected_launch.h"
#include "file_open_error.h"
#include "absent_save.h"
#include "receipt_budget.h"
#include "launch_settings.h"
#include "steam_install.h"
#include "launcher_process.h"
#include "preflight.h"
#include "owned_job.h"
#include "skcoop/launch_protocol.h"
#include "skcoop/lan/protocol.hpp"
#include "skcoop/lan/scenario.hpp"
#include <Psapi.h>
#include "skcoop/save_store.hpp"
#include <detours.h>
#include <bcrypt.h>
#include <TlHelp32.h>
#include <ShlObj.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace skcoop::launcher {
namespace {
namespace fs=std::filesystem;
using namespace skcoop::launch;
constexpr std::string_view game_digest="637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856";
constexpr DWORD game_size=68005144;
constexpr ULONGLONG maximum_run_ms=30ull*60ull*1000ull;
void require(bool value,const char* reason) { if(!value) throw std::runtime_error(reason); }
class Handle {
public:
    explicit Handle(HANDLE value=INVALID_HANDLE_VALUE):value_(value){}
    ~Handle(){reset();}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other) noexcept:value_(std::exchange(other.value_,INVALID_HANDLE_VALUE)){}
    Handle& operator=(Handle&& other) noexcept {
        if(this!=&other){reset();value_=std::exchange(other.value_,INVALID_HANDLE_VALUE);}return *this;
    }
    void reset() noexcept { if(valid())CloseHandle(value_);value_=INVALID_HANDLE_VALUE; }
    bool valid() const noexcept{return value_!=nullptr&&value_!=INVALID_HANDLE_VALUE;}
    HANDLE get() const noexcept{return value_;}
private:HANDLE value_;
};
bool equal(std::wstring_view a,std::wstring_view b) {
    return a.size()==b.size()&&CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
}
bool child_path(std::wstring_view path,std::wstring_view ancestor) {
    return path.size()>ancestor.size()+1&&equal(path.substr(0,ancestor.size()),ancestor)&&path[ancestor.size()]==L'\\';
}
fs::path normalized(const fs::path& input) {
    const auto raw=input.wstring();
    require(raw.size()>3&&raw.size()<path_units&&input.is_absolute()&&raw[1]==L':'&&
        (raw[2]==L'\\'||raw[2]==L'/')&&((raw[0]>=L'a'&&raw[0]<=L'z')||(raw[0]>=L'A'&&raw[0]<=L'Z')),
        "An explicit local absolute path of fewer than 1024 UTF-16 units is required");
    for(const auto& component:input.relative_path()) {
        const auto text=component.wstring();
        require(!text.empty()&&text!=L"."&&text!=L".."&&text.back()!=L'.'&&text.back()!=L' '&&
            text.find_first_of(L"<>:\"|?*")==std::wstring::npos,"Ambiguous path component");
    }
    auto result=input.lexically_normal();result.make_preferred();return result;
}
std::wstring final_path(HANDLE handle) {
    std::wstring output(32768,L'\0');
    const auto count=GetFinalPathNameByHandleW(handle,output.data(),static_cast<DWORD>(output.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    require(count>0&&count<output.size(),"Cannot resolve a pinned path");output.resize(count);
    require(output.starts_with(L"\\\\?\\")&&output.size()>=7&&output[5]==L':',"Pinned path is not a local drive path");
    output.erase(0,4);return output;
}
struct Identity { DWORD volume,high,low; bool operator==(const Identity&) const=default; };
class Directory {
public:
    explicit Directory(const fs::path& requested):path(normalized(requested)) {
        auto prefix=path.root_path();
        std::vector<fs::path> prefixes{prefix};
        for(const auto& part:path.relative_path()){prefix/=part;prefixes.push_back(prefix);}
        for(const auto& item:prefixes) {
            Handle handle(CreateFileW(item.c_str(),FILE_READ_ATTRIBUTES|FILE_TRAVERSE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            require(handle.valid(),"Cannot pin an existing directory chain");
            BY_HANDLE_FILE_INFORMATION info{};
            require(GetFileInformationByHandle(handle.get(),&info)&&
                (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),
                "Directory chain contains an unavailable/reparse/non-directory component");
            require(equal(final_path(handle.get()),item.wstring()),"Directory aliases are not accepted");
            identities.push_back({info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow});
            pins.push_back(std::move(handle));
        }
        wchar_t filesystem[32]{};
        require(GetVolumeInformationByHandleW(pins.back().get(),nullptr,0,nullptr,nullptr,nullptr,filesystem,32)&&
            equal(filesystem,L"NTFS"),"Protected launch requires existing NTFS directories");
    }
    fs::path path;
    std::vector<Identity> identities;
private:std::vector<Handle> pins;
};
void separate(const Directory& a,const Directory& b) {
    require(std::find(a.identities.begin(),a.identities.end(),b.identities.back())==a.identities.end()&&
        std::find(b.identities.begin(),b.identities.end(),a.identities.back())==b.identities.end(),
        "Private destination and protected source trees overlap physically");
}
std::string hex(std::span<const std::uint8_t> bytes) {
    std::ostringstream text;text<<std::hex<<std::setfill('0');
    for(auto byte:bytes)text<<std::setw(2)<<static_cast<unsigned>(byte);return text.str();
}
std::wstring ascii_wide(std::string_view text) {return {text.begin(),text.end()};}
std::array<std::uint8_t,32> sha256_handle(HANDLE file,std::uint64_t size) {
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    auto check=[](NTSTATUS status){require(status>=0,"BCrypt operation failed");};
    try {
        check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0));
        DWORD bytes{},returned{};check(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&bytes),sizeof(bytes),&returned,0));
        std::vector<UCHAR> object(bytes),buffer(1024*1024);
        check(BCryptCreateHash(algorithm,&hash,object.data(),bytes,nullptr,0,0));
        LARGE_INTEGER zero{};require(SetFilePointerEx(file,zero,nullptr,FILE_BEGIN)!=FALSE,"Cannot rewind pinned file");
        for(std::uint64_t at=0;at<size;) {
            const auto wanted=static_cast<DWORD>(std::min<std::uint64_t>(buffer.size(),size-at));DWORD read{};
            require(ReadFile(file,buffer.data(),wanted,&read,nullptr)&&read==wanted,"Cannot hash pinned file completely");
            check(BCryptHashData(hash,buffer.data(),read,0));at+=read;
        }
        std::array<std::uint8_t,32> result{};check(BCryptFinishHash(hash,result.data(),static_cast<ULONG>(result.size()),0));
        BCryptDestroyHash(hash);hash=nullptr;BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;return result;
    } catch(...) {if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);throw;}
}
class File {
public:
    File(const fs::path& requested,DWORD sharing,std::uint64_t maximum=128u*1024u*1024u,bool hash=true,bool allow_empty=false)
        :path(normalized(requested)),parent(path.parent_path()),handle(CreateFileW(path.c_str(),GENERIC_READ,sharing,nullptr,
            OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)) {
        if(!handle.valid())throw FileOpenError(path,GetLastError());
        BY_HANDLE_FILE_INFORMATION info{};
        require(GetFileInformationByHandle(handle.get(),&info)&&info.nNumberOfLinks==1&&
            !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),"File is a directory/reparse point/hardlink or unavailable");
        require(equal(final_path(handle.get()),path.wstring()),"File alias rejected");
        size=(static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
        require((allow_empty||size>0)&&size<=maximum,"Selected file size outside bound");
        if(hash)digest=sha256_handle(handle.get(),size);
    }
    void read(std::uint64_t offset,void* destination,DWORD bytes) const {
        require(offset<=size&&bytes<=size-offset,"PE/file range is truncated");
        LARGE_INTEGER position{};position.QuadPart=static_cast<LONGLONG>(offset);DWORD count{};
        require(SetFilePointerEx(handle.get(),position,nullptr,FILE_BEGIN)&&ReadFile(handle.get(),destination,bytes,&count,nullptr)&&count==bytes,
            "Cannot read pinned file range");
    }
    bool unchanged() const {
        LARGE_INTEGER current{};require(GetFileSizeEx(handle.get(),&current)&&current.QuadPart>=0,"Cannot recheck pinned size");
        return static_cast<std::uint64_t>(current.QuadPart)==size&&sha256_handle(handle.get(),size)==digest;
    }
    fs::path path;Directory parent;Handle handle;std::uint64_t size{};std::array<std::uint8_t,32> digest{};
};
class OriginalFile {
    fs::path path;
    std::unique_ptr<AbsentSave> absent;
public:
    std::unique_ptr<File> file;
    std::array<std::uint8_t,32> digest{};
    OriginalFile(const fs::path& selected,bool allow_missing):path(normalized(selected)) {
        const auto attrs=GetFileAttributesW(path.c_str());const auto error=GetLastError();
        if(allow_missing&&attrs==INVALID_FILE_ATTRIBUTES&&error==ERROR_FILE_NOT_FOUND)absent=std::make_unique<AbsentSave>(path);
        else {file=std::make_unique<File>(path,0,64u*1024u*1024u,true,allow_missing);digest=file->digest;}
    }
    HANDLE lock_handle()const noexcept{return file?file->handle.get():absent->parent_handle();}
    bool present()const noexcept{return static_cast<bool>(file);}
    bool expected_state()const noexcept{return file||absent->still_absent();}
    bool unchanged()const{return file?file->unchanged():absent->still_absent();}
    std::string json_digest()const{return file?std::string("\"")+hex(sha256_handle(file->handle.get(),file->size))+"\"":"null";}
};
void validate_runtime_pe(const File& file) {
    IMAGE_DOS_HEADER dos{};file.read(0,&dos,sizeof(dos));
    require(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>=static_cast<LONG>(sizeof(dos)),"Runtime DOS header rejected");
    IMAGE_NT_HEADERS64 nt{};file.read(static_cast<std::uint64_t>(dos.e_lfanew),&nt,sizeof(nt));
    require(nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&
        (nt.FileHeader.Characteristics&IMAGE_FILE_DLL)&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC&&
        nt.FileHeader.NumberOfSections>0&&nt.FileHeader.NumberOfSections<=96&&
        nt.FileHeader.SizeOfOptionalHeader>=sizeof(IMAGE_OPTIONAL_HEADER64),"Runtime must be a native AMD64 DLL");
    require(nt.OptionalHeader.NumberOfRvaAndSizes>IMAGE_DIRECTORY_ENTRY_EXPORT&&
        (!nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress),"Managed/no-export runtime rejected");
    std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
    file.read(static_cast<std::uint64_t>(dos.e_lfanew)+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader,
        sections.data(),static_cast<DWORD>(sections.size()*sizeof(IMAGE_SECTION_HEADER)));
    const auto raw=[&](DWORD rva,DWORD count)->std::uint64_t {
        if(rva<nt.OptionalHeader.SizeOfHeaders&&count<=nt.OptionalHeader.SizeOfHeaders-rva)return rva;
        for(const auto& section:sections) if(rva>=section.VirtualAddress&&rva-section.VirtualAddress<=section.SizeOfRawData&&
            count<=section.SizeOfRawData-(rva-section.VirtualAddress))return static_cast<std::uint64_t>(section.PointerToRawData)+rva-section.VirtualAddress;
        throw std::runtime_error("Unbacked runtime export RVA");
    };
    const auto& directory=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    require(directory.Size>=sizeof(IMAGE_EXPORT_DIRECTORY),"Runtime needs an export directory");
    IMAGE_EXPORT_DIRECTORY exports{};file.read(raw(directory.VirtualAddress,sizeof(exports)),&exports,sizeof(exports));
    require(exports.Base<=1&&exports.NumberOfFunctions>1-exports.Base,"Runtime ordinal 1 missing");
    const auto index=1-exports.Base;DWORD function{};
    require(index<96&&exports.AddressOfFunctions<=MAXDWORD-index*sizeof(DWORD),"Invalid ordinal table");
    file.read(raw(exports.AddressOfFunctions+index*static_cast<DWORD>(sizeof(DWORD)),sizeof(function)),&function,sizeof(function));
    require(function>0&&function<nt.OptionalHeader.SizeOfImage&&
        !(function>=directory.VirtualAddress&&function-directory.VirtualAddress<directory.Size),"Runtime ordinal 1 must be a local native export");
}

struct Options {fs::path game,original,runtime,roaming,coop_root,output,config;std::wstring account;bool actor_follow_test{},actor_locomotion_test{},actor_pad_reference_test{},actor_graph_locomotion_test{},actor_lan_test{},independent_profile{},combat_preview{};std::uint32_t lan_role{};std::string lan_code;};
Options options_from(const fs::path& config,const fs::path& runtime_root=executable_directory()) {
    const auto settings=read_launch_settings(config);
    const auto installed=inspect_steam_install();
    require(installed.build_id=="5794815","Unsupported Steam build ID");
    Options result;
    result.config=normalized(fs::absolute(config));
    result.game=normalized(settings.test_executable);
    result.original=normalized(installed.executable);
    result.runtime=normalized(runtime_root/L"skcoop_runtime.dll");
    result.coop_root=normalized(settings.coop_profile_root);
    require(result.coop_root.wstring().size()<MAX_PATH,"Cooperative roaming root exceeds the native AppData API bound");
    result.output=normalized(settings.receipt_directory);result.account=settings.account;
    result.actor_follow_test=settings.actor_follow_test;
    result.actor_locomotion_test=settings.actor_locomotion_test;
    result.actor_pad_reference_test=settings.actor_pad_reference_test;
    result.actor_graph_locomotion_test=settings.actor_graph_locomotion_test;
    result.independent_profile=settings.independent_profile;result.combat_preview=settings.combat_preview;result.actor_lan_test=settings.actor_lan_test;result.lan_role=settings.lan_role;result.lan_code=settings.lan_code;
    if(result.combat_preview)require(result.lan_role==1&&lan::parse_ticket(result.lan_code)->address=="127.0.0.1","Combat preview requires a local host ticket");
    wchar_t roaming[MAX_PATH]{};
    require(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,roaming)),"Cannot resolve actual roaming profile");
    result.roaming=normalized(roaming);
    const auto common=normalized(installed.library_root/L"steamapps"/L"common");
    require(equal(result.game.filename().wstring(),L"sekiro.exe")&&
        equal(result.game.parent_path().parent_path().wstring(),common.wstring())&&
        !equal(result.game.parent_path().wstring(),result.original.parent_path().wstring()),
        "Only an independent sibling test installation inside the same Steam library is supported");
    require(!child_path(result.runtime.wstring(),common.wstring()),"Runtime must remain outside Steam game installs");
    return result;
}
bool another_game(DWORD allowed_pid=0) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));require(snapshot.valid(),"Cannot inspect existing process names");
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    require(Process32FirstW(snapshot.get(),&entry)!=FALSE,"Cannot enumerate existing process names");
    do {if(entry.th32ProcessID!=allowed_pid&&equal(entry.szExeFile,L"sekiro.exe"))return true;}while(Process32NextW(snapshot.get(),&entry));
    require(GetLastError()==ERROR_NO_MORE_FILES,"Process enumeration incomplete");return false;
}
std::string quote(std::string_view value) {
    std::string result="\"";
    for(const auto c:value){if(c=='"'||c=='\\'){result+='\\';result+=c;}
        else if(static_cast<unsigned char>(c)<32){std::ostringstream escape;escape<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<unsigned>(static_cast<unsigned char>(c));result+=escape.str();}
        else result+=c;}return result+'"';
}
class Receipt {
public:
    Receipt(const Directory& directory,const std::array<std::uint8_t,16>& nonce)
        :path(directory.path/(L"game-test-"+ascii_wide(hex(nonce))+L".jsonl")) {
        handle=Handle(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        require(handle.valid(),"Cannot create exclusive new local launch receipt");
        require(equal(final_path(handle.get()),path.wstring()),"Receipt canonical identity changed");
    }
    void emit(const std::string& json) {
        require(budget.permits(json.size()),"Receipt size bound exceeded");
        const auto line=json+"\n";DWORD count{};
        require(WriteFile(handle.get(),line.data(),static_cast<DWORD>(line.size()),&count,nullptr)&&count==line.size()&&FlushFileBuffers(handle.get()),
            "Cannot persist local launch receipt");budget.committed(json.size());std::cout<<line<<std::flush;
    }
    bool sample(const std::string& json) {
        if(!budget.permits(json.size(),true))return false;
        emit(json);return true;
    }
    fs::path path;
private:Handle handle;ReceiptBudget budget;
};
std::vector<wchar_t> environment() {
    auto* raw=GetEnvironmentStringsW();require(raw!=nullptr,"Cannot copy child environment");
    std::vector<std::wstring> entries;
    try {
        std::size_t units{};
        for(auto* entry=raw;*entry;entry+=std::wcslen(entry)+1) {
            const std::wstring value(entry);units+=value.size()+1;require(units<1024*1024&&entries.size()<8192,"Environment exceeds bound");
            const auto split=value.find(L'=');
            if(split!=std::wstring::npos&&(equal(std::wstring_view(value).substr(0,split),L"SteamAppId")||equal(std::wstring_view(value).substr(0,split),L"SteamGameId")))continue;
            entries.push_back(value);
        }
        FreeEnvironmentStringsW(raw);raw=nullptr;
    } catch(...){if(raw)FreeEnvironmentStringsW(raw);throw;}
    entries.emplace_back(L"SteamAppId=814380");entries.emplace_back(L"SteamGameId=814380");
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_LESS_THAN;});
    std::vector<wchar_t> block;
    for(const auto& entry:entries){block.insert(block.end(),entry.begin(),entry.end());block.push_back(L'\0');}block.push_back(L'\0');return block;
}
std::string ansi_path(const fs::path& path) {
    const auto text=path.wstring();BOOL replacement{};
    const bool utf8=GetACP()==CP_UTF8;
    const auto flags=utf8?WC_ERR_INVALID_CHARS:WC_NO_BEST_FIT_CHARS;
    auto* substituted=utf8?nullptr:&replacement;
    const auto n=WideCharToMultiByte(CP_ACP,flags,text.c_str(),-1,nullptr,0,nullptr,substituted);
    require(n>0&&n<=1024&&!replacement,"Detours DLL path cannot be represented exactly in the current codepage");
    std::string result(static_cast<std::size_t>(n),'\0');
    require(WideCharToMultiByte(CP_ACP,flags,text.c_str(),-1,result.data(),n,nullptr,substituted)==n&&!replacement,
        "Cannot encode exact Detours DLL path");return result;
}
void copy_text(wchar_t* destination,std::size_t capacity,std::wstring_view source) {
    require(source.size()<capacity,"Payload string too long");std::copy(source.begin(),source.end(),destination);destination[source.size()]=L'\0';
}
LONG load(const volatile LONG& value){return InterlockedCompareExchange(const_cast<volatile LONG*>(&value),0,0);}
LONG64 load(const volatile LONG64& value){return InterlockedCompareExchange64(const_cast<volatile LONG64*>(&value),0,0);}
std::string counters(const SharedStatus& s) {
    std::ostringstream value;
    value<<"{\"phase\":"<<load(s.phase)<<",\"fatal_error\":"<<load(s.fatal_error)<<",\"hooks_mask\":"<<load(s.hooks_mask)
        <<",\"per_operation_counts_may_be_unmeasured\":true,\"routed_opens\":"<<load(s.routed_opens)
        <<",\"routed_metadata\":"<<load(s.routed_metadata)<<",\"routed_copies\":"<<load(s.routed_copies)<<",\"routed_deletes\":"<<load(s.routed_deletes)
        <<",\"denied_operations\":"<<load(s.denied_operations)<<",\"primary_write_opens\":"<<load(s.primary_write_opens)
        <<",\"backup_write_opens\":"<<load(s.backup_write_opens)<<",\"unsupported_operations\":"<<load(s.unsupported_operations)
        <<",\"successful_primary_reads\":"<<load(s.successful_primary_reads)<<",\"successful_backup_reads\":"<<load(s.successful_backup_reads)
        <<",\"process_detach_count\":"<<load(s.process_detach_count)<<",\"native_aggregate\":{\"redirected\":"<<load(s.native_redirected)
        <<",\"denied\":"<<load(s.native_denied)<<",\"passthrough\":"<<load(s.native_passthrough)<<",\"internal_bypass\":"<<load(s.native_internal_bypass)
        <<",\"appdata_redirects\":"<<load(s.native_appdata_redirects)<<",\"directory_queries\":"<<load(s.native_directory_queries)
        <<",\"copy_failures\":"<<load(s.native_copy_failures)<<",\"destination_may_be_modified\":"<<load(s.native_destination_may_be_modified)<<"}"
        <<",\"actor\":{\"phase\":"<<load(s.actor_phase)<<",\"error\":"<<load(s.actor_error)
        <<",\"creation_calls\":"<<load(s.actor_creation_calls)<<",\"ticks\":"<<load(s.actor_ticks)
        <<",\"pose_writes\":"<<load(s.actor_pose_writes)<<",\"motion_paused\":"<<load(s.actor_motion_paused)
        <<",\"creation_attempts\":"<<load(s.actor_creation_attempts)<<",\"world_callbacks\":"<<load(s.actor_world_callbacks)
        <<",\"callback_total_us\":"<<load(s.actor_callback_total_us)<<",\"callback_max_us\":"<<load(s.actor_callback_max_us)
        <<",\"factory_us\":"<<load(s.actor_factory_us)<<",\"max_frame_interval_us\":"<<load(s.actor_max_frame_interval_us)
        <<",\"locomotion_writes\":"<<load(s.actor_locomotion_writes)<<",\"zero_commands\":"<<load(s.actor_zero_commands)
        <<",\"controller_callbacks\":"<<load(s.actor_controller_callbacks)<<",\"controller_context_errors\":"<<load(s.actor_controller_context_errors)
        <<",\"source_captures\":"<<load(s.actor_source_captures)<<",\"source_nonzero\":"<<load(s.actor_source_nonzero)
        <<",\"action_finalizes\":"<<load(s.actor_action_finalizes)<<",\"invalid_deltas\":"<<load(s.actor_invalid_deltas)
        <<",\"observed_displacement_mm\":"<<load(s.actor_observed_displacement_mm)
        <<",\"graph_writes\":"<<load(s.actor_graph_writes)<<",\"graph_nonzero\":"<<load(s.actor_graph_nonzero)
        <<"},\"lan\":{\"state\":"<<load(s.lan_state)<<",\"error\":"<<load(s.lan_error)<<",\"system_error\":"<<load(s.lan_system_error)
        <<",\"wait\":"<<load(s.lan_wait)<<",\"frames_sent\":"<<load(s.lan_frames_sent)<<",\"frames_received\":"<<load(s.lan_frames_received)
        <<",\"rejected\":"<<load(s.lan_rejected)<<",\"stale_frames\":"<<load(s.lan_stale_frames)
        <<"},\"actions\":{\"error\":"<<load(s.action_error)<<",\"sprinting\":"<<load(s.action_sprinting)
        <<",\"captured\":"<<load(s.action_captured)<<",\"replayed\":"<<load(s.action_replayed)
        <<",\"discarded\":"<<load(s.action_discarded)<<",\"released\":"<<load(s.action_released)
        <<",\"ignored_sources\":"<<load(s.action_ignored_sources)<<",\"unsupported_source\":"<<load(s.action_unsupported_source)
        <<",\"jump_captured\":"<<load(s.action_jump_captured)<<",\"step_captured\":"<<load(s.action_step_captured)<<",\"sprint_captured\":"<<load(s.action_sprint_captured)
        <<",\"jump_replayed\":"<<load(s.action_jump_replayed)<<",\"step_replayed\":"<<load(s.action_step_replayed)<<",\"sprint_replayed\":"<<load(s.action_sprint_replayed)
        <<",\"guarding\":"<<load(s.action_guarding)<<",\"guard_rearm_required\":"<<load(s.action_guard_rearm_required)
        <<",\"attack_captured\":"<<load(s.action_attack_captured)<<",\"attack_replayed\":"<<load(s.action_attack_replayed)
        <<",\"guard_enter_captured\":"<<load(s.action_guard_enter_captured)<<",\"guard_exit_captured\":"<<load(s.action_guard_exit_captured)
        <<",\"guard_enter_replayed\":"<<load(s.action_guard_enter_replayed)<<",\"guard_exit_replayed\":"<<load(s.action_guard_exit_replayed)<<"}}";
    return value.str();
}
std::string process_metrics(HANDLE process) {
    FILETIME created{},exited{},kernel{},user{};
    PROCESS_MEMORY_COUNTERS memory{sizeof(memory)};
    const auto count=[](FILETIME t) { return (static_cast<std::uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime; };
    const bool cpu=GetProcessTimes(process,&created,&exited,&kernel,&user)!=FALSE;
    const bool ram=K32GetProcessMemoryInfo(process,&memory,sizeof(memory))!=FALSE;
    return "{\"cpu_100ns\":"+(cpu?std::to_string(count(kernel)+count(user)):"null")+
        ",\"working_set_bytes\":"+(ram?std::to_string(memory.WorkingSetSize):"null")+"}";
}
class Child {
public:
    ~Child(){stop();if(thread)CloseHandle(thread);if(process)CloseHandle(process);}
    void stop() noexcept {
        if(!process||WaitForSingleObject(process,0)==WAIT_OBJECT_0)return;
        TerminateProcess(process,70); // Only the exact child returned by our successful creation.
        while(WaitForSingleObject(process,10000)!=WAIT_OBJECT_0)
            std::cerr<<"MANUAL_GUARD: owned child did not terminate; original save locks remain held.\n"<<std::flush;
    }
    HANDLE process{},thread{};DWORD pid{};
};
void wait_foreign_close(Receipt& receipt,DWORD own_pid,bool& detected,bool& monitor_error) noexcept {
    bool reported=false;
    for(;;) {
        bool present=false;
        try {present=another_game(own_pid);}
        catch(...) {monitor_error=true;present=true;}
        if(!present)return;
        // A failed enumeration is uncertainty, never permission to release pins.
        if(!monitor_error)detected=true;
        if(!reported) {
            try {receipt.emit("{\"event\":\"manual_action_required\",\"reason\":\"foreign_game_or_monitor_uncertain\",\"original_save_locks_held\":true,\"foreign_process_terminated\":false}");}
            catch(...) {std::cerr<<"Private save locks remain held; log unavailable.\n";}
            reported=true;
        }
        Sleep(500);
    }
}
LaunchStatus launch_snapshot(const SharedStatus& s,std::string_view receipt_name,bool finished=false,bool completed=false) {
    return {load(s.phase)==static_cast<LONG>(Phase::ready),finished,completed,s.child_pid,static_cast<std::uint32_t>(load(s.hooks_mask)),
        static_cast<std::uint32_t>(load(s.actor_phase)),static_cast<std::uint32_t>(load(s.actor_error)),static_cast<std::uint32_t>(load(s.lan_state)),
        static_cast<std::uint32_t>(load(s.lan_error)),static_cast<std::uint32_t>(load(s.lan_wait)),static_cast<std::uint64_t>(load(s.lan_frames_sent)),static_cast<std::uint64_t>(load(s.lan_frames_received)),load(s.actor_motion_paused)!=0,
        static_cast<std::uint32_t>(load(s.action_error)),static_cast<std::uint64_t>(load(s.action_captured)),static_cast<std::uint64_t>(load(s.action_replayed)),
        load(s.action_guard_rearm_required)!=0,load(s.action_guarding)!=0,std::string(receipt_name)};
}
int run(const Options& options, bool check_only,const LaunchObserver& observer={}) {
    require(!another_game(),"Close all existing Sekiro processes before starting this isolated harness");
    File game(options.game,FILE_SHARE_READ);
    require(game.size==game_size&&hex(game.digest)==game_digest,"Independent executable digest/size mismatch");
    const auto inspected=skcoop::launcher::inspect_executable(game.path);
    require(inspected.sha256==game_digest&&inspected.machine==IMAGE_FILE_MACHINE_AMD64,"Independent game PE rejected");
    File original(options.original,FILE_SHARE_READ);
    require(original.size==game_size&&hex(original.digest)==game_digest,"Installed Steam executable is unsupported");
    separate(original.parent,game.parent);
    File runtime(options.runtime,FILE_SHARE_READ);validate_runtime_pe(runtime);
    // Pin native dependencies in both copies; compare the bytes without loading
    // game/Steam DLLs into the launcher. No DLL is deployed in either install.
    std::vector<std::unique_ptr<File>> companions;
    for(const auto name:{L"steam_api64.dll",L"bink2w64.dll",L"oo2core_6_win64.dll",L"amd_ags_x64.dll",
                          L"fmod_event64.dll",L"fmod_event_net64.dll",L"fmodex64.dll"}) {
        auto source=std::make_unique<File>(original.path.parent_path()/name,FILE_SHARE_READ);
        auto copy=std::make_unique<File>(game.path.parent_path()/name,FILE_SHARE_READ);
        require(source->size==copy->size&&source->digest==copy->digest,"Copied native dependency differs from original Steam install");
        companions.push_back(std::move(source));companions.push_back(std::move(copy));
    }
    // Large archives are checked for independent regular files and matching
    // lengths. This is not a cryptographic verification of Steam's depots.
    for(unsigned i=1;i<=5;++i)for(const auto extension:{L".bdt",L".bhd"}) {
        const auto name=L"Data"+std::to_wstring(i)+extension;
        auto source=std::make_unique<File>(original.path.parent_path()/name,FILE_SHARE_READ,20ull*1024*1024*1024,false);
        auto copy=std::make_unique<File>(game.path.parent_path()/name,FILE_SHARE_READ,20ull*1024*1024*1024,false);
        require(source->size==copy->size,"Copied data archive is incomplete");
        companions.push_back(std::move(source));companions.push_back(std::move(copy));
    }
    for(const auto name:{L"dinput8.dll",L"dxgi.dll",L"winmm.dll",L"dsound.dll",L"version.dll"})
        require(GetFileAttributesW((game.path.parent_path()/name).c_str())==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND,
                "Independent test install contains an unsupported proxy loader or cannot be inspected");
    File appid(game.path.parent_path()/L"steam_appid.txt",FILE_SHARE_READ,16);
    std::array<char,16> appid_text{};appid.read(0,appid_text.data(),static_cast<DWORD>(appid.size));
    const std::string appid_value(appid_text.data(),static_cast<std::size_t>(appid.size));
    require(appid_value=="814380"||appid_value=="814380\n"||appid_value=="814380\r\n","Test install requires the correct pre-existing Steam AppID file");
    Directory roaming(options.roaming),coop(options.coop_root),output(options.output);
    Directory source_account(roaming.path/L"Sekiro"/options.account),destination_account(coop.path/L"Sekiro"/options.account);
    separate(roaming,coop);separate(source_account,destination_account);
    separate(roaming,output);separate(coop,output);
    separate(game.parent,output);separate(original.parent,output);
    separate(roaming,game.parent);separate(coop,game.parent);
    separate(roaming,original.parent);separate(coop,original.parent);
    {
        for(const auto name:{L"S0000.co2",L"S0000.co2.bak"}) {
            const auto path=destination_account.path/name;
            if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES) {
                File checked(path,FILE_SHARE_READ,64u*1024u*1024u);
            } else require(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot inspect private save leaf");
        }
        const auto path=destination_account.path/L".skcoop-store.lock";
        Handle lease(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(lease.valid()) {
            BY_HANDLE_FILE_INFORMATION info{};
            require(GetFileInformationByHandle(lease.get(),&info)&&info.nNumberOfLinks==1&&
                !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
                equal(final_path(lease.get()),path.wstring()),"Unsafe private profile lease");
        } else require(GetLastError()==ERROR_FILE_NOT_FOUND,"Isolated profile is already in use or unavailable");
    }
    if(!check_only) {
        auto probe_store=skcoop::saves::CoOpSaveStore::open({destination_account.path.wstring(),
            (roaming.path/L"Sekiro").wstring(),L"co2"});
        require(static_cast<bool>(probe_store),"Cannot acquire prepared isolated save profile");
        // DLL acquires its own process-lifetime lease; it refuses if another
        // controller acquires it in this interval. There is no solo fallback.
    }
    // No creation or repair of normal saves. Deny ALL other reads and writes for
    // the entire test, including a process launched externally through Steam.
    OriginalFile primary(source_account.path/L"S0000.sl2",options.independent_profile);
    OriginalFile backup(source_account.path/L"S0000.sl2.bak",options.independent_profile);
    if(options.actor_lan_test&&!options.independent_profile){File seed(destination_account.path/L"S0000.co2",FILE_SHARE_READ,64u*1024u*1024u);
        require(seed.digest==primary.digest,"LAN controlled fixture requires a fresh isolated copy of this original save");}
    std::unique_ptr<File> graphics;
    const auto graphics_path=roaming.path/L"Sekiro"/L"GraphicsConfig.xml";
    if(GetFileAttributesW(graphics_path.c_str())!=INVALID_FILE_ATTRIBUTES)
        graphics=std::make_unique<File>(graphics_path,0,1024u*1024u);
    else require(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot inspect original graphics configuration");
    if(check_only) {
        const auto launcher=executable_directory()/L"skcoop_launcher.exe";
        std::wcout<<L"Supported Steam build: 5794815 (executable SHA-256 verified)\n"
            L"Independent executable/native dependencies: verified; archive lengths match\n"
            L"Profile paths/files and original save locks: checked; launch revalidates the lease\n"
            L"Inspection only; no process created and no DLL loaded.\n"
            L"Steam launch option for this explicit integration test:\n"
            <<quote_argument(launcher.wstring())<<L" --steam-start ";
        if(!equal(options.config.wstring(),(executable_directory()/L"config"/L"launch.ini").wstring()))
            std::wcout<<quote_argument(options.config.wstring())<<L" ";
        std::wcout<<L"%command%\n";
        return 0;
    }
    std::array<std::uint8_t,16> nonce{};
    require(BCryptGenRandom(nullptr,nonce.data(),static_cast<ULONG>(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Cannot generate handshake nonce");
    Receipt receipt(output,nonce);
    const auto receipt_name="game-test-"+hex(nonce)+".jsonl";
    receipt.emit("{\"event\":\"prepared\",\"protocol\":"+std::to_string(protocol_version)+",\"nonce\":"+quote(hex(nonce))+",\"exe_sha256\":"+quote(hex(game.digest))+
        ",\"runtime_sha256\":"+quote(hex(runtime.digest))+",\"solo_primary_before\":"+primary.json_digest()+",\"solo_backup_before\":"+backup.json_digest()+",\"solo_primary_present\":"+(primary.present()?"true":"false")+",\"solo_backup_present\":"+(backup.present()?"true":"false")+
        ",\"solo_handles_share_mode\":"+std::string(primary.present()&&backup.present()?"0":"null")+",\"missing_originals_monitored\":"+(!primary.present()||!backup.present()?"true":"false")+",\"original_saves_created\":false}");
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SharedStatus),nullptr));
    Handle ready(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    require(mapping.valid()&&ready.valid(),"Cannot prepare unnamed launch handshake objects");
    auto* shared=static_cast<SharedStatus*>(MapViewOfFile(mapping.get(),FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(SharedStatus)));
    require(shared!=nullptr,"Cannot map shared handshake status");
    struct View {void* pointer;~View(){UnmapViewOfFile(pointer);}} view{shared};
    std::memset(shared,0,sizeof(*shared));shared->struct_size=sizeof(*shared);shared->version=protocol_version;shared->host_pid=GetCurrentProcessId();
    std::copy(nonce.begin(),nonce.end(),shared->nonce);InterlockedExchange(&shared->phase,static_cast<LONG>(Phase::payload_prepared));
    OwnedJob job; // Closing/losing the host kills only its assigned child.
    Child child;
    bool ready_observed{},foreign_detected{},monitor_error{},timed_out{};DWORD exit_code=STILL_ACTIVE;
    struct FinalProtection {
        Child& child;Receipt& receipt;bool& foreign;bool& monitor_error;bool active=true;
        ~FinalProtection() {
            if(active){child.stop();wait_foreign_close(receipt,child.pid,foreign,monitor_error);}
        }
    } final_protection{child,receipt,foreign_detected,monitor_error};
    std::string failure;
    try {
        auto block=environment();auto dll=ansi_path(runtime.path);
        auto command=quote_argument(game.path.wstring());
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        require(DetourCreateProcessWithDllExW(game.path.c_str(),command.data(),nullptr,nullptr,FALSE,
            CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT|CREATE_NEW_PROCESS_GROUP,block.data(),game.path.parent_path().c_str(),&startup,&process,dll.c_str(),nullptr)!=FALSE,
            "Detours could not create the independent suspended child");
        child.process=process.hProcess;child.thread=process.hThread;child.pid=process.dwProcessId;
        job.assign(child.process); // Must succeed before the first resume.
        // Keep the original-file locks alive inside the child as well. If the
        // launcher is forcibly killed, job teardown and ordinary handle cleanup
        // cannot expose these files to an application thread still running.
        for(const auto source:{primary.lock_handle(),backup.lock_handle(),graphics?graphics->handle.get():INVALID_HANDLE_VALUE}) {
            if(source==INVALID_HANDLE_VALUE)continue;
            HANDLE child_lock{};
            require(DuplicateHandle(GetCurrentProcess(),source,child.process,&child_lock,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,
                "Cannot retain original save locks in the suspended owned child");
        }
        shared->child_pid=child.pid;
        Payload payload{};payload.struct_size=sizeof(payload);payload.version=protocol_version;payload.host_pid=GetCurrentProcessId();payload.child_pid=child.pid;
        payload.actor_test=options.actor_lan_test?ActorTest::lan:options.actor_graph_locomotion_test?ActorTest::graph_locomotion:
            (options.actor_pad_reference_test?ActorTest::pad_reference:
            (options.actor_locomotion_test?ActorTest::locomotion:(options.actor_follow_test?ActorTest::follow:ActorTest::off)));
        std::copy(nonce.begin(),nonce.end(),payload.nonce);std::copy(game.digest.begin(),game.digest.end(),payload.executable_sha256);
        if(options.actor_lan_test){payload.lan_role=options.lan_role;payload.lan_profile=options.combat_preview?2u:(options.independent_profile?1u:0u);std::copy(options.lan_code.begin(),options.lan_code.end(),payload.lan_ticket);
            const auto& scenario=options.combat_preview?lan::local_combat_scenario:(options.independent_profile?lan::independent_motion_scenario:primary.digest);std::copy(scenario.begin(),scenario.end(),payload.scenario_sha256);}
        copy_text(payload.exe_path,path_units,game.path.wstring());copy_text(payload.solo_roaming,path_units,roaming.path.wstring());
        copy_text(payload.coop_roaming,path_units,coop.path.wstring());copy_text(payload.account,account_units,options.account);
        require(DuplicateHandle(GetCurrentProcess(),mapping.get(),child.process,&payload.mapping_handle,0,FALSE,DUPLICATE_SAME_ACCESS)&&
            DuplicateHandle(GetCurrentProcess(),ready.get(),child.process,&payload.ready_event,EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,0),"Cannot duplicate launch IPC handles into our suspended child");
        require(DetourCopyPayloadToProcess(child.process,payload_guid,&payload,sizeof(payload))!=FALSE,"Cannot install complete payload before child resume");
        require(ResumeThread(child.thread)==1,"Child thread did not have the expected suspended count");
        const auto deadline=GetTickCount64()+maximum_run_ms;
        const HANDLE waits[]{ready.get(),child.process};
        const auto startup_wait=WaitForMultipleObjects(2,waits,FALSE,60000);
        require(startup_wait==WAIT_OBJECT_0,"Child exited/timed out before READY");
        require(shared->struct_size==sizeof(*shared)&&shared->version==protocol_version&&shared->host_pid==GetCurrentProcessId()&&shared->child_pid==child.pid&&
            std::equal(nonce.begin(),nonce.end(),shared->nonce)&&load(shared->phase)==static_cast<LONG>(Phase::ready)&&load(shared->fatal_error)==0&&load(shared->hooks_mask)!=0,
            "READY signal did not contain a matching successful bootstrap receipt");
        ready_observed=true;
        receipt.emit("{\"event\":\"ready\",\"child_pid\":"+std::to_string(child.pid)+",\"nonce\":"+quote(hex(nonce))+",\"bootstrap\":"+counters(*shared)+"}");
        if(observer)observer(launch_snapshot(*shared,receipt_name));
        ULONGLONG actor_report_at{};std::array<LONG,8> previous_signal{};
        bool actor_samples_full{};
        for(;;) {
            require(primary.expected_state()&&backup.expected_state(),"An originally absent personal save appeared outside the isolated profile; the test was stopped without deleting it");
            wait_foreign_close(receipt,child.pid,foreign_detected,monitor_error);
            if(foreign_detected)throw std::runtime_error("A foreign/Steam-relaunched Sekiro was observed; lifecycle is unverified");
            const auto result=WaitForSingleObject(child.process,500);
            if(result==WAIT_OBJECT_0)break;
            require(result==WAIT_TIMEOUT,"Cannot wait on owned child");
            if(observer)observer(launch_snapshot(*shared,receipt_name));
            const std::array<LONG,8> signal{static_cast<LONG>(load(shared->hooks_mask)),load(shared->actor_phase),load(shared->actor_error),load(shared->lan_state),load(shared->lan_error),load(shared->lan_wait),load(shared->fatal_error),load(shared->action_error)};
            if(options.actor_follow_test && !actor_samples_full && (GetTickCount64()-actor_report_at>=(options.independent_profile?30000u:1000u)||signal!=previous_signal)) {
                previous_signal=signal;
                actor_report_at=GetTickCount64();
                actor_samples_full=!receipt.sample("{\"event\":\"actor_status\",\"elapsed_ms\":"+std::to_string(GetTickCount64()-(deadline-maximum_run_ms))+
                    ",\"child_pid\":"+std::to_string(child.pid)+",\"process\":"+process_metrics(child.process)+",\"native\":"+counters(*shared)+"}");
                if(actor_samples_full)receipt.emit("{\"event\":\"actor_sampling_limit\",\"game_kept_running\":true,\"final_receipt_reserved\":true}");
            }
            if(GetTickCount64()>=deadline){timed_out=true;throw std::runtime_error("Owned child exceeded 30-minute run budget");}
        }
        require(GetExitCodeProcess(child.process,&exit_code)!=FALSE,"Cannot obtain child exit status");
    } catch(const std::exception& error) {failure=error.what();child.stop();GetExitCodeProcess(child.process,&exit_code);}
    // A relaunch may appear only as the original child exits. Keep both normal
    // save files pinned until any foreign game has closed manually.
    wait_foreign_close(receipt,child.pid,foreign_detected,monitor_error);
    const bool primary_unchanged=primary.unchanged(),backup_unchanged=backup.unchanged();
    const bool graphics_unchanged=!graphics||graphics->unchanged();
    const bool normal_exit=exit_code==0;
    const bool completed=failure.empty()&&ready_observed&&!foreign_detected&&!monitor_error&&!timed_out&&normal_exit&&primary_unchanged&&backup_unchanged&&graphics_unchanged&&load(shared->fatal_error)==0;
    receipt.emit("{\"event\":\"final\",\"host_run_completed\":"+std::string(completed?"true":"false")+",\"gameplay_or_save_lifecycle_accepted\":false,\"ready_observed\":"+
        (ready_observed?"true":"false")+",\"child_exit_code\":"+std::to_string(exit_code)+",\"timed_out\":"+(timed_out?"true":"false")+
        ",\"foreign_game_observed\":"+(foreign_detected?"true":"false")+",\"foreign_monitor_error\":"+(monitor_error?"true":"false")+",\"solo_primary_unchanged\":"+(primary_unchanged?"true":"false")+
        ",\"solo_backup_unchanged\":"+(backup_unchanged?"true":"false")+",\"graphics_unchanged\":"+(graphics_unchanged?"true":"false")+",\"solo_primary_after\":"+primary.json_digest()+
        ",\"solo_backup_after\":"+backup.json_digest()+",\"native\":"+counters(*shared)+",\"failure\":"+quote(failure)+"}");
    if(observer)observer(launch_snapshot(*shared,receipt_name,true,completed));
    final_protection.active=false;
    return completed?0:40;
}


// Pinned parents and CREATE_NEW guarantee preparation never overwrites any
// existing file or follows a replacement link. Large existing archives retain
// the launcher's documented length-only verification limit.
void ensure_setup_directory(const fs::path& path){
    if(!CreateDirectoryW(path.c_str(),nullptr))require(GetLastError()==ERROR_ALREADY_EXISTS,"Cannot create test directory; check free space and library permissions");
    Directory pinned(path);
}
void new_bytes(const fs::path& path,std::string_view value){
    Directory parent(path.parent_path());
    Handle output(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    require(output.valid(),"Cannot create a new setup file; existing files are never overwritten");
    BY_HANDLE_FILE_INFORMATION info{};require(GetFileInformationByHandle(output.get(),&info)&&info.nNumberOfLinks==1&&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&equal(final_path(output.get()),path.wstring()),"Unsafe new setup file");
    DWORD written{};require(value.size()<=MAXDWORD&&WriteFile(output.get(),value.data(),static_cast<DWORD>(value.size()),&written,nullptr)&&written==value.size()&&FlushFileBuffers(output.get()),"Cannot finish new setup file");
}
void copy_new_or_compare(const fs::path& from,const fs::path& to,bool allow_existing){
    File source(from,FILE_SHARE_READ,32ull*1024*1024*1024,false);Directory destination(to.parent_path());separate(source.parent,destination);
    const auto attributes=GetFileAttributesW(to.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES){require(allow_existing,"Prepared profile already has a save; refusing overwrite");
        File existing(to,FILE_SHARE_READ,32ull*1024*1024*1024,false);require(source.size==existing.size,"Existing test file has a different length");
        if(source.size<=128u*1024u*1024u)require(sha256_handle(source.handle.get(),source.size)==sha256_handle(existing.handle.get(),existing.size),"Existing test file differs from source");return;}
    require(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot inspect selected test leaf");
    Handle output(CreateFileW(to.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));require(output.valid(),"Cannot create independent test file; check free space and library permissions");
    BY_HANDLE_FILE_INFORMATION info{};require(GetFileInformationByHandle(output.get(),&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&equal(final_path(output.get()),to.wstring()),"Unsafe independent destination");
    std::vector<std::uint8_t> buffer(1024*1024);LARGE_INTEGER zero{};require(SetFilePointerEx(source.handle.get(),zero,nullptr,FILE_BEGIN)!=FALSE,"Cannot rewind source");
    for(std::uint64_t at=0;at<source.size;){const auto wanted=static_cast<DWORD>(std::min<std::uint64_t>(buffer.size(),source.size-at));DWORD read{},written{};
        require(ReadFile(source.handle.get(),buffer.data(),wanted,&read,nullptr)&&read==wanted&&WriteFile(output.get(),buffer.data(),read,&written,nullptr)&&written==read,"Incomplete test copy; retry into a new folder after resolving disk space");at+=read;}
    require(FlushFileBuffers(output.get())!=FALSE,"Cannot flush independent test copy");
}
void copy_asset_tree(const fs::path& from,const fs::path& to,unsigned depth=0){
    require(depth<8,"Unexpected game asset nesting");Directory source(from);ensure_setup_directory(to);Directory destination(to);separate(source,destination);
    for(const auto& entry:fs::directory_iterator(from)){
        const auto attr=GetFileAttributesW(entry.path().c_str());require(attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_REPARSE_POINT),"Game asset has a link or cannot be inspected");
        if(attr&FILE_ATTRIBUTE_DIRECTORY)copy_asset_tree(entry.path(),to/entry.path().filename(),depth+1);
        else copy_new_or_compare(entry.path(),to/entry.path().filename(),true);
    }
}
std::string utf8(std::wstring_view text){const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);require(n>0,"Cannot encode setup path");std::string result(n,0);require(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),n,nullptr,nullptr)==n,"Cannot encode setup path");return result;}

} // namespace
static fs::path prepare_lan_impl(const fs::path& requested,std::uint32_t role,std::string_view code,bool independent,bool combat_preview=false){
    require((role==1||role==2)&&lan::parse_ticket(code).has_value(),"Invalid LAN role or room code");
    require(!combat_preview||(independent&&role==1&&lan::parse_ticket(code)->address=="127.0.0.1"),"Combat preview requires a local host ticket");
    require(!another_game(),"Close Sekiro before preparing the protected LAN profile");
    Directory internal(normalized(fs::absolute(requested)));
    require(internal.path.wstring().size()+(independent?52:24)<MAX_PATH,"Extract the kit to a shorter local folder, for example C:\\SekiroCoopTeste");
    File runtime(internal.path/L"skcoop_runtime.dll",FILE_SHARE_READ);validate_runtime_pe(runtime);
    File launcher(internal.path/L"skcoop_launcher.exe",FILE_SHARE_READ);
    const auto installed=inspect_steam_install();require(installed.build_id=="5794815","Sekiro is missing or this Steam build is unsupported");
    File original(installed.executable,FILE_SHARE_READ);require(original.size==game_size&&hex(original.digest)==game_digest,"Installed Sekiro executable is unsupported");
    DWORD active{},type{},bytes=sizeof(active);
    require(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"ActiveUser",RRF_RT_REG_DWORD,&type,&active,&bytes)==ERROR_SUCCESS&&active,"Open Steam and sign in before preparing the game");
    const auto account=std::to_wstring(76561197960265728ull+active);
    wchar_t roaming_buffer[MAX_PATH]{};require(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,roaming_buffer)),"Cannot find original save folder");
    Directory roaming(roaming_buffer);separate(roaming,internal);separate(original.parent,internal);
    if(independent){ensure_setup_directory(roaming.path/L"Sekiro");ensure_setup_directory(roaming.path/L"Sekiro"/account);}
    OriginalFile primary(roaming.path/L"Sekiro"/account/L"S0000.sl2",independent);
    OriginalFile backup(roaming.path/L"Sekiro"/account/L"S0000.sl2.bak",independent);
    auto game=installed.library_root/L"steamapps"/L"common"/L"Sekiro-SkCoop-Test";ensure_setup_directory(game);
    Directory test(game);separate(original.parent,test);separate(internal,test);separate(roaming,test);
    for(const auto name:{L"sekiro.exe",L"steam_api64.dll",L"bink2w64.dll",L"oo2core_6_win64.dll",L"amd_ags_x64.dll",L"fmod_event64.dll",L"fmod_event_net64.dll",L"fmodex64.dll",L"pack.dat"})copy_new_or_compare(original.path.parent_path()/name,game/name,true);
    for(unsigned i=1;i<=5;++i)for(const auto ext:{L".bdt",L".bhd"}){const auto name=L"Data"+std::to_wstring(i)+ext;copy_new_or_compare(original.path.parent_path()/name,game/name,true);}
    for(const auto name:{L"movie",L"sound"})copy_asset_tree(original.path.parent_path()/name,game/name);
    const auto appid=game/L"steam_appid.txt";
    if(GetFileAttributesW(appid.c_str())==INVALID_FILE_ATTRIBUTES){require(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot inspect test AppID file");new_bytes(appid,"814380\n");}
    // A fresh profile per setup preserves every previous trial and save.
    const auto id=hex(lan::random_id()).substr(0,12);
    if(independent){ensure_setup_directory(internal.path/L"profiles");ensure_setup_directory(internal.path/L"profiles"/account);}
    const auto profile=independent?internal.path/L"profiles"/account/L"movement-v2":internal.path/ascii_wide("profile-"+id);
    ensure_setup_directory(profile);ensure_setup_directory(profile/L"Sekiro");ensure_setup_directory(profile/L"Sekiro"/account);
    // Copy from handles already denying all access, rather than reopening the
    // original save while its protective pin is held.
    const auto clone=[&](const File& source,const fs::path& destination){std::string value(static_cast<std::size_t>(source.size),0);source.read(0,value.data(),static_cast<DWORD>(value.size()));new_bytes(destination,value);};
    if(!independent){clone(*primary.file,profile/L"Sekiro"/account/L"S0000.co2");clone(*backup.file,profile/L"Sekiro"/account/L"S0000.co2.bak");}
    else{
        const auto owner=profile/L"profile.owner";const auto expected=utf8(account)+"\n";
        if(GetFileAttributesW(owner.c_str())==INVALID_FILE_ATTRIBUTES){require(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot inspect profile owner");
            for(const auto name:{L"S0000.co2",L"S0000.co2.bak"})require(GetFileAttributesW((profile/L"Sekiro"/account/name).c_str())==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND,"Existing unowned profile was not imported");
            new_bytes(owner,expected);
        }else{File checked(owner,FILE_SHARE_READ,128);std::string actual(static_cast<std::size_t>(checked.size),0);checked.read(0,actual.data(),static_cast<DWORD>(actual.size()));require(actual==expected,"This test profile belongs to another Steam account");}
    }
    const auto graphics=roaming.path/L"Sekiro"/L"GraphicsConfig.xml";
    if(GetFileAttributesW(graphics.c_str())!=INVALID_FILE_ATTRIBUTES){
        const auto private_graphics=profile/L"Sekiro"/L"GraphicsConfig.xml";
        if(independent&&GetFileAttributesW(private_graphics.c_str())!=INVALID_FILE_ATTRIBUTES){File checked(private_graphics,FILE_SHARE_READ,1024u*1024u);}
        else copy_new_or_compare(graphics,private_graphics,false);
    }
    else require(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot inspect graphics configuration");
    ensure_setup_directory(internal.path/L"config");ensure_setup_directory(internal.path/L"receipts");
    const auto config=internal.path/L"config"/ascii_wide("lan-"+id+".ini");
    const auto settings=std::string("[LAUNCH]\ntest_executable=")+utf8(game.wstring()+L"\\sekiro.exe")+"\ncoop_profile_root="+utf8(profile.wstring())+"\naccount="+utf8(account)+"\nreceipt_directory="+utf8((internal.path/L"receipts").wstring())+"\nactor_test=lan\nlan_role="+(role==1?"host":"guest")+"\nlan_code="+std::string(code)+"\n"+(combat_preview?"lan_profile=combat-preview\n":(independent?"lan_profile=independent\n":""));
    (void)parse_launch_settings(settings);new_bytes(config,settings);
    require(primary.unchanged()&&backup.unchanged(),"Original saves changed during preparation");
    // Pinned original handles close on return. Launch revalidates every file,
    // profile, lease and seed before a suspended game can execute.
    return config;
}
std::wstring prepare_lan_test(const fs::path& requested,std::uint32_t role,std::string_view code){
    const auto config=prepare_lan_impl(requested,role,code,false);
    return quote_argument((fs::absolute(requested)/L"skcoop_launcher.exe").wstring())+L" --steam-start "+quote_argument(config.wstring())+L" %command%";
}
fs::path prepare_direct_lan(const fs::path& requested,std::uint32_t role,std::string_view code,bool combat_preview){
    return prepare_lan_impl(requested,role,code,true,combat_preview);
}
int check_protected_launch(const fs::path& config) {return run(options_from(config),true);}
int direct_protected_launch(const fs::path& config,const fs::path& runtime_directory,const LaunchObserver& observer) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    const auto options=options_from(config,runtime_directory.empty()?executable_directory():runtime_directory);
    DWORD active{},bytes=sizeof(active);
    require(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"ActiveUser",RRF_RT_REG_DWORD,nullptr,&active,&bytes)==ERROR_SUCCESS&&active,
        "Abra a Steam e entre na conta que vai jogar.");
    require(options.account==std::to_wstring(76561197960265728ull+active),"A conta da Steam mudou. Prepare o perfil desta conta no assistente.");
    // The original game and Steam DLLs remain unmodified. Steam still validates
    // the app's real license; a Steam failure is a failed launch, never bypassed.
    return run(options,false,observer);
}
int steam_protected_launch(const fs::path& config,std::wstring_view original_command) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    wchar_t appid[16]{};
    require(GetEnvironmentVariableW(L"SteamAppId",appid,16)==6&&std::wstring_view(appid)==L"814380",
        "Protected launch requires Steam's original Sekiro launch context");
    const auto options=options_from(config);
    require(equal(normalized(fs::path(original_command)).wstring(),options.original.wstring()),
        "Steam original command does not match its detected Sekiro installation");
    return run(options,false);
}
} // namespace skcoop::launcher
