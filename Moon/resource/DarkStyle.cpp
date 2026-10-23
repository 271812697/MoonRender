/*
###############################################################################
#                                                                             #
# The MIT License                                                             #
#                                                                             #
# Copyright (C) 2017 by Juergen Skrotzky (JorgenVikingGod@gmail.com)          #
#               >> https://github.com/Jorgen-VikingGod                        #
#                                                                             #
# Sources: https://github.com/Jorgen-VikingGod/Qt-Frameless-Window-DarkStyle  #
#                                                                             #
###############################################################################
*/

#include "DarkStyle.h"

#include "editor/Theme.h"
#include "editor/UiScale.h"

DarkStyle::DarkStyle() : DarkStyle(styleBase()) {}

DarkStyle::DarkStyle(QStyle *style) : QProxyStyle(style) {}

QStyle *DarkStyle::styleBase(QStyle *style) const {
  static QStyle *base =
      !style ? QStyleFactory::create(QStringLiteral("Fusion")) : style;
  return base;
}

QStyle *DarkStyle::baseStyle() const { return styleBase(); }

void DarkStyle::polish(QPalette &palette) {
  // The colours live in Theme (there is a light scheme as well); this only installs them.
  const MOON::ThemeTokens &t = MOON::Theme::Tokens();
  palette.setColor(QPalette::Window, t.window);
  palette.setColor(QPalette::WindowText, t.text);
  palette.setColor(QPalette::Disabled, QPalette::WindowText, t.textDisabled);
  palette.setColor(QPalette::Base, t.well);
  palette.setColor(QPalette::AlternateBase, t.surface);
  palette.setColor(QPalette::ToolTipBase, t.surface);
  palette.setColor(QPalette::ToolTipText, t.text);
  palette.setColor(QPalette::Text, t.text);
  palette.setColor(QPalette::Disabled, QPalette::Text, t.textDisabled);
  palette.setColor(QPalette::Dark, t.window.darker(115));
  palette.setColor(QPalette::Shadow, t.window.darker(135));
  palette.setColor(QPalette::Mid, t.border);
  palette.setColor(QPalette::Light, t.surface.lighter(115));
  palette.setColor(QPalette::Button, t.surface);
  palette.setColor(QPalette::ButtonText, t.text);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, t.textDisabled);
  palette.setColor(QPalette::BrightText, Qt::white);
  palette.setColor(QPalette::Link, t.accent);
  palette.setColor(QPalette::Highlight, t.accent);
  palette.setColor(QPalette::HighlightedText, t.accentText);
  palette.setColor(QPalette::Disabled, QPalette::Highlight, t.border);
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, t.textDisabled);
}
void DarkStyle::polish(QApplication *app) {
  if (!app) return;

  // The whole chrome is sized off this font, so it is where the UI scale is applied: the
  // ribbon reads its own metrics from UiScale(), which keeps the two in step.
  QFont defaultFont = QApplication::font();
  defaultFont.setPointSizeF((defaultFont.pointSizeF() + 1.0) * MOON::UiScale());
  app->setFont(defaultFont);

  // Palette, stylesheet tokens and the icons all come from the same table, so the scheme can be
  // switched at runtime (View > 主题) without rebuilding anything.
  MOON::Theme::Apply(app);
}
