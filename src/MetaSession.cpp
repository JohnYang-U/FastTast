// ---------------------------------------------------------------------------
// MetaSession.cpp  --  唯一 include MetaApp.h 的翻译单元
// ---------------------------------------------------------------------------
#include "MetaSession.h"

#include "MetaApp.h"    // -> meta.h -> meta_core.h / meta_boot_def.h

#include <windows.h>

#include <cstring>
#include <mutex>
#include <string>

namespace metasession
{
namespace
{

std::mutex g_lock;
LogSink    g_sink[kMaxSlots] = { nullptr };
void*      g_user[kMaxSlots] = { nullptr };

HMODULE g_hMod = nullptr;
std::string g_appPath;

// 函数指针(动态加载, 因此不需要 MetaApp.lib)
typedef META_RESULT (__stdcall* PFN_RegisterLog)(
    const int, const METAAPP_Conn_Log_Display_CallBackForMultiThread, void*);
typedef METAAPP_HIGH_LEVEL_RESULT (__stdcall* PFN_Connect)(
    const int, METAAPP_CONN_STTING_T*, METAAPP_CONNCT_CNF_T*, unsigned int);
typedef METAAPP_HIGH_LEVEL_RESULT (__stdcall* PFN_ApToModem)(const int);
typedef METAAPP_HIGH_LEVEL_RESULT (__stdcall* PFN_Disconnect)(
    const int, METAAPP_DISCONNECT_META_T, unsigned int);

PFN_RegisterLog g_pfnRegisterLog   = nullptr;
PFN_Connect     g_pfnConnect       = nullptr;
PFN_ApToModem   g_pfnApToModem     = nullptr;
PFN_Disconnect  g_pfnDisconnect    = nullptr;

void __stdcall forwardLog(const char* text, void* user)
{
    if (text == nullptr)
        return;

    const int slot = static_cast<int>(reinterpret_cast<intptr_t>(user));
    if (slot < 0 || slot >= kMaxSlots)
        return;

    LogSink sink = nullptr;
    void*   arg  = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_lock);
        sink = g_sink[slot];
        arg  = g_user[slot];
    }
    if (sink != nullptr)
        sink(text, arg);
}

void copyStr(char* dst, size_t cap, const char* src)
{
    if (dst == nullptr || cap == 0)
        return;
    if (src == nullptr)
    {
        dst[0] = '\0';
        return;
    }
    size_t n = std::strlen(src);
    if (n >= cap)
        n = cap - 1;
    std::memcpy(dst, src, n);
    dst[n] = '\0';
}

void makeDirs(const std::string& path)
{
    if (path.empty())
        return;
    std::string cur;
    cur.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i)
    {
        const char c = path[i];
        cur.push_back(c);
        if ((c == '\\' || c == '/') && cur.size() > 3)
            ::CreateDirectoryA(cur.substr(0, cur.size() - 1).c_str(), nullptr);
    }
    ::CreateDirectoryA(path.c_str(), nullptr);
}

std::string narrow(const std::wstring& w);

std::string win32ErrorText(DWORD code)
{
    // 用 W 版 + UTF-8 转换: FormatMessageA 返回的是 ANSI(中文系统上是 GBK),
    // 直接当 UTF-8 用会乱码。
    wchar_t wbuf[512] = { 0 };
    DWORD n = ::FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        wbuf, static_cast<DWORD>(sizeof(wbuf) / sizeof(wbuf[0])) - 1, nullptr);
    if (n == 0)
    {
        char tmp[64];
        std::snprintf(tmp, sizeof(tmp), "Win32 error %lu", static_cast<unsigned long>(code));
        return std::string(tmp);
    }
    std::wstring w(wbuf, n);
    while (!w.empty() && (w[w.size() - 1] == L'\r' || w[w.size() - 1] == L'\n' || w[w.size() - 1] == L' '))
        w.erase(w.size() - 1);
    return narrow(w);
}

