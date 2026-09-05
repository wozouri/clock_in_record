#ifndef ELATABWIDGET_H
#define ELATABWIDGET_H

#include <QTabWidget>

#include "ElaProperty.h"

class ElaTabBar;
class ElaCustomTabWidget;
class ElaTabWidgetPrivate;
class ELA_EXPORT ElaTabWidget : public QTabWidget
{
    Q_OBJECT
        Q_Q_CREATE(ElaTabWidget)
        Q_PROPERTY_CREATE(bool, IsTabTransparent);

public:
    explicit ElaTabWidget(QWidget* parent = nullptr);
    ~ElaTabWidget();
    void setTabPosition(TabPosition position);
    void connectToTargetTabBar(ElaTabBar* tabBar);

    ElaTabBar* getCustomTabBar();
Q_SIGNALS:
    Q_SIGNAL void removeTabRequested(int index);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;
    virtual void tabRemoved(int index) override;

private:
    friend class ElaTabBarPrivate;
};

#endif // ELATABWIDGET_H
