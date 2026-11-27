// Theme.h - light and dark colour themes for the whole application.
// Uses Qt's "Fusion" style, which follows the QPalette exactly, so one
// palette switch recolours every widget (custom painters read palette() too).
#pragma once
#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>

namespace ui {

// True while the dark theme is active.
inline bool& darkTheme() { static bool on = false; return on; }

// Applies the light (dark = false) or dark (dark = true) theme to the application.
inline void applyTheme(bool dark) {
    darkTheme() = dark;
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    if (dark) {
        QColor window("#2b2d30"), base("#1e1f22"), text("#e6e6e6"), button("#3a3c40");
        p.setColor(QPalette::Window, window);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, QColor("#2f3134"));
        p.setColor(QPalette::ToolTipBase, QColor("#3a3c40"));
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::PlaceholderText, QColor("#8a8a8a"));
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, button);
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::BrightText, Qt::red);
        p.setColor(QPalette::Light, QColor("#4a4c50"));
        p.setColor(QPalette::Midlight, QColor("#404246"));
        p.setColor(QPalette::Mid, QColor("#5a5d62"));
        p.setColor(QPalette::Dark, QColor("#191a1c"));
        p.setColor(QPalette::Shadow, Qt::black);
        p.setColor(QPalette::Highlight, QColor("#3d6fb4"));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, QColor("#6ea8fe"));
        // Disabled widgets: greyed-out text
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            p.setColor(QPalette::Disabled, role, QColor("#77797d"));
        p.setColor(QPalette::Disabled, QPalette::Base, window);
        p.setColor(QPalette::Disabled, QPalette::Button, window);
    } else {
        QColor window("#f3f4f6"), text("#1f2328");
        p.setColor(QPalette::Window, window);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, Qt::white);
        p.setColor(QPalette::AlternateBase, QColor("#f6f8fa"));
        p.setColor(QPalette::ToolTipBase, QColor("#ffffe1"));
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::PlaceholderText, QColor("#8c959f"));
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, QColor("#e9ebef"));
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::BrightText, Qt::red);
        p.setColor(QPalette::Light, Qt::white);
        p.setColor(QPalette::Midlight, QColor("#e3e5e8"));
        p.setColor(QPalette::Mid, QColor("#b8bcc2"));
        p.setColor(QPalette::Dark, QColor("#8c9096"));
        p.setColor(QPalette::Shadow, QColor("#5f6368"));
        p.setColor(QPalette::Highlight, QColor("#2f6fd0"));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, QColor("#0969da"));
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            p.setColor(QPalette::Disabled, role, QColor("#a0a4aa"));
        p.setColor(QPalette::Disabled, QPalette::Base, window);
    }
    QApplication::setPalette(p);
}

} // namespace ui
