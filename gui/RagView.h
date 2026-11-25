// RagView.h - resource-allocation graph drawn with QGraphicsScene.
// Processes = circles (top row), resources = squares with one dot per instance
// (bottom row). Assignment edges go resource -> process, request edges go
// process -> resource. Deadlocked processes and cycle edges are red.
#pragma once
#include <QGraphicsView>
#include <vector>
#include "deadlock.h"

class RagView : public QGraphicsView {
    Q_OBJECT
public:
    explicit RagView(QWidget* parent = nullptr);
    // Redraws the whole graph. rowPids gives the label of each process row.
    void showGraph(const RagGraph& g, const Matrix& alloc, const Matrix& request, const Vec& totalInstances,
                   const std::vector<int>& deadlocked, const std::vector<int>& killed,
                   const std::vector<int>& rowPids);
protected:
    void resizeEvent(QResizeEvent* e) override;
private:
    void addArrow(QPointF from, QPointF to, double fromR, double toR, double offset,
                  const QColor& c, bool dashed, int count);
};
