#ifndef ELACONTENTDIALOG_H
#define ELACONTENTDIALOG_H
#include <QAbstractNativeEventFilter>
#include <QDialog>

#include "ElaAppBar.h"
#include "ElaProperty.h"
class ElaContentDialogPrivate;
class ELA_EXPORT ElaContentDialog : public QDialog
{
    Q_OBJECT
    Q_Q_CREATE(ElaContentDialog)
    Q_TAKEOVER_NATIVEEVENT_H
public:
    explicit ElaContentDialog(QWidget* parent);
    ~ElaContentDialog() override;
    Q_SLOT virtual void onLeftButtonClicked();
    Q_SLOT virtual void onMiddleButtonClicked();
    Q_SLOT virtual void onRightButtonClicked();
    void setCentralWidget(QWidget* centralWidget);

    // 设置遮罩的边距
    void setMaskMargins(const QMargins& margins);
    QMargins maskMargins() const;
    void setMaskDragParentEnabled(bool enabled);

    // 控制按钮可见性
    void setLeftButtonVisible(bool visible);
    void setMiddleButtonVisible(bool visible);
    void setRightButtonVisible(bool visible);

    // 控制按钮启用状态
    void setLeftButtonEnabled(bool enabled);
    void setMiddleButtonEnabled(bool enabled);
    void setRightButtonEnabled(bool enabled);

    // 修改默认内容的文本
    void setTitleText(QString text);
    void setSubTitleText(QString text);

    void setLeftButtonText(QString text);
    void setMiddleButtonText(QString text);
    void setRightButtonText(QString text);

    void setIsAutoClose(bool isAutoClose);
    void close();
Q_SIGNALS:
    Q_SIGNAL void leftButtonClicked();
    Q_SIGNAL void middleButtonClicked();
    Q_SIGNAL void rightButtonClicked();

protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
};

#endif // ELACONTENTDIALOG_H
