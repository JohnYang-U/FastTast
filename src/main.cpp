// ---------------------------------------------------------------------------
// main.cpp  --  入口
//
// 两种模式:
//   1) 图形界面 (默认): METAQuickCheck.exe
//   2) 命令行自检:      METAQuickCheck.exe --selftest
//                       METAQuickCheck.exe --selftest --dir "D:\path\to\meta\bin"
//
// GUI 程序默认没有控制台, 所以 --selftest 会自动 AttachConsole 把结果打到
// 调用它的 cmd / 终端里, 同时写一份 METAQuickCheck_selftest.txt 到 exe 目录。
// ---------------------------------------------------------------------------
#include "CalBridge.h"
#include "MainWindow.h"
#include "MetaSession.h"
#include "Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QTextStream>

#include <cstdio>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{

QString appDir()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
}

// 常见运行时目录候选
QStringList runtimeDirCandidates()
{
    QStringList list;
    list << appDir();
    list << QDir::cleanPath(appDir() + QStringLiteral("/../../sdk/META/bin"));
    list << QDir::cleanPath(appDir() + QStringLiteral("/../../../METASharingLibrary/METACalibrationLibrary/CustomerRelease"));
    list << QDir::cleanPath(appDir() + QStringLiteral("/sdk/META/bin"));
    list << QDir::cleanPath(appDir() + QStringLiteral("/../bin"));
    return list;
}

QString detectRuntimeDir()
{
    // 1) 显式设置
    QSettings s(appDir() + QStringLiteral("/METAQuickCheck.ini"), QSettings::IniFormat);
    s.setIniCodec("UTF-8");
    const QString configured = s.value(QStringLiteral("Env/RuntimeDir")).toString();
    if (!configured.isEmpty() && QFileInfo::exists(configured))
        return configured;

    // 2) 环境变量
    const QString env = qEnvironmentVariable("META_RUNTIME_DIR");
    if (!env.isEmpty() && QFileInfo::exists(env))
        return env;

    // 3) CMake 烘焙进来的自包含运行库目录
#ifdef MQC_RUNTIME_DIR
    {
        const QString baked = QDir::toNativeSeparators(QString::fromUtf8(MQC_RUNTIME_DIR));
        if (QFileInfo::exists(baked + QStringLiteral("/METACalibrationLibrary.dll")))
            return baked;
    }
#endif

    // 4) 优先找含 METACalibrationLibrary.dll 的目录
    const QStringList cands = runtimeDirCandidates();
    for (int i = 0; i < cands.size(); ++i)
    {
        if (QFileInfo::exists(cands.at(i) + QStringLiteral("/METACalibrationLibrary.dll")))
            return cands.at(i);
    }
    // 5) 退一步: 含 MetaCore.dll 的目录
    for (int i = 0; i < cands.size(); ++i)
    {
        if (QFileInfo::exists(cands.at(i) + QStringLiteral("/MetaCore.dll")))
            return cands.at(i);
    }
    return QString::fromStdWString(calbridge::guessRuntimeDir());
}

// GUI 子系统程序没有控制台。--selftest 时:
//   - 如果 stdout 已被重定向(管道/文件), GetStdHandle 仍然有效 -> 直接 WriteFile
//   - 否则分配一个控制台窗口
// 这样在 PowerShell / cmd 里重定向、以及直接双击运行都能看到输出。
void ensureOutput()
{
#ifdef _WIN32
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE)
    {
        ::AllocConsole();
        ::SetConsoleTitleW(L"METAQuickCheck selftest");
    }
#endif
}

// 直接写 OS 句柄, 绕开 CRT 的 stdout 绑定问题
void writeOut(const QString& text)
{
#ifdef _WIN32
    QByteArray u = text.toUtf8();
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    if (!u.isEmpty())
        ::WriteFile(h, u.constData(), static_cast<DWORD>(u.size()), &written, nullptr);
    ::WriteFile(h, "\r\n", 2, &written, nullptr);
#else
    Q_UNUSED(text);
#endif
}

struct SelfTest
{
    QStringList lines;
    int failures = 0;

    void out(const QString& s)
    {
        lines << s;
        writeOut(s);
    }
    void ok(const QString& s)   { out(QStringLiteral("  [OK]   ") + s); }
    void warn(const QString& s) { out(QStringLiteral("  [WARN] ") + s); }
    void fail(const QString& s) { out(QStringLiteral("  [FAIL] ") + s); ++failures; }
};

