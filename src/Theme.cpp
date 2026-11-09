// ---------------------------------------------------------------------------
// Theme.cpp
// ---------------------------------------------------------------------------
#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QIcon>

namespace
{

// 只改这几处; 按钮 / 分组框 / 标签页全部用 Qt 自己的样子, 不做额外装饰。
const char* const kStyleSheet = R"QSS(
QPlainTextEdit#logView {
    font-family: "Consolas", "Cascadia Mono", "Courier New", monospace;
}
QHeaderView::section {
    background: #f2f2f2;
    border: none;
    border-right: 1px solid #dcdcdc;
    border-bottom: 1px solid #dcdcdc;
    padding: 4px 6px;
}
)QSS";

} // namespace

namespace theme
{

void apply(QApplication& app)
{
    // Fusion 在 Windows 各版本上长得一致, 也不会被系统主题改样
    app.setStyle(QStringLiteral("Fusion"));

    QFont f = app.font();
    f.setFamilies(QStringList() << QStringLiteral("Microsoft YaHei UI")
                                << QStringLiteral("Microsoft YaHei")
                                << QStringLiteral("Segoe UI"));
    f.setPointSize(9);
    app.setFont(f);

    app.setStyleSheet(QString::fromUtf8(kStyleSheet));
    app.setWindowIcon(icon(QStringLiteral("app")));
}

QIcon icon(const QString& name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
}

} // namespace theme
