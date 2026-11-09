// ---------------------------------------------------------------------------
// MainWindow.cpp
// ---------------------------------------------------------------------------
#include "MainWindow.h"

#include "CalBridge.h"
#include "LogBus.h"
#include "MetaSession.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

// ===========================================================================
// 工作线程: 所有会阻塞的 DLL 调用都放这里, GUI 线程不卡
// (不用 Q_OBJECT, 直接复用 QThread::finished 信号, 省掉 moc 的麻烦)
// ===========================================================================
class RunThread : public QThread
{
public:
    enum Kind
    {
        KRun = 0,
        KCalList,
        KCalRead,
        KCalExport,
        KCalImport,
        KCalWrite
    };

    explicit RunThread(QObject* parent = nullptr)
        : QThread(parent), kind(KRun), handle(0), stopFlag(nullptr), listOk(false) {}

    Kind              kind;
    int               handle;
    caltypes::Config  cfg;
    std::wstring      iniPath;
    std::vector<char> ids;
    int*              stopFlag;

    // 结果
    caltypes::RunResult                 result;
    bool                                listOk;
    std::string                         error;
    std::vector<caltypes::CalDataItem>  items;

protected:
    void run() override
    {
        switch (kind)
        {
        case KRun:
            result = calbridge::run(handle, cfg, stopFlag);
            break;
        case KCalList:
            listOk = calbridge::listTargetCalDataItems(handle, &items, &error);
            break;
        case KCalRead:
            result = calbridge::readCalDataToTarget(handle, ids, stopFlag);
            break;
        case KCalExport:
            result = calbridge::exportCalDataToFile(handle, iniPath, ids, stopFlag);
            break;
        case KCalImport:
            result = calbridge::importCalDataFromFile(handle, iniPath, ids, stopFlag);
            break;
        case KCalWrite:
            result = calbridge::writeCalDataToTarget(handle, ids, stopFlag);
            break;
        }
    }
};

// ===========================================================================
// 日志/状态回调 (C 函数指针, 可被 DLL 自己的线程调用)
// ===========================================================================
static void __cdecl calLogSink(const char* text, void* /*user*/)
{
    LogBus::instance().post("CAL", text);
}

static void __cdecl metaLogSink(const char* text, void* /*user*/)
{
    LogBus::instance().post("META", text);
}

static void __cdecl calStatusSink(const caltypes::StatusEvent& ev, void* /*user*/)
{
    const QString item = QString::fromUtf8(ev.itemName.c_str());
    const QString msg  = QString::fromUtf8(ev.message.c_str());
    const QString tag  = (ev.statusType != 0) ? QStringLiteral("ERROR") : QStringLiteral("INFO");
    LogBus::instance().post("STATUS",
        QStringLiteral("[%1][%2] %3").arg(tag).arg(item, msg));
}

// ===========================================================================
// 仪器型号表 (取值来自 E_RCTLIB_DEVICE_TYPE)
// ===========================================================================
namespace
{

struct InstItem
{
    int         value;
    const char* label;
};

const InstItem kInsts[] =
{
    {  0, "AGILENT 8960" },
    {  1, "R&S CMU200" },
    {  2, "R&S CMW500" },
    {  3, "StarPoint SP6010" },
    {  4, "Anritsu MT8820" },
    {  5, "Anritsu MT8870" },
    {  6, "CTP3110" },
    {  7, "Transcom T6280" },
    {  8, "AGILENT EXT" },
    {  9, "LitePoint IQxstream" },
    { 11, "Welzek T6290" },
    { 12, "LitePoint IQxstream-M" },
    { 13, "StarPoint SP9010" },
    { 14, "CTP3308E" },
    { 15, "IQgig-5G" },
    { 16, "R&S CMP200" },
    { 17, "DT3308F" },
    { 18, "Anritsu MT8000" },
    { 19, "Keysight EXM6681A" },
    { 20, "IQgig-IF" },
    { 21, "R&S CMP180" },
    { 22, "Keysight EXM6680A" },
    { 23, "R&S CMW100" },
    { 24, "LitePoint IQxstream-5G" },
    { 25, "Keysight EXM6640A" },
    { 26, "StarPoint SP9100" },
    { 27, "CALLBACK_RCT (自定义)" },
    { 28, "DUMMY (不接仪器, 空跑)" },
};

const int kInstCount = static_cast<int>(sizeof(kInsts) / sizeof(kInsts[0]));

QComboBox* makeInstCombo(QWidget* parent, int defaultValue)
{
    QComboBox* cb = new QComboBox(parent);
    int defaultIdx = 0;
    for (int i = 0; i < kInstCount; ++i)
    {
        cb->addItem(QString::fromUtf8(kInsts[i].label), kInsts[i].value);
        if (kInsts[i].value == defaultValue)
            defaultIdx = i;
    }
    cb->setCurrentIndex(defaultIdx);
    return cb;
}

int comboValue(const QComboBox* cb)
{
    if (cb == nullptr)
        return 0;
    const QVariant v = cb->currentData();
    return v.isValid() ? v.toInt() : 0;
}

void selectComboValue(QComboBox* cb, int value)
{
    if (cb == nullptr)
        return;
    for (int i = 0; i < cb->count(); ++i)
    {
        if (cb->itemData(i).toInt() == value)
        {
            cb->setCurrentIndex(i);
            return;
        }
    }
}

QString categoryOf(const std::string& enumName)
{
    const QString e = QString::fromStdString(enumName);
    if (e.contains(QLatin1String("SELF_CAL")) || e.contains(QLatin1String("MULTIMODE"))
        || e.contains(QLatin1String("COMBINE")))
        return QStringLiteral("自校准 (Pre/Post Self Cal)");
    if (e.contains(QLatin1String("COTMS")) || e.contains(QLatin1String("CO_TMS")))
        return QStringLiteral("Co-TMS");
    if (e.contains(QLatin1String("IOT_NTN")))
        return QStringLiteral("IoT-NTN 卫星物联网");
    if (e.contains(QLatin1String("TAS")))
        return QStringLiteral("TAS 天线切换");
    if (e.contains(QLatin1String("GGE")) || e.contains(QLatin1String("GSM")))
        return QStringLiteral("GSM / GPRS / EDGE");
    if (e.contains(QLatin1String("WCDMA")))
        return QStringLiteral("WCDMA / HSPA");
    if (e.contains(QLatin1String("TDSCDMA")))
        return QStringLiteral("TD-SCDMA");
    if (e.contains(QLatin1String("C2K")) || e.contains(QLatin1String("CDMA")))
        return QStringLiteral("CDMA2000 / EVDO");
    if (e.contains(QLatin1String("LTE")))
        return QStringLiteral("LTE");
    if (e.contains(QLatin1String("NR")))
        return QStringLiteral("NR 5G");
    if (e.contains(QLatin1String("WCN")) || e.contains(QLatin1String("WIFI"))
        || e.contains(QLatin1String("BT")) || e.contains(QLatin1String("GPS")))
        return QStringLiteral("WiFi / BT / GPS");
    return QStringLiteral("其它");
}

const char* kCategoryOrder[] =
{
    "自校准 (Pre/Post Self Cal)",
    "GSM / GPRS / EDGE",
    "WCDMA / HSPA",
    "TD-SCDMA",
    "CDMA2000 / EVDO",
    "LTE",
    "NR 5G",
    "IoT-NTN 卫星物联网",
    "TAS 天线切换",
    "Co-TMS",
    "WiFi / BT / GPS",
    "其它",
};

int categoryRank(const QString& cat)
{
    for (int i = 0; i < static_cast<int>(sizeof(kCategoryOrder) / sizeof(kCategoryOrder[0])); ++i)
    {
        if (cat == QString::fromUtf8(kCategoryOrder[i]))
            return i;
    }
    return 999;
}

QString wToQ(const std::wstring& w)
{
    return QString::fromStdWString(w);
}

std::wstring qToW(const QString& q)
{
    return q.toStdWString();
}

// taskId / 开关名 / 路径这类"机器串"统一用等宽字体, 视觉上一眼能和中文说明区分开
QFont monoFont()
{
    static const QFont f = []()
    {
        QFont m = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        m.setPointSize(9);
        return m;
    }();
    return f;
}

} // namespace

// ===========================================================================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("METAQuickCheck  -  MTK 校准/综测快速验证工具"));
    resize(1280, 860);

    buildUi();

    // 日志总线 -> 日志面板
    connect(&LogBus::instance(), &LogBus::batch, this, &MainWindow::onAppendLog);

    // DLL 回调
    calbridge::setLogSink(&calLogSink, nullptr);
    calbridge::setStatusSink(&calStatusSink, nullptr);

    m_flagValues.assign(calbridge::flags().size(), false);

    refreshTaskTree();
    loadSettings();

    log(QStringLiteral("UI"), QStringLiteral("METAQuickCheck 已启动。建议先点【① 环境与连接】里的『重新自检』。"));
}

MainWindow::~MainWindow()
{
    if (m_thread != nullptr)
    {
        m_stopFlag = 0x9876;
        m_thread->wait(5000);
    }
    calbridge::setLogSink(nullptr, nullptr);
    calbridge::setStatusSink(nullptr, nullptr);
    calbridge::unloadDll();
    metasession::unloadApp();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_connectedSlot >= 0)
    {
        metasession::disconnect(m_connectedSlot, metasession::DisconnectReboot);
        m_connectedSlot = -1;
    }
    saveSettings();
    LogBus::instance().flushNow();
    QMainWindow::closeEvent(e);
}

// ---------------------------------------------------------------------------
// 日志
// ---------------------------------------------------------------------------
void MainWindow::log(const QString& source, const QString& text)
{
    LogBus::instance().post(source, text);
}

void MainWindow::onAppendLog(const QStringList& lines)
{
    if (m_logView == nullptr || lines.isEmpty())
        return;

    QScrollBar* sb = m_logView->verticalScrollBar();
    const bool atBottom = (sb->value() >= sb->maximum() - 4);

    // 一次插入整批, 比逐行 append 快很多
    m_logView->appendPlainText(lines.join(QLatin1Char('\n')));

    if (m_autoScroll != nullptr && m_autoScroll->isChecked() && atBottom)
        sb->setValue(sb->maximum());
}

void MainWindow::onClearLog()
{
    if (m_logView != nullptr)
        m_logView->clear();
}

void MainWindow::onSaveLog()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存日志"),
        QDir::currentPath() + QStringLiteral("/metaquickcheck_%1.log")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        QStringLiteral("日志 (*.log);;文本 (*.txt)"));
    if (path.isEmpty() || m_logView == nullptr)
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), f.errorString());
        return;
    }
    f.write(m_logView->toPlainText().toUtf8());
    f.close();
    log(QStringLiteral("UI"), QStringLiteral("日志已保存: %1").arg(path));
}

