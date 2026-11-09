// ---------------------------------------------------------------------------
// CalBridge.cpp  --  唯一直接使用 META 头文件的翻译单元
//
// 这里做三件事:
//   1) 按 META 头文件原样使用结构体(保证与 DLL 的 ABI 完全一致)
//   2) 用 LoadLibrary/GetProcAddress 动态加载 METACalibrationLibrary.dll
//   3) 把"测试项开关名"翻译成结构体里的字节偏移, 让 UI 可以按名字开关任意项
//
// 结构体布局由 CMakeLists.txt 里那组宏决定, 必须与 DLL 的编译宏一致, 否则 ABI
// 不匹配。自检会用 sizeof 校验。
// ---------------------------------------------------------------------------

#define NOMINMAX          // 避免 windows.h 定义 min/max 宏, 破坏 std::min
#include "CalBridge.h"

#include "METACalibrationLibrary.h"   // -> METAStatusDefinition.h / METACommonDefinition.h

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace calbridge
{
namespace
{

// ---------------------------------------------------------------------------
// 1. 测试项开关表(自动生成)
// ---------------------------------------------------------------------------
struct RawFlag
{
    const char* name;
    const char* group;
    const char* desc;
    size_t      offset;
};

const RawFlag kRawFlags[] =
{
#include "gen/FlagTable.inc"
};

const size_t kRawFlagCount = sizeof(kRawFlags) / sizeof(kRawFlags[0]);

// ---------------------------------------------------------------------------
// 1b. DLL 明确拒绝的开关
//
// 头文件里这些 bool 是存在的、也能写进去, 但 DLL 并不支持: 只要它被置位,
// 整次执行的第一步就会被拒绝, 整条序列直接失败。
// 这里把它们标出来 —— UI 置灰不可勾选, 写 CFG 时也跳过, 免得用户踩。
//
// 名字必须与 FlagTable.inc 里的全名完全一致。
// ---------------------------------------------------------------------------
struct RawFlagNote
{
    const char* name;
    const char* reason;
};

const RawFlagNote kRawFlagNotes[] =
{
    { "ggeCalibrationItems.b_gge_txiq_cal",
      "DLL 不支持该开关, 置位会让整次执行直接失败" },
};

const RawFlagNote* findFlagNote(const char* name)
{
    for (size_t i = 0; i < sizeof(kRawFlagNotes) / sizeof(kRawFlagNotes[0]); ++i)
    {
        if (std::strcmp(kRawFlagNotes[i].name, name) == 0)
            return &kRawFlagNotes[i];
    }
    return nullptr;
}

// 分组键 -> 中文标签
const char* groupLabelOf(const std::string& g)
{
    struct M { const char* key; const char* label; };
    static const M kMap[] =
    {
        { "nrCalibrationItems",           "NR 校准" },
        { "nrPreTestItems",               "NR 预测试" },
        { "nrNsftItems",                  "NR 综测" },
        { "nrFr2NsftItems",               "NR FR2 综测" },
        { "selfCalibrationItems",         "自校准 (Pre/Post Self Cal)" },
        { "ggeCalibrationItems",          "GSM/GPRS/EDGE 校准" },
        { "ggeNsftItems",                 "GSM/GPRS/EDGE 综测" },
        { "wcdmaCalibrationItems",        "WCDMA 校准" },
        { "wcdmaNsftItems",               "WCDMA 综测" },
        { "tdscdmaCalibrationItems",      "TD-SCDMA 校准" },
        { "tdscdmaNsftItems",             "TD-SCDMA 综测" },
        { "lteCalibrationItems",          "LTE 校准" },
        { "ltePreTestItems",              "LTE 预测试" },
        { "lteNsftItems",                 "LTE 综测" },
        { "lteAntSwTestItems",            "LTE 天线开关测试" },
        { "c2kCalibrationItems",          "CDMA2000 校准" },
        { "c2kNsftItems",                 "CDMA2000 综测" },
        { "wcnTestItems",                 "WiFi / BT / GPS" },
        { "tasTestItems",                 "TAS 测试" },
        { "othersCalibrationItems",       "其它校准" },
        { "coTmsCalibrationItems",        "Co-TMS 校准" },
        { "cddcTestItems",                "CDDC 测试" },
        { "transceiverPowerTestItems",    "Transceiver Power 测试" },
        { "iotNtnCalibrationItems",       "IoT-NTN 校准" },
        { "iotNtnNsftItems",              "IoT-NTN 综测" },
        { "rffeCheckTestItems",           "RFFE Check (MIPI)" },
        { "oslCalibrationItems",          "OSL 校准" },
        { "efuseDataSaveItems",           "EFUSE 数据查询" },
    };
    for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); ++i)
    {
        if (g == kMap[i].key)
            return kMap[i].label;
    }
    return g.c_str();
}

