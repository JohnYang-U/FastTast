# ===========================================================================
#  METAQuickCheck  --  MTK 校准 / 综测 快速验证工具
#  构建: Visual Studio 2019 + Qt5 (5.14.2 msvc2017 32bit)
#
#  典型用法:
#     build.bat
#     :: 或手工:
#     cmake -S . -B build -A Win32 -DCMAKE_PREFIX_PATH=D:/Qt5/5.14.2/msvc2017
#     cmake --build build --config Release
# ===========================================================================
cmake_minimum_required(VERSION 3.16)
project(METAQuickCheck VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ---------------------------------------------------------------------------
# 必须是 Win32(x86): META 的 DLL(MetaCore/MetaApp/METAInstrumentLibrary/
# METACalibrationLibrary) 都是 32 位的, 64 位 Qt 链接不上。
# ---------------------------------------------------------------------------
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "METAQuickCheck 必须编译成 32 位 (Win32)。请用:\n"
        "  cmake -S . -B build -A Win32 -DCMAKE_PREFIX_PATH=D:/Qt5/5.14.2/msvc2017")
endif()

# ---------------------------------------------------------------------------
# Qt5
# ---------------------------------------------------------------------------
find_package(Qt5 5.9 REQUIRED COMPONENTS Core Gui Widgets)
message(STATUS "Qt5 : ${Qt5_DIR}")

# ---------------------------------------------------------------------------
# 定位 META 头文件
#   1) 本目录下 sdk/META/include      <- 自包含打包, 默认命中
#   2) -DMETA_SRC_DIR=... / 环境变量 META_SRC_DIR  (指向 META_DLL 源码树)
#   3) 上一级 / 上两级目录            (把本工程放进 META_DLL 里时)
# ---------------------------------------------------------------------------
set(META_SRC_DIR "" CACHE PATH "META_DLL 源码树根目录(包含 METACalibrationLibrary/ 等)")

set(_meta_roots "")
list(APPEND _meta_roots "${CMAKE_CURRENT_SOURCE_DIR}/sdk/META/include")
if(META_SRC_DIR)
    list(APPEND _meta_roots "${META_SRC_DIR}")
endif()
if(DEFINED ENV{META_SRC_DIR})
    list(APPEND _meta_roots "$ENV{META_SRC_DIR}")
endif()
list(APPEND _meta_roots "${CMAKE_CURRENT_SOURCE_DIR}/..")
list(APPEND _meta_roots "${CMAKE_CURRENT_SOURCE_DIR}/../..")

set(META_ROOT "")
foreach(_r IN LISTS _meta_roots)
    if(EXISTS "${_r}/METACalibrationLibrary/Interface/METACalibrationLibrary.h")
        set(META_ROOT "${_r}")
        break()
    endif()
endforeach()

if(NOT META_ROOT)
    message(FATAL_ERROR
        "找不到 META 头文件。请把 METACalibrationLibrary.h 等放到 "
        "${CMAKE_CURRENT_SOURCE_DIR}/sdk/META/include/ , 或用 -DMETA_SRC_DIR=... 指定源码树。")
endif()

get_filename_component(META_ROOT "${META_ROOT}" ABSOLUTE)
message(STATUS "META 头文件: ${META_ROOT}")

set(META_INCLUDE_DIRS
    "${META_ROOT}/METACalibrationLibrary/Interface"
    "${META_ROOT}/CommonUtility"
    "${META_ROOT}/CommonUtility/METAStatusUtility"
    "${META_ROOT}/CommonUtility/METALogUtility"
    "${META_ROOT}/CommonUtility/OSAbstraction"
    "${META_ROOT}/METAInstrumentLibrary/Interface"
    "${META_ROOT}/Build/Win32"
    "${META_ROOT}/ImportLibrary/third_party/zlib"
    "${META_ROOT}/ImportLibrary/third_party/Simpleini"
    "${META_ROOT}/ImportLibrary/third_party/rapidjson"
)

# ---------------------------------------------------------------------------
# 这组宏必须与 METACalibrationLibrary.dll 编译时使用的完全一致
#
# 依据: METASharingLibrary/METACalibrationLibrary/METACalibrationLibrary.vcxproj
#       里的 PreprocessorDefinitions。
#
# 其中 __META_LTE__ / __META_C2K__ / __META_WCDMA__ / __META_IOTNTN__ 会
# 增减 cfg 结构体成员, __NO_WCN_TEST__ 会增减 wcnTestItems —— 一旦不一致,
# METACalibrationLibrary_COMMON_CFG_TW 的布局就变了, 传给 DLL 会直接崩。
# 当前组合下 sizeof(COMMON_CFG_TW) == 296, 工具启动自检里会校验这个值。
# ---------------------------------------------------------------------------
set(META_DEFINES
    __META_TD_A__
    __META_DLL_TD_A__
    __META_LTE__
    __META_WCDMA__
    __META_IOTNTN__
    __INVERSE_ORDER__
    __CHECK_REENTRANCY__
    __META_C2K__
    __META_LAB_TUNING__
    _CRT_SECURE_NO_WARNINGS
)