// ---------------------------------------------------------------------------
// 界面搭建
// ---------------------------------------------------------------------------
void MainWindow::buildUi()
{
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(createEnvTab(),     QStringLiteral("环境与连接"));
    m_tabs->addTab(createRunTab(),     QStringLiteral("校准 / 综测"));
    m_tabs->addTab(createFlagTab(),    QStringLiteral("测试项开关"));
    m_tabs->addTab(createCalDataTab(), QStringLiteral("校准数据 (Gen99)"));

    QSplitter* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_tabs);
    splitter->addWidget(createLogPanel());
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setChildrenCollapsible(false);

    QWidget* central = new QWidget(this);
    QVBoxLayout* root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);
    root->addWidget(splitter);
    setCentralWidget(central);

    // 工具栏就是原来的纯文字动作, 不做图标/分组
    QToolBar* tb = addToolBar(QStringLiteral("主工具栏"));
    tb->setMovable(false);
    tb->addAction(QStringLiteral("重新自检"), this, &MainWindow::onReloadDll);
    tb->addAction(QStringLiteral("连接 DUT"), this, &MainWindow::onConnect);
    tb->addAction(QStringLiteral("断开"), this, &MainWindow::onDisconnect);
    tb->addSeparator();
    tb->addAction(QStringLiteral("清空日志"), this, &MainWindow::onClearLog);
    tb->addAction(QStringLiteral("保存日志"), this, &MainWindow::onSaveLog);
    tb->addSeparator();
    tb->addAction(QStringLiteral("关于"), this, &MainWindow::onAbout);

    statusBar()->showMessage(QStringLiteral("就绪"));
}

QWidget* MainWindow::createEnvTab()
{
    QWidget* w = new QWidget(this);
    QVBoxLayout* v = new QVBoxLayout(w);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(10);

    // ---- DLL 环境表 ----
    QGroupBox* gbEnv = new QGroupBox(QStringLiteral("运行环境自检"), w);
    QVBoxLayout* envLay = new QVBoxLayout(gbEnv);
    envLay->setContentsMargins(12, 8, 12, 12);
    envLay->setSpacing(10);

    QHBoxLayout* dirRow = new QHBoxLayout();
    dirRow->setSpacing(8);
    dirRow->addWidget(new QLabel(QStringLiteral("META 运行时目录"), gbEnv));
    m_runtimeDirEdit = new QLineEdit(gbEnv);
    m_runtimeDirEdit->setPlaceholderText(
        QStringLiteral("存放 METACalibrationLibrary.dll / MetaApp.dll / MetaCore.dll 的目录"));
    dirRow->addWidget(m_runtimeDirEdit, 1);
    QPushButton* btnBrowse = new QPushButton(QStringLiteral("浏览"), gbEnv);
    connect(btnBrowse, &QPushButton::clicked, this, &MainWindow::onBrowseRuntimeDir);
    dirRow->addWidget(btnBrowse);
    QPushButton* btnReload = new QPushButton(QStringLiteral("重新自检"), gbEnv);
    connect(btnReload, &QPushButton::clicked, this, &MainWindow::onReloadDll);
    dirRow->addWidget(btnReload);
    envLay->addLayout(dirRow);

    m_envTable = new QTableWidget(0, 2, gbEnv);
    m_envTable->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("项目") << QStringLiteral("值"));
    m_envTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_envTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_envTable->verticalHeader()->setVisible(false);
    m_envTable->verticalHeader()->setDefaultSectionSize(26);
    m_envTable->setShowGrid(false);
    m_envTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_envTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_envTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_envTable->setMinimumHeight(160);
    envLay->addWidget(m_envTable, 1);

    // ---- 连接设置 ----
    QGroupBox* gbConn = new QGroupBox(QStringLiteral("DUT 连接"), w);
    QGridLayout* g = new QGridLayout(gbConn);
    g->setContentsMargins(12, 8, 12, 12);
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(9);
    g->setColumnMinimumWidth(0, 130);

    int row = 0;
    g->addWidget(new QLabel(QStringLiteral("连接方式")), row, 0);
    m_connModeCombo = new QComboBox(gbConn);
    m_connModeCombo->addItem(QStringLiteral("自动扫描 (推荐, DUT 需断电重启)"), 0);
    m_connModeCombo->addItem(QStringLiteral("指定串口 (自动扫描会串台时用)"), 1);
    m_connModeCombo->addItem(QStringLiteral("DUT 已在 META 模式"), 2);
    g->addWidget(m_connModeCombo, row, 1, 1, 3);
    ++row;

    g->addWidget(new QLabel(QStringLiteral("Preloader 口")), row, 0);
    m_preloadSpin = new QSpinBox(gbConn);
    m_preloadSpin->setRange(0, 255);
    m_preloadSpin->setSpecialValueText(QStringLiteral("自动"));
    g->addWidget(m_preloadSpin, row, 1);

    g->addWidget(new QLabel(QStringLiteral("ETS / Kernel 口")), row, 2);
    m_kernelSpin = new QSpinBox(gbConn);
    m_kernelSpin->setRange(0, 255);
    m_kernelSpin->setSpecialValueText(QStringLiteral("自动"));
    g->addWidget(m_kernelSpin, row, 3);
    ++row;

    g->addWidget(new QLabel(QStringLiteral("会话槽号 (meta_handle)")), row, 0);
    m_slotSpin = new QSpinBox(gbConn);
    m_slotSpin->setRange(0, metasession::kMaxSlots - 1);
    m_slotSpin->setValue(0);
    g->addWidget(m_slotSpin, row, 1);

    g->addWidget(new QLabel(QStringLiteral("连接超时 (ms)")), row, 2);
    m_timeoutSpin = new QSpinBox(gbConn);
    m_timeoutSpin->setRange(5000, 600000);
    m_timeoutSpin->setSingleStep(5000);
    m_timeoutSpin->setValue(120000);
    g->addWidget(m_timeoutSpin, row, 3);
    ++row;

    m_connModemCheck = new QCheckBox(
        QStringLiteral("连接后继续 AP→Modem (校准必须勾选; 仅 AP 测试可不勾)"), gbConn);
    m_connModemCheck->setChecked(true);
    g->addWidget(m_connModemCheck, row, 0, 1, 4);
    ++row;

    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    m_btnConnect = new QPushButton(QStringLiteral("连接 DUT"), gbConn);
    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnect);
    m_btnDisconnect = new QPushButton(QStringLiteral("断开"), gbConn);
    m_btnDisconnect->setEnabled(false);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    btnRow->addWidget(m_btnConnect);
    btnRow->addWidget(m_btnDisconnect);
    btnRow->addSpacing(6);
    m_connStatus = new QLabel(QStringLiteral("未连接"), gbConn);
    m_connStatus->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    btnRow->addWidget(m_connStatus);
    btnRow->addStretch(1);
    g->addLayout(btnRow, row, 0, 1, 4);
    // 多余高度留在底部空白, 不要摊到各行上
    g->setRowStretch(row + 1, 1);

    // 左右分栏: 宽窗口下把横向空间用起来, 900px 高的窗口也能一次看全自检结果
    // 连接卡按内容高度贴合(不要被 splitter 拉成一个大空壳)
    QWidget* connWrap = new QWidget(w);
    QVBoxLayout* cw = new QVBoxLayout(connWrap);
    cw->setContentsMargins(0, 0, 0, 0);
    cw->addWidget(gbConn);
    cw->addStretch(1);

    QSplitter* hs = new QSplitter(Qt::Horizontal, w);
    hs->setChildrenCollapsible(false);
    hs->addWidget(gbEnv);
    hs->addWidget(connWrap);
    hs->setStretchFactor(0, 3);
    hs->setStretchFactor(1, 2);
    hs->setSizes(QList<int>() << 780 << 470);
    v->addWidget(hs, 1);

    return w;
}

