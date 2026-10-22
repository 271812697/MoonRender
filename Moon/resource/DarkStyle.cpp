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

DarkStyle::DarkStyle() : DarkStyle(styleBase()) {}

DarkStyle::DarkStyle(QStyle *style) : QProxyStyle(style) {}

QStyle *DarkStyle::styleBase(QStyle *style) const {
  static QStyle *base =
      !style ? QStyleFactory::create(QStringLiteral("Fusion")) : style;
  return base;
}

QStyle *DarkStyle::baseStyle() const { return styleBase(); }

void DarkStyle::polish(QPalette &palette) {
  // A cool, slightly blue dark grey instead of the flat neutral one: the viewport's own
  // background is a cool grey too, so the panes and the 3D view sit in the same family
  // instead of one looking warm next to the other. The accent is the blue the ribbon icons
  // use for their checked state.
  const QColor window(30, 35, 41);
  const QColor surface(38, 44, 51);
  const QColor well(25, 30, 36);
  const QColor text(207, 214, 223);
  const QColor textDisabled(102, 112, 124);
  const QColor accent(79, 163, 255);

  palette.setColor(QPalette::Window, window);
  palette.setColor(QPalette::WindowText, text);
  palette.setColor(QPalette::Disabled, QPalette::WindowText, textDisabled);
  palette.setColor(QPalette::Base, well);
  palette.setColor(QPalette::AlternateBase, surface);
  palette.setColor(QPalette::ToolTipBase, surface);
  palette.setColor(QPalette::ToolTipText, text);
  palette.setColor(QPalette::Text, text);
  palette.setColor(QPalette::Disabled, QPalette::Text, textDisabled);
  palette.setColor(QPalette::Dark, QColor(22, 26, 31));
  palette.setColor(QPalette::Shadow, QColor(16, 20, 26));
  palette.setColor(QPalette::Mid, QColor(47, 53, 61));
  palette.setColor(QPalette::Light, QColor(58, 66, 76));
  palette.setColor(QPalette::Button, surface);
  palette.setColor(QPalette::ButtonText, text);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, textDisabled);
  palette.setColor(QPalette::BrightText, Qt::white);
  palette.setColor(QPalette::Link, accent);
  palette.setColor(QPalette::Highlight, accent);
  // Dark glyphs on the bright accent: white on this blue only reaches about 2.4:1.
  palette.setColor(QPalette::HighlightedText, QColor(11, 24, 34));
  palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(58, 66, 76));
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, textDisabled);
}

#include "editor/UiScale.h"

void DarkStyle::polish(QApplication *app) {
  if (!app) return;

  // The whole chrome is sized off this font, so it is where the UI scale is applied: the
  // ribbon reads its own metrics from UiScale(), which keeps the two in step.
  QFont defaultFont = QApplication::font();
  defaultFont.setPointSizeF((defaultFont.pointSizeF() + 1.0) * MOON::UiScale());
  app->setFont(defaultFont);

  // loadstylesheet
  QFile qfDarkstyle(QStringLiteral(":/darkstyle/darkstyle.qss"));
  if (qfDarkstyle.open(QIODevice::ReadOnly | QIODevice::Text)) {
    // set stylesheet
    QString qsStylesheet = QString::fromLatin1(qfDarkstyle.readAll());
    app->setStyleSheet(qsStylesheet);
    qfDarkstyle.close();
  }
}
