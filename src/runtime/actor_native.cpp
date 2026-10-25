#include "actor_native.h"
#include "actor_demo.h"
#include <bcrypt.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace skcoop::actors {
namespace {
std::uintptr_t base{};
launch::SharedStatus* status{};
Demo demo;
volatile LONG entered{};
DWORD native_thread{};
bool armed{};
ULONGLONG armed_at{};
using Update = void (*)(void*, void*);
Update original_update{};
constexpr std::uintptr_t creator_vtable = 0x2a952a8;
constexpr std::uintptr_t consumer = 0xc0f7a0;

bool readable(std::uintptr_t p, std::size_t n, bool executable=false) noexcept {
    if (!p || !n || p+n < p) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<void*>(p), &m, sizeof(m)) != sizeof(m) ||
        m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        p+n > reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize) return false;
    const DWORD access=m.Protect & 0xff;
    return executable ? (m.Type == MEM_IMAGE && reinterpret_cast<std::uintptr_t>(m.AllocationBase)==base &&
        (access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY)) :
        (access==PAGE_READONLY || access==PAGE_READWRITE || access==PAGE_WRITECOPY ||
         access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY);
}
template<class T> T read(std::uintptr_t p) {
    if (!readable(p,sizeof(T))) throw std::runtime_error("Unreadable native relationship");
    T value{}; std::memcpy(&value,reinterpret_cast<const void*>(p),sizeof(T)); return value;
}
std::uintptr_t pointer(std::uintptr_t p) { return read<std::uintptr_t>(p); }
bool typed(std::uintptr_t p, std::uintptr_t table) { return p && pointer(p)==base+table; }
bool model(std::uintptr_t p) {
    const auto component=pointer(p+0x50), root=pointer(p+0x1ff8);
    if (!component || !root) return false;
    if (pointer(component+0x10)!=p) throw std::runtime_error("Component owner changed");
    const auto node=pointer(root+0x68);
    return node && readable(node+0x80,32);
}
Vector native_vector(std::uintptr_t rva, Token p) {
    alignas(16) Vector v{};
    using Getter=void* (*)(void*,float*);
    reinterpret_cast<Getter>(base+rva)(reinterpret_cast<void*>(p),v.data());
    return v;
}
class NativeGame final : public Game {
public:
    void* creator{};
    Frame frame{};
    bool sample(Frame& out) override {
        const auto world=pointer(base+0x3d7a1e0);
        if (!world) return false;
        if (!typed(world,0x2a30058) || pointer(world+0x3148)!=reinterpret_cast<std::uintptr_t>(creator) ||
            !typed(reinterpret_cast<std::uintptr_t>(creator),creator_vtable)) throw std::runtime_error("World/creator changed");
        const auto primary=pointer(world+0x88);
        if (!primary) return false;
        const auto count=read<std::int32_t>(world+0x40);
        const auto array=pointer(world+0x48);
        if (!typed(primary,0x2a2b338) || count!=6 || !array || pointer(array)!=primary ||
            !typed(pointer(primary+0x58),0x29d4b30)) throw std::runtime_error("Player registry/type changed");
        frame={world,primary,array,pointer(array+0x38),read<std::uint32_t>(primary+0x78),model(primary),{},{},GetTickCount64()};
        if (frame.primary_ready) {
            frame.position=native_vector(0x9f0870,primary);
            frame.forward=native_vector(0x9eeb40,primary);
        }
        out=frame; return true;
    }
    Token create() override {
        // Recheck sources at the exact game-thread creation boundary.
        const auto world=frame.world, p=frame.primary, c=reinterpret_cast<std::uintptr_t>(creator);
        if (read<std::uint32_t>(c+0x40) || read<std::uint32_t>(c+0x44) ||
            pointer(world+0x88)!=p || pointer(frame.registry)!=p || frame.field_handle>0xffff)
            throw std::runtime_error("Creator not idle");
        for (unsigned i=1;i<6;++i) if (pointer(frame.registry+i*0x38)) throw std::runtime_error("Additional slot occupied");
        const auto data_owner=pointer(base+0x3d67408), source_owner=pointer(base+0x3d5aac0);
        if (!data_owner || !source_owner) throw std::runtime_error("Missing factory sources");
        const auto data=data_owner+0x31e0, source=pointer(source_owner+8);
        if (!source || data!=pointer(p+0x21f8) || source!=pointer(p+0x2000)) throw std::runtime_error("Factory sources changed");
        const auto argument=read<std::uint32_t>(p+0x70);
        // The successful experiment covered this descriptor family only.
        if (argument!=0 || frame.field_handle!=0xa000) throw std::runtime_error("Unsupported spawn descriptor");
        struct alignas(16) Parameters {
            std::uint32_t source_handle, manipulator;
            std::uintptr_t source, unused, data;
            std::uint32_t flags;
            std::uint8_t padding[12];
        };
        static_assert(sizeof(Parameters)==48 && offsetof(Parameters,data)==24);
        const Parameters params{frame.field_handle&0x7ff,0,source,0,data,0,{}};
        using Create=void* (*)(void*,std::uint32_t,const void*);
        const auto result=reinterpret_cast<std::uintptr_t>(reinterpret_cast<Create>(base+0xa49460)(
            reinterpret_cast<void*>(world),argument,&params));
        const auto guest=pointer(frame.registry+0x38);
        if (!guest || guest!=result || !typed(guest,0x2a2b338) ||
            !typed(pointer(guest+0x58),0x29d2ae0) || pointer(world+0x88)!=p || pointer(frame.registry)!=p)
            throw std::runtime_error("Factory result/registry mismatch");
        return guest;
    }
    bool actor_ready(Token p) override {
        if (!typed(p,0x2a2b338) || !typed(pointer(p+0x58),0x29d2ae0)) throw std::runtime_error("Guest type changed");
        return model(p);
    }
    Vector position(Token p) override { return native_vector(0x9f0870,p); }
    void place(Token p, const Vector& value) override {
        alignas(16) Vector aligned=value;
        using Setter=void (*)(void*,const float*);
        reinterpret_cast<Setter>(base+0x9f6320)(reinterpret_cast<void*>(p),aligned.data());
    }
};
NativeGame game;
void publish() noexcept {
    InterlockedExchange(&status->actor_phase,static_cast<LONG>(demo.phase));
    InterlockedExchange(&status->actor_error,static_cast<LONG>(demo.error));
    InterlockedExchange64(&status->actor_creation_calls,static_cast<LONG64>(demo.creation_calls));
    InterlockedExchange64(&status->actor_ticks,static_cast<LONG64>(demo.ticks));
    InterlockedExchange64(&status->actor_pose_writes,static_cast<LONG64>(demo.pose_writes));
}
void step(void* creator) noexcept {
    try {
        if (!armed) {
            DWORD foreground_pid{};
            GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
            if (foreground_pid!=GetCurrentProcessId() || !(GetAsyncKeyState(VK_F8)&0x8000)) return;
            armed=true; armed_at=GetTickCount64();
        }
        if (!demo.creation_calls && GetTickCount64()-armed_at>10000) { demo.fail(Error::binding); return; }
        game.creator=creator; demo.tick(game);
    }
    catch (...) { demo.fail(Error::binding); }
}
void guarded_step(void* creator) noexcept {
    __try { step(creator); }
    __except(EXCEPTION_EXECUTE_HANDLER) { demo.fail(Error::native_fault); }
}
void update(void* creator, void* frame) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (caller==base+0xa50a31 && InterlockedCompareExchange(&entered,1,0)==0) {
        const auto thread=GetCurrentThreadId();
        if (!native_thread) native_thread=thread;
        if (native_thread!=thread) demo.fail(Error::binding);
        else guarded_step(creator);
        publish();
        InterlockedExchange(&entered,0);
    }
    original_update(creator,frame);
}
// Digests of 16-byte entry anchors retained from the exact, live target.
// These detect mapping/decryption/competing-hook mismatches, not full-function integrity.
struct Anchor {std::uintptr_t rva; const char* digest;};
constexpr Anchor anchors[]{
    {consumer,"5fde78be3252ecfca1c4143700632282584b08b75a54cfba353d37adb04a60d4"},
    {0xa49460,"991a45851ffef6489fbb8955d680f424c1cf752079c2bc76f2e505a98827526f"},
    {0x9f0870,"56adb9feaddc38abdd4a2e2a2ce735fcf2136f20ccc8c4b58cd335005f5065bb"},
    {0x9eeb40,"8e783b451d7df60c638925306c10b7e42fd10c83f94fd9d0fc5cee184b118802"},
    {0x9f6320,"40d2bce47fa15ca88b903bf018e28b259446104de897b2e51d0c9f39bbaf4923"}};
