#pragma once

/**
 * @file IconsFontAwesome6.h
 * @brief FontAwesome 6 Free Solid 图标的 UTF-8 编码宏定义
 *
 * 用法: 在 ImGui 按钮/文本中直接使用宏，如:
 *   ImGui::Button(ICON_FA_PLAY " Play");
 *
 * 前置条件: 初始化 ImGui 时加载 fa-solid-900.ttf 并 MergeMode 启用。
 * 若字体未加载，图标将显示为乱码（但不影响程序运行）。
 */

// ── 常用编辑/媒体控制图标 ──
#define ICON_FA_PLAY       "\xef\x81\x8b"  // f04b
#define ICON_FA_PAUSE      "\xef\x81\x8c"  // f04c
#define ICON_FA_STOP       "\xef\x81\x8d"  // f04d
#define ICON_FA_FORWARD    "\xef\x81\x8e"  // f04e
#define ICON_FA_BACKWARD   "\xef\x81\x8a"  // f04a
#define ICON_FA_STEP_FORWARD  "\xef\x81\x91" // f051
#define ICON_FA_STEP_BACKWARD "\xef\x81\x88" // f048
#define ICON_FA_REPEAT     "\xef\x81\xa3"  // f063
#define ICON_FA_SYNC       "\xef\x80\xa1"  // f021

// ── 变换/编辑工具图标 ──
#define ICON_FA_ARROWS        "\xef\x81\xb1"  // f07b  (arrows-alt)
#define ICON_FA_UP_DOWN_LEFT_RIGHT "\xef\x82\xb2" // f0b2 (move)
#define ICON_FA_ROTATE_LEFT   "\xef\x8f\xa2"  // f3e2 (undo)
#define ICON_FA_ROTATE_RIGHT  "\xef\x8f\xa3"  // f3e3 (redo)
#define ICON_FA_EXPAND        "\xef\x81\xa5"  // f065 (expand-arrows-alt)
#define ICON_FA_COMPRESS      "\xef\x81\xa6"  // f066
#define ICON_FA_OBJECT_GROUP  "\xef\x89\x87"  // f247
#define ICON_FA_OBJECT_UNGROUP "\xef\x89\x88" // f248

// ── 视图/显示图标 ──
#define ICON_FA_EYE         "\xef\x81\xae"  // f06e
#define ICON_FA_EYE_SLASH   "\xef\x81\xb0"  // f070
#define ICON_FA_GRID        "\xef\x85\x91"  // f151 (th)
#define ICON_FA_LIST        "\xef\x80\xba"  // f03a
#define ICON_FA_FILM        "\xef\x80\x88"  // f008 (video)

// ── 吸附/对齐图标 ──
#define ICON_FA_MAGNET      "\xef\x81\xb6"  // f076
#define ICON_FA_RULER       "\xef\x95\x85"  // f545
#define ICON_FA_GRIP_LINES  "\xef\x9e\xa4"  // f7a4

// ── 手势/交互图标 ──
#define ICON_FA_HAND_POINTER  "\xef\x89\x9a" // f25a
#define ICON_FA_HAND_PAPER    "\xef\x89\x96" // f256
#define ICON_FA_MOUSE_POINTER "\xef\x89\x85" // f245
#define ICON_FA_CROSSHAIRS    "\xef\x81\x9b" // f05b
#define ICON_FA_SEARCH        "\xef\x80\x82" // f002
#define ICON_FA_SEARCH_PLUS   "\xef\x80\x8e" // f00e
#define ICON_FA_SEARCH_MINUS  "\xef\x80\x90" // f010

// ── 通用/操作图标 ──
#define ICON_FA_GEAR        "\xef\x80\x93"  // f013 (alias for cog)
#define ICON_FA_COG         "\xef\x80\x93"  // f013 (settings)
#define ICON_FA_WRENCH      "\xef\x82\xad"  // f0ad
#define ICON_FA_TRASH       "\xef\x87\xb8"  // f1f8
#define ICON_FA_PLUS        "\xef\x81\xa7"  // f067
#define ICON_FA_MINUS       "\xef\x81\xa8"  // f068
#define ICON_FA_TIMES       "\xef\x80\x8d"  // f00d (close/xmark)
#define ICON_FA_CHECK       "\xef\x80\x8c"  // f00c
#define ICON_FA_REDO        "\xef\x80\x9e"  // f01e
#define ICON_FA_UNDO        "\xef\x83\xa2"  // f0e2
#define ICON_FA_SAVE        "\xef\x83\x87"  // f0c7
#define ICON_FA_FOLDER_OPEN "\xef\x81\xbc"  // f07c
#define ICON_FA_FILE        "\xef\x85\x9b"  // f15b
#define ICON_FA_CAMERA      "\xef\x80\xb0"  // f030
#define ICON_FA_VIDEO       "\xef\x80\xbd"  // f03d
#define ICON_FA_PAINT_BRUSH "\xef\x87\xbc"  // f1fc
#define ICON_FA_CUBE        "\xef\x86\xb2"  // f1b2
#define ICON_FA_CUBES       "\xef\x86\xb3"  // f1b3
#define ICON_FA_CIRCLE      "\xef\x84\x91"  // f111
#define ICON_FA_DOT_CIRCLE  "\xef\x86\x92"  // f192
#define ICON_FA_EXCLAMATION_TRIANGLE "\xef\x81\xb1" // f071
#define ICON_FA_QUESTION_CIRCLE "\xef\x81\x99" // f059
#define ICON_FA_INFO_CIRCLE "\xef\x81\x9a"  // f05a
#define ICON_FA_BUG         "\xef\x86\x88"  // f188
#define ICON_FA_TERMINAL    "\xef\x84\xa0"  // f120
#define ICON_FA_CODE        "\xef\x84\xa1"  // f121
#define ICON_FA_SUN         "\xef\x86\x85"  // f185
#define ICON_FA_MOON        "\xef\x86\x86"  // f186