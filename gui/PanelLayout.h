// PanelLayout.h - shared, responsive panel layout for every module tab.
//
//   ConfigPanel     scrollable configuration panel: both scroll bars "as needed" (vertical for
//                   long forms, horizontal only as a fallback), no maximum width, a minimum width
//                   derived from font metrics, and responsive form policies (see makeResponsive).
//   PanelSplitter   QSplitter that knows the role of each panel (configuration, centre view,
//                   inspector, detail area). It computes default sizes from the panels' size hints
//                   and the available width, keeps the centre view non-collapsible, lets the side
//                   panels be collapsed, draws visible handles (hover highlight, resize cursor,
//                   double-click = restore defaults), supports show / hide / focus-view, and saves
//                   the user's sizes in QSettings (validated against the window when restored).
//   ui::makeResponsive  form rows wrap at narrow widths, long labels wrap, combo boxes do not
//                   force their longest item as minimum width (wide pop-up + tooltip instead).
#pragma once
#include <QList>
#include <QScrollArea>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTimer>

class PanelSplitter;

namespace ui {
// Applies responsive policies to all forms, labels and combo boxes below root.
void makeResponsive(QWidget* root);
// Every PanelSplitter currently alive below root (used by the View menu).
QList<PanelSplitter*> panelSplitters(QWidget* root);
}

// Scroll area that gives its content exactly the viewport size, and grows the content beyond the
// viewport only when the content's real MINIMUM (height computed for the current width, so
// wrapped text counts) does not fit. Then, and only then, the scroll bars appear.
class FitScrollArea : public QScrollArea {
    Q_OBJECT
public:
    explicit FitScrollArea(QWidget* content, QWidget* parent = nullptr);
    void fit();
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
private:
    bool fitting_ = false;
};

class ConfigPanel : public FitScrollArea {
    Q_OBJECT
public:
    explicit ConfigPanel(QWidget* content, QWidget* parent = nullptr);
    // Same responsive scroll container for inspector / detail panels (objectName = name).
    static ConfigPanel* wrap(QWidget* content, const QString& name);
    QSize sizeHint() const override;              // wide enough for the content without h-scrolling
    QSize minimumSizeHint() const override;       // narrow, from font metrics: the user may shrink it
};

// Top-level page container: the page keeps its natural layout, but when the window is smaller
// than the page's minimum size the page scrolls instead of being cut off (or forcing the window
// to be larger than the screen).
class PageScroll : public FitScrollArea {
    Q_OBJECT
public:
    explicit PageScroll(QWidget* page, QWidget* parent = nullptr);
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;
};

class PanelSplitter : public QSplitter {
    Q_OBJECT
public:
    enum Role { Config, Center, Inspector, Detail };
    // id: key for the saved sizes ("" = not saved)
    PanelSplitter(Qt::Orientation o, const QString& id, QWidget* parent = nullptr);
    // weight: share of the remaining space (Center / Detail); Config and Inspector take their
    // preferred size (capped by maxShare of the available space).
    int addPanel(QWidget* w, Role role, int weight = 1, double maxShare = 0.38);
    void resetLayout();                           // default sizes, forget the saved ones
    bool hasPanel(Role r) const;
    void setPanelVisible(Role r, bool on);
    bool panelVisible(Role r) const;
    void setFocusView(bool on);                   // hide everything except the centre view
    bool focusView() const { return focus_; }
    QList<int> defaultSizes() const;
    QWidget* panel(Role r) const;
    // Persistence switch: off during the self-test unless a test enables it with its own
    // settings name (so the user's saved layout is never touched).
    static void setPersistence(bool enabled, const QString& settingsName = "OS-Sim");
    static bool persistence();
    void saveNow() { save(); }
protected:
    QSplitterHandle* createHandle() override;
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
private:
    void save();
    bool restore();
    QString id_;
    QList<Role> roles_;
    QList<int> weights_;
    QList<double> maxShare_;
    QList<bool> hiddenBeforeFocus_;
    bool applied_ = false, focus_ = false;
    QTimer saveTimer_;
};

// Splitter handle: visible grip, hover highlight; double-click restores the default sizes.
class PanelHandle : public QSplitterHandle {
    Q_OBJECT
public:
    PanelHandle(Qt::Orientation o, QSplitter* parent);
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    bool hover_ = false;
};
