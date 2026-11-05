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
    constexpr float TITLE_H = 38.0f;
    constexpr float NAV_H = 34.0f;
    constexpr float INPUT_H = 46.0f;
    constexpr float BOT_H = 40.0f;
    constexpr float ROW_H = 48.0f;
    constexpr float CIRCLE_R = 8.0f;
}

enum HitType {
    H_BALL,
    H_NAV_ALL,
    H_NAV_QUAD,
    H_NAV_PLANS,
    H_NAV_FOREST,
    H_BTN_REFRESH,
    H_BTN_HANDOVER,
    H_BTN_HISTORY,
    H_BTN_FOREST_STATS,
    H_INPUT_BOX,
    H_TODO_CIRCLE,
    H_TODO_TEXT,
    H_PLAN_ADD,
    H_PLAN_ITEM,
    H_FOREST_TOGGLE,
    H_FOREST_GIVEUP,
    H_REST_FINISH
};

struct Hit {
    D2D1_RECT_F rc;
    HitType type;
    int index1;
    int index2;
};

struct Toast {
    std::wstring text;
    float life = 0.0f;
    float maxLife = 2.0f;
    float alpha = 0.0f;
};

enum AppView {
    VIEW_ALL,
    VIEW_QUADRANTS,
    VIEW_PLANS,
    VIEW_FOREST
};

enum ForestPlantState {
    PLANT_IDLE,
    PLANT_GROWING,
    PLANT_RESTING
};

class App {
public:
    void create(HWND hwnd);
    void destroy();
    void render();
    void tick(float dt);

    void onMouseMove(float x, float y);
    void onLButtonDown(float x, float y);
    void onRButtonUp(float x, float y);
    void onWheel(int delta);
    void onChar(wchar_t ch);
    void onKeyDown(int vk);

    void expandWindow();
    void collapseWindow();
    bool isExpanded() const { return m_expanded; }

    void showToast(const std::wstring& msg);
    void copyHandover();
    void checkReminders();

    float toDip(int px) const;
    int toPx(float dip) const;

private:
    HWND m_hwnd = nullptr;
    float m_w = 60.0f, m_h = 60.0f;
    
    // 状态机
    bool m_expanded = false;
    float m_expandT = 0.0f;      // 0.0f = Ball, 1.0f = Panel
    float m_collapseTimer = 0.0f;// 离界收缩计时器
    bool m_mouseInside = false;

    AppView m_view = VIEW_ALL;
    std::vector<Project> m_projects;
    std::vector<Plan> m_plans;
    std::vector<HistoryItem> m_history;
    Config m_cfg;

    std::vector<Hit> m_hits;
    std::wstring m_inputText;
    std::wstring m_parsedPreview;
    float m_scroll = 0.0f;
    float m_scrollTarget = 0.0f;
    float m_contentH = 0.0f;

    // 专注森林与休息
    ForestPlantState m_plantState = PLANT_IDLE;
    float m_forestRemain = 25.0f * 60.0f;
    float m_forestTotal = 25.0f * 60.0f;
    float m_restRemain = 5.0f * 60.0f;
    float m_restTotal = 5.0f * 60.0f;
    bool m_showSuccessDialog = false;

    // Toast
    Toast m_toast;

    // 待确认完成缓冲（防止误触）
    std::wstring m_pendingUuid;
    float m_pendingTimer = 0.0f;

    void rebuildHits();
    void commitInput();
    void completeTodo(const std::wstring& uuid);
    void postponeTodo(const std::wstring& uuid);
    void deleteTodo(const std::wstring& uuid);
    void deletePlan(const std::wstring& plan_uuid);

    void drawBall(Gfx& g);
    void drawPanel(Gfx& g);
    void drawToast(Gfx& g);
    void drawSuccessDialog(Gfx& g);
};

extern App* g_app;
