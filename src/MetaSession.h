// ---------------------------------------------------------------------------
// MetaSession.h
//
// 对 MetaApp.dll 的极薄封装: 负责"把 DUT 拉进 META 模式"这一件事。
//
// 与 DutPortBinder/metabridge 相同原则:
//   - 本头文件不含 META SDK 头 / 不含 Qt 头
//   - 真正的 MetaApp.h 只在 MetaSession.cpp 里出现
//   - 用 LoadLibrary/GetProcAddress 动态加载, 缺 DLL 时给友好提示
//
// 关键概念(踩过的坑, 记在这里):
//   * meta_handle 就是 threadSlot, 取值 0..META_MAX_THREADS-1(32)。
//     METACalibrationLibrary_* 的第一个参数用的就是这个槽号, 不是 hMDHandle。
//   * autoscan(=true) 时 kernelComPort / preloaderPort 必须填 0, 由 DLL 自己扫。
//   * arg_size 必须传 sizeof(METAAPP_CONN_STTING_T), DLL 用它校验结构体版本,
//     不匹配会返回 METAAPP_HIGH_LEVEL_ARG_SIZE_NOT_MATCH(1000)。
// ---------------------------------------------------------------------------
#pragma once

#include <string>

namespace metasession
{

const int kMaxSlots = 32;   // MetaApp.h: META_MAX_THREADS = 32

// 断开行为, 与 METAAPP_DISCONNECT_META_PARA_E 一致
enum DisconnectMode
{
    DisconnectDoNothing = 0,
    DisconnectPowerOff  = 1,
    DisconnectReboot    = 2
};

typedef void (*LogSink)(const char* text, void* user);

struct ConnOptions
{
    bool        autoScan         = true;    // true: DLL 自己扫端口
    int         fixedPreloadPort = 0;       // autoScan=false 时用
    int         fixedKernelPort  = 0;       // autoScan=false 时用
    bool        alreadyInMeta    = false;   // DUT 已在 META 模式
    unsigned    timeoutMs        = 120000;
    bool        enableAdb        = true;
    unsigned    mdMode           = 0xC000;
    std::string mdDbPath;
    std::string apDbPath        = "bypass";
    std::string logSavePath;                // 空 = 关闭 DLL 日志
};

struct ConnResult
{
    int     ret        = -1;   // METAAPP_HIGH_LEVEL_RESULT
    bool    ok         = false;
    bool    dbMismatch = false;
    bool    argSizeBad = false;
    unsigned argSize   = 0;
    int     mdHandle   = 0;
    int     preloadPortCnf = 0;
    int     kernelPortCnf  = 0;
    std::string text;       // 错误说明
};

// 加载 / 卸载 MetaApp.dll
bool loadApp(const std::wstring& runtimeDir, std::string* err);
void unloadApp();
bool appLoaded();
std::string appPath();

// 给某个槽注册日志接收器(可重复调用覆盖)
bool registerLog(int slot, LogSink sink, void* user);

// 进入 META。成功后 meta_handle = slot
ConnResult connectTarget(int slot, const ConnOptions& opt, int* stopFlag);

// AP -> Modem (校准必须; 只做 AP 测试可以跳过)
// 返回 METAAPP_HIGH_LEVEL_RESULT 原始值
int connectModem(int slot);

// 退出 META
bool disconnect(int slot, int mode);

// sizeof(METAAPP_CONN_STTING_T), 用于和 DLL 版本对表
unsigned structArgSize();

// 返回码说明(UTF-8)
const char* resultText(int ret);

} // namespace metasession
