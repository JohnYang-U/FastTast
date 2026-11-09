// ---------------------------------------------------------------------------
// CalTypes.h
//
// 纯 C++ 数据类型, 不含 Qt / 不含 META SDK 头文件。
// 目的是让 UI 层不必 include 任何 META 头(避免 <windows.h> / meta.h 的宏污染)。
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

namespace caltypes
{

// DLL / 环境的加载状态
struct DllStatus
{
    bool        loaded = false;
    std::string dllPath;        // 实际加载到的 METACalibrationLibrary.dll
    std::string runtimeDir;     // 用于 SetDllDirectory 的运行时目录
    std::string error;          // 失败原因(含 Win32 错误码文本)

    // 版本
    bool        versionOk = false;
    unsigned    major = 0, minor = 0, build = 0, patch = 0;
    std::string versionText;

    // ABI 自检: sizeof(METACalibrationLibrary_COMMON_CFG_TW)
    unsigned    cfgSize = 0;

    // 已解析到的关键导出数量
    int         resolvedExports = 0;
    int         missingExports  = 0;
    std::string missingList;
};

// 一个测试项(任务)
struct TaskInfo
{
    int         id = -1;
    std::string enumName;   // 枚举名, 如 LTE_NSFT
    std::string name;       // 显示名
    std::string deps;       // 前置依赖(逗号分隔 taskId)
    bool        isTest = false;   // true=综测(NSFT), false=校准
    bool        supported = true; // false = DLL 里 entry 为 NULL(表里有但没实现)
    std::string note;             // DLL 里 taskDescription 原文, 如 "Not support now"
};

// 一个测试项开关(对应 COMMON_CFG_TW 内的一个 bool)
struct FlagInfo
{
    std::string name;       // 全名: lteCalibrationItems.b_lte_tpc_cal
    std::string group;      // 分组键: lteCalibrationItems
    std::string groupLabel; // 中文分组名
    std::string desc;       // 头文件里的注释
    bool        supported = true; // false = DLL 会显式拒绝这个开关
    std::string note;             // 不支持的原因(给 UI 做 tooltip)
};

// 运行配置(对应 METACalibrationLibrary_COMMON_CFG_TW)
struct Config
{
    // 路径(宽字符, 直接传给 DLL)
    std::wstring cfgPath;          // *.cfg  META 校准配置文件(必须存在)
    std::wstring iniPath;          // *.ini  初始化值文件
    std::wstring logPath;          // *.log  日志输出
    std::wstring outputPath;       // *.cal  校准数据输出
    std::wstring calResultPath;    // *.csv  校准报告目录
    std::wstring spDbPath;         // AP NVRAM database
    std::wstring mdDbPath;         // MD NVRAM database
    std::wstring goldenIniPath;    // golden calibration data ini

    // 仪器型号(取值见 E_RCTLIB_DEVICE_TYPE_META, 0 = 不测该制式)
    int devTypeGge = 0;
    int devTypeWcdma = 0;
    int devTypeTdscdma = 0;
    int devTypeLte = 0;
    int devTypeNr = 0;
    int devTypeNrFr2 = 0;
    int devTypeC2k = 0;
    int devTypeWcnBt = 0;
    int devTypeWcnWifi = 0;
    int devTypeWcnGps = 0;
    int devTypeIotNtn = 0;

    bool resetTester = false;
    int  testerRfPort = 0;

    // 与 flags() 同序的开关值
    std::vector<bool> flagValues;

    // 要执行的序列, 逗号分隔 taskId, 如 "29,30"
    std::string sequence;
};

// DLL 回传的状态事件(对应 S_META_STATUS_T)
struct StatusEvent
{
    int         statusCode = 0;
    int         statusType = 0;   // 0=信息, 1=错误(见 E_META_STATUS_TYPE)
    std::string itemName;
    std::string message;
};

// 一次运行的结果
struct RunResult
{
    bool        ok = false;
    int         raw = -1;         // E_METACalibrationLibrary_RESULT
    std::string error;
    std::string lastStatus;       // GetLastStatus 的文本
    unsigned long long lastStatusEncoded = 0;
    double      seconds = 0.0;
};

// 校准数据项(对应 S_CalibrationLibrary_CalDataItemContext)
struct CalDataItem
{
    int         itemId = 0;
    int         hwType = 0;
    std::string name;
    std::string category;
    std::string description;
    int         dataType = 0;
};

} // namespace caltypes
