// SyncViews.h - QPainter widgets of the Synchronization tab (display only; every
// value comes from core/sync.h, these classes never change the simulation).
//   SyncView     - animated scene: entry section, busy-wait lane, critical section,
//                  finished lane, bounded buffer and every sync object with its waiting queue.
//   CodeView     - pseudocode of every program, current line of each process, last executed line.
//   SyncTimeline - one row per process, one column per step (run / in CS / spin / blocked).
#pragma once
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <vector>

class SyncSim;

// Short chip label: "P0", "Prod1" -> "P1", "Cons0" -> "C0".
QString syncShortName(const QString& name);

class SyncView : public QWidget {
    Q_OBJECT
public:
    explicit SyncView(QWidget* parent = nullptr);
    // Shows sim (may be nullptr). The process chips glide to their new place in animMs.
    void setSim(const SyncSim* sim);
    void refresh(int animMs);
    QSize sizeHint() const override { return QSize(560, 360); }
    QSize minimumSizeHint() const override { return QSize(280, 180); }

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    struct Box { QRectF rect; QString title, value; int obj = -1; QColor accent; };
    struct Layout {
        QRectF buffer, ready, spin, cs, done;
        std::vector<Box> boxes;           // sync objects and shared variables (bottom row)
    };
    Layout layout() const;
    std::vector<QPointF> targets(const Layout& l) const;
    void snap();

    const SyncSim* sim_ = nullptr;
    std::vector<QPointF> from_, to_, cur_;
    QElapsedTimer clock_;
    int duration_ = 0;
    QTimer anim_;
};

class CodeView : public QWidget {
    Q_OBJECT
public:
    explicit CodeView(QWidget* parent = nullptr);
    void setSim(const SyncSim* sim);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(200, 120); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const SyncSim* sim_ = nullptr;
};

class SyncTimeline : public QWidget {
    Q_OBJECT
public:
    explicit SyncTimeline(QWidget* parent = nullptr);
    void setSim(const SyncSim* sim);      // call again after every step
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    const SyncSim* sim_ = nullptr;
};
