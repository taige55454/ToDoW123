#pragma once
#include <windows.h>
#include <d2d1.h>
#include <vector>
#include <string>
#include "storage.h"
#include "gfx.h"

namespace AppC {
    constexpr float BALL_SIZE = 60.0f;
    constexpr float PANEL_W = 360.0f;
    constexpr float PANEL_H = 640.0f;
    constexpr float TAB_H = 34.0f;
    constexpr float TITLE_H = 36.0f;
    constexpr float BOT_INPUT_H = 46.0f;
    constexpr float ROW_H = 30.0f;
    constexpr float CIRCLE_R = 7.0f;
    constexpr float BADGE_MIN_W = 28.0f;
    constexpr float BADGE_H = 18.0f;
    constexpr float CARD_GAP = 8.0f;
    constexpr float CARD_INNER = 10.0f;
}

enum HitType {
    H_NONE,
    H_BALL,
    H_TAB_ALL,
    H_TAB_QUAD,
    H_TAB_PLANS,
    H_TAB_FOREST,
    H_BTN_REFRESH,
    H_BTN_HANDOVER,
    H_BTN_HISTORY,
    H_BTN_FOREST_LOG,
    H_INPUT_BOX,
    H_TODO_CIRCLE,
    H_TODO_TEXT,
    H_PLAN_ITEM,
    H_PROJ_NAME,
    H_PROJ_DEL,
    H_FOREST_START,
    H_FOREST_GIVEUP,
    H_TOAST
};

struct Hit {
    D2D1_RECT_F rc;
    HitType type;
    int pi, ti;
};

enum ViewMode {
    VIEW_ALL,
    VIEW_QUADRANTS,
    VIEW_PLANS,
    VIEW_FOREST
};

class App {
public:
    void create(HWND hwnd);
    void destroy();
    void render();
    void tick(float dt);

    LRESULT onNcHitTest(POINT screenPt);
    void onMouseMove(float x, float y);
    void onMouseLeave();
    void onLButtonDown(float x, float y);
    void onLButtonDblClk(float x, float y);
    void onRButtonUp(float x, float y);
    void onWheel(int delta);
    void onChar(wchar_t ch);
    void onKeyDown(int vk);

    // 窗口尺寸与定位
    float toDip(int px) const;
    int toPx(float dip) const;
    bool isExpanded() const { return m_expanded; }

    void showFromTray(bool openSettingsPanel);
    void hideToTray();
    void exitFromTray();
    void requestRedraw();

    void showToast(const std::wstring& msg);

private:
    HWND m_hwnd = nullptr;
    float m_w = AppC::BALL_SIZE;
    float m_h = AppC::BALL_SIZE;

    // 悬浮球 & 展开收起动效
    bool m_expanded = false;
    float m_expandProgress = 0.0f; // 0.0 = Ball, 1.0 = Panel
    float m_collapseTimer = 0.0f;
    bool m_mouseInside = false;

    // 视图
    ViewMode m_curView = VIEW_ALL;

    // 数据源
    std::vector<Project> m_projects;
    std::vector<Plan> m_plans;
    std::vector<HistoryItem> m_history;
    std::vector<ForestRecord> m_forestLogs;
    Config m_cfg;

    // 待办/计划/象限辅助
    std::vector<Hit> m_hits;
    float m_scroll = 0.0f, m_scrollTarget = 0.0f;
    float m_contentH = 0.0f;

    // 底部实时快捷输入框
    std::wstring m_quickInput;
    std::wstring m_quickPreview;
    bool m_inputFocused = false;
    float m_cursorBlink = 0.0f;

    // 专注森林引擎
    bool m_forestRunning = false;
    bool m_forestResting = false;
    float m_forestRemainSecs = 1500.0f;
    float m_forestTotalSecs = 1500.0f;
    std::wstring m_forestTaskText;

    // Toast 系统
    std::wstring m_toastText;
    float m_toastAlpha = 0.0f;
    float m_toastTimer = 0.0f;

    // 交互辅助
    int m_confirmDonePi = -1;
    int m_confirmDoneTi = -1;
    float m_confirmDoneTimer = 0.0f;

    void expandWindow();
    void collapseWindow();
    void updateWindowShape(float progress);
    void syncPlansToToday();
    void commitQuickInput();
    void completeTodo(int pi, int ti);
    void postponeTodo(int pi, int ti);
    void removeTodo(int pi, int ti);
    void removePlan(int index);
    void copyHandover();
    void openHistoryReport();
    void openForestReport();
    void triggerForestToggle();
    void triggerForestGiveup();

    void rebuildHits();
    void clampScroll();
    void saveAll();
};

extern App* g_app;
