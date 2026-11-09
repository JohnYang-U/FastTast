// ---------------------------------------------------------------------------
// MainWindow.h
// ---------------------------------------------------------------------------
#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>

#include "CalTypes.h"

class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QPlainTextEdit;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QListWidget;
class QTabWidget;
class QLabel;
class RunThread;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // 启动后自动做一次环境自检
    void startupSelfCheck();

    // 启动时定位到指定的标签页(0..3), 便于截图/支持
    void setInitialTab(int index);

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    // 日志
    void onAppendLog(const QStringList& lines);
    void onClearLog();
    void onSaveLog();

    // 环境/连接
    void onBrowseRuntimeDir();
    void onReloadDll();
    void onConnect();
    void onDisconnect();

    // 测试项
    void onTaskItemChanged(QTreeWidgetItem* item, int column);
    void onSelectAll();
    void onSelectNone();
    void onSelectCalOnly();
    void onSelectTestOnly();
    void onUpdateSequence();

    // 路径
    void onBrowseCfg();
    void onBrowseIni();
    void onBrowseLog();
    void onBrowseOut();
    void onBrowseCalRes();

    // 运行
    void onRun();
    void onStop();
    void onThreadFinished();

    // 开关页
    void onFlagGroupChanged();
    void onFlagSearch(const QString& text);
    void onFlagAllOn();
    void onFlagAllOff();
    void onFlagSaveProfile();
    void onFlagLoadProfile();

    // 校准数据页
    void onCalListItems();
    void onCalRead();
    void onCalExport();
    void onCalImport();
    void onCalWrite();

    void onAbout();

private:
    void buildUi();
    QWidget* createHeaderBar();
    QWidget* createEnvTab();
    QWidget* createRunTab();
    QWidget* createFlagTab();
    QWidget* createCalDataTab();
    QWidget* createLogPanel();

    void refreshEnvTable();
    void refreshTaskTree();
    void refreshFlagTable();
    void refreshCalTable();
    void refreshStatusChips();

    void collectConfig(caltypes::Config& out) const;
    void loadSettings();
    void saveSettings();

    void setBusy(bool busy);
    void log(const QString& source, const QString& text);
    QString runtimeDir() const;
    int  connectedSlot() const { return m_connectedSlot; }
    QVector<int> checkedTaskIds() const;
    QVector<char> checkedCalItemIds() const;
    int flagCount() const { return static_cast<int>(m_flagValues.size()); }

    // ---- 控件 ----
    QTabWidget*     m_tabs = nullptr;

    // 顶部标题栏(标题 + 状态胶囊 + 操作按钮)
    QLabel*         m_chipLib = nullptr;      // 校准库状态
    QLabel*         m_chipDut = nullptr;      // DUT 连接状态

    // 环境页
    QTableWidget*   m_envTable = nullptr;
    QLineEdit*      m_runtimeDirEdit = nullptr;
    QComboBox*      m_connModeCombo = nullptr;
    QSpinBox*       m_preloadSpin = nullptr;
    QSpinBox*       m_kernelSpin = nullptr;
    QSpinBox*       m_slotSpin = nullptr;
    QSpinBox*       m_timeoutSpin = nullptr;
    QCheckBox*      m_connModemCheck = nullptr;
    QPushButton*    m_btnConnect = nullptr;
    QPushButton*    m_btnDisconnect = nullptr;
    QLabel*         m_connStatus = nullptr;

    // 运行页
    QTreeWidget*    m_taskTree = nullptr;
    QLineEdit*      m_seqEdit = nullptr;
    QLineEdit*      m_cfgEdit = nullptr;
    QLineEdit*      m_iniEdit = nullptr;
    QLineEdit*      m_logPathEdit = nullptr;
    QLineEdit*      m_outPathEdit = nullptr;
    QLineEdit*      m_calResEdit = nullptr;
    QLineEdit*      m_spDbEdit = nullptr;
    QLineEdit*      m_mdDbEdit = nullptr;
    QComboBox*      m_instLte = nullptr;
    QComboBox*      m_instNr = nullptr;
    QComboBox*      m_instGge = nullptr;
    QComboBox*      m_instWcdma = nullptr;
    QComboBox*      m_instC2k = nullptr;
    QComboBox*      m_instTd = nullptr;
    QSpinBox*       m_rfPortSpin = nullptr;
    QCheckBox*      m_resetTesterCheck = nullptr;
    QPushButton*    m_btnRun = nullptr;
    QPushButton*    m_btnStop = nullptr;
    QProgressBar*   m_progress = nullptr;

    // 开关页
    QListWidget*    m_flagGroups = nullptr;
    QTableWidget*   m_flagTable = nullptr;
    QLineEdit*      m_flagSearch = nullptr;
    QLabel*         m_flagInfo = nullptr;

    // 校准数据页
    QPushButton*    m_btnCalList = nullptr;
    QPushButton*    m_btnCalRead = nullptr;
    QPushButton*    m_btnCalExport = nullptr;
    QPushButton*    m_btnCalImport = nullptr;
    QPushButton*    m_btnCalWrite = nullptr;
    QTableWidget*   m_calTable = nullptr;
    QLabel*         m_calInfo = nullptr;

    // 日志
    QPlainTextEdit* m_logView = nullptr;
    QCheckBox*      m_autoScroll = nullptr;

    // ---- 状态 ----
    int             m_connectedSlot = -1;
    int             m_stopFlag = 0;
    RunThread*      m_thread = nullptr;
    bool            m_busy = false;
    std::vector<bool> m_flagValues;        // 与 calbridge::flags() 同序
    QString         m_curFlagGroup;
    QVector<char>   m_calItemIds;          // 校准数据项 id
};