std::vector<caltypes::FlagInfo> buildFlags()
{
    std::vector<caltypes::FlagInfo> v;
    v.reserve(kRawFlagCount);
    for (size_t i = 0; i < kRawFlagCount; ++i)
    {
        caltypes::FlagInfo f;
        f.name  = kRawFlags[i].name;
        f.group = kRawFlags[i].group;
        f.desc  = kRawFlags[i].desc;
        // groupLabel 需要稳定存储, 分组只有 28 个, 用静态字符串池即可
        f.groupLabel = groupLabelOf(f.group);
        if (const RawFlagNote* note = findFlagNote(kRawFlags[i].name))
        {
            f.supported = false;
            f.note = note->reason;
        }
        v.push_back(f);
    }
    return v;
}

// ---------------------------------------------------------------------------
// 2. 任务项表(自动生成)
// ---------------------------------------------------------------------------
struct RawTask
{
    int         id;
    const char* enumName;
    const char* name;
    const char* deps;
    int         isTest;
    int         supported;   // 0 = DLL 里 entry 为 NULL(表里有但没实现)
    const char* note;        // DLL 的 taskDescription 原文
};

const RawTask kRawTasks[] =
{
#include "gen/TaskCatalog.inc"
};

const size_t kRawTaskCount = sizeof(kRawTasks) / sizeof(kRawTasks[0]);

std::vector<caltypes::TaskInfo> buildTasks()
{
    std::vector<caltypes::TaskInfo> v;
    v.reserve(kRawTaskCount);
    for (size_t i = 0; i < kRawTaskCount; ++i)
    {
        caltypes::TaskInfo t;
        t.id        = kRawTasks[i].id;
        t.enumName  = kRawTasks[i].enumName;
        t.name      = kRawTasks[i].name;
        t.deps      = kRawTasks[i].deps;
        t.isTest    = (kRawTasks[i].isTest != 0);
        t.supported = (kRawTasks[i].supported != 0);
        t.note      = kRawTasks[i].note;
        v.push_back(t);
    }
    return v;
}

// ---------------------------------------------------------------------------
// 3. 动态加载
// ---------------------------------------------------------------------------
HMODULE g_hMod = nullptr;
std::string g_runtimeDir;
std::string g_dllPath;

// 函数指针
typedef int  (__stdcall* PFN_GetDLLVer)(unsigned int*, unsigned int*, unsigned int*, unsigned int*);
typedef E_METACalibrationLibrary_RESULT (__stdcall* PFN_RegisterCallBack)(
    const int, const METACalibrationLibrary_Log_Display_CallBack);
typedef E_METACalibrationLibrary_RESULT (__stdcall* PFN_StatusCallBackRegister)(
    const int, void*, const META_Status_CallBack);
typedef E_METACalibrationLibrary_RESULT (__stdcall* PFN_StartEx)(
    const int, const S_CalibrationLibrary_TestOptionControl*,
    const METACalibrationLibrary_COMMON_CFG_TW*, int*);
typedef const S_META_STATUS_T* (__stdcall* PFN_GetLastStatus)(const int);
typedef const unsigned long long (__stdcall* PFN_GetLastStatusEncodeValue)(const int);
typedef E_METACalibrationLibrary_RESULT (__stdcall* PFN_InitCalDataOperation)(const int, wchar_t*);
typedef E_METACalibrationLibrary_RESULT (__stdcall* PFN_DeInitCalDataOperation)(const int);
typedef int  (__stdcall* PFN_GetTargetCalDataItemCount)(const int);
typedef E_METACalibrationLibrary_RESULT (__stdcall* PFN_GetTargetCalDataItemArray)(
    const int, int*, S_CalibrationLibrary_CalDataItemContext*);
typedef E_METACalibrationLibrary_RESULT (__stdcall* PFN_ReadCalDataFromTarget)(
    const int, const S_CalibrationLibrary_CalDataOperationReq*,
    const S_CalibrationLibrary_CalDataOperationOption*, int*);
typedef E_METACalibrationLibrary_RESULT (__stdcall* PFN_WriteCalDataToTarget)(
    const int, const S_CalibrationLibrary_CalDataOperationReq*,
    const S_CalibrationLibrary_CalDataOperationOption*, int*);
