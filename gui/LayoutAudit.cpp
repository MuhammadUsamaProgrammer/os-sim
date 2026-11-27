// LayoutAudit.cpp - finds controls that are clipped and cannot be reached.
#include "LayoutAudit.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QSlider>
#include <QTabBar>
#include <QWidget>

static bool interactive(QWidget* w) {
    return qobject_cast<QAbstractButton*>(w) || qobject_cast<QComboBox*>(w) || qobject_cast<QAbstractSpinBox*>(w) ||
           qobject_cast<QLineEdit*>(w) || qobject_cast<QAbstractItemView*>(w) || qobject_cast<QSlider*>(w);
}

// Parts of composite widgets (combo box internals, spin box editors, tab bar buttons,
// item-view children) are checked through their owner, not on their own.
static bool insideComposite(QWidget* w, QWidget* root) {
    for (QWidget* p = w->parentWidget(); p && p != root; p = p->parentWidget())
        if (qobject_cast<QAbstractItemView*>(p) || qobject_cast<QComboBox*>(p) || qobject_cast<QAbstractSpinBox*>(p) ||
            qobject_cast<QTabBar*>(p) || qobject_cast<QMenu*>(p) || qobject_cast<QScrollBar*>(p))
            return true;
    return qobject_cast<QScrollBar*>(w) != nullptr;
}

static QString describe(QWidget* w) {
    QString t;
    if (auto* b = qobject_cast<QAbstractButton*>(w)) t = b->text();
    else if (auto* c = qobject_cast<QComboBox*>(w)) t = c->currentText();
    else if (!w->accessibleName().isEmpty()) t = w->accessibleName();
    return QString("%1 '%2'").arg(w->metaObject()->className(), t.left(30));
}

LayoutAuditResult auditControls(QWidget* root, const QString& context) {
    LayoutAuditResult r;
    const auto all = root->findChildren<QWidget*>();
    for (QWidget* w : all) {
        if (!w->isVisible() || !interactive(w) || insideComposite(w, root)) continue;
        ++r.checked;
        QRect rect = w->rect();                      // in w's coordinates
        QWidget* cur = w;
        bool scrolled = false;
        QString why;
        for (QWidget* p = w->parentWidget(); p; p = p->parentWidget()) {
            rect.translate(cur->pos());              // now in p's coordinates
            cur = p;
            auto* sa = p->parentWidget() ? qobject_cast<QAbstractScrollArea*>(p->parentWidget()) : nullptr;
            bool isViewport = sa && sa->viewport() == p;
            QRect area = p->rect();
            const int tol = 2;
            bool outH = rect.left() < area.left() - tol || rect.right() > area.right() + tol;
            bool outV = rect.top() < area.top() - tol || rect.bottom() > area.bottom() + tol;
            if (isViewport) {
                // reachable if the scroll bar in that direction can move far enough
                QScrollBar* h = sa->horizontalScrollBar();
                QScrollBar* v = sa->verticalScrollBar();
                bool hOk = !outH || (sa->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff && h->maximum() > 0);
                bool vOk = !outV || (sa->verticalScrollBarPolicy() != Qt::ScrollBarAlwaysOff && v->maximum() > 0);
                if (!hOk || !vOk) { why = QString("clipped by scroll area %1 with no usable %2 scroll bar").arg(sa->objectName().isEmpty() ? sa->metaObject()->className() : sa->objectName()).arg(!hOk ? "horizontal" : "vertical"); break; }
                if (outH || outV) scrolled = true;
                // continue from the scroll area itself (the content is clipped to it)
                rect = QRect(QPoint(0, 0), sa->size());
                cur = sa;
                p = sa;
                if (p == root) break;
                continue;
            }
            if ((outH || outV) && !w->isWindow()) { why = QString("extends outside %1 (%2 px)").arg(p->metaObject()->className()).arg(outH ? std::max(area.left() - rect.left(), rect.right() - area.right()) : std::max(area.top() - rect.top(), rect.bottom() - area.bottom())); break; }
            if (p == root) break;
        }
        if (!why.isEmpty()) r.problems << QString("%1: %2 %3").arg(context, describe(w), why);
        else if (scrolled) ++r.needScroll;
    }
    return r;
}
