// LayoutAudit.h - geometry checks used by the layout regression tests (--selftest and
// --layout-report). Nothing here changes the GUI; it only measures it.
#pragma once
#include <QString>
#include <QStringList>

class QWidget;

struct LayoutAuditResult {
    int checked = 0;            // interactive controls inspected
    int needScroll = 0;         // controls only reachable by scrolling (allowed, counted)
    QStringList problems;       // controls that are clipped and cannot be reached
};

// Checks every VISIBLE interactive control below root (buttons, combo boxes, spin boxes,
// line edits, item views, sliders). A control is accessible when, at every ancestor up to
// root, it lies inside the ancestor's rectangle - or, at a scroll-area viewport, when the
// scroll area can scroll to it (scroll bar enabled with a range that covers it).
LayoutAuditResult auditControls(QWidget* root, const QString& context);
