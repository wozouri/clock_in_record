#include "TimeInputEdit.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSignalBlocker>

namespace {

QTime parseWholeTime(const QString& input, bool* complete = nullptr)
{
    const QString text = input.trimmed().replace(QChar(0xff1a), QLatin1Char(':'));
    static const QRegularExpression separated(QStringLiteral("^([0-9]{1,2}):([0-9]{1,2})$"));
    static const QRegularExpression compact(QStringLiteral("^[0-9]{3,4}$"));
    const auto match = separated.match(text);
    if (match.hasMatch()) {
        if (complete) *complete = match.captured(2).size() == 2;
        return QTime(match.captured(1).toInt(), match.captured(2).toInt());
    }
    if (compact.match(text).hasMatch()) {
        if (complete) *complete = text.size() == 4;
        return QTime(text.left(text.size() - 2).toInt(), text.right(2).toInt());
    }
    return {};
}

}

TimeInputEdit::TimeInputEdit(QWidget* parent)
    : QTimeEdit(parent)
{
    setWrapping(true);
    setToolTip(QStringLiteral("全选后可直接输入时间，如 08:59、8:59、0859 或 859；支持 Ctrl+V 粘贴。"));
    connect(this, &QTimeEdit::timeChanged, this, [this] { m_wholeTimeInput = false; });
}

QValidator::State TimeInputEdit::validate(QString& text, int& position) const
{
    // Keep incomplete text out of QDateTimeEdit's section parser.
    return m_wholeTimeInput ? QValidator::Intermediate : QTimeEdit::validate(text, position);
}

bool TimeInputEdit::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter
            || (m_wholeTimeInput && key == Qt::Key_Escape)) {
            event->accept();
            return true;
        }
    }
    return QTimeEdit::event(event);
}

void TimeInputEdit::finishWholeTimeInput(bool cancel)
{
    if (!m_wholeTimeInput) return;
    const QTime parsed = parseWholeTime(lineEdit()->text());
    const QSignalBlocker blocker(lineEdit());
    m_wholeTimeInput = false;
    if (!cancel && parsed.isValid()) setTime(parsed);
    lineEdit()->setText(time().toString(displayFormat()));
    lineEdit()->setCursorPosition(lineEdit()->text().size());
}

void TimeInputEdit::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        finishWholeTimeInput();
        QTimeEdit::keyPressEvent(event);
        // QDateTimeEdit ignores Enter after emitting editingFinished. Accept it
        // here so it cannot activate the dialog's default button.
        event->accept();
        return;
    }
    const bool paste = event->matches(QKeySequence::Paste);
    const bool printable = !event->text().isEmpty() && event->text().at(0).isPrint()
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool erase = event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete;
    const bool allSelected = lineEdit()->hasSelectedText()
        && lineEdit()->selectedText().size() == lineEdit()->text().size();
    if (isReadOnly() || (!m_wholeTimeInput && !(allSelected && (printable || paste || erase)))) {
        QTimeEdit::keyPressEvent(event);
        return;
    }

    m_wholeTimeInput = true;
    if (event->key() == Qt::Key_Escape) {
        finishWholeTimeInput(true);
        event->accept();
        return;
    }
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        finishWholeTimeInput();
        QTimeEdit::keyPressEvent(event);
        return;
    default:
        break;
    }

    {
        // Native cursor signals also invoke the section parser, so defer them
        // until the complete time can be applied through setTime().
        const QSignalBlocker blocker(lineEdit());
        QAbstractSpinBox::keyPressEvent(event);
    }
    bool complete = false;
    const QTime parsed = parseWholeTime(lineEdit()->text(), &complete);
    if (parsed.isValid() && (complete || paste)) finishWholeTimeInput();
    event->accept();
}

void TimeInputEdit::focusOutEvent(QFocusEvent* event)
{
    finishWholeTimeInput();
    QTimeEdit::focusOutEvent(event);
}

void TimeInputEdit::mousePressEvent(QMouseEvent* event)
{
    finishWholeTimeInput();
    QTimeEdit::mousePressEvent(event);
}

bool TimeInputEdit::focusNextPrevChild(bool next)
{
    finishWholeTimeInput();
    return QTimeEdit::focusNextPrevChild(next);
}

void TimeInputEdit::stepBy(int steps)
{
    finishWholeTimeInput();
    QTimeEdit::stepBy(steps);
}
