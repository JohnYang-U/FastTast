// ---------------------------------------------------------------------------
// LogBus.h
//
// 日志总线: DLL / 工作线程里的日志统一投递到 GUI 线程显示。
//   - post() 可以在任意线程调用(内部有锁, 不碰 Qt 对象)
//   - 累积到一定量或每 50ms 由 GUI 线程批量取出, 避免高频日志把 UI 拖死
// ---------------------------------------------------------------------------
#pragma once

#include <QObject>
#include <QStringList>
#include <QMutex>
#include <QVector>

class QTimer;

class LogBus : public QObject
{
    Q_OBJECT
public:
    static LogBus& instance();

    // 线程安全。source 为来源标签(如 "CAL"/"META"/"UI")
    void post(const char* source, const char* text);
    void post(const QString& source, const QString& text);

    // 是否把日志同时写入文件(空 = 关闭)
    void setMirrorFile(const QString& path);
    QString mirrorFile() const;

    // 立即把缓冲刷出去(退出前调用)
    void flushNow();

signals:
    // 一批日志行, 每行已带 "[时间][来源] 内容"
    void batch(const QStringList& lines);

private slots:
    void onTick();

private:
    explicit LogBus(QObject* parent = nullptr);
    Q_DISABLE_COPY(LogBus)

    mutable QMutex   m_mutex;
    QVector<QString> m_pending;
    QTimer*          m_timer;
    QString          m_mirrorFile;
    QStringList      m_fileBuffer;
    int              m_dropped;
};
