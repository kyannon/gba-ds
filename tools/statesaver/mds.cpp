// State Saver's DS emulator: melonDS's core compiled to WebAssembly, with
// its free BIOS and generated firmware (no system files needed), booting
// games directly. Savestates are melonDS's own (.mln) format.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>

#include "NDS.h"
#include "NDSCart.h"
#include "Platform.h"
#include "SPU.h"
#include "Savestate.h"
#include "GPU.h"

#define EXPORT extern "C" __attribute__((visibility("default")))

using namespace melonDS;

/* ------------------------------------------------------------ Platform */

namespace melonDS::Platform
{
void SignalStop(StopReason, void*) {}
std::string GetLocalFilePath(const std::string& f) { return f; }

static FileHandle* open_file(const std::string& path, FileMode mode)
{
    const char* m = (mode & FileMode::Write) ? ((mode & FileMode::Read) ? "r+b" : "wb") : "rb";
    if ((mode & FileMode::Append)) m = "ab";
    return (FileHandle*) fopen(path.c_str(), m);
}
FileHandle* OpenFile(const std::string& path, FileMode mode) { return open_file(path, mode); }
FileHandle* OpenLocalFile(const std::string& path, FileMode mode) { return open_file(path, mode); }
bool FileExists(const std::string& name) { FILE* f = fopen(name.c_str(), "rb"); if (f) fclose(f); return f != nullptr; }
bool LocalFileExists(const std::string& name) { return FileExists(name); }
bool CheckFileWritable(const std::string&) { return false; }
bool CheckLocalFileWritable(const std::string&) { return false; }
bool CloseFile(FileHandle* f) { return fclose((FILE*) f) == 0; }
bool IsEndOfFile(FileHandle* f) { return feof((FILE*) f) != 0; }
bool FileReadLine(char* str, int count, FileHandle* f) { return fgets(str, count, (FILE*) f) != nullptr; }
u64 FilePosition(FileHandle* f) { return (u64) ftell((FILE*) f); }
bool FileSeek(FileHandle* f, s64 off, FileSeekOrigin o)
{
    int w = o == FileSeekOrigin::Start ? SEEK_SET : o == FileSeekOrigin::Current ? SEEK_CUR : SEEK_END;
    return fseek((FILE*) f, (long) off, w) == 0;
}
void FileRewind(FileHandle* f) { rewind((FILE*) f); }
u64 FileRead(void* data, u64 size, u64 count, FileHandle* f) { return fread(data, size, count, (FILE*) f); }
bool FileFlush(FileHandle* f) { return fflush((FILE*) f) == 0; }
u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* f) { return fwrite(data, size, count, (FILE*) f); }
u64 FileWriteFormatted(FileHandle* f, const char* fmt, ...)
{
    va_list a; va_start(a, fmt); int n = vfprintf((FILE*) f, fmt, a); va_end(a); return n < 0 ? 0 : (u64) n;
}
u64 FileLength(FileHandle* f)
{
    long p = ftell((FILE*) f); fseek((FILE*) f, 0, SEEK_END); long n = ftell((FILE*) f); fseek((FILE*) f, p, SEEK_SET);
    return (u64) n;
}
void Log(LogLevel level, const char* fmt, ...)
{
    if (level < LogLevel::Warn) return;
    va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a);
}

// single-threaded: the soft renderer runs unthreaded, so these are never used to run work
struct Thread {};
Thread* Thread_Create(std::function<void()>) { return new Thread; }
void Thread_Free(Thread* t) { delete t; }
void Thread_Wait(Thread*) {}
struct Semaphore { int n = 0; };
Semaphore* Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore* s) { delete s; }
void Semaphore_Reset(Semaphore* s) { s->n = 0; }
void Semaphore_Wait(Semaphore* s) { if (s->n > 0) s->n--; }
bool Semaphore_TryWait(Semaphore* s, int) { if (s->n > 0) { s->n--; return true; } return false; }
void Semaphore_Post(Semaphore* s, int count) { s->n += count; }
struct Mutex {};
Mutex* Mutex_Create() { return new Mutex; }
void Mutex_Free(Mutex* m) { delete m; }
void Mutex_Lock(Mutex*) {}
void Mutex_Unlock(Mutex*) {}
bool Mutex_TryLock(Mutex*) { return true; }
void Sleep(u64) {}
u64 GetMSCount() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (u64) t.tv_sec * 1000 + t.tv_nsec / 1000000; }
u64 GetUSCount() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (u64) t.tv_sec * 1000000 + t.tv_nsec / 1000; }

// saves stay in the cart's memory; mds_save_export hands them out
void WriteNDSSave(const u8*, u32, u32, u32, void*) {}
void WriteGBASave(const u8*, u32, u32, u32, void*) {}
void WriteFirmware(const Firmware&, u32, u32, void*) {}
void WriteDateTime(int, int, int, int, int, int, void*) {}