QWidget* MainWindow::createRunTab()
{
    QWidget* w = new QWidget(this);
    QHBoxLayout* h = new QHBoxLayout(w);
    h->setContentsMargins(14, 14, 14, 14);
    h->setSpacing(12);

    // ---- 左: 测试项树 ----
    QGroupBox* gbTree = new QGroupBox(QStringLiteral("测试项"), w);
    QVBoxLayout* treeLay = new QVBoxLayout(gbTree);
    treeLay->setContentsMargins(12, 8, 12, 12);
    treeLay->setSpacing(9);

    QHBoxLayout* selRow = new QHBoxLayout();
    selRow->setSpacing(6);
    QPushButton* bAll = new QPushButton(QStringLiteral("全选"), gbTree);
    QPushButton* bNone = new QPushButton(QStringLiteral("全不选"), gbTree);
    QPushButton* bCal = new QPushButton(QStringLiteral("仅校准"), gbTree);
    QPushButton* bTest = new QPushButton(QStringLiteral("仅综测"), gbTree);
    QPushButton* bBtns[] = { bAll, bNone, bCal, bTest };
    Q_UNUSED(bBtns);
    connect(bAll, &QPushButton::clicked, this, &MainWindow::onSelectAll);
    connect(bNone, &QPushButton::clicked, this, &MainWindow::onSelectNone);
    connect(bCal, &QPushButton::clicked, this, &MainWindow::onSelectCalOnly);
    connect(bTest, &QPushButton::clicked, this, &MainWindow::onSelectTestOnly);
    selRow->addWidget(bAll);
    selRow->addWidget(bNone);
    selRow->addWidget(bCal);
    selRow->addWidget(bTest);
    selRow->addStretch(1);
    treeLay->addLayout(selRow);

    m_taskTree = new QTreeWidget(gbTree);
    m_taskTree->setHeaderLabels(QStringList()
                                << QStringLiteral("测试项") << QStringLiteral("ID")
                                << QStringLiteral("类型"));
    m_taskTree->setColumnWidth(1, 50);
    m_taskTree->setColumnWidth(2, 64);
    m_taskTree->setAlternatingRowColors(true);
    m_taskTree->setUniformRowHeights(true);
    m_taskTree->setIndentation(16);
    m_taskTree->setRootIsDecorated(true);
    connect(m_taskTree, &QTreeWidget::itemChanged, this, &MainWindow::onTaskItemChanged);
    treeLay->addWidget(m_taskTree, 1);

    // ---- 右: 路径 + 仪器 + 执行 ----
    // 放到 QScrollArea 里, 小屏(比如远程桌面 1024x768)也不会被裁掉
    QWidget* right = new QWidget(w);
    QVBoxLayout* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(10);

    QGroupBox* gbPath = new QGroupBox(QStringLiteral("文件与仪器"), right);
    QGridLayout* g = new QGridLayout(gbPath);
    g->setContentsMargins(12, 8, 12, 12);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(7);
    int row = 0;

    struct PathRow { const QString label; QLineEdit** edit; const char* slot; const QString filter; };
    // 手动逐行添加, 便于挂浏览按钮
    auto addPathRow = [&](const QString& label, QLineEdit*& edit, void (MainWindow::*handler)(),
                          const QString& filter) -> void
    {
        edit = new QLineEdit(gbPath);
        g->addWidget(new QLabel(label), row, 0);
        g->addWidget(edit, row, 1);
        QPushButton* b = new QPushButton(QStringLiteral("..."), gbPath);
        b->setFixedWidth(34);
        b->setToolTip(QStringLiteral("浏览"));
        connect(b, &QPushButton::clicked, this, handler);
        g->addWidget(b, row, 2);
        Q_UNUSED(filter);
        ++row;
    };

    addPathRow(QStringLiteral("配置文件 *.cfg (必需)"), m_cfgEdit, &MainWindow::onBrowseCfg, QString());
    addPathRow(QStringLiteral("初始值 *.ini"), m_iniEdit, &MainWindow::onBrowseIni, QString());
    addPathRow(QStringLiteral("日志 *.log"), m_logPathEdit, &MainWindow::onBrowseLog, QString());
    addPathRow(QStringLiteral("校准数据 *.cal"), m_outPathEdit, &MainWindow::onBrowseOut, QString());
    addPathRow(QStringLiteral("校准报告目录"), m_calResEdit, &MainWindow::onBrowseCalRes, QString());

    m_spDbEdit = new QLineEdit(gbPath);
    m_spDbEdit->setPlaceholderText(QStringLiteral("AP NVRAM database (可空)"));
    g->addWidget(new QLabel(QStringLiteral("AP DB")), row, 0);
    g->addWidget(m_spDbEdit, row, 1, 1, 2);
    ++row;

    m_mdDbEdit = new QLineEdit(gbPath);
    m_mdDbEdit->setPlaceholderText(QStringLiteral("MD NVRAM database (可空)"));
    g->addWidget(new QLabel(QStringLiteral("MD DB")), row, 0);
    g->addWidget(m_mdDbEdit, row, 1, 1, 2);
    ++row;

    m_instLte   = makeInstCombo(gbPath, 2);
    m_instNr    = makeInstCombo(gbPath, 2);
    m_instGge   = makeInstCombo(gbPath, 2);
    m_instWcdma = makeInstCombo(gbPath, 2);
    m_instC2k   = makeInstCombo(gbPath, 2);
    m_instTd    = makeInstCombo(gbPath, 2);

    g->addWidget(new QLabel(QStringLiteral("LTE 仪器")), row, 0);
    g->addWidget(m_instLte, row, 1, 1, 2);
    ++row;
    g->addWidget(new QLabel(QStringLiteral("NR 仪器")), row, 0);
    g->addWidget(m_instNr, row, 1, 1, 2);
    ++row;
    g->addWidget(new QLabel(QStringLiteral("GSM 仪器")), row, 0);
    g->addWidget(m_instGge, row, 1, 1, 2);
    ++row;
    g->addWidget(new QLabel(QStringLiteral("WCDMA 仪器")), row, 0);
    g->addWidget(m_instWcdma, row, 1, 1, 2);
    ++row;
    g->addWidget(new QLabel(QStringLiteral("C2K 仪器")), row, 0);
    g->addWidget(m_instC2k, row, 1, 1, 2);
    ++row;
    g->addWidget(new QLabel(QStringLiteral("TD-SCDMA 仪器")), row, 0);
    g->addWidget(m_instTd, row, 1, 1, 2);
    ++row;

    g->addWidget(new QLabel(QStringLiteral("仪器 RF 口")), row, 0);
    m_rfPortSpin = new QSpinBox(gbPath);
    m_rfPortSpin->setRange(0, 64);
    g->addWidget(m_rfPortSpin, row, 1, 1, 2);
    ++row;

    m_resetTesterCheck = new QCheckBox(QStringLiteral("开始前复位仪器"), gbPath);
    g->addWidget(m_resetTesterCheck, row, 0, 1, 3);
    ++row;

    rv->addWidget(gbPath);

    QGroupBox* gbSeq = new QGroupBox(QStringLiteral("执行序列"), right);
    QVBoxLayout* sv = new QVBoxLayout(gbSeq);
    sv->setContentsMargins(12, 8, 12, 12);
    sv->setSpacing(9);
    QHBoxLayout* seqRow = new QHBoxLayout();
    seqRow->setSpacing(8);
    m_seqEdit = new QLineEdit(gbSeq);
    m_seqEdit->setToolTip(QStringLiteral(
        "可以手工改写执行顺序/子集, 例如 \"29,30\"。\n"
        "勾选左侧测试项时会自动重填。\n"
        "注意: DLL 对未知 id 静默忽略、重复 id 会跑两遍, 也不支持 \"29-31\" 这种范围写法。"));
    seqRow->addWidget(m_seqEdit, 1);
    QPushButton* btnGen = new QPushButton(QStringLiteral("按勾选生成"), gbSeq);
    connect(btnGen, &QPushButton::clicked, this, &MainWindow::onUpdateSequence);
    seqRow->addWidget(btnGen);
    sv->addLayout(seqRow);

    QHBoxLayout* runRow = new QHBoxLayout();
    runRow->setSpacing(8);
    m_btnRun = new QPushButton(QStringLiteral("开始"), gbSeq);
    m_btnRun->setMinimumHeight(30);
    connect(m_btnRun, &QPushButton::clicked, this, &MainWindow::onRun);
    m_btnStop = new QPushButton(QStringLiteral("停止"), gbSeq);
    m_btnStop->setMinimumHeight(30);
    m_btnStop->setEnabled(false);
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onStop);
    runRow->addWidget(m_btnRun);
    runRow->addWidget(m_btnStop);
    runRow->addStretch(1);
    sv->addLayout(runRow);

    m_progress = new QProgressBar(gbSeq);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    sv->addWidget(m_progress);

    rv->addStretch(1);

    QScrollArea* rightScroll = new QScrollArea(w);
    rightScroll->setWidgetResizable(true);
    rightScroll->setWidget(right);
    rightScroll->setMinimumWidth(400);
    rightScroll->setFrameShape(QFrame::NoFrame);

    // 执行序列固定在底部: 开始/停止必须随时可点, 不能跟着滚动区一起滚走
    QWidget* rightCol = new QWidget(w);
    QVBoxLayout* colLay = new QVBoxLayout(rightCol);
    colLay->setContentsMargins(0, 0, 0, 0);
    colLay->setSpacing(10);
    colLay->addWidget(rightScroll, 1);
    colLay->addWidget(gbSeq, 0);

    // 给左侧树一个上限: 否则 QTreeWidget 的 Expanding 策略会把右侧面板挤成一条缝
    gbTree->setMaximumWidth(560);

    // 用 QSplitter 而不是固定比例布局: 小屏时可以拖动分割条, 不会被裁掉
    QSplitter* hs = new QSplitter(Qt::Horizontal, w);
    hs->addWidget(gbTree);
    hs->addWidget(rightCol);
    hs->setChildrenCollapsible(false);
    hs->setStretchFactor(0, 3);
    hs->setStretchFactor(1, 4);
    hs->setSizes(QList<int>() << 430 << 500);
    h->addWidget(hs);

    return w;
}

QWidget* MainWindow::createFlagTab()
{
    QWidget* w = new QWidget(this);
    QVBoxLayout* v = new QVBoxLayout(w);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(10);

    QLabel* hint = new QLabel(
        QStringLiteral("共 %1 项子开关。跑校准项时除了勾任务, 还要打开对应子开关(本组全开最快)。")
            .arg(calbridge::flags().size()), w);
    hint->setWordWrap(true);
    v->addWidget(hint);

    QHBoxLayout* toolRow = new QHBoxLayout();
    toolRow->setSpacing(8);
    m_flagSearch = new QLineEdit(w);
    m_flagSearch->setPlaceholderText(QStringLiteral("搜索开关名 / 说明..."));
    m_flagSearch->setClearButtonEnabled(true);
    connect(m_flagSearch, &QLineEdit::textChanged, this, &MainWindow::onFlagSearch);
    toolRow->addWidget(m_flagSearch, 1);

    QPushButton* bOn = new QPushButton(QStringLiteral("本组全开"), w);
    QPushButton* bOff = new QPushButton(QStringLiteral("本组全关"), w);
    connect(bOn, &QPushButton::clicked, this, &MainWindow::onFlagAllOn);
    connect(bOff, &QPushButton::clicked, this, &MainWindow::onFlagAllOff);
    toolRow->addWidget(bOn);
    toolRow->addWidget(bOff);

    QPushButton* bSave = new QPushButton(QStringLiteral("保存方案"), w);
    QPushButton* bLoad = new QPushButton(QStringLiteral("载入方案"), w);
    connect(bSave, &QPushButton::clicked, this, &MainWindow::onFlagSaveProfile);
    connect(bLoad, &QPushButton::clicked, this, &MainWindow::onFlagLoadProfile);
    toolRow->addWidget(bSave);
    toolRow->addWidget(bLoad);
    v->addLayout(toolRow);

    QSplitter* sp = new QSplitter(Qt::Horizontal, w);
    sp->setChildrenCollapsible(false);

    m_flagGroups = new QListWidget(sp);
    m_flagGroups->setMaximumWidth(260);
    m_flagGroups->setAlternatingRowColors(false);
    connect(m_flagGroups, &QListWidget::currentRowChanged, this, &MainWindow::onFlagGroupChanged);
    sp->addWidget(m_flagGroups);

    QWidget* rightWrap = new QWidget(sp);
    QVBoxLayout* rv = new QVBoxLayout(rightWrap);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(6);

    m_flagTable = new QTableWidget(0, 3, rightWrap);
    m_flagTable->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("启用") << QStringLiteral("开关名")
                      << QStringLiteral("说明"));
    m_flagTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_flagTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_flagTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_flagTable->verticalHeader()->setVisible(false);
    m_flagTable->verticalHeader()->setDefaultSectionSize(28);
    m_flagTable->setShowGrid(false);
    m_flagTable->setAlternatingRowColors(true);
    m_flagTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_flagTable->setSelectionMode(QAbstractItemView::SingleSelection);
    rv->addWidget(m_flagTable, 1);

    m_flagInfo = new QLabel(rightWrap);
    rv->addWidget(m_flagInfo);

    sp->addWidget(rightWrap);
    sp->setStretchFactor(0, 0);
    sp->setStretchFactor(1, 1);
    v->addWidget(sp, 1);

    // 填充分组
    {
        QListWidgetItem* all = new QListWidgetItem(QStringLiteral("== 全部开关 =="), m_flagGroups);
        Q_UNUSED(all);
        QStringList seen;
        const std::vector<caltypes::FlagInfo>& fs = calbridge::flags();
        for (size_t i = 0; i < fs.size(); ++i)
        {
            const QString label = QString::fromStdString(fs[i].groupLabel);
            if (!seen.contains(label))
            {
                seen.append(label);
                m_flagGroups->addItem(label);
            }
        }
    }
    m_flagGroups->setCurrentRow(0);
    return w;
}

