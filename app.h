#pragma once
#include <windows.h>
#include <d2d1.h>
#include <vector>
#include <string>
#include "storage.h"

struct Hit {
    D2D1_RECT_F rc;
    enum Type { H_PROJ_NAME, H_PROJ_DEL, H_TODO_CIRCLE, H_TODO_TEXT, H_NEWTODO, H_PROJ_CARD } type;
    int pi, ti;
};

struct FadeOut { int pi, ti; float alpha, off; };

enum EditMode { ED_NONE, ED_EDIT_PROJECT, ED_ADD_PROJECT, ED_NEW_TODO, ED_EDIT_TODO, ED_PREF_PREFIX };

class App {
public:
    void create(HWND hwnd);
    void destroy();
    void render();
    void tick(float dt);
    LRESULT onNcHitTest(POINT screenPt);
    void onMoving(RECT* r);
    void onSizing(RECT* r);
    void onMinMax(MINMAXINFO* mm);
    void onSize();
    void onWheel(int delta);
    void onChar(wchar_t ch);
    void onKeyDown(int vk);
    void onCompositionUpdate(const std::wstring& s);
    void onCompositionResult(const std::wstring& s);
    void onCompositionEnd();
    void onMouseMove(float x, float y);
    void onLButtonUp(float x, float y);
    void onLButtonDblClk(float x, float y);
    void onLButtonDown(float x, float y);
    void onEditChanged(bool userKey);
    void onEditReturn();
    void onEditEscape();
    void onEditKillFocus();

    bool isEditing() const { return m_editMode != ED_NONE; }
    void requestClose();
    void showFromTray(bool openSettingsPanel);
    void hideToTray();
    void exitFromTray();

private:
    HWND m_hwnd = nullptr;
    HWND m_edit = nullptr;
    int m_editPosX = -1, m_editPosY = -1, m_editPosW = -1, m_editPosH = -1;
    float m_w = 0, m_h = 0;
    std::vector<Project> m_projects;
    Config m_cfg;
    std::vector<Hit> m_hits;
    std::vector<D2D1_RECT_F> m_cardRects;
    float m_contentH = 0;
    float m_scroll = 0, m_scrollTarget = 0;
    float m_setScroll = 0, m_setScrollTarget = 0, m_setContentH = 0;
    float m_aboutScroll = 0, m_aboutScrollTarget = 0, m_aboutContentH = 0;
    float m_overlayAlpha = 0;
    float m_aboutAlpha = 0;
    float m_snapT = 0, m_closeT = 0, m_addT = 0, m_gearT = 0, m_projDelT = 0, m_circT = 0, m_checkT = 0, m_aboutBtnT = 0;
    float m_addSpin = 0, m_gearSpin = 0, m_projDelSpin = 0;
    bool m_hoverSnap = false, m_hoverClose = false, m_hoverAdd = false, m_hoverGear = false, m_hoverCheckbox = false, m_hoverAboutBtn = false;
    int m_hoverProjDel = -1;
    int m_hovCircPi = -1, m_hovCircTi = -2;
    bool m_suppressCircleHover = false;
    float m_lastMouseX = 0, m_lastMouseY = 0;
    bool m_settings = false, m_about = false, m_addingProject = false;
    EditMode m_editMode = ED_NONE;
    int m_editPi = -1, m_editTi = -1;
    std::wstring m_editText, m_compositionText;
    std::wstring m_lastEditLayout;
    D2D1_RECT_F m_editRectDip = {};
    float m_cursorBlink = 0;
    int m_lastCaretPos = -1;
    std::vector<std::wstring> m_cands;
    int m_candSel = -1;
    bool m_reentering = false;
    bool m_imeLegacy = false;
    float m_imeHideTimer = 0;
    float m_pollImeTimer = 0;
    std::vector<FadeOut> m_fades;
    bool m_closing = false;
    float m_closeAnim = 0;
    int m_closeFrames = 0;
    bool m_hidingToTray = false;
    float m_hideAnim = 0;
    bool m_draggingCard = false;
    int m_dragPi = -1, m_dragTarget = -1;
    float m_dragGrabOffset = 0, m_dragCardTop = 0;
    float m_dragCurrentContentY = 0, m_dragMouseContentY = 0;
    bool m_cardDropAnimating = false;
    int m_dropTarget = -1;
    float m_dropStartY = 0, m_dropEndY = 0, m_dropAnim = 0;
    bool m_snapping = false;
    int m_snapFromX = 0, m_snapFromY = 0, m_snapToX = 0, m_snapToY = 0;
    float m_snapAnim = 0, m_snapScale = 1.0f;
    bool m_snappedToTarget = false;
    bool m_snapRightArmed = true, m_snapLeftArmed = true, m_snapBottomArmed = true, m_snapTopArmed = true;
    float m_startupT = 0;
    bool m_startupRaised = false;

    // =====================================================
    // 新增：专注森林相关成员变量
    // =====================================================
    bool m_forestRunning = false;
    float m_forestRemainSecs = 0.0f;
    float m_forestTotalSecs = 0.0f;
    float m_forestElapsedSecs = 0.0f;
    bool m_forestDone = false;
    bool m_hoverForestBtn = false;
    float m_forestBtnT = 0.0f;

    float toDip(int px) const;
    int toPx(float dip) const;
    void approach(float& cur, float target, float dt, float speed);
    void requestRedraw();
    void clampScroll();
    void saveAll();
    void bringToFront();
    bool animating() const;
    float cardHeaderH() const;
    void applyCardTextSize();
    void ensureEditCreated();
    void hideImeWindows();
    bool detectLegacyIme();
    void refreshImeMode();
    std::wstring editGetText();
    void editSetText(const std::wstring& s);
    void beginEdit(EditMode mode, int pi, int ti, const std::wstring& initial);
    void startEdit(EditMode mode, int pi, int ti, const std::wstring& initial);
    void endEdit(bool applyFocus);
    float editUnderlineDip() const;
    int editTextHeightPx() const;
    void positionEdit();
    int editCaretPos() const;
    void setEditCaretFromPoint(float x, float y);
    void drawEditCaret(class Gfx& g, int fid, const std::wstring& text, float left, float top, float width, float height);
    void pollIme();
    void cancelEdit();
    void commitEdit();
    void addProjectCommit(const std::wstring& name);
    void focusNewTodo(int pi);
    void completeTodo(int pi, int ti);
    void delProject(int pi);
    void openSettings();
    void closeSettings();
    void openAbout();
    void closeAbout();
    void openHistory();
    // =====================================================
    // 新增：专注森林方法
    // =====================================================
    void openForest();
    
    float todoRowH(const std::wstring& text, float maxW);
    float rowHForTodo(int pi, int ti, float maxW);
    float todoCircleY(const std::wstring& text, float rowTop, float maxW);
    void rebuildHits();
    void beginCardDrag(int pi, float contentY);
    float drawProjectCard(class Gfx& g, int pi, float yContent, bool floating);
    D2D1_COLOR_F lerpColor(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) const;
};

extern App* g_app;
LRESULT CALLBACK EditSubproc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