typedef E_METACalibrationLibrary_RESULT (__stdcall* PFN_ImportCalDataFromFile)(
    const int, const wchar_t*, const S_CalibrationLibrary_CalDataOperationReq*,
    const S_CalibrationLibrary_CalDataOperationOption*, int*);
typedef E_METACalibrationLibrary_RESULT (__stdcall* PFN_ExportCalDataToFile)(
    const int, const wchar_t*, const S_CalibrationLibrary_CalDataOperationReq*,
    const S_CalibrationLibrary_CalDataOperationOption*, int*);

PFN_GetDLLVer                          g_GetDLLVer = nullptr;
PFN_RegisterCallBack                   g_RegisterCallBack = nullptr;
PFN_StatusCallBackRegister             g_StatusCallBackRegister = nullptr;
PFN_StartEx                            g_StartEx = nullptr;
PFN_GetLastStatus                      g_GetLastStatus = nullptr;
PFN_GetLastStatusEncodeValue           g_GetLastStatusEncodeValue = nullptr;
PFN_InitCalDataOperation               g_InitCalDataOperation = nullptr;
PFN_DeInitCalDataOperation             g_DeInitCalDataOperation = nullptr;
PFN_GetTargetCalDataItemCount          g_GetTargetCalDataItemCount = nullptr;
PFN_GetTargetCalDataItemArray          g_GetTargetCalDataItemArray = nullptr;
PFN_ReadCalDataFromTarget              g_ReadCalDataFromTarget = nullptr;
PFN_WriteCalDataToTarget               g_WriteCalDataToTarget = nullptr;
PFN_ImportCalDataFromFile              g_ImportCalDataFromFile = nullptr;
PFN_ExportCalDataToFile                g_ExportCalDataToFile = nullptr;

std::string g_missingExports;

// 回调
LogSink    g_logSink = nullptr;
void*      g_logUser = nullptr;
StatusSink g_statusSink = nullptr;
void*      g_statusUser = nullptr;

void __stdcall logTrampoline(const char* text)
{
    if (g_logSink != nullptr && text != nullptr)
        g_logSink(text, g_logUser);
}

void __stdcall statusTrampoline(const S_META_STATUS_T* st)
{
    if (g_statusSink == nullptr || st == nullptr)
        return;
    caltypes::StatusEvent ev;
    ev.statusCode = static_cast<int>(st->statusCode);
    ev.statusType = static_cast<int>(st->statusType);
    ev.itemName   = st->itemName;
    ev.message    = st->statusMsgbuf;
    g_statusSink(ev, g_statusUser);
}

std::string win32ErrorText(DWORD code)
{
    // W 版 + UTF-8: FormatMessageA 在中文系统上返回 GBK, 直接当 UTF-8 用会乱码
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

    int m = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (m <= 1)
        return std::string();
    std::string s(static_cast<size_t>(m - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], m, nullptr, nullptr);
    return s;
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

std::wstring widen(const std::string& a)
{
    if (a.empty())
        return std::wstring();
    int n = ::MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, nullptr, 0);
    if (n <= 1)
        return std::wstring();
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, &w[0], n);
    return w;
}

