#pragma once
#include <QRect>
#include <QSize>
#include <QString>
class QApplication;
class QWidget;
namespace stel {
// Scale application dimensions, independently of the monitor's DPI setting.
inline constexpr qreal UiScale = 0.5;
int uiPixels(int pixels);
QSize uiSize(QSize size);
QString uiStyleSheet(const QString &style);
void configureUiDpi(); // Before QApplication, on both Qt 5 and Qt 6.
void compactApplicationFont(QApplication &);
// Screen bounds use Qt logical pixels, never physical pixels.
QSize boundedWindowSize(QSize requested,QSize available,bool keepAspect=false);
// Window sizes are nominal dimensions; UiScale is applied once here.
void manageWindowLayout(QWidget *,QSize preferred,QSize minimum={},bool keepAspect=false);
}