QWidget* MainWindow::createCalDataTab()
{
    QWidget* w = new QWidget(this);
    QVBoxLayout* v = new QVBoxLayout(w);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(10);

    QLabel* hint = new QLabel(
        QStringLiteral("仅 Gen99 (M90) 及以上平台支持; 老平台调用返回 -1。"), w);
    hint->setWordWrap(true);
    v->addWidget(hint);

    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(8);
    m_btnCalList = new QPushButton(QStringLiteral("列出 DUT 支持的项"), w);
    m_btnCalRead = new QPushButton(QStringLiteral("读取到文件"), w);
    m_btnCalExport = new QPushButton(QStringLiteral("导出到 ini"), w);
    m_btnCalImport = new QPushButton(QStringLiteral("从 ini 导入"), w);
    m_btnCalWrite = new QPushButton(QStringLiteral("写入 DUT"), w);
    connect(m_btnCalList, &QPushButton::clicked, this, &MainWindow::onCalListItems);
    connect(m_btnCalRead, &QPushButton::clicked, this, &MainWindow::onCalRead);
    connect(m_btnCalExport, &QPushButton::clicked, this, &MainWindow::onCalExport);
    connect(m_btnCalImport, &QPushButton::clicked, this, &MainWindow::onCalImport);
    connect(m_btnCalWrite, &QPushButton::clicked, this, &MainWindow::onCalWrite);
    row->addWidget(m_btnCalList);
    row->addWidget(m_btnCalRead);
    row->addWidget(m_btnCalExport);
    row->addWidget(m_btnCalImport);
    row->addWidget(m_btnCalWrite);
    row->addStretch(1);
    v->addLayout(row);

    QGroupBox* gbTable = new QGroupBox(QStringLiteral("校准数据项"), w);
    QVBoxLayout* tv = new QVBoxLayout(gbTable);
    tv->setContentsMargins(12, 8, 12, 12);
    tv->setSpacing(6);

    m_calTable = new QTableWidget(0, 6, gbTable);
    m_calTable->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("选择") << QStringLiteral("ID")
                      << QStringLiteral("HW") << QStringLiteral("名称")
                      << QStringLiteral("类别") << QStringLiteral("说明"));
    m_calTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_calTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_calTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_calTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_calTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_calTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_calTable->verticalHeader()->setVisible(false);
    m_calTable->verticalHeader()->setDefaultSectionSize(28);
    m_calTable->setShowGrid(false);
    m_calTable->setAlternatingRowColors(true);
    m_calTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_calTable->setSelectionMode(QAbstractItemView::SingleSelection);
    tv->addWidget(m_calTable, 1);

    m_calInfo = new QLabel(QStringLiteral("尚未查询"), gbTable);
    tv->addWidget(m_calInfo);

    v->addWidget(gbTable, 1);

    return w;
}

QWidget* MainWindow::createLogPanel()
{
    QWidget* w = new QWidget(this);
    QVBoxLayout* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);

    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(8);
    QLabel* logTitle = new QLabel(QStringLiteral("运行日志"), w);
    row->addWidget(logTitle);
    m_autoScroll = new QCheckBox(QStringLiteral("自动滚动"), w);
    m_autoScroll->setChecked(true);
    row->addWidget(m_autoScroll);
    row->addStretch(1);

    QPushButton* bClear = new QPushButton(QStringLiteral("清空"), w);
    connect(bClear, &QPushButton::clicked, this, &MainWindow::onClearLog);
    row->addWidget(bClear);

    QPushButton* bSaveLog = new QPushButton(QStringLiteral("保存"), w);
    connect(bSaveLog, &QPushButton::clicked, this, &MainWindow::onSaveLog);
    row->addWidget(bSaveLog);
    v->addLayout(row);

    m_logView = new QPlainTextEdit(w);
    m_logView->setObjectName(QStringLiteral("logView"));
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(200000);
    {
        // 等宽字体: 日志里的 taskId / 路径 / 数值对齐更整齐
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(9);
        m_logView->setFont(mono);
    }
    v->addWidget(m_logView, 1);

    return w;
}

// ---------------------------------------------------------------------------
// 环境自检
// ---------------------------------------------------------------------------
// 在运行时目录和 exe 目录里找 DLL(Windows 两者都会搜)
static QString locateDll(const QString& name, const QString& runtimeDir)
{
    const QStringList dirs = QStringList()
                             << runtimeDir
                             << QCoreApplication::applicationDirPath();
    for (int i = 0; i < dirs.size(); ++i)
    {
        if (dirs.at(i).isEmpty())
            continue;
        const QString p = QDir(dirs.at(i)).filePath(name);
        if (QFileInfo::exists(p))
            return QDir::toNativeSeparators(QFileInfo(p).absoluteFilePath());
    }
    return QString();
}

void MainWindow::refreshEnvTable()
{
    if (m_envTable == nullptr)
        return;

    struct Row { QString key; QString val; QString color; };
    QVector<Row> rows;

    // 1) 运行时目录
    const QString rt = runtimeDir();
    rows.append({ QStringLiteral("META 运行时目录"),
                  rt.isEmpty() ? QStringLiteral("(未设置)") : rt,
                  rt.isEmpty() ? QStringLiteral("#c00") : QString() });

    // 2) MetaApp.dll
    if (metasession::appLoaded())
        rows.append({ QStringLiteral("MetaApp.dll"),
                      QString::fromStdString(metasession::appPath()), QStringLiteral("#080") });
    else
        rows.append({ QStringLiteral("MetaApp.dll"),
                      QStringLiteral("未加载 (点『重新自检』)"), QStringLiteral("#c00") });

    // 3) MetaCore.dll
    {
        const QString core = locateDll(QStringLiteral("MetaCore.dll"), rt);
        rows.append({ QStringLiteral("MetaCore.dll"),
                      core.isEmpty() ? QStringLiteral("未找到") : core,
                      core.isEmpty() ? QStringLiteral("#c00") : QStringLiteral("#080") });
    }

    // 4) METACalibrationLibrary.dll
    const caltypes::DllStatus st = calbridge::status();
    if (st.loaded)
        rows.append({ QStringLiteral("METACalibrationLibrary.dll"),
                      QString::fromStdString(st.dllPath), QStringLiteral("#080") });
    else
        rows.append({ QStringLiteral("METACalibrationLibrary.dll"),
                      QStringLiteral("未加载"), QStringLiteral("#c00") });

    // 5) 校准库版本
    rows.append({ QStringLiteral("校准库版本"),
                  st.versionOk ? QString::fromStdString(st.versionText)
                               : QStringLiteral("(未取到)"),
                  st.versionOk ? QStringLiteral("#080") : QString() });

    // 6) ABI
    {
        const QString abi = QStringLiteral("sizeof(COMMON_CFG_TW) = %1").arg(st.cfgSize);
        rows.append({ QStringLiteral("结构体 ABI (须与 DLL 一致)"), abi,
                      st.cfgSize == 296 ? QStringLiteral("#080") : QStringLiteral("#c80") });
    }

    // 7) 导出解析
    rows.append({ QStringLiteral("导出函数解析"),
                  QStringLiteral("已解析 %1 / 缺失 %2%3")
                      .arg(st.resolvedExports)
                      .arg(st.missingExports)
                      .arg(st.missingList.empty()
                               ? QString()
                               : (QStringLiteral("  [") + QString::fromStdString(st.missingList) + QStringLiteral("]"))),
                  st.missingExports == 0 ? QStringLiteral("#080") : QStringLiteral("#c80") });

    // 8) 测试项/开关数量
    rows.append({ QStringLiteral("可用测试项 / 开关"),
                  QStringLiteral("%1 项 / %2 项")
                      .arg(calbridge::tasks().size())
                      .arg(calbridge::flags().size()),
                  QString() });

    // 9) 错误
    if (!st.error.empty())
        rows.append({ QStringLiteral("加载错误"),
                      QString::fromStdString(st.error), QStringLiteral("#c00") });

    m_envTable->setRowCount(rows.size());
    const QFont mono = monoFont();
    for (int i = 0; i < rows.size(); ++i)
    {
        QTableWidgetItem* k = new QTableWidgetItem(rows[i].key);
        QTableWidgetItem* v = new QTableWidgetItem(rows[i].val);
        v->setToolTip(rows[i].val);
        v->setFont(mono);          // 路径 / 版本号用等宽字体, 对齐好看
        if (!rows[i].color.isEmpty())
            v->setForeground(QColor(rows[i].color));
        m_envTable->setItem(i, 0, k);
        m_envTable->setItem(i, 1, v);
    }

}

void MainWindow::startupSelfCheck()
{
    // 首次运行: 自动猜一个运行时目录填进去, 省得用户手工找
    if (m_runtimeDirEdit->text().trimmed().isEmpty())
    {
        const std::wstring guessed = calbridge::guessRuntimeDir();
        // cleanPath 把 "..\..\.." 这类相对段规整掉, 便于阅读和复制
        const QString pretty = QDir::toNativeSeparators(
            QDir::cleanPath(QString::fromStdWString(guessed)));
        m_runtimeDirEdit->setText(pretty);
    }
    QTimer::singleShot(0, this, &MainWindow::onReloadDll);
}

void MainWindow::setInitialTab(int index)
{
    if (m_tabs != nullptr && index >= 0 && index < m_tabs->count())
        m_tabs->setCurrentIndex(index);
}

void MainWindow::onBrowseRuntimeDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 META 运行时目录"), runtimeDir());
    if (dir.isEmpty())
        return;
    m_runtimeDirEdit->setText(QDir::toNativeSeparators(dir));
    onReloadDll();
}

void MainWindow::onReloadDll()
{
    const QString rt = runtimeDir();

    std::string errCal, errApp;

    // MetaApp 先加载(校准库运行时依赖它)
    if (!metasession::loadApp(qToW(rt), &errApp))
    {
        log(QStringLiteral("UI"), QStringLiteral("MetaApp.dll 加载失败: %1")
                                     .arg(QString::fromStdString(errApp)));
    }
    else
    {
        log(QStringLiteral("UI"), QStringLiteral("MetaApp.dll 已加载: %1")
                                     .arg(QString::fromStdString(metasession::appPath())));
        metasession::registerLog(m_slotSpin == nullptr ? 0 : m_slotSpin->value(), &metaLogSink, nullptr);
    }

    if (!calbridge::loadDll(qToW(rt), &errCal))
    {
        log(QStringLiteral("UI"), QStringLiteral("METACalibrationLibrary.dll 加载失败: %1")
                                     .arg(QString::fromStdString(errCal)));
    }
    else
    {
        const caltypes::DllStatus st = calbridge::status();
        log(QStringLiteral("UI"), QStringLiteral("校准库已加载: %1  版本 %2  ABI=%3")
                                     .arg(QString::fromStdString(st.dllPath))
                                     .arg(QString::fromStdString(st.versionText))
                                     .arg(st.cfgSize));
        if (st.cfgSize != 296)
        {
            log(QStringLiteral("UI"),
                QStringLiteral("警告: sizeof(COMMON_CFG_TW)=%1, 与 DLL 编译时的 296 不一致! "
                               "请检查 CMakeLists 里的 __META_LTE__ / __META_C2K__ / "
                               "__NO_WCN_TEST__ 宏是否与 DLL 一致。").arg(st.cfgSize));
        }
    }

    refreshEnvTable();
    saveSettings();   // 首次自检后就把路径等写盘, 便于命令行 selftest 复用
}

