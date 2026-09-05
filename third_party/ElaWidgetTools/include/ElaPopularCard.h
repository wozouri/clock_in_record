#ifndef ELAPOPULARCARD_H
#define ELAPOPULARCARD_H

#include <QPixmap>
#include <QWidget>

#include "ElaProperty.h"
class ElaPopularCardPrivate;
enum class PCardFloatState {
    App,
    Add,
    Floder,
    Loading,
    TabWindow
};
class ELA_EXPORT ElaPopularCard : public QWidget
{
    Q_OBJECT
    Q_Q_CREATE(ElaPopularCard)
    Q_PROPERTY_CREATE_Q_H(int, BorderRadius)
    Q_PROPERTY_CREATE_Q_H(QPixmap, CardPixmap)
    Q_PROPERTY_CREATE_Q_H(QString, Title)
    Q_PROPERTY_CREATE_Q_H(QString, SubTitle)
    Q_PROPERTY_CREATE_Q_H(QString, InteractiveTips)
    Q_PROPERTY_CREATE_Q_H(QString, DetailedText)
    Q_PROPERTY_CREATE_Q_H(QWidget*, CardFloatArea)
    Q_PROPERTY_CREATE_Q_H(QPixmap, CardFloatPixmap)
public:
    explicit ElaPopularCard(QWidget* parent = nullptr);
    ~ElaPopularCard() override;

    PCardFloatState getPCardFloatState();
    void setPCardFloatState(PCardFloatState st);
    void setEnableTheBottomButton(bool on);

    void forceHideFloater();// 强制隐藏当前实例的悬浮窗
    static void clearActiveFloater();//关闭全局当前正显示的任意悬浮窗
    static ElaPopularCard* s_pActiveCard;

Q_SIGNALS:
    Q_SIGNAL void popularCardClicked();
    Q_SIGNAL void sigPopularCardRemoveBTCliecked();
    Q_SIGNAL void sigPopularCardModfiyBTCliecked();

protected:
    virtual bool event(QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
};

#endif // ELAPOPULARCARD_H
