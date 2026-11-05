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
    constexpr float TITLE_H = 42.0f;
    constexpr float NAV_H = 36.0f;
    constexpr float INPUT_H = 54.0f;
    constexpr float ROW_H = 54.0f;
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
    H_TODO_ITEM,
    H_QUAD_ITEM,
    H_PLAN_ADD,
    H_PLAN_ITEM,
    H_FOREST_MINUS,
    H_FOREST_PLUS,
    H_FOREST_TOGGLE,
    H_FOREST_GIVEUP,
    // 弹窗内部热区
    H_MODAL_CLOSE,
    H_DLG_TITLE_EDIT,
    H_DLG_PRIO_CHK,
    H_DLG_DUE_CHK,
    H_DLG_DUE_PREV,
    H_DLG_DUE_NEXT,
    H_DLG_TIME_CHK,
    H_DLG_HOUR_PREV,
    H_DLG_HOUR_NEXT,
    H_DLG_MIN_PREV,
    H_DLG_MIN_NEXT,
    H_DLG_RULE_NONE,
    H_DLG_RULE_DAILY,
    H_DLG_RULE_WEEKDAYS,
    H_DLG_RULE_WEEKLY,
    H_DLG_RULE_MONTHLY,
    H_DLG_WEEKDAY_BTN,
    H_DLG_MONTH_EDIT,
    H_DLG_SAVE,
    H_DLG_CANCEL,
    H_SUCCESS_OK
};

struct Hit {
    D2D1_RECT_F rc;
    HitType type;
    int index = 0;
};

enum AppView {
    VIEW_ALL = 0,
    VIEW_QUADRANTS = 1,
    VIEW_PLANS = 2,
    VIEW_FOREST = 3
};

enum ModalMode {
    MODAL_NONE = 0,
    MODAL_HISTORY,
    MODAL_FOREST_STATS,
    MODAL_EDIT_TASK,
    MODAL_EDIT_PLAN,
    MODAL_FOCUS_SUCCESS
};

enum EditTarget {
    ET_NONE = 0,
    ET_QUICK_INPUT,
    ET_DLG_TITLE,
    ET_DLG_MONTH_DAYS
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
    void onEditTextChanged();
    void onEditReturn();
    void onEditEscape();

    void expandWindow();
    void collapseWindow();
    void checkCollapse();
    bool isExpanded() const { return m_expanded; }
    bool isAnimating() const;

    void showToast(const std::wstring& msg);
    void copyHandover();
    void checkReminders();
    void toggleAutoStartFromTray();

    float toDip(int px) const;
    int toPx(float dip) const;

private:
    HWND m_hwnd = nullptr;
    HWND m_edit = nullptr;
    EditTarget m_editTarget = ET_NONE;

    bool m_expanded = false;
    float m_expandT = 0.0f;         // 0.0 = 悬浮球, 1.0 = 展开面板
    int m_lastRgnState = -1;        // 0 = 球形区域, 1 = 完整面板区域
    float m_collapseCountdown = -1.0f;

    AppView m_view = VIEW_ALL;
    ModalMode m_modal = MODAL_NONE;
    bool m_isSysMenuOpen = false;

    std::vector<Project> m_projects;
    std::vector<Plan> m_plans;
    std::vector<HistoryItem> m_history;
    std::vector<ForestRecord> m_forestRecords;
    Config m_cfg;
    std::wstring m_lastSyncDate;

    std::vector<Hit> m_hits;
    std::wstring m_quickInput;
    std::wstring m_parsedPreview;

    float m_scrollAll = 0.0f, m_scrollModal = 0.0f;
    std::wstring m_pendingUuid;
    float m_pendingTimer = 0.0f;

    // 专注森林状态
    bool m_forestRunning = false;
    bool m_forestResting = false;
    bool m_forestWitheredAnim = false;
    float m_forestWitheredTimer = 0.0f;
    float m_forestRemain = 25.0f * 60.0f;
    std::wstring m_forestTaskUuid;
    std::wstring m_forestTaskTitle;

    // 编辑/新建计划弹窗状态
    bool m_dlgIsPlan = false;
    bool m_dlgIsNew = false;
    bool m_dlgIsPeriodicInst = false;
    std::wstring m_dlgTargetUuid;
    std::wstring m_dlgTitle;
    int m_dlgPriority = 0;
    bool m_dlgHasDue = false;
    std::wstring m_dlgDueDate;
    bool m_dlgHasTime = false;
    int m_dlgHour = 18;
    int m_dlgMinute = 0;
    std::wstring m_dlgRepeatRule; // "", "daily", "weekdays", "weekly", "monthly"
    std::vector<int> m_dlgRepeatDays;
    std::wstring m_dlgMonthDaysStr;

    // Toast
    std::wstring m_toastText;
    float m_toastTimer = 0.0f;

    void updateWindowRegion();
    void focusHiddenEdit(EditTarget target, const std::wstring& initText);
    void sortTodos();
    void commitQuickAdd();
    void handleTodoClick(const std::wstring& uuid);
    void completeTodoByUuid(const std::wstring& uuid);
    void postponeTodoByUuid(const std::wstring& uuid);
    void deleteTodoByUuid(const std::wstring& uuid);
    void openTaskEditor(const TodoItem* task, const Plan* plan, bool isPlan, bool isNew);
    void saveDialogEdit();

    void addHit(D2D1_RECT_F rc, HitType type, int index = 0);
    void drawBall(Gfx& g);
    void drawPanel(Gfx& g);
    void drawViewAll(Gfx& g, float top, float bot);
    void drawViewQuadrants(Gfx& g, float top, float bot);
    void drawViewPlans(Gfx& g, float top, float bot);
    void drawViewForest(Gfx& g, float top, float bot);
    void drawModal(Gfx& g);
    void drawToast(Gfx& g);
};

extern App* g_app;
