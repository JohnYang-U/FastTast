# METAQuickCheck

MTK 模块（工厂机 / 工程样机）的校准与综测快速验证工具。

界面是 Qt5，算法全部在 MTK 官方的 `METACalibrationLibrary.dll` 里；本工具只负责把它的
公开 C 接口驱动起来并显示结果，不实现任何校准逻辑。

![校准 / 综测](shots/tab2-run.png)

## 快速开始

> 本仓库**不含 MTK 的 META SDK**（头文件与 DLL 都是 MTK 的机密/专有内容，不随仓库分发）。
> 要真正构建并连接模块，需要 MTK 授权提供的 META SDK，放置方式见 [`sdk/META/README.md`](sdk/META/README.md)。

```bat
build.bat D:\Qt5\5.14.2\msvc2017 clean
build\bin\METAQuickCheck.exe --selftest
build\bin\METAQuickCheck.exe
```

自检最后一行出现 `结果: 通过 (0 个致命问题)`、且 `ABI = 296`、`已解析 14 / 缺失 0`，说明环境正常。

## 环境要求

| 项 | 要求 |
|---|---|
| 架构 | **必须 Win32 (x86)**。META 的 DLL 全是 32 位，`CMakeLists.txt` 会拦住 64 位编译 |
| Qt | 5.14.2 `msvc2017` **32 位**，例如 `D:\Qt5\5.14.2\msvc2017` |
| Visual Studio | 2019（16.x），或 2022 + v142 工具集。`build.bat` 自动探测，没有 2019 就用 `2022 -T v142` |
| CMake | ≥ 3.16 |

机器上如果同时装了 Qt6，`windeployqt` 会被 PATH 里的 `qmake` 带到 Qt6 目录而失败；
`build.bat` 已经把 Qt5 的 `bin` 前置到 PATH，`CMakeLists.txt` 里那条 windeployqt 也单独注入了 PATH，不用改。

## 用法

四个标签页按顺序走：

1. **环境与连接** — 确认 DLL 加载、库版本、ABI、导出解析数量；连接 DUT（自动扫描 / 指定串口 /
   已在 META 模式），可选继续 AP→Modem。
2. **校准 / 综测** — 左侧按制式分组勾选测试项（74 个），右侧填 `*.cfg` / `*.ini` / 日志 /
   校准数据路径和 6 类制式的仪器型号；勾选结果自动生成执行序列，也可以手工改写。
3. **测试项开关** — 192 个 bool 子项开关，按 28 个分组浏览/搜索，支持本组全开/全关。
4. **校准数据 (Gen99)** — 仅 Gen99 (M90) 及以上：列出 DUT 支持的项、读取、导出到 ini、从 ini 导入、写入 DUT。

命令行：

```bat
METAQuickCheck.exe --selftest [--dir <META 运行时目录>]   :: 环境自检（控制台 + 文本报告）
METAQuickCheck.exe --tab 2 --size 1400x900              :: 启动直接定位标签页 / 指定窗口尺寸
```

## 几个容易踩的点

- **子开关全关不等于报错**：DLL 会跳过测试项并返回 `SUCCESS`，表现是"跑完了、绿的、但什么都没测"。
  工具在开始前会提示这一点。跑校准项前先用「本组全开」把对应子开关打开。
- **停止**用哨兵 `0x9876`，生效粒度是每条 SCPI 命令 / 每个频点，不是测试项边界，所以不会立刻停。
- **执行序列**对未知 id 静默忽略、重复 id 会跑两遍、不支持 `29-31` 这种范围写法；建议用「按勾选生成」。
- **NR FR2 post-self cal / C2K post-self cal** 这两个测试项当前 DLL 尚未实现，
  工具里置灰、不能勾选（写进序列也不会执行）。
- **GGE TX IQ 校准**这个开关当前 DLL 明确不支持，勾上会导致整次执行在第一步就失败；
  工具里已置灰，写配置时也会跳过它。
- **判成败只看 `StartEx` 的返回值**。`GetLastStatus` 是"最后一次上报"，成功上报也会覆盖它，
  不能当作失败原因。
- **`meta_handle` 是连接时的会话槽号（0~31）**，不是 `hMDHandle`。

## 排障

| 现象 | 原因 / 处理 |
|---|---|
| 自检 `METACalibrationLibrary.dll 加载失败：找不到指定的模块` | 同目录缺依赖（`MetaApp.dll` / `MetaCore.dll` / `METAInstrumentLibrary.dll` / VC 运行库）。把整包 `bin` 放同一目录，或 `--selftest --dir` 指过去 |
| 自检 `ABI 不一致!` | 工具的头文件与 DLL 版本不匹配。换同版本 `sdk/META/include` 后重编，并重跑 `gen_tables.py` |
| 连接返回 `4` | 找不到 Preloader 口：模块没断电重启 / USB 线 / MTK 驱动 / 超时太短 |
| 连接返回 `1000` | `METAAPP_CONN_STTING_T` 大小不匹配，`MetaApp.h` 与 `MetaApp.dll` 版本不一致 |
| 连接返回 `14` / `15` | AP / MD DB 不匹配，端口已拿到，工具按可继续处理 |
| 点「开始」立刻失败 | 检查 `*.cfg` 是否存在、序列是否为空、对应分组子开关是否打开 |
| 校准数据页返回 `-1` | 该系列接口仅 Gen99 (M90) 及以上支持 |
| 跑完没有任何日志 | `*.cfg` 里的 `log_path` 没能建出 logger 时，DLL 的日志回调一条都不会来 |

## 目录结构

```
CMakeLists.txt          定位 Qt5 / sdk/META，烘焙 MQC_RUNTIME_DIR，构建后跑 windeployqt
build.bat               一键构建（自动选 VS 生成器）
pack.bat                打包（slim=源码+头文件，full=再带运行库与构建产物）
src/
  main.cpp              入口 + --selftest
  CalTypes.h            纯 C++ 数据类型（不含 Qt / META 头）
  CalBridge.h/.cpp      唯一 include METACalibrationLibrary.h 的 TU：动态加载、开关表、执行
  MetaSession.h/.cpp    唯一 include MetaApp.h 的 TU：连接 / AP→Modem / 断开
  LogBus.h/.cpp         线程安全日志总线
  MainWindow.h/.cpp     四个标签页 + 日志 + 工作线程
  Theme.h/.cpp          字体、日志等宽、表头底色（其余用 Qt 默认样式）
  gen/FlagTable.inc     192 个开关 → offsetof 偏移（自动生成，**不进仓库**）
  gen/TaskCatalog.inc   74 个测试项 + DLL 是否实现（自动生成，**不进仓库**）
tools/gen_tables.py     解析官方头文件/源码生成上面两张表
sdk/META/               自己放 MTK 的 SDK 用：include/（头文件）+ bin/（运行库），**不进仓库**
```

换 META 版本：替换 `sdk/META/include` 与 `sdk/META/bin`，然后

```bat
python tools\gen_tables.py --src-dir "<新 META_DLL 源码树>"
build.bat D:\Qt5\5.14.2\msvc2017 clean
```

`ABI` 不再是 296 是正常的，按自检报告把新值同步到 `main.cpp` / `MainWindow.cpp` 的校验处即可。

## 说明

- 本工具不含任何校准算法；测量与数据都来自 MTK 官方库。
- `sdk/META/` 下的头文件与运行库版权归 MTK，不随本仓库分发。
- 单 DUT、无工位管理 / MES / 良率统计，定位是工程师现场快检与排障。
