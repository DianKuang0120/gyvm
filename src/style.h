// gyvm - 深色主题（贴合 KDE Breeze Dark；强调色 #3daee9）
#pragma once

// 说明：基于 Fusion 风格 + 这套 QSS，得到一致的圆角/配色观感。
inline const char *kStyleSheet = R"QSS(
QWidget {
    color: #eff0f1;
    font-family: "Noto Sans";
    font-size: 14px;
}

QMainWindow, QDialog { background: #232629; }

/* 菜单栏 / 菜单 */
QMenuBar { background: #232629; padding: 2px 4px; }
QMenuBar::item { padding: 6px 10px; border-radius: 6px; background: transparent; }
QMenuBar::item:selected { background: #3daee9; color: #10171d; }
QMenu { background: #292c30; border: 1px solid #3b4045; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 7px 22px; border-radius: 6px; }
QMenu::item:selected { background: #3daee9; color: #10171d; }
QMenu::separator { height: 1px; background: #3b4045; margin: 5px 8px; }

/* 工具栏 */
QToolBar { background: #232629; border: none; padding: 6px; spacing: 4px; }
QToolBar::separator { width: 1px; background: #3b4045; margin: 6px 6px; }
QToolButton { color: #eff0f1; padding: 7px 12px; border-radius: 8px; background: transparent; }
QToolButton:hover { background: #31363b; }
QToolButton:pressed { background: #3daee9; color: #10171d; }
QToolButton:disabled { color: #7f8c8d; }

/* 分割器 */
QSplitter::handle { background: #1b1e20; }
QSplitter::handle:horizontal { width: 2px; }

/* 左侧列表 */
QListWidget { background: #1b1e20; border: none; border-radius: 12px; padding: 6px; outline: 0; }
QListWidget::item { padding: 11px 12px; border-radius: 9px; margin: 2px 2px; }
QListWidget::item:hover { background: #292c30; }
QListWidget::item:selected { background: #3daee9; color: #10171d; }
QListWidget::item:selected:active { background: #3daee9; color: #10171d; }

/* 标签页 */
QTabWidget::pane { border: none; background: #232629; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab {
    background: transparent; color: #a7b0b5;
    padding: 8px 18px; margin: 6px 2px; border-radius: 9px;
}
QTabBar::tab:hover { background: #292c30; color: #eff0f1; }
QTabBar::tab:selected { background: #31363b; color: #eff0f1; }

/* 分组框（卡片） */
QGroupBox {
    border: 1px solid #3b4045; border-radius: 12px;
    margin-top: 16px; padding: 16px 12px 12px 12px; background: #292c30;
}
QGroupBox::title {
    subcontrol-origin: margin; subcontrol-position: top left;
    left: 14px; padding: 0 6px; color: #a7b0b5;
}

/* 输入控件 */
QLineEdit, QSpinBox, QPlainTextEdit {
    background: #1b1e20; border: 1px solid #3b4045; border-radius: 9px;
    padding: 7px 10px; selection-background-color: #3daee9; selection-color: #10171d;
}
QLineEdit:focus, QSpinBox:focus, QPlainTextEdit:focus { border: 1px solid #3daee9; }
QLineEdit:disabled, QSpinBox:disabled { color: #7f8c8d; }
QPlainTextEdit { font-family: "Noto Sans Mono"; font-size: 13px; }
QSpinBox::up-button, QSpinBox::down-button { width: 18px; background: #31363b; border: none; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: #3daee9; }

/* 按钮 */
QPushButton {
    background: #31363b; border: 1px solid #3b4045; border-radius: 9px;
    padding: 7px 16px;
}
QPushButton:hover { background: #3b4045; }
QPushButton:pressed { background: #3daee9; color: #10171d; border-color: #3daee9; }
QPushButton:disabled { color: #7f8c8d; background: #292c30; }

/* 勾选框 */
QCheckBox { spacing: 9px; padding: 3px 0; }
QCheckBox::indicator { width: 18px; height: 18px; }
QCheckBox::indicator:unchecked { border: 1px solid #3b4045; border-radius: 5px; background: #1b1e20; }
QCheckBox::indicator:checked { border: 1px solid #3daee9; border-radius: 5px; background: #3daee9; }
QCheckBox::indicator:disabled { background: #292c30; border-color: #33383c; }

/* 状态栏 */
QStatusBar { background: #1b1e20; color: #a7b0b5; }
QStatusBar::item { border: none; }

/* 滚动条 */
QScrollBar:vertical { background: transparent; width: 11px; margin: 2px; }
QScrollBar::handle:vertical { background: #4a5157; border-radius: 5px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: #3daee9; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 2px; }
QScrollBar::handle:horizontal { background: #4a5157; border-radius: 5px; min-width: 28px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
)QSS";
