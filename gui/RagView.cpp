// RagView.cpp - draws nodes, edges with arrowheads and highlights the deadlock.
#include "RagView.h"
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QResizeEvent>
#include <QEvent>
#include <QtMath>
#include <algorithm>
#include "Style.h"
#include "Theme.h"

static const double kProcR = 22, kResHalf = 24;

RagView::RagView(QWidget* parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
    setMinimumSize(300, 220);
}

void RagView::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    if (!scene()->items().isEmpty()) fitInView(scene()->itemsBoundingRect().adjusted(-10, -10, 10, 10), Qt::KeepAspectRatio);
}

void RagView::changeEvent(QEvent* e) {
    QGraphicsView::changeEvent(e);
    if (e->type() == QEvent::PaletteChange && hasGraph_)
        showGraph(g_, alloc_, request_, total_, deadlocked_, killed_, rowPids_);
}

// Draws a straight edge between two node centres, trimmed to the node borders,
// shifted sideways by `offset` (so a request and an assignment between the same
// pair do not overlap), with an arrowhead at the target and an optional count.
void RagView::addArrow(QPointF a, QPointF b, double ra, double rb, double offset,
                       const QColor& c, bool dashed, int count) {
    QLineF line(a, b);
    double len = line.length();
    if (len < 1) return;
    QPointF dir = (b - a) / len, normal(-dir.y(), dir.x());
    QPointF p1 = a + dir * ra + normal * offset, p2 = b - dir * rb + normal * offset;
    QPen pen(c, 2);
    if (dashed) pen.setStyle(Qt::DashLine);
    scene()->addLine(QLineF(p1, p2), pen);
    QPointF back = p2 - dir * 12;
    QPolygonF head({p2, back + normal * 5, back - normal * 5});
    scene()->addPolygon(head, QPen(c), QBrush(c));
    if (count > 1) {
        auto* t = scene()->addText(QString("x%1").arg(count));
        t->setDefaultTextColor(c);
        t->setPos((p1 + p2) / 2 + normal * 8 - QPointF(8, 10));
    }
}

void RagView::showGraph(const RagGraph& g, const Matrix& alloc, const Matrix& request, const Vec& total,
                        const std::vector<int>& deadlocked, const std::vector<int>& killed,
                        const std::vector<int>& rowPids) {
    if (&g != &g_) {                                    // remember the inputs (for theme redraws)
        hasGraph_ = true; g_ = g; alloc_ = alloc; request_ = request; total_ = total;
        deadlocked_ = deadlocked; killed_ = killed; rowPids_ = rowPids;
    }
    QGraphicsScene* s = scene();
    s->clear();
    const QColor ink = palette().text().color();        // outlines and labels follow the theme
    const QColor edge = ink.lighter(ui::darkTheme() ? 80 : 100);
    int n = g.nProcs, m = g.nRes;
    double width = std::max(std::max(n, m) * 90.0, 300.0);
    auto procPos = [&](int i) { return QPointF(width * (i + 0.5) / n, 40); };
    auto resPos = [&](int j) { return QPointF(width * (j + 0.5) / m, 220); };
    auto contains = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };

    // Which edges belong to the cycle (consecutive nodes, wrapping around)?
    auto inCycle = [&](int from, int to) {
        const auto& c = g.cycleNodes;
        for (size_t k = 0; k < c.size(); ++k)
            if (c[k] == from && c[(k + 1) % c.size()] == to) return true;
        return false;
    };

    // Edges first, so nodes are painted on top.
    for (const RagEdge& e : g.edges) {
        bool red = inCycle(e.from, e.to);
        if (e.isRequest) {                                  // process -> resource (dashed)
            int i = e.from, j = e.to - n;
            addArrow(procPos(i), resPos(j), kProcR, kResHalf, -7, red ? ui::faultColor() : edge,
                     true, request[i][j]);
        } else {                                            // resource -> process (solid)
            int j = e.from - n, i = e.to;
            addArrow(resPos(j), procPos(i), kResHalf, kProcR, -7, red ? ui::faultColor() : QColor(ui::darkTheme() ? "#7aa7d9" : "#4e79a7"),
                     false, alloc[i][j]);
        }
    }
    // Process nodes
    for (int i = 0; i < n; ++i) {
        QPointF c = procPos(i);
        bool dead = contains(deadlocked, i), gone = contains(killed, i);
        QColor fill = gone ? ui::idleColor() : dead ? ui::faultColor() : ui::pidColor(rowPids[i]).lighter(140);
        s->addEllipse(c.x() - kProcR, c.y() - kProcR, 2 * kProcR, 2 * kProcR, QPen(ink, dead ? 3 : 1.5), fill);
        auto* t = s->addText(QString("P%1").arg(rowPids[i]));
        t->setDefaultTextColor(ui::darkInk());          // light fill -> dark label
        QFont f = t->font(); f.setBold(true); t->setFont(f);
        t->setPos(c - QPointF(t->boundingRect().width() / 2, t->boundingRect().height() / 2));
        if (gone) {
            auto* k = s->addText("killed");
            k->setDefaultTextColor(ink);
            k->setPos(c + QPointF(-k->boundingRect().width() / 2, -kProcR - 22));
        }
    }
    // Resource nodes with one dot per instance
    for (int j = 0; j < m; ++j) {
        QPointF c = resPos(j);
        s->addRect(c.x() - kResHalf, c.y() - kResHalf, 2 * kResHalf, 2 * kResHalf, QPen(ink, 1.5),
                   palette().alternateBase());
        auto* t = s->addText(QString(QChar('A' + j)));     // resource type A, B, C ...
        t->setDefaultTextColor(ink);
        t->setPos(c + QPointF(-t->boundingRect().width() / 2, kResHalf + 2));
        int inst = j < (int)total.size() ? total[j] : 0, shown = std::min(inst, 16);
        int cols = 4;
        for (int k = 0; k < shown; ++k) {
            double x = c.x() - kResHalf + 7 + (k % cols) * 10, y = c.y() - kResHalf + 7 + (k / cols) * 10;
            s->addEllipse(x, y, 6, 6, QPen(Qt::NoPen), QBrush(ink));
        }
        if (inst > shown) {
            auto* more = s->addText(QString("+%1").arg(inst - shown));
            more->setDefaultTextColor(ink);
            more->setPos(c + QPointF(kResHalf, -10));
        }
    }
    // Legend
    auto* legend = s->addText("solid: assignment (resource -> process)\ndashed: request (process -> resource)\n"
                              "red: deadlocked process / cycle edge");
    legend->setDefaultTextColor(palette().placeholderText().color());
    legend->setPos(0, 300);
    s->setSceneRect(s->itemsBoundingRect().adjusted(-10, -10, 10, 10));
    fitInView(s->sceneRect(), Qt::KeepAspectRatio);
}
