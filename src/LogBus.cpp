#include "LogBus.h"

#include <QDateTime>
#include <QFile>
#include <QMutexLocker>
#include <QTextStream>
#include <QTimer>

namespace
{
// 单批最多取多少行, 防止一次刷太多卡 UI
const int kMaxBatch = 800;
// 待显示缓冲上限, 超了丢最旧的
const int kMaxPending = 20000;
} // namespace

LogBus& LogBus::instance()
{
    static LogBus s_instance;
    return s_instance;
}

LogBus::LogBus(QObject* parent)
    : QObject(parent), m_timer(nullptr), m_dropped(0)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, &LogBus::onTick);
    m_timer->start();
}

void LogBus::post(const char* source, const char* text)
{
    if (text == nullptr)
        return;
    post(QString::fromUtf8(source == nullptr ? "" : source), QString::fromUtf8(text));
}

void LogBus::post(const QString& source, const QString& text)
{
    // 只做一次内存拷贝, 不创建任何 Qt 对象
    QString line;
    line.reserve(text.size() + 32);
    line += QLatin1Char('[');
    line += QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    line += QLatin1String("][");
    line += source;
    line += QLatin1String("] ");
    line += text;

    QMutexLocker lk(&m_mutex);
    if (m_pending.size() >= kMaxPending)
    {
        m_pending.remove(0, kMaxBatch);
        ++m_dropped;
    }
    m_pending.append(line);
}

void LogBus::setMirrorFile(const QString& path)
{
    QMutexLocker lk(&m_mutex);
    m_mirrorFile = path;
    if (!path.isEmpty())
        m_fileBuffer.clear();
}

QString LogBus::mirrorFile() const
{
    QMutexLocker lk(&m_mutex);
    return m_mirrorFile;
}

void LogBus::onTick()
{
    QStringList lines;
    QStringList toFile;
    QString     file;

    {
        QMutexLocker lk(&m_mutex);
        if (m_pending.isEmpty() && m_fileBuffer.isEmpty())
            return;

        const int n = qMin(m_pending.size(), kMaxBatch);
        lines.reserve(n);
        for (int i = 0; i < n; ++i)
            lines.append(m_pending.at(i));
        if (n > 0)
            m_pending.remove(0, n);

        if (m_dropped > 0 && lines.size() < kMaxBatch)
        {
            lines.append(QStringLiteral("[..] 日志过多, 已丢弃 %1 条").arg(m_dropped));
            m_dropped = 0;
        }

        if (!m_mirrorFile.isEmpty())
        {
            file = m_mirrorFile;
            toFile = m_fileBuffer;
            m_fileBuffer.clear();
        }
    }

    if (!lines.isEmpty())
        emit batch(lines);

    if (!file.isEmpty() && !toFile.isEmpty())
    {
        QFile f(file);
        if (f.open(QIODevice::Append | QIODevice::Text))
        {
            QTextStream ts(&f);
            ts.setCodec("UTF-8");
            for (int i = 0; i < toFile.size(); ++i)
                ts << toFile.at(i) << QLatin1Char('\n');
        }
    }
}

void LogBus::flushNow()
{
    onTick();
}
