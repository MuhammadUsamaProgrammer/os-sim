// ModuleKit.h - shared building blocks for the Phase 2 module tabs (display only).
//
//   PlaybackBar     Run / Pause-Resume / Previous / Next / Stop / Reset, speed slider and a
//                   timeline scrubber over a precomputed deterministic trace (positions 0..N,
//                   0 = initial state). Previous restores the earlier snapshot (the trace is kept).
//   PseudoCodeView  pseudocode lines with the current line highlighted.
//   ModuleHeader    module title, one-line explanation, mode badge (Simulation / Live Linux Demo)
//                   and a status line.
//   ui::semantic*   consistent state colours (always shown together with a text label).
//
// Semantics (documented in README): Run starts playback (from the beginning if at the end);
// Pause/Resume toggles; Stop halts and returns to the initial state (step 0); Reset rebuilds
// the trace from the current inputs (old history is discarded). The speed slider changes only
// the wall-clock animation rate, never the logical results.
#pragma once
#include <QColor>
#include <QListWidget>
#include <QWidget>

class QLabel; class QPushButton; class QSlider; class QTimer;

namespace ui {
inline QColor semIdle()    { return QColor("#9aa0a6"); }   // neutral / idle / New
inline QColor semReady()   { return QColor("#4e79a7"); }   // ready / pending
inline QColor semRunning() { return QColor("#59a14f"); }   // running / active
inline QColor semWaiting() { return QColor("#f28e2b"); }   // waiting / blocked
inline QColor semError()   { return QColor("#e15759"); }   // error / fault
inline QColor semDone()    { return QColor("#b07aa1"); }   // completed / terminated
}

class PlaybackBar : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackBar(QWidget* parent = nullptr);
    void setLength(int steps);                // trace has positions 0..steps; resets to 0
    int length() const { return length_; }
    int position() const { return pos_; }
    void setPosition(int pos);                // emits positionChanged (forward = false)
    bool playing() const;
    int intervalMs() const;
    void setIntervalMs(int ms);
    // Duration a view should use to animate one forward step.
    int animMs() const;

public slots:
    void run();
    void togglePause();
    void next();
    void prev();
    void stop();

signals:
    void positionChanged(int pos, bool forward);   // forward = moved by exactly +1 (animate)
    void resetRequested();                         // owner rebuilds the trace from its inputs
    void finished();                               // playback reached the last step

private:
    void go(int pos, bool forward);
    void updateUi();
    int length_ = 0, pos_ = 0;
    bool paused_ = false;
    QTimer* timer_;
    QPushButton *run_, *pause_, *prev_, *next_, *stop_, *reset_;
    QSlider *scrub_, *speed_;
    QLabel *stepLabel_, *speedLabel_;
};

class PseudoCodeView : public QListWidget {
    Q_OBJECT
public:
    explicit PseudoCodeView(QWidget* parent = nullptr);
    void setLines(const QStringList& lines);
    void setCurrent(int line);               // -1 = none
};

// Lays out a module tab consistently:
//   header
//   [configuration (scrollable) | central visualization | inspector]   (resizable splitters)
//   playback bar (optional)
//   bottom pages (log / comparison / native demo ...)
// All arguments except owner/header may be nullptr.
void assembleModule(QWidget* owner, QWidget* header, QWidget* config, QWidget* center,
                    QWidget* inspector, PlaybackBar* bar, QWidget* bottom);
// Wraps a widget in a group box with a title.
QWidget* titledBox(const QString& title, QWidget* content);

class ModuleHeader : public QWidget {
    Q_OBJECT
public:
    ModuleHeader(const QString& title, const QString& explanation, QWidget* parent = nullptr);
    void setMode(bool liveLinux);            // badge: "Simulation" or "Live Linux Demo"
    void setStatus(const QString& text, const QColor& color = QColor());
    QString status() const;
private:
    QLabel *badge_, *status_;
};
