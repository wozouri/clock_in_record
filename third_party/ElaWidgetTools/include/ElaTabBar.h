#ifndef ELATABBAR_H
#define ELATABBAR_H

#include <QDrag>
#include <QTabBar>

#include "ElaProperty.h"
#include <qcoreevent.h>
#include <qsize.h>
class ElaTabWidget;
class ElaTabBarPrivate;
class ElaCustomTabWidget;
class ELA_EXPORT ElaTabBar : public QTabBar
{
    Q_OBJECT
        Q_Q_CREATE(ElaTabBar)

public:
    explicit ElaTabBar(QWidget* parent = nullptr);
    ~ElaTabBar();

    void connectToTargetTabWidget(ElaTabWidget* tabWidget);

    // addButton相关公共接口
    void setAddButtonEnabled(bool enabled);
    bool isAddButtonEnabled();

    template<typename T>
    static void setCustomTabWidgetCreator()
    {
        static_assert(std::is_base_of<ElaCustomTabWidget, T>::value,
            "T must inherit from ElaCustomTabWidget");
        s_customCreator = []() -> ElaCustomTabWidget* {
            return new T(nullptr);
            };
    }

    // 清除自定义创建器
    static void clearCustomTabWidgetCreator()
    {
        s_customCreator = nullptr;
    }


Q_SIGNALS:
    Q_SIGNAL void tabBarPress(int index);
    Q_SIGNAL void tabDragCreate(QDrag* drag);
    Q_SIGNAL void tabDragDrop(const QMimeData* mimeData);
    Q_SIGNAL void tabDragContinue();
    Q_SIGNAL void addTabRequested(); // 新增：请求添加标签页的信号
    Q_SIGNAL void removeTabRequested(int index); 

protected:
    bool event(QEvent* e)override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dragMoveEvent(QDragMoveEvent* event) override;
    virtual void dragLeaveEvent(QDragLeaveEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;
    virtual QSize sizeHint()const;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void tabInserted(int index) override;
    virtual void tabRemoved(int index) override;

    void paintEvent(QPaintEvent* event)override;
private:
    static ElaCustomTabWidget* createCustomTabWidget();
    typedef std::function<ElaCustomTabWidget* ()> CustomTabWidgetCreator;
    static CustomTabWidgetCreator s_customCreator;
};

#endif // ELATABBAR_H