// ---------------------------------------------------------------------------
// 连接
// ---------------------------------------------------------------------------
void MainWindow::onConnect()
{
    if (!metasession::appLoaded())
    {
        QMessageBox::warning(this, QStringLiteral("未就绪"),
                             QStringLiteral("MetaApp.dll 未加载, 请先在【环境与连接】页点『重新自检』。"));
        return;
    }

    const int slot = m_slotSpin->value();
    metasession::registerLog(slot, &metaLogSink, nullptr);

    metasession::ConnOptions opt;
    const int mode = m_connModeCombo->currentData().toInt();
    opt.autoScan      = (mode == 0);
    opt.alreadyInMeta = (mode == 2);
    opt.fixedPreloadPort = m_preloadSpin->value();
    opt.fixedKernelPort  = m_kernelSpin->value();
    opt.timeoutMs     = static_cast<unsigned>(m_timeoutSpin->value());

    m_btnConnect->setEnabled(false);
    m_connStatus->setText(QStringLiteral("连接中..."));

    log(QStringLiteral("UI"), QStringLiteral("开始连接 DUT (槽位 %1, 方式: %2, 超时 %3ms)")
                                 .arg(slot).arg(m_connModeCombo->currentText())
                                 .arg(opt.timeoutMs));

    const metasession::ConnResult r = metasession::connectTarget(slot, opt, nullptr);

    log(QStringLiteral("UI"), QStringLiteral("METAAPP_ConnectTargetAllInOne_r 返回 %1 (%2), arg_size=%3")
                                 .arg(r.ret).arg(QString::fromStdString(r.text)).arg(r.argSize));

    if (r.argSizeBad)
    {
        QMessageBox::critical(this, QStringLiteral("ABI 不匹配"),
                              QStringLiteral("MetaApp.dll 返回 ARG_SIZE_NOT_MATCH。\n"
                                             "工具用的 MetaApp.h 与 DLL 版本不一致, 请换成同版本头文件。"));
    }

    // DB 不匹配但端口已拿到 -> 仍视为可继续
    if (!r.ok && !r.dbMismatch)
    {
        m_btnConnect->setEnabled(true);
        m_connStatus->setText(QStringLiteral("连接失败 (ret=%1)").arg(r.ret));
        QMessageBox::warning(this, QStringLiteral("连接失败"),
                             QStringLiteral("返回 %1\n%2\n\n参考 MetaApp.h 里的 METAAPP_HIGH_LEVEL_* 枚举。")
                                 .arg(r.ret).arg(QString::fromStdString(r.text)));
        return;
    }

    m_connectedSlot = slot;
    m_slotSpin->setEnabled(false);
    m_btnDisconnect->setEnabled(true);

    log(QStringLiteral("UI"), QStringLiteral("已连接: Preloader=COM%1  ETS=COM%2  hMDHandle=%3")
                                 .arg(r.preloadPortCnf).arg(r.kernelPortCnf).arg(r.mdHandle));

    if (m_connModemCheck->isChecked())
    {
        log(QStringLiteral("UI"), QStringLiteral("继续 AP→Modem ..."));
        const int mret = metasession::connectModem(slot);
        log(QStringLiteral("UI"), QStringLiteral("METAAPP_ApToModemAllInOne_r 返回 %1 (%2)")
                                     .arg(mret).arg(QString::fromUtf8(metasession::resultText(mret))));
        if (mret != 0)
        {
            QMessageBox::warning(this, QStringLiteral("Modem 连接失败"),
                                 QStringLiteral("AP 已连上, 但 AP→Modem 返回 %1。\n"
                                                "校准功能需要 MD 侧, 请检查模块固件/DB。")
                                     .arg(mret));
        }
    }

    m_connStatus->setText(QStringLiteral("已连接 (槽位 %1)").arg(slot));
    statusBar()->showMessage(QStringLiteral("DUT 已连接, 可以开始校准/综测"));
}

void MainWindow::onDisconnect()
{
    if (m_connectedSlot < 0)
        return;

    log(QStringLiteral("UI"), QStringLiteral("断开 DUT (槽位 %1)...").arg(m_connectedSlot));
    const bool ok = metasession::disconnect(m_connectedSlot, metasession::DisconnectReboot);
    log(QStringLiteral("UI"), ok ? QStringLiteral("已断开") : QStringLiteral("断开失败"));

    m_connectedSlot = -1;
    m_slotSpin->setEnabled(true);
    m_btnConnect->setEnabled(true);
    m_btnDisconnect->setEnabled(false);
    m_connStatus->setText(QStringLiteral("未连接"));
    statusBar()->showMessage(QStringLiteral("已断开"));
}

// ---------------------------------------------------------------------------
// 测试项树
// ---------------------------------------------------------------------------
namespace
{
// 只统计"DLL 真的实现了"的子项。不支持的项没有勾选框, 不能算进父子三态里,
// 否则勾了全选之后父节点会因为那几个空壳永远停在 PartiallyChecked。
int supportedChildCount(QTreeWidgetItem* parent)
{
    int n = 0;
    for (int i = 0; i < parent->childCount(); ++i)
    {
        if (parent->child(i)->data(0, Qt::UserRole + 2).toInt() != 0)
            ++n;
    }
    return n;
}

Qt::CheckState aggregateState(int checked, int total)
{
    if (total <= 0 || checked <= 0)
        return Qt::Unchecked;
    return (checked == total) ? Qt::Checked : Qt::PartiallyChecked;
}
} // namespace

void MainWindow::refreshTaskTree()
{
    if (m_taskTree == nullptr)
        return;

    const QSignalBlocker blocker(m_taskTree);

    m_taskTree->clear();

    // 按类别聚合
    struct Cat { QString name; int rank; QVector<caltypes::TaskInfo> items; };
    QVector<Cat> cats;

    const std::vector<caltypes::TaskInfo>& all = calbridge::tasks();
    for (size_t i = 0; i < all.size(); ++i)
    {
        const QString cat = categoryOf(all[i].enumName);
        int idx = -1;
        for (int c = 0; c < cats.size(); ++c)
        {
            if (cats[c].name == cat)
            {
                idx = c;
                break;
            }
        }
        if (idx < 0)
        {
            Cat nc;
            nc.name = cat;
            nc.rank = categoryRank(cat);
            cats.append(nc);
            idx = cats.size() - 1;
        }
        cats[idx].items.append(all[i]);
    }

    std::sort(cats.begin(), cats.end(), [](const Cat& a, const Cat& b) {
        return a.rank < b.rank;
    });

    for (int c = 0; c < cats.size(); ++c)
    {
        QTreeWidgetItem* catItem = new QTreeWidgetItem(m_taskTree);
        catItem->setText(0, QStringLiteral("%1  (%2)").arg(cats[c].name).arg(cats[c].items.size()));
        catItem->setFlags(catItem->flags() | Qt::ItemIsUserCheckable);
        catItem->setCheckState(0, Qt::Unchecked);

        for (int i = 0; i < cats[c].items.size(); ++i)
        {
            const caltypes::TaskInfo& t = cats[c].items.at(i);
            QTreeWidgetItem* it = new QTreeWidgetItem(catItem);
            it->setText(0, QString::fromStdString(t.name));
            it->setText(1, QString::number(t.id));
            it->setFont(1, monoFont());
            it->setText(2, t.isTest ? QStringLiteral("综测") : QStringLiteral("校准"));
            it->setData(0, Qt::UserRole, t.id);
            it->setData(0, Qt::UserRole + 1, 1);   // 叶子标记
            it->setData(0, Qt::UserRole + 2, t.supported ? 1 : 0);

            if (t.supported)
            {
                it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
                it->setCheckState(0, Qt::Unchecked);
            }
            else
            {
                // 这条在 DLL 里没有实现: 写进序列也会被静默跳过, 还不报错。
                // 所以干脆不给勾选框 + 置灰 + 说明。
                it->setFlags(it->flags() & ~Qt::ItemIsUserCheckable);
                it->setData(0, Qt::CheckStateRole, QVariant());
                it->setForeground(0, QBrush(QColor(QStringLiteral("#8c959f"))));
                it->setText(2, QStringLiteral("不支持"));
                it->setForeground(2, QBrush(QColor(QStringLiteral("#8c959f"))));
            }

            QString tip;
            if (!t.supported)
            {
                tip = QStringLiteral("DLL 未实现此测试项");
                if (!t.note.empty())
                    tip += QStringLiteral("\nDLL 说明: \"%1\"").arg(QString::fromStdString(t.note));
                tip += QStringLiteral(
                    "\n写进执行序列也会被静默跳过（不报错，但也不会跑），所以这里禁止勾选。");
            }
            if (!t.deps.empty())
            {
                if (!tip.isEmpty())
                    tip += QStringLiteral("\n\n");
                tip += QStringLiteral("前置依赖 taskId: %1").arg(QString::fromStdString(t.deps));
            }
            if (!tip.isEmpty())
                it->setToolTip(0, tip);
        }
        catItem->setExpanded(true);
    }

    // 默认勾选: 什么都不勾, 让用户明确选择
    m_taskTree->expandAll();
    m_taskTree->resizeColumnToContents(0);
    // 小屏保护: 第一列不要撑得太大, 否则右侧面板被挤掉
    if (m_taskTree->columnWidth(0) > 340)
        m_taskTree->setColumnWidth(0, 340);
    m_taskTree->setMinimumWidth(280);
    m_taskTree->header()->setStretchLastSection(true);
}

void MainWindow::onTaskItemChanged(QTreeWidgetItem* item, int column)
{
    if (item == nullptr || column != 0)
        return;

    const QSignalBlocker blocker(m_taskTree);

    const bool isLeaf = item->data(0, Qt::UserRole + 1).isValid();
    if (isLeaf)
    {
        // 叶子变化 -> 更新父节点三态
        QTreeWidgetItem* p = item->parent();
        if (p != nullptr)
        {
            int checked = 0;
            for (int i = 0; i < p->childCount(); ++i)
            {
                QTreeWidgetItem* c = p->child(i);
                if (c->data(0, Qt::UserRole + 2).toInt() != 0
                    && c->checkState(0) == Qt::Checked)
                    ++checked;
            }
            p->setCheckState(0, aggregateState(checked, supportedChildCount(p)));
        }
    }
    else
    {
        // 父节点变化 -> 同步所有子节点
        const Qt::CheckState cs = item->checkState(0);
        if (cs == Qt::PartiallyChecked)
            return;
        for (int i = 0; i < item->childCount(); ++i)
        {
            QTreeWidgetItem* c = item->child(i);
            // 不支持的项没有勾选框, 也不要写进序列
            if (c->data(0, Qt::UserRole + 2).toInt() == 0)
                continue;
            c->setCheckState(0, cs);
        }
    }

    onUpdateSequence();
}

