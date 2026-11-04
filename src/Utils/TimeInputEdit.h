#pragma once

#include <QTimeEdit>

// Keeps section editing while allowing a selected time to be replaced as a whole.
class TimeInputEdit : public QTimeEdit {
public:
    explicit TimeInputEdit(QWidget* parent = nullptr);

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool focusNextPrevChild(bool next) override;
    void stepBy(int steps) override;
    QValidator::State validate(QString& text, int& position) const override;

private:
    void finishWholeTimeInput(bool cancel = false);
    bool m_wholeTimeInput = false;
};