void MP_Begin(void*) {}
void MP_End(void*) {}
int MP_SendPacket(u8*, int, u64, void*) { return 0; }
int MP_RecvPacket(u8*, u64*, void*) { return 0; }
int MP_SendCmd(u8*, int, u64, void*) { return 0; }
int MP_SendReply(u8*, int, u64, u16, void*) { return 0; }
int MP_SendAck(u8*, int, u64, void*) { return 0; }
int MP_RecvHostPacket(u8*, u64*, void*) { return 0; }
u16 MP_RecvReplies(u8*, u64, u16, void*) { return 0; }
int Net_SendPacket(u8*, int, void*) { return 0; }
int Net_RecvPacket(u8*, void*) { return 0; }
void Camera_Start(int, void*) {}
void Camera_Stop(int, void*) {}
void Camera_CaptureFrame(int, u32*, int, int, bool, void*) {}
void Mic_Start(void*) {}
void Mic_Stop(void*) {}
int Mic_ReadInput(s16*, int, void*) { return 0; }
AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder*) {}
bool AAC_Configure(AACDecoder*, int, int) { return false; }
bool AAC_DecodeFrame(AACDecoder*, const void*, int, void*, int) { return false; }
bool Addon_KeyDown(KeyType, void*) { return false; }
void Addon_RumbleStart(u32, void*) {}
void Addon_RumbleStop(void*) {}
float Addon_MotionQuery(MotionQueryType, void*) { return 0; }
DynamicLibrary* DynamicLibrary_Load(const char*) { return nullptr; }
void DynamicLibrary_Unload(DynamicLibrary*) {}
void* DynamicLibrary_LoadFunction(DynamicLibrary*, const char*) { return nullptr; }
}

/* ------------------------------------------------------------ the console */

static NDS* nds;
static u32 screens[256 * 192 * 2]; // top, then bottom: 0xAARRGGBB
static u8* out;
static size_t outSize;
static char code[5], title[13];

EXPORT void* mds_alloc(size_t n) { return malloc(n); }
EXPORT void mds_free(void* p) { free(p); }

static void set_out(const void* data, size_t n)
{
    free(out);
    out = (u8*) malloc(n ? n : 1);
    outSize = out ? n : 0;
    if (out && n) memcpy(out, data, n);
}
EXPORT u8* mds_out() { return out; }

// The ROM (copied) and, optionally, a cart save to start from (copied).
// 1 on success.
EXPORT int mds_load_rom(const u8* rom, u32 romLen, const u8* sav, u32 savLen)
{
    delete nds;
    nds = nullptr;
    auto data = std::make_unique<u8[]>(romLen);
    memcpy(data.get(), rom, romLen);
    NDSCart::NDSCartArgs cartargs {};
    if (sav && savLen)
    {
        cartargs.SRAM = std::make_unique<u8[]>(savLen);
        memcpy(cartargs.SRAM.get(), sav, savLen);
        cartargs.SRAMLength = savLen;
    }
    NDSArgs args {};
    args.JIT = std::nullopt;
    nds = new NDS(std::move(args), nullptr);
    auto cart = NDSCart::ParseROM(std::move(data), romLen, nullptr, std::move(cartargs));
    if (!cart)
    {
        delete nds;
        nds = nullptr;
        return 0;
    }
    memcpy(code, cart->GetHeader().GameCode, 4);
    code[4] = 0;
    memcpy(title, cart->GetHeader().GameTitle, 12);
    title[12] = 0;
    nds->SetNDSCart(std::move(cart));
    nds->Reset();
    if (nds->NeedsDirectBoot()) nds->SetupDirectBoot("game.nds");
    nds->Start();
    return 1;
}

EXPORT const char* mds_game_code() { return code; }
EXPORT const char* mds_game_title() { return title; }

// keys: GBA-style bits (A, B, Select, Start, Right, Left, Up, Down, R, L) plus X (bit 10), Y (bit 11), pressed = 1
EXPORT void mds_frame(u32 keys, int touching, int tx, int ty)
{
    if (!nds) return;
    nds->SetKeyMask(~keys & 0xFFF);
    if (touching) nds->TouchScreen((u16) tx, (u16) ty);
    else nds->ReleaseScreen();
    nds->RunFrame();
}

EXPORT void mds_reset()
{
    if (!nds) return;
    nds->Reset();
    if (nds->NeedsDirectBoot()) nds->SetupDirectBoot("game.nds");
    nds->Start();
}

// both screens, 256x192 each, stacked (top first), 0xAARRGGBB
EXPORT u32* mds_video()
{
    void* top = nullptr;
    void* bottom = nullptr;
    if (nds && nds->GPU.GetFramebuffers(&top, &bottom) && top && bottom)
    {
        memcpy(screens, top, 256 * 192 * 4);
        memcpy(screens + 256 * 192, bottom, 256 * 192 * 4);
    }
    return screens;
}

EXPORT unsigned mds_audio_rate() { return 48000; }
// up to max stereo frames (int16 L, R)
EXPORT int mds_audio(s16* buf, int max) { return nds ? nds->SPU.ReadOutput(buf, max) : 0; }

// a melonDS savestate (.mln); its bytes at mds_out()
EXPORT size_t mds_state_save()
{
    if (!nds) return 0;
    Savestate state;
    if (state.Error) return 0;
    nds->DoSavestate(&state);
    if (state.Error) return 0;
    state.Finish();
    set_out(state.Buffer(), state.Length());
    return outSize;
}

EXPORT int mds_state_load(u8* data, u32 len)
{
    if (!nds) return 0;
    Savestate state(data, len, false);
    if (state.Error) return 0;
    nds->DoSavestate(&state);
    return state.Error ? 0 : 1;
}

// the cart's save memory as it is now; its bytes at mds_out()
EXPORT size_t mds_save_export()
{
    if (!nds || !nds->GetNDSSave()) return 0;
    set_out(nds->GetNDSSave(), nds->GetNDSSaveLength());
    return outSize;
}

EXPORT u8 mds_read8(u32 addr) { return nds ? nds->ARM9Read8(addr) : 0; }
