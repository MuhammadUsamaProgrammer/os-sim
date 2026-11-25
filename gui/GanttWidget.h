// GanttWidget.h - custom-painted Gantt chart of a CPU schedule.
#pragma once
#include <QWidget>
#include <vector>
#include "scheduler.h"

class GanttWidget : public QWidget {
    Q_OBJECT
public:
    explicit GanttWidget(QWidget* parent = nullptr);
    // Shows a schedule. visibleUntil = -1 draws everything; otherwise only ticks < visibleUntil.
    void setTimeline(const std::vector<Segment>& tl, int visibleUntil = -1);
    void setVisibleUntil(int tick);         // animated mode: reveal up to this tick
    void setTitle(const QString& t) { title_ = t; update(); }
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    std::vector<Segment> tl_;
    int visibleUntil_ = -1;
    QString title_;
};