template <typename T>
bool resolve(T& fn, const char* name)
{
    fn = reinterpret_cast<T>(reinterpret_cast<void*>(::GetProcAddress(g_hMod, name)));
    if (fn == nullptr)
    {
        if (!g_missingExports.empty())
            g_missingExports += ", ";
        g_missingExports += name;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 4. 把 Config 的开关值写进 META 的 CFG 结构体
// ---------------------------------------------------------------------------
void applyFlags(METACalibrationLibrary_COMMON_CFG_TW& c, const std::vector<bool>& values)
{
    const size_t n = (values.size() < kRawFlagCount) ? values.size() : kRawFlagCount;
    char* base = reinterpret_cast<char*>(&c);
    for (size_t i = 0; i < n; ++i)
    {
        if (!values[i])
            continue;
        // 被 DLL 明确拒绝的开关一律不写进去: 这类开关一置位, 整次执行的第一步
        // 就会失败。这里做防御, 免得旧 ini 里残留的 true 又把运行搞挂。
        if (findFlagNote(kRawFlags[i].name) != nullptr)
            continue;
        *reinterpret_cast<bool*>(base + kRawFlags[i].offset) = true;
    }
}

// 构造 cal-data 请求
void fillCalDataReq(S_CalibrationLibrary_CalDataOperationReq& req,
                    std::vector<char>& idStorage,
                    const std::vector<char>& itemIds)
{
    idStorage = itemIds;
    req.logPath      = nullptr;
    req.itemIdCount  = static_cast<unsigned int>(idStorage.size());
    req.itemIdArray  = idStorage.empty() ? nullptr : &idStorage[0];
}

caltypes::RunResult makeResult(int raw, const char* what)
{
    caltypes::RunResult r;
    r.raw = raw;
    r.ok  = (raw == E_METACalibrationLibrary_RESULT_SUCCESS);
    if (!r.ok)
    {
        r.error = what;
        r.error += " 返回 ";
        r.error += std::to_string(raw);
        r.error += " (";
        r.error += resultText(raw);
        r.error += ")";
    }
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// 元信息
// ---------------------------------------------------------------------------
const std::vector<caltypes::FlagInfo>& flags()
{
    static const std::vector<caltypes::FlagInfo> kCache = buildFlags();
    return kCache;
}

const std::vector<caltypes::TaskInfo>& tasks()
{
    static const std::vector<caltypes::TaskInfo> kCache = buildTasks();
    return kCache;
}

int flagIndex(const std::string& name)
{
    const std::vector<caltypes::FlagInfo>& v = flags();
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (v[i].name == name)
            return static_cast<int>(i);
    }
    return -1;
}

// ---------------------------------------------------------------------------
// DLL 生命周期
// ---------------------------------------------------------------------------
std::wstring guessRuntimeDir()
{
#ifdef MQC_RUNTIME_DIR
    // CMake 在编译期烘焙进来的自包含运行库目录(自包含打包时优先用它)
    {
        const std::wstring baked = widen(MQC_RUNTIME_DIR);
        if (::GetFileAttributesW((baked + L"\\METACalibrationLibrary.dll").c_str())
            != INVALID_FILE_ATTRIBUTES)
        {
            return baked;
        }
    }
#endif

    // exe 目录
    char exeBuf[MAX_PATH * 2] = { 0 };
    std::wstring exeDir;
    DWORD n = ::GetModuleFileNameA(nullptr, exeBuf, static_cast<DWORD>(sizeof(exeBuf)));
    if (n > 0 && n < sizeof(exeBuf))
    {
        std::string s(exeBuf, n);
        const size_t pos = s.find_last_of("\\/");
        if (pos != std::string::npos)
            s.erase(pos);
        exeDir = widen(s);
    }
    if (exeDir.empty())
        return L".";

    // 候选(相对 exe 目录)
    static const wchar_t* kRel[] =
    {
        L".",
        L"..\\..\\sdk\\META\\bin",
        L"..\\..\\..\\METASharingLibrary\\METACalibrationLibrary\\CustomerRelease",
        L"..\\..\\..\\METASharingLibrary\\METACalibrationLibrary\\Debug",
        L"..\\..\\..\\bin",
        L"sdk\\META\\bin",
        L"..\\bin",
    };

    const size_t cnt = sizeof(kRel) / sizeof(kRel[0]);

    // 第一轮: 找 METACalibrationLibrary.dll
    for (size_t i = 0; i < cnt; ++i)
    {
        std::wstring dir = exeDir + L"\\" + kRel[i];
        std::wstring probe = dir + L"\\METACalibrationLibrary.dll";
        if (::GetFileAttributesW(probe.c_str()) != INVALID_FILE_ATTRIBUTES)
            return dir;
    }
    // 第二轮: 退而求其次, 找 MetaCore.dll
    for (size_t i = 0; i < cnt; ++i)
    {
        std::wstring dir = exeDir + L"\\" + kRel[i];
        std::wstring probe = dir + L"\\MetaCore.dll";
        if (::GetFileAttributesW(probe.c_str()) != INVALID_FILE_ATTRIBUTES)
            return dir;
    }
    return exeDir;
}

bool loadDll(const std::wstring& runtimeDir, std::string* err)
{
    unloadDll();

    g_missingExports.clear();
    g_runtimeDir = narrow(runtimeDir);

    if (!runtimeDir.empty())
        ::SetDllDirectoryW(runtimeDir.c_str());

    // 优先从运行时目录加载, 其次走默认搜索
    std::wstring candidate = runtimeDir;
    if (!candidate.empty())
    {
        if (candidate[candidate.size() - 1] != L'\\' && candidate[candidate.size() - 1] != L'/')
            candidate += L'\\';
    }
    candidate += L"METACalibrationLibrary.dll";

    ::SetLastError(0);
    g_hMod = ::LoadLibraryW(candidate.c_str());
    if (g_hMod == nullptr)
    {
        ::SetLastError(0);
        g_hMod = ::LoadLibraryW(L"METACalibrationLibrary.dll");
    }

    if (g_hMod == nullptr)
    {
        const DWORD e = ::GetLastError();
        if (err != nullptr)
        {
            *err = "加载 METACalibrationLibrary.dll 失败: " + win32ErrorText(e);
            *err += "\r\n已尝试路径: " + narrow(candidate);
            *err += "\r\n常见原因: 同目录缺少依赖 DLL (MetaCore.dll / MetaApp.dll / "
                    "METAInstrumentLibrary.dll / SLA_Challenge.dll / zlibwapi.dll 等), "
                    "或位数不匹配(必须 Win32)。";
        }
        if (!runtimeDir.empty())
            ::SetDllDirectoryW(nullptr);
        return false;
    }

    char buf[MAX_PATH * 2] = { 0 };
    DWORD n = ::GetModuleFileNameA(g_hMod, buf, static_cast<DWORD>(sizeof(buf)));
    g_dllPath = (n > 0 && n < sizeof(buf)) ? std::string(buf, n) : std::string();

    resolve(g_GetDLLVer, "METACalibrationLibrary_GetDLLVer");
    resolve(g_RegisterCallBack, "METACalibrationLibrary_RegisterCallBack");
    resolve(g_StatusCallBackRegister, "METACalibrationLibrary_StatusCallBackRegister");
    resolve(g_StartEx, "METACalibrationLibrary_StartEx");
    resolve(g_GetLastStatus, "METACalibrationLibrary_GetLastStatus");
    resolve(g_GetLastStatusEncodeValue, "METACalibrationLibrary_GetLastStatusEncodeValue");
    resolve(g_InitCalDataOperation, "METACalibrationLibrary_InitCalDataOperation");
    resolve(g_DeInitCalDataOperation, "METACalibrationLibrary_DeInitCalDataOperation");
    resolve(g_GetTargetCalDataItemCount, "METACalibrationLibrary_GetTargetCalDataItemCount");
    resolve(g_GetTargetCalDataItemArray, "METACalibrationLibrary_GetTargetCalDataItemArray");
    resolve(g_ReadCalDataFromTarget, "METACalibrationLibrary_ReadCalDataFromTarget");
    resolve(g_WriteCalDataToTarget, "METACalibrationLibrary_WriteCalDataToTarget");
    resolve(g_ImportCalDataFromFile, "METACalibrationLibrary_ImportCalDataFromFile");
    resolve(g_ExportCalDataToFile, "METACalibrationLibrary_ExportCalDataToFile");

    if (g_StartEx == nullptr)
    {
        if (err != nullptr)
            *err = "DLL 已加载但缺少关键导出 METACalibrationLibrary_StartEx, 版本可能过旧。";
        unloadDll();
        return false;
    }

    return true;
}

void unloadDll()
{
    if (g_hMod != nullptr)
    {
        ::FreeLibrary(g_hMod);
        g_hMod = nullptr;
    }
    g_dllPath.clear();
    g_GetDLLVer = nullptr;
    g_RegisterCallBack = nullptr;
    g_StatusCallBackRegister = nullptr;
    g_StartEx = nullptr;
    g_GetLastStatus = nullptr;
    g_GetLastStatusEncodeValue = nullptr;
    g_InitCalDataOperation = nullptr;
    g_DeInitCalDataOperation = nullptr;
    g_GetTargetCalDataItemCount = nullptr;
    g_GetTargetCalDataItemArray = nullptr;
    g_ReadCalDataFromTarget = nullptr;
    g_WriteCalDataToTarget = nullptr;
    g_ImportCalDataFromFile = nullptr;
    g_ExportCalDataToFile = nullptr;
    ::SetDllDirectoryW(nullptr);
}

bool isLoaded()
{
    return g_hMod != nullptr;
}

caltypes::DllStatus status()
{
    caltypes::DllStatus s;
    s.loaded = (g_hMod != nullptr);
    s.dllPath = g_dllPath;
    s.runtimeDir = g_runtimeDir;
    s.cfgSize = static_cast<unsigned>(sizeof(METACalibrationLibrary_COMMON_CFG_TW));
    s.missingList = g_missingExports;
    s.missingExports = 0;
    if (!g_missingExports.empty())
    {
        int cnt = 1;
        for (size_t i = 0; i < g_missingExports.size(); ++i)
        {
            if (g_missingExports[i] == ',')
                ++cnt;
        }
        s.missingExports = cnt;
    }
    s.resolvedExports = 14 - s.missingExports;

    if (s.loaded && g_GetDLLVer != nullptr)
    {
        unsigned int a = 0, b = 0, c = 0, d = 0;
        if (g_GetDLLVer(&a, &b, &c, &d) == 0 || true)
        {
            s.major = a; s.minor = b; s.build = c; s.patch = d;
            s.versionOk = true;
            char tmp[128];
            std::snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u", a, b, c, d);
            s.versionText = tmp;
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// 回调
// ---------------------------------------------------------------------------
void setLogSink(LogSink sink, void* user)
{
    g_logSink = sink;
    g_logUser = user;
}

void setStatusSink(StatusSink sink, void* user)
{
    g_statusSink = sink;
    g_statusUser = user;
}

// ---------------------------------------------------------------------------
// 运行
// ---------------------------------------------------------------------------
caltypes::RunResult run(int metaHandle, const caltypes::Config& cfg, int* stopFlag)
{
    caltypes::RunResult r;
    if (!isLoaded())
    {
        r.error = "DLL 未加载";
        return r;
    }
    if (g_StartEx == nullptr)
    {
        r.error = "缺少导出 StartEx";
        return r;
    }

    // 注册回调(幂等, 每次运行前重设一次最稳)
    if (g_RegisterCallBack != nullptr)
        g_RegisterCallBack(metaHandle, reinterpret_cast<METACalibrationLibrary_Log_Display_CallBack>(&logTrampoline));
    if (g_StatusCallBackRegister != nullptr)
        g_StatusCallBackRegister(metaHandle, nullptr,
                                 reinterpret_cast<META_Status_CallBack>(&statusTrampoline));

    METACalibrationLibrary_COMMON_CFG_TW c;
    std::memset(&c, 0, sizeof(c));

    c.nThreadNumber = 1;
    c.cfg_path                  = cfg.cfgPath.empty()        ? nullptr : cfg.cfgPath.c_str();
    c.ini_path                  = cfg.iniPath.empty()        ? nullptr : cfg.iniPath.c_str();
    c.log_path                  = cfg.logPath.empty()        ? nullptr : cfg.logPath.c_str();
    c.output_path               = cfg.outputPath.empty()     ? nullptr : cfg.outputPath.c_str();
    c.cal_result_path           = cfg.calResultPath.empty()  ? nullptr : cfg.calResultPath.c_str();
    c.sp_nvram_database_path    = cfg.spDbPath.empty()       ? nullptr : cfg.spDbPath.c_str();
    c.md_nvram_database_path    = cfg.mdDbPath.empty()       ? nullptr : cfg.mdDbPath.c_str();
    c.golden_data_ini_path      = cfg.goldenIniPath.empty()  ? nullptr : cfg.goldenIniPath.c_str();

    c.i_device_type            = cfg.devTypeGge;
    c.i_device_type_wcdma      = cfg.devTypeWcdma;
    c.i_device_type_tdscdma    = cfg.devTypeTdscdma;
#if defined(__META_LTE__)
    c.i_device_type_lte        = cfg.devTypeLte;
#endif
    c.i_device_type_nr         = cfg.devTypeNr;
    c.i_device_type_nr_fr2     = cfg.devTypeNrFr2;
#if defined(__META_C2K__)
    c.i_device_type_c2k        = cfg.devTypeC2k;
#endif
#if !defined(__NO_WCN_TEST__)
    c.i_device_type_wcn_bt     = cfg.devTypeWcnBt;
    c.i_device_type_wcn_wifi   = cfg.devTypeWcnWifi;
    c.i_device_type_wcn_gps    = cfg.devTypeWcnGps;
#endif
    c.i_device_type_iot_ntn    = cfg.devTypeIotNtn;

    c.resetTester       = cfg.resetTester;
    c.i_tester_rf_port  = cfg.testerRfPort;

    applyFlags(c, cfg.flagValues);

    // 序列: StartEx 会直接解引用 testingSequence, 绝不可以传 nullptr
    const std::wstring seqW = widen(cfg.sequence);

    S_CalibrationLibrary_TestOptionControl opt;
    std::memset(&opt, 0, sizeof(opt));
    opt.instControl.skipInstInitialize      = false;
    opt.instControl.instHandle              = 0;
    opt.testByFreqControl.enableByFreqTest  = false;
    opt.testByFreqControl.testFreqCount     = 0;
    opt.testByFreqControl.testFreqInfoArray = nullptr;
    opt.testingSequence                     = seqW.c_str();

    const clock_t t0 = std::clock();
    const int raw = static_cast<int>(g_StartEx(metaHandle, &opt, &c, stopFlag));
    const clock_t t1 = std::clock();

    r = makeResult(raw, "METACalibrationLibrary_StartEx");

    // 收尾信息
    if (g_GetLastStatus != nullptr)
    {
        const S_META_STATUS_T* st = g_GetLastStatus(metaHandle);
        if (st != nullptr)
        {
            r.lastStatus = std::string(st->itemName) + " | " + std::string(st->statusMsgbuf);
        }
    }
    if (g_GetLastStatusEncodeValue != nullptr)
        r.lastStatusEncoded = g_GetLastStatusEncodeValue(metaHandle);

    r.seconds = static_cast<double>(t1 - t0) / static_cast<double>(CLOCKS_PER_SEC);
    return r;
}

// ---------------------------------------------------------------------------
// 校准数据操作(Gen99 / M90 及以上平台)
// ---------------------------------------------------------------------------
bool initCalData(int metaHandle, const std::wstring& logPath, std::string* err)
{
    if (g_InitCalDataOperation == nullptr)
    {
        if (err) *err = "DLL 不支持 InitCalDataOperation";
        return false;
    }
    std::wstring lp = logPath;
    const int raw = static_cast<int>(g_InitCalDataOperation(metaHandle, lp.empty() ? nullptr : &lp[0]));
    if (raw != E_METACalibrationLibrary_RESULT_SUCCESS)
    {
        if (err) *err = std::string("InitCalDataOperation 失败: ") + resultText(raw);
        return false;
    }
    return true;
}

bool deinitCalData(int metaHandle)
{
    if (g_DeInitCalDataOperation == nullptr)
        return false;
    return g_DeInitCalDataOperation(metaHandle) == E_METACalibrationLibrary_RESULT_SUCCESS;
}

bool listTargetCalDataItems(int metaHandle, std::vector<caltypes::CalDataItem>* out, std::string* err)
{
    if (out == nullptr)
        return false;
    out->clear();

    if (g_GetTargetCalDataItemCount == nullptr || g_GetTargetCalDataItemArray == nullptr)
    {
        if (err) *err = "DLL 不支持 CalData 项枚举";
        return false;
    }

    const int count = g_GetTargetCalDataItemCount(metaHandle);
    if (count <= 0)
    {
        if (err)
        {
            *err = "GetTargetCalDataItemCount 返回 " + std::to_string(count);
            if (count == -1) *err += " (该平台不支持, 仅 Gen99/M90 支持)";
            if (count == -2) *err += " (handle 无效)";
        }
        return false;
    }

    std::vector<S_CalibrationLibrary_CalDataItemContext> buf(static_cast<size_t>(count));
    int n = count;
    const int raw = static_cast<int>(
        g_GetTargetCalDataItemArray(metaHandle, &n, &buf[0]));
    if (raw != E_METACalibrationLibrary_RESULT_SUCCESS)
    {
        if (err) *err = std::string("GetTargetCalDataItemArray 失败: ") + resultText(raw);
        return false;
    }

    for (int i = 0; i < n && i < count; ++i)
    {
        caltypes::CalDataItem it;
        it.itemId      = static_cast<int>(buf[static_cast<size_t>(i)].itemId);
        it.hwType      = static_cast<int>(buf[static_cast<size_t>(i)].hwType);
        it.name        = buf[static_cast<size_t>(i)].name;
        it.category    = buf[static_cast<size_t>(i)].category;
        it.description = buf[static_cast<size_t>(i)].description;
        it.dataType    = static_cast<int>(buf[static_cast<size_t>(i)].dataType);
        out->push_back(it);
    }
    return true;
}

namespace
{
S_CalibrationLibrary_CalDataOperationOption makeOption()
{
    S_CalibrationLibrary_CalDataOperationOption o;
    o.isFailedStop = true;
    return o;
}
} // namespace

caltypes::RunResult readCalDataToTarget(int metaHandle, const std::vector<char>& itemIds, int* stopFlag)
{
    if (g_ReadCalDataFromTarget == nullptr)
    {
        caltypes::RunResult r;
        r.error = "DLL 不支持 ReadCalDataFromTarget";
        return r;
    }
    std::vector<char> storage;
    S_CalibrationLibrary_CalDataOperationReq req;
    fillCalDataReq(req, storage, itemIds);
    const S_CalibrationLibrary_CalDataOperationOption opt = makeOption();
    const int raw = static_cast<int>(g_ReadCalDataFromTarget(metaHandle, &req, &opt, stopFlag));
    return makeResult(raw, "ReadCalDataFromTarget");
}

caltypes::RunResult exportCalDataToFile(int metaHandle, const std::wstring& iniPath,
                                        const std::vector<char>& itemIds, int* stopFlag)
{
    if (g_ExportCalDataToFile == nullptr)
    {
        caltypes::RunResult r;
        r.error = "DLL 不支持 ExportCalDataToFile";
        return r;
    }
    std::vector<char> storage;
    S_CalibrationLibrary_CalDataOperationReq req;
    fillCalDataReq(req, storage, itemIds);
    const S_CalibrationLibrary_CalDataOperationOption opt = makeOption();
    const int raw = static_cast<int>(
        g_ExportCalDataToFile(metaHandle, iniPath.c_str(), &req, &opt, stopFlag));
    return makeResult(raw, "ExportCalDataToFile");
}

caltypes::RunResult importCalDataFromFile(int metaHandle, const std::wstring& iniPath,
                                          const std::vector<char>& itemIds, int* stopFlag)
{
    if (g_ImportCalDataFromFile == nullptr)
    {
        caltypes::RunResult r;
        r.error = "DLL 不支持 ImportCalDataFromFile";
        return r;
    }
    std::vector<char> storage;
    S_CalibrationLibrary_CalDataOperationReq req;
    fillCalDataReq(req, storage, itemIds);
    const S_CalibrationLibrary_CalDataOperationOption opt = makeOption();
    const int raw = static_cast<int>(
        g_ImportCalDataFromFile(metaHandle, iniPath.c_str(), &req, &opt, stopFlag));
    return makeResult(raw, "ImportCalDataFromFile");
}

caltypes::RunResult writeCalDataToTarget(int metaHandle, const std::vector<char>& itemIds, int* stopFlag)
{
    if (g_WriteCalDataToTarget == nullptr)
    {
        caltypes::RunResult r;
        r.error = "DLL 不支持 WriteCalDataToTarget";
        return r;
    }
    std::vector<char> storage;
    S_CalibrationLibrary_CalDataOperationReq req;
    fillCalDataReq(req, storage, itemIds);
    const S_CalibrationLibrary_CalDataOperationOption opt = makeOption();
    const int raw = static_cast<int>(g_WriteCalDataToTarget(metaHandle, &req, &opt, stopFlag));
    return makeResult(raw, "WriteCalDataToTarget");
}

// ---------------------------------------------------------------------------
// 其它
// ---------------------------------------------------------------------------
unsigned cfgStructSize()
{
    return static_cast<unsigned>(sizeof(METACalibrationLibrary_COMMON_CFG_TW));
}

const char* resultText(int raw)
{
    switch (raw)
    {
    case E_METACalibrationLibrary_RESULT_SUCCESS: return "成功";
    case E_METACalibrationLibrary_RESULT_FAILED:  return "失败";
    default:                                      return "未知";
    }
}

std::string checkPaths(const caltypes::Config& cfg)
{
    std::string problems;

    if (cfg.cfgPath.empty())
    {
        problems += "- 未选择配置文件 (*.cfg)\r\n";
    }
    else
    {
        const DWORD attr = ::GetFileAttributesW(cfg.cfgPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
            problems += "- 配置文件不存在: " + narrow(cfg.cfgPath) + "\r\n";
    }

    if (cfg.sequence.empty())
        problems += "- 未选择任何测试项(序列为空)\r\n";

    // log / output 是文件, 检查其父目录是否可写
    struct Item { const wchar_t* path; const char* label; };
    const Item files[] =
    {
        { cfg.logPath.c_str(),        "日志文件" },
        { cfg.outputPath.c_str(),     "校准数据输出" },
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
    {
        if (files[i].path == nullptr || files[i].path[0] == 0)
            continue;
        std::wstring dir(files[i].path);
        const size_t pos = dir.find_last_of(L"\\/");
        if (pos != std::wstring::npos)
            dir.erase(pos);
        else
            dir.clear();
        if (!dir.empty())
        {
            const DWORD attr = ::GetFileAttributesW(dir.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
                problems += std::string("- ") + files[i].label + " 目录不存在: " + narrow(dir) + "\r\n";
        }
    }

    return problems;
}

} // namespace calbridge
