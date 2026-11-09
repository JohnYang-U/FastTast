// ---------------------------------------------------------------------------
// Theme.h  --  全局字体与少量样式
//
// 只做三件事, 其余全部交给 Qt 默认(Fusion)风格:
//   1) 统一字体(中文优先, 9pt)
//   2) 日志区用等宽字体
//   3) 表头加一条浅色底和细分隔线
//
// 只有这三处, 其余交给 Qt 默认(Fusion)风格, 不再叠加装饰。
// ---------------------------------------------------------------------------
#pragma once

#include <QString>

class QApplication;
class QIcon;

namespace theme
{
// 在 main() 里 QApplication 构造之后调用一次
void apply(QApplication& app);

// 只留一个应用图标(窗口/任务栏用); name 不带后缀, 例如 "app"
QIcon icon(const QString& name);

} // namespace theme