QVector<int> MainWindow::checkedTaskIds() const
{
    QVector<int> ids;
    if (m_taskTree == nullptr)
        return ids;

    for (int i = 0; i < m_taskTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* p = m_taskTree->topLevelItem(i);
        for (int j = 0; j < p->childCount(); ++j)
        {
            QTreeWidgetItem* c = p->child(j);
            if (c->checkState(0) == Qt::Checked)
                ids.append(c->data(0, Qt::UserRole).toInt());
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void MainWindow::onUpdateSequence()
{
    const QVector<int> ids = checkedTaskIds();
    QStringList parts;
    for (int i = 0; i < ids.size(); ++i)
        parts << QString::number(ids[i]);

    if (m_seqEdit != nullptr)
    {
        const QSignalBlocker blocker(m_seqEdit);
        m_seqEdit->setText(parts.join(QLatin1Char(',')));
    }
    statusBar()->showMessage(QStringLiteral("已选 %1 个测试项").arg(ids.size()));
}

void MainWindow::onSelectAll()
{
    const QSignalBlocker blocker(m_taskTree);
    for (int i = 0; i < m_taskTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* p = m_taskTree->topLevelItem(i);
        for (int j = 0; j < p->childCount(); ++j)
        {
            QTreeWidgetItem* c = p->child(j);
            if (c->data(0, Qt::UserRole + 2).toInt() == 0)
                continue;   // 不支持的项不带勾选框
            c->setCheckState(0, Qt::Checked);
        }
        p->setCheckState(0, supportedChildCount(p) > 0 ? Qt::Checked : Qt::Unchecked);
    }
    onUpdateSequence();
}

void MainWindow::onSelectNone()
{
    const QSignalBlocker blocker(m_taskTree);
    for (int i = 0; i < m_taskTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* p = m_taskTree->topLevelItem(i);
        for (int j = 0; j < p->childCount(); ++j)
            p->child(j)->setCheckState(0, Qt::Unchecked);
        p->setCheckState(0, Qt::Unchecked);
    }
    onUpdateSequence();
}

void MainWindow::onSelectCalOnly()
{
    const QSignalBlocker blocker(m_taskTree);
    for (int i = 0; i < m_taskTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* p = m_taskTree->topLevelItem(i);
        for (int j = 0; j < p->childCount(); ++j)
        {
            QTreeWidgetItem* c = p->child(j);
            if (c->data(0, Qt::UserRole + 2).toInt() == 0)
                continue;
            c->setCheckState(0, c->text(2) == QStringLiteral("校准") ? Qt::Checked : Qt::Unchecked);
        }
        int checked = 0;
        for (int j = 0; j < p->childCount(); ++j)
        {
            QTreeWidgetItem* c = p->child(j);
            if (c->data(0, Qt::UserRole + 2).toInt() != 0
                && c->checkState(0) == Qt::Checked)
                ++checked;
        }
        p->setCheckState(0, aggregateState(checked, supportedChildCount(p)));
    }
    onUpdateSequence();
}

void MainWindow::onSelectTestOnly()
{
    const QSignalBlocker blocker(m_taskTree);
    for (int i = 0; i < m_taskTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* p = m_taskTree->topLevelItem(i);
        for (int j = 0; j < p->childCount(); ++j)
        {
            QTreeWidgetItem* c = p->child(j);
            if (c->data(0, Qt::UserRole + 2).toInt() == 0)
                continue;
            c->setCheckState(0, c->text(2) == QStringLiteral("综测") ? Qt::Checked : Qt::Unchecked);
        }
        int checked = 0;
        for (int j = 0; j < p->childCount(); ++j)
        {
            QTreeWidgetItem* c = p->child(j);
            if (c->data(0, Qt::UserRole + 2).toInt() != 0
                && c->checkState(0) == Qt::Checked)
                ++checked;
        }
        p->setCheckState(0, aggregateState(checked, supportedChildCount(p)));
    }
    onUpdateSequence();
}

// ---------------------------------------------------------------------------
// 路径浏览
// ---------------------------------------------------------------------------
namespace
{
QString pickFile(QWidget* parent, const QString& title, const QString& filter, const QString& cur)
{
    return QFileDialog::getOpenFileName(parent, title, cur, filter);
}
QString pickSave(QWidget* parent, const QString& title, const QString& filter, const QString& cur)
{
    return QFileDialog::getSaveFileName(parent, title, cur, filter);
}
QString pickDir(QWidget* parent, const QString& title, const QString& cur)
{
    return QFileDialog::getExistingDirectory(parent, title, cur);
}
} // namespace

void MainWindow::onBrowseCfg()
{
    const QString p = pickFile(this, QStringLiteral("选择 META 配置文件 (*.cfg)"),
                               QStringLiteral("META 配置 (*.cfg);;所有文件 (*.*)"),
                               m_cfgEdit->text());
    if (!p.isEmpty())
        m_cfgEdit->setText(QDir::toNativeSeparators(p));
}

void MainWindow::onBrowseIni()
{
    const QString p = pickFile(this, QStringLiteral("选择初始值文件 (*.ini)"),
                               QStringLiteral("INI (*.ini);;所有文件 (*.*)"),
                               m_iniEdit->text());
    if (!p.isEmpty())
        m_iniEdit->setText(QDir::toNativeSeparators(p));
}

void MainWindow::onBrowseLog()
{
    const QString p = pickSave(this, QStringLiteral("选择日志文件"),
                               QStringLiteral("日志 (*.log);;所有文件 (*.*)"),
                               m_logPathEdit->text());
    if (!p.isEmpty())
        m_logPathEdit->setText(QDir::toNativeSeparators(p));
}

void MainWindow::onBrowseOut()
{
    const QString p = pickSave(this, QStringLiteral("选择校准数据输出文件"),
                               QStringLiteral("校准数据 (*.cal);;所有文件 (*.*)"),
                               m_outPathEdit->text());
    if (!p.isEmpty())
        m_outPathEdit->setText(QDir::toNativeSeparators(p));
}

void MainWindow::onBrowseCalRes()
{
    const QString p = pickDir(this, QStringLiteral("选择校准报告目录"), m_calResEdit->text());
    if (!p.isEmpty())
        m_calResEdit->setText(QDir::toNativeSeparators(p));
}

// ---------------------------------------------------------------------------
// 配置收集
// ---------------------------------------------------------------------------
QString MainWindow::runtimeDir() const
{
    if (m_runtimeDirEdit == nullptr)
        return QString();
    QString s = m_runtimeDirEdit->text().trimmed();
    return s.isEmpty() ? QString() : QDir::toNativeSeparators(s);
}

void MainWindow::collectConfig(caltypes::Config& out) const
{
    out.cfgPath        = qToW(m_cfgEdit->text().trimmed());
    out.iniPath        = qToW(m_iniEdit->text().trimmed());
    out.logPath        = qToW(m_logPathEdit->text().trimmed());
    out.outputPath     = qToW(m_outPathEdit->text().trimmed());
    out.calResultPath  = qToW(m_calResEdit->text().trimmed());
    out.spDbPath       = qToW(m_spDbEdit->text().trimmed());
    out.mdDbPath       = qToW(m_mdDbEdit->text().trimmed());

    out.devTypeGge     = comboValue(m_instGge);
    out.devTypeWcdma   = comboValue(m_instWcdma);
    out.devTypeTdscdma = comboValue(m_instTd);
    out.devTypeLte     = comboValue(m_instLte);
    out.devTypeNr      = comboValue(m_instNr);
    out.devTypeNrFr2   = comboValue(m_instNr);
    out.devTypeC2k     = comboValue(m_instC2k);

    out.resetTester    = m_resetTesterCheck->isChecked();
    out.testerRfPort   = m_rfPortSpin->value();

    out.flagValues = m_flagValues;

    out.sequence = m_seqEdit->text().trimmed().toStdString();
}

// ---------------------------------------------------------------------------
// 运行
// ---------------------------------------------------------------------------
void MainWindow::setBusy(bool busy)
{
    m_busy = busy;
    m_btnRun->setEnabled(!busy);
    m_btnStop->setEnabled(busy);
    m_progress->setRange(0, busy ? 0 : 100);
    m_progress->setValue(0);
    m_tabs->setTabEnabled(2, !busy);
}

void MainWindow::onRun()
{
    if (m_busy)
        return;

    if (!calbridge::isLoaded())
    {
        QMessageBox::warning(this, QStringLiteral("未就绪"),
                             QStringLiteral("校准库未加载, 请先点『重新自检』。"));
        return;
    }
    if (m_connectedSlot < 0)
    {
        QMessageBox::warning(this, QStringLiteral("未连接"),
                             QStringLiteral("请先在【环境与连接】页连接 DUT。"));
        return;
    }

    caltypes::Config cfg;
    collectConfig(cfg);

    int enabled = 0;
    for (int i = 0; i < flagCount(); ++i)
    {
        if (m_flagValues[i])
            ++enabled;
    }

    std::string problems = calbridge::checkPaths(cfg);
    if (enabled == 0)
    {
        // DLL 的行为: 子项开关全 false 时, 各测试项会静默跳过并返回成功,
        // 也就是"跑完是绿的, 但什么都没测"。这里必须在开工前说清楚。
        problems +=
            "- 一个子项开关都没开: DLL 会静默跳过所有测试项并返回 SUCCESS\n"
            "  (也就是\"跑完显示成功, 但什么都没测\"), 这不是失败, 但也不是有效结果\r\n";
    }
    if (!problems.empty())
    {
        const int ret = QMessageBox::question(
            this, QStringLiteral("配置检查"),
            QStringLiteral("发现以下问题:\n\n%1\n仍要继续吗?")
                .arg(QString::fromStdString(problems)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
    }

    log(QStringLiteral("UI"), QStringLiteral("========== 开始执行 =========="));
    log(QStringLiteral("UI"), QStringLiteral("槽位=%1  序列=%2")
                                 .arg(m_connectedSlot)
                                 .arg(QString::fromStdString(cfg.sequence)));
    log(QStringLiteral("UI"), QStringLiteral("cfg=%1").arg(QString::fromStdWString(cfg.cfgPath)));
    log(QStringLiteral("UI"), QStringLiteral("ini=%1").arg(QString::fromStdWString(cfg.iniPath)));
    log(QStringLiteral("UI"), QStringLiteral("log=%1").arg(QString::fromStdWString(cfg.logPath)));
    log(QStringLiteral("UI"), QStringLiteral("out=%1").arg(QString::fromStdWString(cfg.outputPath)));
    log(QStringLiteral("UI"), QStringLiteral("已启用的子项开关: %1 / %2")
                                 .arg(enabled).arg(flagCount()));
    if (enabled == 0)
    {
        log(QStringLiteral("UI"),
            QStringLiteral("警告: 子项开关为 0, 本次运行预计会全部跳过 (DLL 返回 SUCCESS 但不测任何东西)"));
    }

    m_stopFlag = 0;

    m_thread = new RunThread(this);
    m_thread->kind = RunThread::KRun;
    m_thread->handle = m_connectedSlot;
    m_thread->cfg = cfg;
    m_thread->stopFlag = &m_stopFlag;

    connect(m_thread, &QThread::finished, this, &MainWindow::onThreadFinished);
    setBusy(true);
    m_thread->start();

    statusBar()->showMessage(QStringLiteral("校准/综测执行中..."));
}

void MainWindow::onStop()
{
    m_stopFlag = 0x9876;
    // DLL 里 0x9876 的检查点在"每条 SCPI 命令 / 每个频点功率档", 不是测试项边界
    log(QStringLiteral("UI"),
        QStringLiteral("已请求停止 (stopFlag=0x9876), 会在当前 SCPI 命令/频点结束后生效..."));
    m_btnStop->setEnabled(false);
}

void MainWindow::onThreadFinished()
{
    if (m_thread == nullptr)
        return;

    RunThread* th = static_cast<RunThread*>(m_thread);
    QString finalStatus;

    if (th != nullptr)
    {
        switch (th->kind)
        {
        case RunThread::KRun:
        {
            const caltypes::RunResult& r = th->result;
            log(QStringLiteral("UI"), QStringLiteral("StartEx 返回 %1 (%2), 耗时 %.1f s")
                                         .arg(r.raw)
                                         .arg(QString::fromUtf8(calbridge::resultText(r.raw)))
                                         .arg(r.seconds));
            if (!r.lastStatus.empty())
                log(QStringLiteral("UI"),
                    QStringLiteral("最后一次上报状态 (仅参考, 判成败以上面的 StartEx 返回码为准): %1")
                        .arg(QString::fromStdString(r.lastStatus)));
            if (!r.error.empty())
                log(QStringLiteral("UI"), QStringLiteral("错误: %1")
                                             .arg(QString::fromStdString(r.error)));
            log(QStringLiteral("UI"), r.ok ? QStringLiteral("========== 执行成功 ==========")
                                           : QStringLiteral("========== 执行失败 =========="));
            finalStatus = r.ok ? QStringLiteral("执行成功") : QStringLiteral("执行失败");
            break;
        }
        case RunThread::KCalList:
        {
            if (th->listOk)
            {
                refreshCalTable();
                m_calInfo->setText(QStringLiteral("共 %1 项").arg(th->items.size()));
                log(QStringLiteral("UI"), QStringLiteral("校准数据项: %1 项")
                                             .arg(th->items.size()));
            }
            else
            {
                m_calInfo->setText(QStringLiteral("查询失败"));
                log(QStringLiteral("UI"), QStringLiteral("查询校准数据项失败: %1")
                                             .arg(QString::fromStdString(th->error)));
                QMessageBox::warning(this, QStringLiteral("查询失败"),
                                     QString::fromStdString(th->error));
            }
            break;
        }
        default:
        {
            const caltypes::RunResult& r = th->result;
            log(QStringLiteral("UI"), QStringLiteral("校准数据操作返回 %1 (%2)")
                                         .arg(r.raw)
                                         .arg(QString::fromUtf8(calbridge::resultText(r.raw))));
            if (!r.ok)
            {
                QMessageBox::warning(this, QStringLiteral("操作失败"),
                                     QString::fromStdString(r.error));
            }
            break;
        }
        }
    }

    m_thread->deleteLater();
    m_thread = nullptr;
    setBusy(false);

    // setBusy(false) 里会刷新胶囊/状态栏, 所以结果提示要放在它之后再写一次
    if (!finalStatus.isEmpty())
        statusBar()->showMessage(finalStatus);
}

// ---------------------------------------------------------------------------
// 开关页
// ---------------------------------------------------------------------------
void MainWindow::refreshFlagTable()
{
    if (m_flagTable == nullptr)
        return;

    const QString group = m_curFlagGroup;
    const QString key = m_flagSearch == nullptr ? QString() : m_flagSearch->text().trimmed();

    m_flagTable->setRowCount(0);

    const std::vector<caltypes::FlagInfo>& fs = calbridge::flags();
    int shown = 0;
    for (size_t i = 0; i < fs.size(); ++i)
    {
        const QString label = QString::fromStdString(fs[i].groupLabel);
        if (!group.isEmpty() && label != group)
            continue;

        const QString name = QString::fromStdString(fs[i].name);
        const QString desc = QString::fromStdString(fs[i].desc);
        if (!key.isEmpty()
            && !name.contains(key, Qt::CaseInsensitive)
            && !desc.contains(key, Qt::CaseInsensitive))
            continue;

        const int row = m_flagTable->rowCount();
        m_flagTable->insertRow(row);

        QTableWidgetItem* chk = new QTableWidgetItem();
        chk->setData(Qt::UserRole, static_cast<int>(i));
        if (fs[i].supported)
        {
            chk->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            chk->setCheckState(static_cast<int>(i) < flagCount() && m_flagValues[static_cast<int>(i)]
                                   ? Qt::Checked : Qt::Unchecked);
        }
        else
        {
            // DLL 明确拒绝这个开关(见 CalBridge.cpp 的 kRawFlagNotes), 置灰不可勾
            chk->setFlags(Qt::ItemIsEnabled);
            chk->setCheckState(Qt::Unchecked);
            chk->setToolTip(QString::fromStdString(fs[i].note));
        }
        m_flagTable->setItem(row, 0, chk);

        const bool supported = fs[i].supported;
        const QColor muted(QStringLiteral("#8c959f"));

        QTableWidgetItem* n = new QTableWidgetItem(name);
        n->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        n->setFont(monoFont());
        m_flagTable->setItem(row, 1, n);

        QTableWidgetItem* d = new QTableWidgetItem(
            supported ? desc
                      : desc + QStringLiteral("   [DLL 不支持，不可启用]"));
        d->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_flagTable->setItem(row, 2, d);

        if (!supported)
        {
            n->setForeground(QBrush(muted));
            d->setForeground(QBrush(muted));
            const QString reason = QString::fromStdString(fs[i].note);
            n->setToolTip(reason);
            d->setToolTip(reason);
        }

        ++shown;
    }

    if (m_flagInfo != nullptr)
    {
        int total = 0;
        for (int i = 0; i < flagCount(); ++i)
        {
            if (m_flagValues[i])
                ++total;
        }
        m_flagInfo->setText(QStringLiteral("当前显示 %1 项   已启用 %2 / %3")
                                .arg(shown).arg(total).arg(flagCount()));
    }

    // 表内勾选变更 -> 立刻写回内存
    connect(m_flagTable, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem* item)
            {
                if (item == nullptr || item->column() != 0)
                    return;
                const int idx = item->data(Qt::UserRole).toInt();
                if (idx >= 0 && idx < flagCount())
                    m_flagValues[idx] = (item->checkState() == Qt::Checked);
                if (m_flagInfo != nullptr)
                {
                    int total = 0;
                    for (int i = 0; i < flagCount(); ++i)
                    {
                        if (m_flagValues[i])
                            ++total;
                    }
                    m_flagInfo->setText(QStringLiteral("当前显示 %1 项   已启用 %2 / %3")
                                            .arg(m_flagTable->rowCount())
                                            .arg(total)
                                            .arg(flagCount()));
                }
            },
            Qt::UniqueConnection);
}

void MainWindow::onFlagGroupChanged()
{
    if (m_flagGroups == nullptr)
        return;
    const int row = m_flagGroups->currentRow();
    if (row <= 0)
        m_curFlagGroup.clear();
    else
        m_curFlagGroup = m_flagGroups->currentItem()->text();
    refreshFlagTable();
}

void MainWindow::onFlagSearch(const QString& /*text*/)
{
    refreshFlagTable();
}

void MainWindow::onFlagAllOn()
{
    const std::vector<caltypes::FlagInfo>& fs = calbridge::flags();
    int skipped = 0;
    for (size_t i = 0; i < fs.size(); ++i)
    {
        if (m_curFlagGroup.isEmpty()
            || QString::fromStdString(fs[i].groupLabel) == m_curFlagGroup)
        {
            if (!fs[i].supported)
            {
                // DLL 会直接拒绝这个开关, 全开时不能把它带进去
                m_flagValues[static_cast<int>(i)] = false;
                ++skipped;
                continue;
            }
            m_flagValues[static_cast<int>(i)] = true;
        }
    }
    refreshFlagTable();
    log(QStringLiteral("UI"), QStringLiteral("开关全开: 分组 '%1'")
                                 .arg(m_curFlagGroup.isEmpty() ? QStringLiteral("全部") : m_curFlagGroup));
    if (skipped > 0)
        log(QStringLiteral("UI"),
            QStringLiteral("已跳过 %1 个 DLL 不支持的开关").arg(skipped));
}

void MainWindow::onFlagAllOff()
{
    const std::vector<caltypes::FlagInfo>& fs = calbridge::flags();
    for (size_t i = 0; i < fs.size(); ++i)
    {
        if (m_curFlagGroup.isEmpty()
            || QString::fromStdString(fs[i].groupLabel) == m_curFlagGroup)
        {
            m_flagValues[static_cast<int>(i)] = false;
        }
    }
    refreshFlagTable();
    log(QStringLiteral("UI"), QStringLiteral("开关全关: 分组 '%1'")
                                 .arg(m_curFlagGroup.isEmpty() ? QStringLiteral("全部") : m_curFlagGroup));
}

void MainWindow::onFlagSaveProfile()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存开关方案"),
        QDir::currentPath() + QStringLiteral("/flag_profile.ini"),
        QStringLiteral("INI (*.ini)"));
    if (path.isEmpty())
        return;

    QSettings s(path, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("Flags"));
    const std::vector<caltypes::FlagInfo>& fs = calbridge::flags();
    for (size_t i = 0; i < fs.size(); ++i)
    {
        if (m_flagValues[static_cast<int>(i)])
            s.setValue(QString::fromStdString(fs[i].name), 1);
    }
    s.endGroup();
    s.sync();
    log(QStringLiteral("UI"), QStringLiteral("开关方案已保存: %1").arg(path));
}

void MainWindow::onFlagLoadProfile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("载入开关方案"), QDir::currentPath(),
        QStringLiteral("INI (*.ini)"));
    if (path.isEmpty())
        return;

    QSettings s(path, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("Flags"));
    const QStringList keys = s.childKeys();

    int applied = 0;
    for (int i = 0; i < keys.size(); ++i)
    {
        const int idx = calbridge::flagIndex(keys.at(i).toStdString());
        if (idx >= 0 && idx < flagCount())
        {
            m_flagValues[idx] = (s.value(keys.at(i)).toInt() != 0);
            ++applied;
        }
    }
    s.endGroup();

    refreshFlagTable();
    log(QStringLiteral("UI"), QStringLiteral("开关方案已载入: %1 (应用 %2 项)").arg(path).arg(applied));
}

