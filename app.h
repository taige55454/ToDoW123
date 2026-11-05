#pragma once
#include "gfx.h"
#include "storage.h"
#include <vector>
#include <string>
#include <map>

// 布局常量
namespace AppC {
constexpr float TITLE_H = 38.0f;
constexpr float BOT_H = 38.0f;
constexpr float RESIZE_M = 5.0f;
constexpr float CORNER_R = 8.0f;
constexpr float MIN_W = 240.0f;
constexpr float MIN_H = 320.0f;
constexpr float RATIO_H_W = 1.5f;
constexpr float CONTENT_PAD = 10.0f;
constexpr float CONTENT_TOP = 8.0f;
constexpr float CARD_GAP = 10.0f;
constexpr float CARD_INNER = 14.0f;
constexpr float CARD_TOP = 10.0f;
constexpr float ROW_H = 26.0f;
constexpr float CIRCLE_R = 7.0f;
constexpr float BADGE_H = 18.0f;
constexpr float BADGE_MIN_W = 22.0f;
constexpr float SNAP_GAP = 15.0f;
constexpr float SCROLL_STEP = 32.0f;
}

enum EditMode { 
    ED_NONE, 
    ED_ADD_PROJECT, 
    ED_EDIT_PROJECT, 
    ED_NEW_TODO, 
    ED_EDIT_TODO, 
    ED_PREF_PREFIX 
};

enum HitType {
    H_NONE, H_TITLEBAR, H_SNAP, H_CLOSE, H_ADD, H_GEAR,
    H_PROJ_NAME, H_PROJ_DEL, H_PROJ_CARD, 
    H_TODO_CIRCLE, H_TODO_TEXT, H_NEWTODO,
    H_OVERLAY_SHADE, H_SETTINGS_PANEL, 
    H_CHECKBOX, H_PREF_FIELD, 
    H_ABOUT_BTN, H_ABOUT_PANEL, H_ABOUT_CLOSE,
    // 新增：四象限、周期计划、专注森林
    H_QUADRANT_TAB, H_PLANS_TAB, H_FOREST_TAB,
    H_PLAN_ITEM, H_PLAN_ADD, H_PLAN_EDIT, H_PLAN_DELETE,
    H_QUADRANT_ITEM, H_FOREST_START, H_FOREST_STOP
};

struct Hit { 
    D2D1_RECT_F rc; 
    HitType type; 
    int pi = -1; 
    int ti = -1; 
    std::wstring data; // 用于存储额外数据（如 plan_uuid）
};

struct FadeTodo { 
    int pi; 
    int ti; 
    float alpha; 
    float off; 
};

// 自然语言解析结果
struct ParsedTask {
    std::wstring title;
    int priority = 0;
    std::wstring due_date;
    std::wstring due_time;
    std::wstring repeat_rule;
    std::vector<int> repeat_days;
    std::wstring base_date;
    std::vector<std::wstring> labels;
};

class App {
public:
    void create(HWND hwnd);
    void destroy();
    void tick(float dt);
    void render();
    void onSize();
    LRESULT onNcHitTest(POINT screenPt);
    void onMouseMove(float xdip, float ydip);
    void onLButtonDown(float xdip, float ydip);
    void onLButtonUp(float xdip, float ydip);
    void onLButtonDblClk(float xdip, float ydip);
    void onWheel(int delta);
    void onChar(wchar_t ch);
    void onKeyDown(int vk);
    void onMoving(RECT* r);
    void onSizing(RECT* r);
    void onMinMax(MINMAXINFO* mm);
    void onEditReturn();
    void onEditEscape();
    void onEditKillFocus();
    void onEditChanged(bool userKey);
    void onCompositionUpdate(const std::wstring& s);
    void onCompositionResult(const std::wstring& s);
    void onCompositionEnd();
    void hideImeWindows();
    void refreshImeMode();
    void showFromTray(bool openSettings);
    void hideToTray();
    void exitFromTray();
    void bringToFront();
    HWND hwnd() const { return m_hwnd; }
    HWND editHwnd() const { return m_edit; }

    float toDip(int px) const;
    int toPx(float dip) const;
    bool isClosing() const { return m_closing; }
    bool isEditing() const { return m_editMode != ED_NONE; }
    void positionEdit();
    int editCaretPos() const;
    void setEditCaretFromPoint(float x, float y);
    void drawEditCaret(Gfx& g, FontId fid, const std::wstring& text,
                       float left, float top, float width, float height);

private:
    HWND m_hwnd = nullptr;
    HWND m_edit = nullptr;
    WNDPROC m_editOldProc = nullptr;
    bool m_reentering = false;
    int m_editPosX = -1, m_editPosY = -1, m_editPosW = -1, m_editPosH = -1;
    float m_imeHideTimer = 0;
    float m_pollImeTimer = 0;
    std::vector<std::wstring> m_cands;
    int m_candSel = -1;
    bool m_imeLegacy = false;

    std::vector<Project> m_projects;
    std::vector<Plan> m_plans;        // ✅ 新增：周期计划列表
    std::vector<HistoryItem> m_history;
    std::vector<ForestRecord> m_forest; // ✅ 新增：森林记录
    Config m_cfg;