int runSelfTest(const QString& forcedDir)
{
    ensureOutput();

    SelfTest t;
    t.out(QStringLiteral("=============================================================="));
    t.out(QStringLiteral(" METAQuickCheck 自检报告"));
    t.out(QStringLiteral(" 时间: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    t.out(QStringLiteral("=============================================================="));
    t.out(QString());

    // ---- [1] 基本环境 ----
    t.out(QStringLiteral("[1] 运行环境"));
    t.out(QStringLiteral("  exe       : ") + appDir());
    t.out(QStringLiteral("  Qt        : ") + QString::fromLatin1(qVersion()));
    t.out(QStringLiteral("  平台      : ") + QSysInfo::prettyProductName());
    t.out(QStringLiteral("  进程位数  : %1").arg(QSysInfo::WordSize));

    QString rt = forcedDir.isEmpty() ? detectRuntimeDir() : forcedDir;
    rt = QDir::toNativeSeparators(rt);
    t.out(QStringLiteral("  运行时目录: ") + rt);
    t.out(QString());

    // ---- [2] 依赖 DLL ----
    t.out(QStringLiteral("[2] META 依赖 DLL"));
    struct Dep { const char* name; bool required; };
    const Dep deps[] =
    {
        { "MetaCore.dll",                 true  },
        { "MetaApp.dll",                  true  },
        { "METACalibrationLibrary.dll",   true  },
        { "METAInstrumentLibrary.dll",    true  },
        { "SLA_Challenge.dll",            false },
        { "zlibwapi.dll",                 false },
        { "FHC_AI.dll",                   false },
    };
    int missingRequired = 0;
    for (int i = 0; i < static_cast<int>(sizeof(deps) / sizeof(deps[0])); ++i)
    {
        const QString name = QString::fromLatin1(deps[i].name);
        // Windows 的搜索顺序: exe 目录 -> 系统目录 -> SetDllDirectory 指定的运行时目录
        // 所以两个目录都要查
        QString found;
        const QString p1 = QDir(rt).filePath(name);
        const QString p2 = QDir(appDir()).filePath(name);
        if (QFileInfo::exists(p1))
            found = p1;
        else if (QFileInfo::exists(p2))
            found = p2;

        if (!found.isEmpty())
        {
            t.ok(QStringLiteral("%1  (%2 KB)  %3")
                     .arg(name)
                     .arg(QFileInfo(found).size() / 1024)
                     .arg(QFileInfo(found).absolutePath()));
        }
        else if (deps[i].required)
        {
            t.fail(QStringLiteral("%1  未找到 (运行时目录和 exe 目录都没有)").arg(name));
            ++missingRequired;
        }
        else
        {
            t.warn(QStringLiteral("%1  未找到 (可选)").arg(name));
        }
    }
    t.out(QString());

    // ---- [3] 加载 MetaApp ----
    t.out(QStringLiteral("[3] MetaApp.dll"));
    std::string err;
    if (metasession::loadApp(rt.toStdWString(), &err))
    {
        t.ok(QStringLiteral("已加载: ") + QString::fromStdString(metasession::appPath()));
        t.out(QStringLiteral("  sizeof(METAAPP_CONN_STTING_T) = %1")
                  .arg(metasession::structArgSize()));
        if (metasession::structArgSize() == 500)
            t.ok(QStringLiteral("连接结构体大小与已知版本一致 (500)"));
        else
            t.warn(QStringLiteral("连接结构体大小为 %1 (已知版本是 500), 可能头/DLL 版本不一致")
                       .arg(metasession::structArgSize()));
    }
    else
    {
        t.fail(QStringLiteral("加载失败: ") + QString::fromStdString(err));
    }
    t.out(QString());

    // ---- [4] 加载校准库 ----
    t.out(QStringLiteral("[4] METACalibrationLibrary.dll"));
    std::string errCal;
    if (calbridge::loadDll(rt.toStdWString(), &errCal))
    {
        const caltypes::DllStatus st = calbridge::status();
        t.ok(QStringLiteral("已加载: ") + QString::fromStdString(st.dllPath));
        t.out(QStringLiteral("  版本: %1").arg(QString::fromStdString(st.versionText)));
        t.out(QStringLiteral("  ABI : sizeof(COMMON_CFG_TW) = %1 (本工具头文件)").arg(st.cfgSize));
        if (st.cfgSize == 296)
            t.ok(QStringLiteral("ABI 与 DLL 预期一致"));
        else
            t.fail(QStringLiteral("ABI 不一致! 请检查 __META_LTE__ / __META_C2K__ / __NO_WCN_TEST__ 宏"));

        if (st.missingExports == 0)
            t.ok(QStringLiteral("全部 %1 个关键导出已解析").arg(st.resolvedExports));
        else
            t.warn(QStringLiteral("缺失导出 %1 个: %2")
                       .arg(st.missingExports)
                       .arg(QString::fromStdString(st.missingList)));

        t.out(QStringLiteral("  测试项数量: %1").arg(calbridge::tasks().size()));
        t.out(QStringLiteral("  子项开关数: %1").arg(calbridge::flags().size()));
    }
    else
    {
        t.fail(QStringLiteral("加载失败: ") + QString::fromStdString(errCal));
        t.out(QStringLiteral("  提示: META 的 DLL 之间有静态依赖, 必须把整包 bin 目录"));
        t.out(QStringLiteral("        (MetaCore/MetaApp/METAInstrumentLibrary/SLA_Challenge/"));
        t.out(QStringLiteral("         zlibwapi/FHC_AI/CO_CLK/TLogger/libxml2 等) 放在同一目录,"));
        t.out(QStringLiteral("         然后用 --dir 指定该目录。"));
    }
    t.out(QString());

    // ---- [5] 配置文件 ----
    t.out(QStringLiteral("[5] 工具配置文件"));
    const QString ini = appDir() + QStringLiteral("/METAQuickCheck.ini");
    if (QFileInfo::exists(ini))
    {
        t.ok(ini);
        QSettings s(ini, QSettings::IniFormat);
        s.setIniCodec("UTF-8");
        const QString cfgPath = s.value(QStringLiteral("Paths/Cfg")).toString();
        if (!cfgPath.isEmpty())
        {
            if (QFileInfo::exists(cfgPath))
                t.ok(QStringLiteral("META 配置文件: ") + cfgPath);
            else
                t.warn(QStringLiteral("META 配置文件不存在: ") + cfgPath);
        }
        else
        {
            t.warn(QStringLiteral("尚未设置 META 配置文件 (*.cfg), 首次运行前请在界面里选一次"));
        }
    }
    else
    {
        t.warn(QStringLiteral("尚未生成 %1 (首次运行 GUI 后会自动生成)").arg(ini));
    }
    t.out(QString());

    // ---- 汇总 ----
    t.out(QStringLiteral("=============================================================="));
    if (t.failures == 0)
        t.out(QStringLiteral(" 结果: 通过 (0 个致命问题)"));
    else
        t.out(QStringLiteral(" 结果: 有 %1 个致命问题, 见上文 [FAIL]").arg(t.failures));
    t.out(QStringLiteral("=============================================================="));

    // 落盘
    const QString report = appDir() + QStringLiteral("/METAQuickCheck_selftest.txt");
    QFile f(report);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream ts(&f);
        ts.setCodec("UTF-8");
        for (int i = 0; i < t.lines.size(); ++i)
            ts << t.lines.at(i) << QLatin1Char('\n');
        writeOut(QStringLiteral("报告已写入: ") + report);
    }

    calbridge::unloadDll();
    metasession::unloadApp();
    return t.failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("METAQuickCheck"));
    QApplication::setOrganizationName(QStringLiteral("METAQuickCheck"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    // 全局主题(字体 / 调色板 / QSS / 图标)在构造任何窗口之前装好
    theme::apply(app);

    const QStringList args = QCoreApplication::arguments();

    if (args.contains(QStringLiteral("--selftest")) || args.contains(QStringLiteral("-t")))
    {
        QString dir;
        const int i = args.indexOf(QStringLiteral("--dir"));
        if (i >= 0 && i + 1 < args.size())
            dir = QDir::toNativeSeparators(args.at(i + 1));
        const int rc = runSelfTest(dir);
        return rc;
    }

    MainWindow w;
    w.show();

    // --size WxH : 强制窗口尺寸(远程桌面/小屏机器上很有用; 也可用于截图)
    const int si = args.indexOf(QStringLiteral("--size"));
    if (si >= 0 && si + 1 < args.size())
    {
        const QStringList wh = args.at(si + 1).split(QLatin1Char('x'), QString::SkipEmptyParts);
        if (wh.size() == 2)
        {
            bool ok1 = false, ok2 = false;
            const int ww = wh.at(0).toInt(&ok1);
            const int hh = wh.at(1).toInt(&ok2);
            if (ok1 && ok2 && ww > 400 && hh > 300)
                w.resize(ww, hh);
        }
    }

    // --tab N : 启动时直接定位到某个标签页(截图/支持用)
    const int ti = args.indexOf(QStringLiteral("--tab"));
    if (ti >= 0 && ti + 1 < args.size())
    {
        bool ok = false;
        const int tab = args.at(ti + 1).toInt(&ok);
        if (ok)
            w.setInitialTab(tab);
    }

    w.startupSelfCheck();
    return app.exec();
}
