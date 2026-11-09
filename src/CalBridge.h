// ---------------------------------------------------------------------------
// CalBridge.h
//
// METACalibrationLibrary.dll 的薄封装。
//
// 设计要点(与 DutPortBinder/metabridge 相同思路):
//   1) 本头文件不含任何 META SDK 头, 也不含 Qt 头 -> 可以安全地被 UI 层 include。
//   2) 真正的 METACalibrationLibrary.h 只出现在 CalBridge.cpp 这一个翻译单元里。
//   3) DLL 用 LoadLibrary/GetProcAddress 动态加载:
//        - 编译期不依赖 METACalibrationLibrary.lib(不需要 152MB 的 DLL 在场就能编译)
//        - 缺 DLL 时能给出精确原因, 而不是让程序直接崩
//        - 同一份 exe 可以换不同版本的 META_DLL
//   4) 测试项开关通过"名字 -> 字节偏移"表设置, 偏移由 offsetof 在编译期校验。
// ---------------------------------------------------------------------------
#pragma once

#include "CalTypes.h"

#include <string>
#include <vector>

namespace calbridge
{

// ---- 元信息 ----------------------------------------------------------------
// 全部测试项开关(193 个)。顺序固定, Config::flagValues 与之一一对应。
const std::vector<caltypes::FlagInfo>& flags();

// 全部任务项(74 个), 由 META 的任务映射表生成。
const std::vector<caltypes::TaskInfo>& tasks();

// 某个 flag 名的下标, 找不到返回 -1
int flagIndex(const std::string& name);

// ---- DLL 生命周期 ----------------------------------------------------------
// 猜测 META 运行时目录: 优先返回含有 METACalibrationLibrary.dll 的目录,
// 找不到则返回 exe 所在目录。用于首次启动时自动填好路径。
std::wstring guessRuntimeDir();

// runtimeDir: 存放 METACalibrationLibrary.dll 及其依赖的目录。会先 SetDllDirectory 到该目录。
// 成功返回 true; 失败时 err 里带 Win32 错误码与提示。
bool loadDll(const std::wstring& runtimeDir, std::string* err);
void unloadDll();
bool isLoaded();
caltypes::DllStatus status();

// ---- 回调 ------------------------------------------------------------------
// 日志回调(可能来自 DLL 自己的线程), 实现方需保证线程安全。
using LogSink = void (*)(const char* text, void* user);
void setLogSink(LogSink sink, void* user);

// 状态回调(S_META_STATUS_T)
using StatusSink = void (*)(const caltypes::StatusEvent& ev, void* user);
void setStatusSink(StatusSink sink, void* user);

// ---- 运行 ------------------------------------------------------------------
// 阻塞执行校准/综测序列。必须在工作线程里调用(GUI 线程会卡)。
// metaHandle: 已连接的 META 会话槽号(0..31)
// stopFlag  : 置 0x9876 可让 DLL 尽快结束流程
caltypes::RunResult run(int metaHandle, const caltypes::Config& cfg, int* stopFlag);

// ---- 校准数据(仅 Gen99/M90 平台支持) ---------------------------------------
// 初始化 cal-data 操作上下文; logPath 可为空。
bool initCalData(int metaHandle, const std::wstring& logPath, std::string* err);
bool deinitCalData(int metaHandle);
// 读取 DUT 侧支持的校准数据项清单
bool listTargetCalDataItems(int metaHandle, std::vector<caltypes::CalDataItem>* out, std::string* err);
// 读取 / 导出 / 导入 / 写入
caltypes::RunResult readCalDataToTarget(int metaHandle, const std::vector<char>& itemIds, int* stopFlag);
caltypes::RunResult exportCalDataToFile(int metaHandle, const std::wstring& iniPath,
                                        const std::vector<char>& itemIds, int* stopFlag);
caltypes::RunResult importCalDataFromFile(int metaHandle, const std::wstring& iniPath,
                                          const std::vector<char>& itemIds, int* stopFlag);
caltypes::RunResult writeCalDataToTarget(int metaHandle, const std::vector<char>& itemIds, int* stopFlag);

// ---- 其它 ------------------------------------------------------------------
// sizeof(METACalibrationLibrary_COMMON_CFG_TW) 的编译期值, 用于 ABI 自检
unsigned cfgStructSize();
// E_METACalibrationLibrary_RESULT 的可读说明
const char* resultText(int raw);
// 检查一组文件/目录是否存在, 返回人类可读的问题清单(空 = 没问题)
std::string checkPaths(const caltypes::Config& cfg);

} // namespace calbridge
