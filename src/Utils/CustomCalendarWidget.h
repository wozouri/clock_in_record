#ifndef CUSTOMCALENDARWIDGET_H
#define CUSTOMCALENDARWIDGET_H

#include <QCalendarWidget>
#include <QTableView>
#include <QAction>
#include <QDate>
#include <QList>
#include <QShowEvent>

class QRubberBand;

// �Զ��������ؼ���֧���Ҽ��˵�
class CustomCalendarWidget : public QCalendarWidget {
    Q_OBJECT
    QMap<QDate, QVariantMap> m_data;
public:
    explicit CustomCalendarWidget(QWidget* parent = nullptr);
    void setupEventFilters();

    void paintCell(QPainter* painter, const QRect& rect, const QDate& date) const override;

    QList<QDate> selectedDates() const;
    void setSelectedDates(const QList<QDate>& dates);
    void setCustomData(const QDate& date, const QVariantMap& value);
    void clearCustomData(const QDate& date);
    void clearSelection();

signals:
    void selectionChanged();
    void dateDoubleClicked(const QDate& date);
    void copyRequested(const QDate& date);
    void deleteRequested(const QList<QDate>& dates);

private slots:
    void showContextMenu(const QPoint& pos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void selectDateFromClick(const QDate& date, Qt::KeyboardModifiers modifiers);
    QList<QDate> datesInRect(const QRect& rect) const;
    void finishRubberBandSelection();
    void selectDateRange(const QDate& start, const QDate& end, bool additive);
    void setSingleSelection(const QDate& date);
    void toggleDateSelection(const QDate& date);
    bool isDateSelected(const QDate& date) const;
    void refreshSelection(const QList<QDate>& datesToUpdate);
    QDate dateAt(const QPoint& pos) const;

    // tableView for date grid
    QTableView* m_tableView;
    QRubberBand* m_selectionRubberBand = nullptr;
    QList<QDate> m_selectedDates;
    QDate m_selectionAnchorDate;
    QDate m_dragStartDate;
    QPoint m_dragStartPosition;
    Qt::KeyboardModifiers m_dragModifiers = Qt::NoModifier;
    bool m_dragSelectionActive = false;
};

#endif // CUSTOMCALENDARWIDGET_H