# ---------------------------------------------------------------------------
# 源文件
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/LogBus.h
    src/LogBus.cpp
    src/CalTypes.h
    src/CalBridge.h
    src/CalBridge.cpp
    src/MetaSession.h
    src/MetaSession.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/Theme.h
    src/Theme.cpp
    res/theme.qrc
    src/gen/FlagTable.inc
    src/gen/TaskCatalog.inc
)

set(HEADERS
    src/CalBridge.h
    src/CalTypes.h
    src/LogBus.h
    src/MainWindow.h
    src/MetaSession.h
    src/Theme.h
)

add_executable(METAQuickCheck WIN32 ${SOURCES} ${HEADERS})

target_include_directories(METAQuickCheck PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${META_INCLUDE_DIRS}
)

target_compile_definitions(METAQuickCheck PRIVATE ${META_DEFINES})

target_link_libraries(METAQuickCheck PRIVATE
    Qt5::Core Qt5::Gui Qt5::Widgets
)

if(WIN32 AND TARGET Qt5::WinMain)
    target_link_libraries(METAQuickCheck PRIVATE Qt5::WinMain)
endif()

# ---------------------------------------------------------------------------
# MSVC 选项
#   /utf-8 : 源码 + 生成的 .inc 里都有中文, 必须显式按 UTF-8 编译
#   /MP    : 多进程编译, 明显加速
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(METAQuickCheck PRIVATE /utf-8 /MP /W3 /wd4996)
    target_link_options(METAQuickCheck PRIVATE "/SUBSYSTEM:WINDOWS")
endif()

set_target_properties(METAQuickCheck PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY    "${CMAKE_BINARY_DIR}/bin"
)

# ---------------------------------------------------------------------------
# META 运行库目录
#
# 自包含打包时放在 sdk/META/bin(含 METACalibrationLibrary.dll / MetaCore.dll 等),
# 这里把绝对路径"烘焙"进 exe, 运行时就一定找得到, 不依赖当前工作目录。
# 界面里的『META 运行时目录』仍然可以改成别的 META 版本包。
# ---------------------------------------------------------------------------
set(MQC_RUNTIME_DIR "")
foreach(_cand IN ITEMS
        "${CMAKE_CURRENT_SOURCE_DIR}/sdk/META/bin"
        "${META_ROOT}/../bin"
        "${META_ROOT}/bin")
    if(EXISTS "${_cand}/METACalibrationLibrary.dll" AND EXISTS "${_cand}/MetaCore.dll")
        get_filename_component(MQC_RUNTIME_DIR "${_cand}" ABSOLUTE)
        break()
    endif()
endforeach()

if(MQC_RUNTIME_DIR)
    # 反斜杠在 C 字符串里要转义, 统一换成正斜杠(Win32 API 也认)
    file(TO_CMAKE_PATH "${MQC_RUNTIME_DIR}" _mqc_runtime_fwd)
    message(STATUS "META 运行库: ${MQC_RUNTIME_DIR}")
    target_compile_definitions(METAQuickCheck PRIVATE
        "MQC_RUNTIME_DIR=\"${_mqc_runtime_fwd}\"")
else()
    message(WARNING
        "没有找到 META 运行库目录(需含 METACalibrationLibrary.dll + MetaCore.dll)。\n"
        "  请把 META 的 DLL 放到 ${CMAKE_CURRENT_SOURCE_DIR}/sdk/META/bin/,\n"
        "  运行时也可以在界面里手工指定『META 运行时目录』。")
endif()

# ---------------------------------------------------------------------------
# windeployqt
# ---------------------------------------------------------------------------
option(DEPLOY_QT "构建后自动运行 windeployqt" ON)
if(DEPLOY_QT)
    get_filename_component(_qt_bin_dir "${Qt5_DIR}/../../../bin" ABSOLUTE)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        # windeployqt 通过 PATH 里的 qmake 定位 Qt 安装; 若 PATH 里另有一套 Qt6,
        # 它会拿着 Qt6 的目录去找 Qt5Widgets.dll 而失败。这里单独给这条命令
        # 前置 Qt5 的 bin, 这样直接 `cmake --build build` 也能通过。
        add_custom_command(TARGET METAQuickCheck POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E env
                    "PATH=${_qt_bin_dir};$ENV{PATH}"
                    "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-opengl-sw
                    --no-system-d3d-compiler --no-compiler-runtime
                    "$<TARGET_FILE:METAQuickCheck>"
            COMMENT "windeployqt")
    else()
        message(WARNING "没找到 windeployqt, 运行时请把 Qt 的 bin 加入 PATH")
    endif()
endif()