// ---------------------------------------------------------------------------
// 校准数据页
// ---------------------------------------------------------------------------
void MainWindow::refreshCalTable()
{
    if (m_calTable == nullptr || m_thread == nullptr)
        return;

    RunThread* th = static_cast<RunThread*>(m_thread);
    if (th == nullptr)
        return;

    m_calTable->setRowCount(0);
    m_calItemIds.clear();

    for (size_t i = 0; i < th->items.size(); ++i)
    {
        const caltypes::CalDataItem& it = th->items[i];
        const int row = m_calTable->rowCount();
        m_calTable->insertRow(row);

        QTableWidgetItem* chk = new QTableWidgetItem();
        chk->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        chk->setCheckState(Qt::Unchecked);
        m_calTable->setItem(row, 0, chk);

        m_calTable->setItem(row, 1, new QTableWidgetItem(QString::number(it.itemId)));
        m_calTable->setItem(row, 2, new QTableWidgetItem(
            it.hwType == 2 ? QStringLiteral("FR2")
                           : (it.hwType == 1 ? QStringLiteral("FR1") : QStringLiteral("-"))));
        m_calTable->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(it.name)));
        m_calTable->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(it.category)));
        m_calTable->setItem(row, 5, new QTableWidgetItem(QString::fromStdString(it.description)));
    }
}

