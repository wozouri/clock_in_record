#ifndef CUSTOMCALENDARWIDGET_H
#define CUSTOMCALENDARWIDGET_H

#include <QCalendarWidget>
#include <QTableView>
#include <QMenu>
#include <QAction>
#include <QDate>
#include <QShowEvent>

// �Զ��������ؼ���֧���Ҽ��˵�
class CustomCalendarWidget : public QCalendarWidget {
    Q_OBJECT
    QMap<QDate, QVariantMap> m_data;
public:
    explicit CustomCalendarWidget(QWidget* parent = nullptr);
    void setupEventFilters();

    void paintCell(QPainter* painter, const QRect& rect, const QDate& date) const override;

    void setCustomData(const QDate& date, const QVariantMap& value);
    void clearCustomData(const QDate& date);
signals:
    void deleteRequested(const QDate& date);

private slots:
    void showContextMenu(const QPoint& pos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QDate dateAt(const QPoint& pos);

    // tableView for date grid
    QTableView* m_tableView;
};

#endif // CUSTOMCALENDARWIDGET_H
