/// @file actuallabel.h
/// @brief Definition of a custom GUI label component

#ifndef ACTUALLABEL_H
#define ACTUALLABEL_H
#include <QtWidgets>

/// @brief Custom GUI label component
class ActualLabel: public QLabel {
    Q_OBJECT
using QLabel::QLabel;
public:
    void mouseDoubleClickEvent(QMouseEvent *event) {
        if(event->button() & Qt::LeftButton) emit doubleclicked(text());
        event->accept();
    }
signals:
    void doubleclicked(QString);
};

#endif // ACTUALLABEL_H