    float m_w = 0, m_h = 0;
    float m_scroll = 0, m_scrollTarget = 0, m_contentH = 0;

    EditMode m_editMode = ED_NONE;
    int m_editPi = -1, m_editTi = -1;
    bool m_addingProject = false;
    
    // ✅ 新增：视图切换状态
    enum ViewMode { VIEW_ALL, VIEW_QUADRANTS, VIEW_PLANS, VIEW_FOREST };
    ViewMode m_currentView = VIEW_ALL;

    // ✅ 新增：专注森林状态
    bool m_forestRunning = false;
    float m_forestRemainingSecs = 0;
    float m_forestTotalSecs = 1500; // 25 分钟
    std::wstring m_forestTaskUuid;
    
    bool m_settings = false;
    float m_setScroll = 0, m_setScrollTarget = 0, m_setContentH = 0;
    float m_aboutScroll = 0, m_aboutScrollTarget = 0, m_aboutContentH = 0;
    bool m_about = false;
    bool m_closing = false;
    std::wstring m_editText;
    std::wstring m_compositionText;
    std::wstring m_lastEditLayout;
    float m_cursorBlink = 0;
    int m_lastCaretPos = -1;
    bool m_hidingToTray = false;
    float m_hideAnim = 0;
    float m_closeAnim = 0;
    int m_closeFrames = 0;
    float m_startupT = 0;
    bool m_startupRaised = false;
    bool m_snapping = false;
    bool m_snappedToTarget = false;
    float m_snapScale = 1.0f;
    float m_snapAnim = 0;
    int m_snapFromX = 0, m_snapFromY = 0, m_snapToX = 0, m_snapToY = 0;
    bool m_snapRightArmed = false;
    bool m_snapBottomArmed = false;
    bool m_snapLeftArmed = true;
    bool m_snapTopArmed = true;
    float m_overlayAlpha = 0;
    float m_aboutAlpha = 0;

    int m_hoverSnap = 0; float m_snapT = 0;
    int m_hoverClose = 0; float m_closeT = 0;
    int m_hoverAdd = 0; float m_addT = 0;
    int m_hoverGear = 0; float m_gearT = 0;
    float m_addSpin = 0;
    float m_gearSpin = 0;
    int m_hoverProjDel = -1; float m_projDelT = 0;
    float m_projDelSpin = 0;
    int m_hovCircPi = -1, m_hovCircTi = -2; float m_circT = 0;
    bool m_suppressCircleHover = false;
    float m_lastMouseX = -1, m_lastMouseY = -1;
    int m_hoverCheckbox = 0; float m_checkT = 0;
    int m_hoverAboutBtn = 0; float m_aboutBtnT = 0;

    std::vector<FadeTodo> m_fades;
    std::vector<Hit> m_hits;
    std::vector<D2D1_RECT_F> m_cardRects;
    D2D1_RECT_F m_editRectDip = {};
    bool m_draggingCard = false;
    int m_dragPi = -1;
    int m_dragTarget = -1;
    float m_dragGrabOffset = 0;
    float m_dragCardTop = 0;
    float m_dragCurrentContentY = 0;
    float m_dragMouseContentY = 0;
    bool m_cardDropAnimating = false;
    float m_dropAnim = 0;
    float m_dropStartY = 0;
    float m_dropEndY = 0;
    int m_dropTarget = -1;

    // ✅ 新增：自然语言解析
    ParsedTask parseQuickInput(const std::wstring& input);
    int computeQuadrant(const TodoItem& todo);
    void syncRepeatPlans();
    std::wstring getRepeatRuleDisplay(const std::wstring& rule, const std::vector<int>& days);
    
    void rebuildHits();
    void beginCardDrag(int pi, float contentY);
    float drawProjectCard(Gfx& g, int pi, float yContent, bool floating);
    void pollIme();
    bool detectLegacyIme();
    float todoRowH(const std::wstring& text, float maxW);
    float rowHForTodo(int pi, int ti, float maxW);
    float todoCircleY(const std::wstring& text, float rowTop, float maxW);
    float cardHeaderH() const;
    void applyCardTextSize();
    float editUnderlineDip() const;
    int editTextHeightPx() const;
    void openHistory();
    void beginEdit(EditMode mode, int pi, int ti, const std::wstring& initial);
    void startEdit(EditMode mode, int pi, int ti, const std::wstring& initial);
    void endEdit(bool applyFocus);
    void ensureEditCreated();
    std::wstring editGetText();
    void editSetText(const std::wstring& s);
    void commitEdit();
    void cancelEdit();
    void addProjectCommit(const std::wstring& name);
    void focusNewTodo(int pi);
    void completeTodo(int pi, int ti);
    void delProject(int pi);
    void openSettings();
    void closeSettings();
    void openAbout();
    void closeAbout();
    void requestClose();
    void requestRedraw();
    bool animating() const;
    float lerp(float a, float b, float t) const { return a + (b - a) * t; }
    void approach(float& cur, float target, float dt, float speed);
    D2D1_COLOR_F lerpColor(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) const;
    void clampScroll();
    void saveAll();
};

extern App* g_app;
LRESULT CALLBACK EditSubproc(HWND, UINT, WPARAM, LPARAM);
