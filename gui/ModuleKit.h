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
#include <QString>
#include <QWidget>

class QLabel; class QPushButton; class QSlider; class QTimer; class QBoxLayout;

namespace ui {
inline QColor semIdle()    { return QColor("#9aa0a6"); }   // neutral / idle / New
inline QColor semReady()   { return QColor("#4e79a7"); }   // ready / pending
inline QColor semRunning() { return QColor("#59a14f"); }   // running / active
inline QColor semWaiting() { return QColor("#f28e2b"); }   // waiting / blocked
inline QColor semError()   { return QColor("#e15759"); }   // error / fault
inline QColor semDone()    { return QColor("#b07aa1"); }   // completed / terminated
// WCAG contrast ratio between two colours (1 = none, 21 = black on white).
double contrastRatio(const QColor& a, const QColor& b);
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
    QSize minimumSizeHint() const override;       // the two-row minimum, so it never forces width

public slots:
    void run();
    void togglePause();
    void pause();                                  // pauses only if playing (resume with Pause / Run)
    void next();
    void prev();
    void stop();

signals:
    void positionChanged(int pos, bool forward);   // forward = moved by exactly +1 (animate)
    void resetRequested();                         // owner rebuilds the trace from its inputs
    void finished();                               // playback reached the last step

protected:
    void resizeEvent(QResizeEvent* e) override;   // one row when wide, two rows when narrow
private:
    void go(int pos, bool forward);
    void updateUi();
    QBoxLayout* outer_;
    QWidget *buttons_, *scrubRow_;
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
// All arguments except owner/header may be nullptr. The panels use PanelLayout.h: the
// configuration panel scrolls (both directions as needed) and every splitter is resizable,
// restorable (double-click a handle) and remembered under layoutId (default: header title).
void assembleModule(QWidget* owner, QWidget* header, QWidget* config, QWidget* center,
                    QWidget* inspector, PlaybackBar* bar, QWidget* bottom, const QString& layoutId = QString());
// Wraps a widget in a group box with a title.
QWidget* titledBox(const QString& title, QWidget* content);

// ---- standard "Load Sample" control (see SampleRegistry.h)
// Wraps a configuration widget so that its top holds an (initially empty, hidden) sample slot.
// assembleModule() does this for every module; pages with their own layout call it themselves.
QWidget* withSampleSlot(QWidget* config);
// Puts w into the first sample slot below page; returns false if the page has none.
bool installInSampleSlot(QWidget* page, QWidget* w);

class SampleRegistry; struct SampleDef; class QToolButton; class QMenu;

// Prominent accent button "Load Sample" with the module's sample menu (categories as sections,
// each entry's tooltip = description / mode / parameters) plus "Browse samples..." (dialog with
// Load and Load && Run), and a one-line label naming the loaded sample. Reads the registry lazily,
// so samples registered later (or of a hidden module) are always offered. Colours follow the
// palette's highlight colour and are refreshed when the theme changes.
class SampleButton : public QWidget {
    Q_OBJECT
public:
    SampleButton(SampleRegistry* reg, const QString& module, QWidget* parent = nullptr);
    QString module() const { return module_; }
    QToolButton* button() const { return button_; }
    QMenu* menu() const { return menu_; }
    void rebuildMenu();                        // also called before the menu opens
    QStringList menuSampleIds() const;         // ids offered by the menu (self-test)
    QString currentText() const;
    QColor background() const { return bg_; }
    QColor foreground() const { return fg_; }
    void setDialogTitle(const QString& t) { title_ = t; }
    // Opens the browser dialog; returns true if a sample was loaded.
    bool browse();
protected:
    void changeEvent(QEvent* e) override;
private:
    void restyle();
    void showLoaded(const SampleDef& d);
    SampleRegistry* reg_;
    QString module_, title_;
    QToolButton* button_;
    QMenu* menu_;
    QLabel* current_;
    QColor bg_, fg_;
    bool styling_ = false;
};

class ModuleHeader : public QWidget {
    Q_OBJECT
public:
    ModuleHeader(const QString& title, const QString& explanation, QWidget* parent = nullptr);
    void setMode(bool liveLinux);            // badge: "Simulation" or "Live Linux Demo"
    void setBadge(const QString& text, const QColor& color);   // any other badge (e.g. "Live System")
    void setStatus(const QString& text, const QColor& color = QColor());
    QString status() const;
    QString title() const { return title_; }
private:
    QLabel *badge_, *status_;
    QString title_;
};