QVector<char> MainWindow::checkedCalItemIds() const
{
    QVector<char> ids;
    if (m_calTable == nullptr)
        return ids;
    for (int r = 0; r < m_calTable->rowCount(); ++r)
    {
        QTableWidgetItem* chk = m_calTable->item(r, 0);
        QTableWidgetItem* idItem = m_calTable->item(r, 1);
        if (chk != nullptr && idItem != nullptr && chk->checkState() == Qt::Checked)
            ids.append(static_cast<char>(idItem->text().toInt()));
    }
    return ids;
}

void MainWindow::onCalListItems()
{
    if (m_busy)
        return;
    if (m_connectedSlot < 0)
    {
        QMessageBox::warning(this, QStringLiteral("未连接"),
                             QStringLiteral("请先连接 DUT。"));
        return;
    }

    m_thread = new RunThread(this);
    m_thread->kind = RunThread::KCalList;
    m_thread->handle = m_connectedSlot;
    m_thread->stopFlag = &m_stopFlag;
    connect(m_thread, &QThread::finished, this, &MainWindow::onThreadFinished);

    m_calInfo->setText(QStringLiteral("查询中..."));
    setBusy(true);
    m_thread->start();
}

namespace
{
std::vector<char> toVec(const QVector<char>& v)
{
    std::vector<char> out;
    out.reserve(static_cast<size_t>(v.size()));
    for (int i = 0; i < v.size(); ++i)
        out.push_back(v.at(i));
    return out;
}
} // namespace

void MainWindow::onCalRead()
{
    if (m_busy || m_connectedSlot < 0)
        return;
    const QVector<char> ids = checkedCalItemIds();
    if (ids.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先勾选要操作的校准数据项。"));
        return;
    }

    m_thread = new RunThread(this);
    m_thread->kind = RunThread::KCalRead;
    m_thread->handle = m_connectedSlot;
    m_thread->ids = toVec(ids);
    m_thread->stopFlag = &m_stopFlag;
    connect(m_thread, &QThread::finished, this, &MainWindow::onThreadFinished);
    setBusy(true);
    m_thread->start();
}

void MainWindow::onCalExport()
{
    if (m_busy || m_connectedSlot < 0)
        return;
    const QVector<char> ids = checkedCalItemIds();
    if (ids.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先勾选要导出的校准数据项。"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出校准数据到 ini"),
        QDir::currentPath() + QStringLiteral("/caldata_export.ini"),
        QStringLiteral("INI (*.ini)"));
    if (path.isEmpty())
        return;

    m_thread = new RunThread(this);
    m_thread->kind = RunThread::KCalExport;
    m_thread->handle = m_connectedSlot;
    m_thread->ids = toVec(ids);
    m_thread->iniPath = qToW(QDir::toNativeSeparators(path));
    m_thread->stopFlag = &m_stopFlag;
    connect(m_thread, &QThread::finished, this, &MainWindow::onThreadFinished);
    log(QStringLiteral("UI"), QStringLiteral("导出校准数据到: %1").arg(path));
    setBusy(true);
    m_thread->start();
}

void MainWindow::onCalImport()
{
    if (m_busy || m_connectedSlot < 0)
        return;
    const QVector<char> ids = checkedCalItemIds();
    if (ids.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先勾选要导入的校准数据项。"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("从 ini 导入校准数据"), QDir::currentPath(),
        QStringLiteral("INI (*.ini);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    m_thread = new RunThread(this);
    m_thread->kind = RunThread::KCalImport;
    m_thread->handle = m_connectedSlot;
    m_thread->ids = toVec(ids);
    m_thread->iniPath = qToW(QDir::toNativeSeparators(path));
    m_thread->stopFlag = &m_stopFlag;
    connect(m_thread, &QThread::finished, this, &MainWindow::onThreadFinished);
    log(QStringLiteral("UI"), QStringLiteral("从 %1 导入校准数据").arg(path));
    setBusy(true);
    m_thread->start();
}

void MainWindow::onCalWrite()
{
    if (m_busy || m_connectedSlot < 0)
        return;
    const QVector<char> ids = checkedCalItemIds();
    if (ids.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先勾选要写入的校准数据项。"));
        return;
    }
    const int ret = QMessageBox::question(
        this, QStringLiteral("确认写入"),
        QStringLiteral("即将把校准数据写入 DUT (%1 项), 这会覆盖模块里的现有校准数据。\n继续吗?")
            .arg(ids.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    m_thread = new RunThread(this);
    m_thread->kind = RunThread::KCalWrite;
    m_thread->handle = m_connectedSlot;
    m_thread->ids = toVec(ids);
    m_thread->stopFlag = &m_stopFlag;
    connect(m_thread, &QThread::finished, this, &MainWindow::onThreadFinished);
    log(QStringLiteral("UI"), QStringLiteral("写入校准数据到 DUT (%1 项)").arg(ids.size()));
    setBusy(true);
    m_thread->start();
}

// ---------------------------------------------------------------------------
// 设置持久化
// ---------------------------------------------------------------------------
namespace
{
QString settingsPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/METAQuickCheck.ini");
}
} // namespace

void MainWindow::loadSettings()
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setIniCodec("UTF-8");

    const QString rtSaved = s.value(QStringLiteral("Env/RuntimeDir")).toString();
    m_runtimeDirEdit->setText(rtSaved.isEmpty()
                                  ? QString()
                                  : QDir::toNativeSeparators(QDir::cleanPath(rtSaved)));
    m_connModeCombo->setCurrentIndex(s.value(QStringLiteral("Env/ConnMode"), 0).toInt());
    m_preloadSpin->setValue(s.value(QStringLiteral("Env/PreloadPort"), 0).toInt());
    m_kernelSpin->setValue(s.value(QStringLiteral("Env/KernelPort"), 0).toInt());
    m_slotSpin->setValue(s.value(QStringLiteral("Env/Slot"), 0).toInt());
    m_timeoutSpin->setValue(s.value(QStringLiteral("Env/Timeout"), 120000).toInt());
    m_connModemCheck->setChecked(s.value(QStringLiteral("Env/ConnectModem"), true).toBool());

    m_cfgEdit->setText(s.value(QStringLiteral("Paths/Cfg")).toString());
    m_iniEdit->setText(s.value(QStringLiteral("Paths/Ini")).toString());
    m_logPathEdit->setText(s.value(QStringLiteral("Paths/Log")).toString());
    m_outPathEdit->setText(s.value(QStringLiteral("Paths/Out")).toString());
    m_calResEdit->setText(s.value(QStringLiteral("Paths/CalRes")).toString());

    selectComboValue(m_instLte,   s.value(QStringLiteral("Inst/Lte"), 2).toInt());
    selectComboValue(m_instNr,    s.value(QStringLiteral("Inst/Nr"), 2).toInt());
    selectComboValue(m_instGge,   s.value(QStringLiteral("Inst/Gge"), 2).toInt());
    selectComboValue(m_instWcdma, s.value(QStringLiteral("Inst/Wcdma"), 2).toInt());
    selectComboValue(m_instC2k,   s.value(QStringLiteral("Inst/C2k"), 2).toInt());
    selectComboValue(m_instTd,    s.value(QStringLiteral("Inst/Td"), 2).toInt());

    // 开关
    const std::vector<caltypes::FlagInfo>& fs = calbridge::flags();
    s.beginGroup(QStringLiteral("FlagValues"));
    for (size_t i = 0; i < fs.size(); ++i)
    {
        const QString key = QString::fromStdString(fs[i].name);
        if (s.contains(key))
            m_flagValues[static_cast<int>(i)] = (s.value(key).toInt() != 0);
    }
    s.endGroup();

    refreshFlagTable();
}

void MainWindow::saveSettings()
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setIniCodec("UTF-8");

    s.setValue(QStringLiteral("Env/RuntimeDir"), m_runtimeDirEdit->text());
    s.setValue(QStringLiteral("Env/ConnMode"), m_connModeCombo->currentIndex());
    s.setValue(QStringLiteral("Env/PreloadPort"), m_preloadSpin->value());
    s.setValue(QStringLiteral("Env/KernelPort"), m_kernelSpin->value());
    s.setValue(QStringLiteral("Env/Slot"), m_slotSpin->value());
    s.setValue(QStringLiteral("Env/Timeout"), m_timeoutSpin->value());
    s.setValue(QStringLiteral("Env/ConnectModem"), m_connModemCheck->isChecked());

    s.setValue(QStringLiteral("Paths/Cfg"), m_cfgEdit->text());
    s.setValue(QStringLiteral("Paths/Ini"), m_iniEdit->text());
    s.setValue(QStringLiteral("Paths/Log"), m_logPathEdit->text());
    s.setValue(QStringLiteral("Paths/Out"), m_outPathEdit->text());
    s.setValue(QStringLiteral("Paths/CalRes"), m_calResEdit->text());

    s.setValue(QStringLiteral("Inst/Lte"), comboValue(m_instLte));
    s.setValue(QStringLiteral("Inst/Nr"), comboValue(m_instNr));
    s.setValue(QStringLiteral("Inst/Gge"), comboValue(m_instGge));
    s.setValue(QStringLiteral("Inst/Wcdma"), comboValue(m_instWcdma));
    s.setValue(QStringLiteral("Inst/C2k"), comboValue(m_instC2k));
    s.setValue(QStringLiteral("Inst/Td"), comboValue(m_instTd));

    s.beginGroup(QStringLiteral("FlagValues"));
    const std::vector<caltypes::FlagInfo>& fs = calbridge::flags();
    for (size_t i = 0; i < fs.size(); ++i)
    {
        s.setValue(QString::fromStdString(fs[i].name),
                   m_flagValues[static_cast<int>(i)] ? 1 : 0);
    }
    s.endGroup();
    s.sync();
}

// ---------------------------------------------------------------------------
void MainWindow::onAbout()
{
    const caltypes::DllStatus st = calbridge::status();
    QMessageBox::about(
        this, QStringLiteral("关于"),
        QStringLiteral(
            "METAQuickCheck\n\n"
            "MTK 校准 / 综测快速验证工具。只封装 METACalibrationLibrary.dll 的公开接口,\n"
            "不含校准算法本身。\n\n"
            "校准库: %1\n"
            "版本: %2\n"
            "ABI: sizeof(COMMON_CFG_TW) = %3\n"
            "测试项: %4 个, 子项开关: %5 个")
            .arg(st.loaded ? QString::fromStdString(st.dllPath) : QStringLiteral("未加载"))
            .arg(QString::fromStdString(st.versionText))
            .arg(st.cfgSize)
            .arg(calbridge::tasks().size())
            .arg(calbridge::flags().size()));
}