std::string narrow(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return std::string();
    std::string s(static_cast<size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

// MetaApp.dll 是 __stdcall 导出的, 名字带修饰: _METAAPP_XXX@16
// (用 dumpbin /exports MetaApp.dll 可以看到)。这里两种都试。
// argBytes 是该函数的参数总字节数, 用来拼修饰名。
void* findExport(HMODULE h, const char* name, int argBytes)
{
    void* p = reinterpret_cast<void*>(::GetProcAddress(h, name));
    if (p != nullptr)
        return p;

    static const int kSizes[] = { 0, 4, 8, 12, 16, 20, 24, 28, 32 };
    char buf[256];
    // 先试调用方给出的精确值, 再逐个兜底
    for (int n = -1; n < static_cast<int>(sizeof(kSizes) / sizeof(kSizes[0])); ++n)
    {
        const int sz = (n < 0) ? argBytes : kSizes[n];
        if (sz < 0)
            continue;
        std::snprintf(buf, sizeof(buf), "_%s@%d", name, sz);
        p = reinterpret_cast<void*>(::GetProcAddress(h, buf));
        if (p != nullptr)
            return p;
    }
    return nullptr;
}

template <typename T>
void resolve(T& fn, const char* name, int argBytes)
{
    fn = reinterpret_cast<T>(findExport(g_hMod, name, argBytes));
}

} // namespace

// ---------------------------------------------------------------------------
bool loadApp(const std::wstring& runtimeDir, std::string* err)
{
    unloadApp();

    if (!runtimeDir.empty())
        ::SetDllDirectoryW(runtimeDir.c_str());

    std::wstring candidate = runtimeDir;
    if (!candidate.empty())
    {
        const wchar_t last = candidate[candidate.size() - 1];
        if (last != L'\\' && last != L'/')
            candidate += L'\\';
    }
    candidate += L"MetaApp.dll";

    ::SetLastError(0);
    g_hMod = ::LoadLibraryW(candidate.c_str());
    if (g_hMod == nullptr)
    {
        ::SetLastError(0);
        g_hMod = ::LoadLibraryW(L"MetaApp.dll");
    }

    if (g_hMod == nullptr)
    {
        const DWORD e = ::GetLastError();
        if (err != nullptr)
        {
            *err = "加载 MetaApp.dll 失败: " + win32ErrorText(e);
            *err += "\r\n已尝试: " + narrow(candidate);
            *err += "\r\n提示: MetaApp.dll 依赖 MetaCore.dll 等同目录 DLL, 且必须是 Win32 位。";
        }
        return false;
    }

    char buf[MAX_PATH * 2] = { 0 };
    DWORD n = ::GetModuleFileNameA(g_hMod, buf, static_cast<DWORD>(sizeof(buf)));
    g_appPath = (n > 0 && n < sizeof(buf)) ? std::string(buf, n) : std::string();

    resolve(g_pfnRegisterLog, "METAAPP_RegisterCallBackFor_Conn_Log_Display", 12);
    resolve(g_pfnConnect,     "METAAPP_ConnectTargetAllInOne_r",               16);
    resolve(g_pfnApToModem,   "METAAPP_ApToModemAllInOne_r",                    4);
    resolve(g_pfnDisconnect,  "METAAPP_DisConnectMeta_Ex_r",                   16);

    if (g_pfnConnect == nullptr)
    {
        if (err != nullptr)
            *err = "MetaApp.dll 已加载但缺少 METAAPP_ConnectTargetAllInOne_r 导出。";
        unloadApp();
        return false;
    }
    return true;
}

void unloadApp()
{
    if (g_hMod != nullptr)
    {
        ::FreeLibrary(g_hMod);
        g_hMod = nullptr;
    }
    g_appPath.clear();
    g_pfnRegisterLog = nullptr;
    g_pfnConnect = nullptr;
    g_pfnApToModem = nullptr;
    g_pfnDisconnect = nullptr;
    ::SetDllDirectoryW(nullptr);
}

bool appLoaded()
{
    return g_hMod != nullptr;
}

std::string appPath()
{
    return g_appPath;
}

// ---------------------------------------------------------------------------
bool registerLog(int slot, LogSink sink, void* user)
{
    if (slot < 0 || slot >= kMaxSlots)
        return false;

    {
        std::lock_guard<std::mutex> lk(g_lock);
        g_sink[slot] = sink;
        g_user[slot] = user;
    }

    if (g_pfnRegisterLog == nullptr)
        return false;

    const META_RESULT ret = g_pfnRegisterLog(
        slot,
        reinterpret_cast<METAAPP_Conn_Log_Display_CallBackForMultiThread>(&forwardLog),
        reinterpret_cast<void*>(static_cast<intptr_t>(slot)));
    return (ret == META_SUCCESS);
}

// ---------------------------------------------------------------------------
ConnResult connectTarget(int slot, const ConnOptions& opt, int* stopFlag)
{
    ConnResult r;
    r.argSize = static_cast<unsigned>(sizeof(METAAPP_CONN_STTING_T));

    if (g_pfnConnect == nullptr)
    {
        r.text = "MetaApp.dll 未加载";
        return r;
    }

    METAAPP_CONN_STTING_T set;
    std::memset(&set, 0, sizeof(set));

    METAAPP_CONNCT_CNF_T cnf;
    std::memset(&cnf, 0, sizeof(cnf));

    int dummyStop = 0;

    set.stopFlag     = (stopFlag != nullptr) ? stopFlag : &dummyStop;
    set.autoScanPort = opt.autoScan;
    set.connectType  = METAAPP_CONN_CONNCET_BY_USB;
    set.connectMode  = opt.alreadyInMeta ? METAAPP_CONN_ALREADY_IN_META_MODE
                                         : METAAPP_CONN_BOOT_META_MODE;
    set.uTimeOutMs   = opt.timeoutMs;

    set.bootSetting.bEnableAdbDevice = opt.enableAdb;
    set.bootSetting.uMDMode          = static_cast<unsigned short>(opt.mdMode & 0xFFFFu);
    set.bootSetting.uPortNumber      = (opt.autoScan || opt.fixedPreloadPort <= 0)
                                           ? 0u
                                           : static_cast<unsigned int>(opt.fixedPreloadPort);

    set.connectSetting.kernelComPort  = opt.autoScan ? 0 : opt.fixedKernelPort;
    set.connectSetting.bDbFileFromDUT = false;

    char apDb[1024] = { 0 };
    char mdDb[1024] = { 0 };
    copyStr(apDb, sizeof(apDb), opt.apDbPath.empty() ? "bypass" : opt.apDbPath.c_str());
    set.connectSetting.pApDbFilePath = apDb;
    if (!opt.mdDbPath.empty())
    {
        copyStr(mdDb, sizeof(mdDb), opt.mdDbPath.c_str());
        set.connectSetting.pMdDbFilePath = mdDb;
    }
    else
    {
        set.connectSetting.pMdDbFilePath = nullptr;
    }
    set.connectSetting.pMdDbFilePath1 = nullptr;

    set.logSetting.enableDllLog         = !opt.logSavePath.empty();
    set.logSetting.enableUartLog        = false;
    set.logSetting.enablePcUsbDriverLog = false;
    set.logSetting.enableBootprofLog    = false;
    set.logSetting.iMDLoggEnable        = 0;
    set.logSetting.iMobileLogEnable     = 0;
    set.logSetting.iConnsysLogEnable    = 0;
    if (!opt.logSavePath.empty())
    {
        makeDirs(opt.logSavePath);
        copyStr(set.logSetting.logSavePath, sizeof(set.logSetting.logSavePath),
                opt.logSavePath.c_str());
    }

    const int ret = static_cast<int>(g_pfnConnect(
        slot, &set, &cnf, static_cast<unsigned int>(sizeof(set))));

    r.ret = ret;
    r.ok  = (ret == METAAPP_HIGH_LEVEL_SUCCESS);
    r.dbMismatch = (ret == METAAPP_HIGH_LEVEL_AP_DATABASE_NOT_MATCH ||
                    ret == METAAPP_HIGH_LEVEL_MD_DATABASE_NOT_MATCH);
    r.argSizeBad = (ret == METAAPP_HIGH_LEVEL_ARG_SIZE_NOT_MATCH);
    r.mdHandle   = cnf.hMDHandle;
    r.preloadPortCnf = static_cast<int>(cnf.uPreloaderPortNumber);
    r.kernelPortCnf  = static_cast<int>(cnf.uKernelPortNumber);
    r.text = resultText(ret);
    return r;
}

int connectModem(int slot)
{
    if (g_pfnApToModem == nullptr)
        return -1;
    return static_cast<int>(g_pfnApToModem(slot));
}

bool disconnect(int slot, int mode)
{
    if (g_pfnDisconnect == nullptr)
        return false;

    METAAPP_DISCONNECT_META_T para;
    std::memset(&para, 0, sizeof(para));
    para.eDisconPara = static_cast<METAAPP_DISCONNECT_META_PARA_E>(mode);
    para.bDoBackupNv = false;

    const METAAPP_HIGH_LEVEL_RESULT ret =
        g_pfnDisconnect(slot, para, static_cast<unsigned int>(sizeof(para)));
    return (ret == METAAPP_HIGH_LEVEL_SUCCESS);
}

unsigned structArgSize()
{
    return static_cast<unsigned>(sizeof(METAAPP_CONN_STTING_T));
}

const char* resultText(int ret)
{
    switch (ret)
    {
    case METAAPP_HIGH_LEVEL_SUCCESS:                  return "成功";
    case METAAPP_HIGH_LEVEL_FAIL:                     return "失败(未细分)";
    case METAAPP_HIGH_LEVEL_HANDLE_ALLOC_FAIL:        return "分配句柄失败";
    case METAAPP_HIGH_LEVEL_OPEN_DLL_LOG_FAIL:        return "打开 DLL 日志失败(检查日志目录)";
    case METAAPP_HIGH_LEVEL_GET_PRELOAD_PORT_FAIL:    return "找不到 Preloader/BootROM 端口(是否已断电重启? USB/驱动?)";
    case METAAPP_HIGH_LEVEL_BOOT_PRELOAD_FAIL:        return "Preloader 通信失败(供电/线材)";
    case METAAPP_HIGH_LEVEL_GET_KERNEL_PORT:          return "找不到 Kernel/ETS 端口";
    case METAAPP_HIGH_LEVEL_CONNECT_AP_META_FAIL:     return "连接 AP META 失败";
    case METAAPP_HIGH_LEVEL_GET_AP_VERSION_FAIL:      return "读取 AP 版本失败";
    case METAAPP_HIGH_LEVEL_LOAD_AP_DATABASE_FROM_DUT_FAIL: return "从 DUT 读取 AP DB 失败";
    case METAAPP_HIGH_LEVEL_LOAD_MD_DATABASE_FROM_DUT_FAIL: return "从 DUT 读取 MD DB 失败";
    case METAAPP_HIGH_LEVEL_INIT_AP_DATABASE_FAIL:    return "初始化 AP DB 失败";
    case METAAPP_HIGH_LEVEL_INIT_MINICOMLOGGER_FAIL:  return "初始化 META log 失败";
    case METAAPP_HIGH_LEVEL_OPEN_MODEM_LOG_FAIL:      return "打开 Modem log 失败";
    case METAAPP_HIGH_LEVEL_AP_DATABASE_NOT_MATCH:    return "AP DB 不匹配(端口已拿到, 可继续)";
    case METAAPP_HIGH_LEVEL_MD_DATABASE_NOT_MATCH:    return "MD DB 不匹配(端口已拿到, 可继续)";
    case METAAPP_HIGH_LEVEL_PRELOAD_SLA_FAIL:         return "Preloader SLA 认证失败";
    case METAAPP_HIGH_LEVEL_OPEN_AP_LOG_FAIL:         return "打开 AP log 失败";
    case METAAPP_HIGH_LEVEL_ARG_SIZE_NOT_MATCH:       return "结构体大小不匹配: 头文件与 MetaApp.dll 版本不一致!";
    default:                                          return "未知返回码(见 MetaApp.h)";
    }
}

} // namespace metasession