bool anchors_match() noexcept {
    for (const auto& a:anchors) {
        if (!readable(base+a.rva,16,true)) return false;
        std::array<unsigned char,32> digest{};
        if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(base+a.rva),16,
            digest.data(),static_cast<ULONG>(digest.size()))<0) return false;
        constexpr char hex[]="0123456789abcdef";
        for (std::size_t i=0;i<digest.size();++i)
            if (a.digest[i*2]!=hex[digest[i]>>4] || a.digest[i*2+1]!=hex[digest[i]&15]) return false;
    }
    return true;
}
DWORD WINAPI worker(void*) noexcept {
    base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto start=GetTickCount64();
    while (!anchors_match()) {
        if (GetTickCount64()-start>=180000) { demo.fail(Error::hook_timeout); publish(); return 0; }
        Sleep(250);
    }
    auto* slot=reinterpret_cast<void* volatile*>(base+creator_vtable+0x48);
    if (!readable(reinterpret_cast<std::uintptr_t>(slot),sizeof(void*)) ||
        *slot!=reinterpret_cast<void*>(base+consumer)) { demo.fail(Error::hook_conflict); publish(); return 0; }
    original_update=reinterpret_cast<Update>(base+consumer);
    // Prevent callback work until page protection and hook ownership are settled.
    InterlockedExchange(&entered,1);
    DWORD protection{};
    if (!VirtualProtect(const_cast<void**>(slot),sizeof(void*),PAGE_READWRITE,&protection)) {
        demo.fail(Error::hook_conflict); publish(); InterlockedExchange(&entered,0); return 0;
    }
    const auto previous=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&update),reinterpret_cast<void*>(original_update));
    DWORD ignored{};
    const bool restored=VirtualProtect(const_cast<void**>(slot),sizeof(void*),protection,&ignored)!=FALSE;
    if (previous!=reinterpret_cast<void*>(original_update) || !restored) {
        // If protection restoration failed, remove only our own pointer.
        if (!restored) {
            InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(original_update),reinterpret_cast<void*>(&update));
            VirtualProtect(const_cast<void**>(slot),sizeof(void*),protection,&ignored);
        }
        demo.fail(Error::hook_conflict); publish(); InterlockedExchange(&entered,0); return 0;
    }
    InterlockedOr64(&status->hooks_mask,2);
    InterlockedExchange(&entered,0);
    // The callback and immutable original remain pinned until process exit.
    // Stopping the bounded demo disables all actor reads/writes, without freeing actors.
    return 0;
}
} // namespace
bool start_native_demo(launch::SharedStatus* shared) noexcept {
    if (status || !shared) return false;
    status=shared;
    InterlockedExchange(&status->actor_phase,static_cast<LONG>(Phase::waiting));
    HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);
    if (!thread) { status=nullptr; return false; }
    CloseHandle(thread); return true;
}
} // namespace skcoop::actors
