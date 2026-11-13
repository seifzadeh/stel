#pragma once
#include <QIcon>
#include <QPixmap>
class QTabWidget;
namespace stel {
QIcon applicationIcon();
QPixmap applicationWordmark(qreal devicePixelRatio=1);
enum class NavigationIcon { Phone, Messages, Contacts, History, Accounts, Audio, Video, Network, Security, Info, General, Globe, Forward, Document };
enum class NavigationLevel { Primary, Settings, Compact };
QIcon navigationIcon(NavigationIcon);
void styleNavigationTabs(QTabWidget *,NavigationLevel);
}
