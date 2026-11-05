#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include "app.h"
#include "gfx.h"
#include <cmath>
#include <algorithm>
#include <map>
#include <set>

App* g_app = nullptr;
static WNDPROC g_origEditProc = nullptr;

float App::toDip(int px) const { return g_gfx.dpiScale > 0 ? px / g_gfx.dpiScale : (float)px; }
int App::toPx(float dip) const { return (int)(dip * g_gfx.dpiScale + 0.5f); }

// ============================================================
// 完整复刻 Python 版 QuickAddParser 自然语言解析器
// ============================================================
struct ParsedTaskInfo {
    std::wstring title;
    int priority = 0;
    std::wstring due_date;
    std::wstring due_time;
    std::wstring repeat_rule;
    std::vector<int> repeat_days;
    std::wstring preview;
};

static int cn_to_num(const std::wstring& s) {
    if (s.empty()) return 0;
    bool allDigit = true;
    for (wchar_t c : s) if (c < L'0' || c > L'9') { allDigit = false; break; }
    if (allDigit) return _wtoi(s.c_str());

    auto mapChar = [](wchar_t c) -> int {
        switch (c) {
            case L'一': return 1; case L'二': case L'两': return 2;
            case L'三': return 3; case L'四': return 4; case L'五': return 5;
            case L'六': return 6; case L'七': return 7; case L'八': return 8;
            case L'九': return 9; case L'十': return 10; default: return 0;
        }
    };
    if (s.size() == 1) return mapChar(s[0]);
    if (s.size() == 2) {
        if (s[0] == L'十') return 10 + mapChar(s[1]);
        if (s[1] == L'十') return mapChar(s[0]) * 10;
    }
    if (s.size() == 3 && s[1] == L'十') return mapChar(s[0]) * 10 + mapChar(s[2]);
    return 0;
}

static ParsedTaskInfo parse_quick_input(const std::wstring& raw) {
    ParsedTaskInfo res;
    std::wstring text = raw;
    while (!text.empty() && text.front() == L' ') text.erase(0, 1);
    while (!text.empty() && text.back() == L' ') text.pop_back();
    if (text.empty()) return res;

    std::vector<std::wstring> labels;

    // 1. 优先级 (! / ！ / 重要)
    if (text[0] == L'!' || text[0] == L'！') {
        res.priority = 1;
        labels.push_back(L"⭐ 重要");
        text.erase(0, 1);
    } else {
        size_t p = text.find(L"重要");
        if (p != std::wstring::npos) {
            res.priority = 1;
            labels.push_back(L"⭐ 重要");
            text.erase(p, 2);
        }
    }

    // 2. 日期词
    std::wstring tdate = today_str();
    if (text.find(L"大后天") != std::wstring::npos) {
        res.due_date = add_days_str(tdate, 3);
        labels.push_back(L"大后天");
        text.erase(text.find(L"大后天"), 3);
    } else if (text.find(L"后天") != std::wstring::npos) {
        res.due_date = add_days_str(tdate, 2);
        labels.push_back(L"后天");
        text.erase(text.find(L"后天"), 2);
    } else if (text.find(L"明天") != std::wstring::npos) {
        res.due_date = add_days_str(tdate, 1);
        labels.push_back(L"明天");
        text.erase(text.find(L"明天"), 2);
    } else if (text.find(L"今天") != std::wstring::npos) {
        res.due_date = tdate;
        labels.push_back(L"今天");
        text.erase(text.find(L"今天"), 2);
    }

    // 3. 周期词 (每天 / 工作日 / 每周... / 每月...号)
    if (text.find(L"每天") != std::wstring::npos) {
        res.repeat_rule = L"daily";
        labels.push_back(L"🔁 每天");
        text.erase(text.find(L"每天"), 2);
    } else if (text.find(L"工作日") != std::wstring::npos) {
        res.repeat_rule = L"weekdays";
        labels.push_back(L"🔁 工作日");
        text.erase(text.find(L"工作日"), 3);
    } else if (text.find(L"每周") != std::wstring::npos) {
        size_t p = text.find(L"每周");
        res.repeat_rule = L"weekly";
        size_t endP = p + 2;
        std::set<int> wdays;
        while (endP < text.size()) {
            wchar_t c = text[endP];
            int wd = -1;
            if (c == L'一') wd = 0; else if (c == L'二' || c == L'两') wd = 1;
            else if (c == L'三') wd = 2; else if (c == L'四') wd = 3;
            else if (c == L'五') wd = 4; else if (c == L'六') wd = 5;
            else if (c == L'日' || c == L'天') wd = 6;
            if (wd >= 0) { wdays.insert(wd); endP++; }
            else if (c == L'、' || c == L',' || c == L'，' || c == L'和') { endP++; }
            else break;
        }
        if (wdays.empty()) wdays.insert(0);
        res.repeat_days.assign(wdays.begin(), wdays.end());
        labels.push_back(L"🔁 " + format_rule_display(L"weekly", res.repeat_days));
        text.erase(p, endP - p);
        if (res.due_date.empty()) res.due_date = tdate;
    } else if (text.find(L"每月") != std::wstring::npos) {
        size_t p = text.find(L"每月");
        size_t endHao = text.find_first_of(L"号日", p + 2);
        if (endHao != std::wstring::npos) {
            res.repeat_rule = L"monthly";
            std::wstring sub = text.substr(p + 2, endHao - (p + 2));
            std::set<int> mdays;
            std::wstring curNum;
            for (wchar_t c : sub) {
                if (c >= L'0' && c <= L'9') curNum += c;
                else if (!curNum.empty()) {
                    int v = _wtoi(curNum.c_str());
                    if (v >= 1 && v <= 31) mdays.insert(v);
                    curNum.clear();
                }
            }
            if (!curNum.empty()) {
                int v = _wtoi(curNum.c_str());
                if (v >= 1 && v <= 31) mdays.insert(v);
            }
            if (mdays.empty()) mdays.insert(1);
            res.repeat_days.assign(mdays.begin(), mdays.end());
            labels.push_back(L"🔁 " + format_rule_display(L"monthly", res.repeat_days));
            text.erase(p, endHao - p + 1);
            if (res.due_date.empty()) res.due_date = tdate;
        }
    }

    // 4. 时刻解析 (HH:MM 或 下午3点半)
    for (size_t i = 0; i + 4 <= text.size(); ++i) {
        if (text[i] >= L'0' && text[i] <= L'2' && (text[i+1] == L':' || text[i+1] == L'：' || (i + 4 < text.size() && (text[i+2] == L':' || text[i+2] == L'：')))) {
            size_t colon = (text[i+1] == L':' || text[i+1] == L'：') ? (i + 1) : (i + 2);
            if (colon + 2 < text.size() && text[colon+1] >= L'0' && text[colon+1] <= L'5' && text[colon+2] >= L'0' && text[colon+2] <= L'9') {
                int hh = _wtoi(text.substr(i, colon - i).c_str());
                int mm = _wtoi(text.substr(colon + 1, 2).c_str());
                if (hh >= 0 && hh < 24) {
                    wchar_t tb[16]; swprintf_s(tb, L"%02d:%02d", hh, mm);
                    res.due_time = tb;
                    labels.push_back(L"🕒 " + res.due_time);
                    text.erase(i, (colon + 3) - i);
                    break;
                }
            }
        }
    }
    if (res.due_time.empty()) {
        size_t dian = text.find(L"点");
        if (dian != std::wstring::npos && dian > 0) {
            size_t start = dian;
            while (start > 0 && (iswdigit(text[start-1]) || std::wstring(L"零一二两三四五六七八九十").find(text[start-1]) != std::wstring::npos)) start--;
            if (start < dian) {
                int hh = cn_to_num(text.substr(start, dian - start));
                int mm = 0;
                size_t endPos = dian + 1;
                if (endPos < text.size() && text[endPos] == L'半') { mm = 30; endPos++; }
                if (start >= 2) {
                    std::wstring ap = text.substr(start - 2, 2);
                    if (ap == L"下午" || ap == L"晚上") { if (hh < 12) hh += 12; start -= 2; }
                    else if (ap == L"上午" || ap == L"早上") { if (hh == 12) hh = 0; start -= 2; }
                }
                wchar_t tb[16]; swprintf_s(tb, L"%02d:%02d", hh % 24, mm);
                res.due_time = tb;
                labels.push_back(L"🕒 " + res.due_time);
                text.erase(start, endPos - start);
            }
        }
    }

    if (!res.due_time.empty() && res.due_date.empty()) res.due_date = tdate;

    while (!text.empty() && text.front() == L' ') text.erase(0, 1);
    while (!text.empty() && text.back() == L' ') text.pop_back();
    res.title = text.empty() ? raw : text;
    if (text.empty()) { labels.clear(); res.priority = 0; }

    for (size_t i = 0; i < labels.size(); ++i) {
        if (i > 0) res.preview += L" | ";
        res.preview += labels[i];
    }
    return res;
}

static int compute_quadrant(const TodoItem& t) {
    bool imp = (t.priority == 1);
    bool urg = false;
    std::wstring today = today_str();
    if (!t.due_date.empty() && t.due_date <= today) urg = true;
    else if (!t.due_time.empty() && (t.due_date.empty() || t.due_date == today)) urg = true;
    if (imp && urg) return 1;
    if (imp && !urg) return 2;
    if (!imp && urg) return 3;
    return 4;
}

static LRESULT CALLBACK SubEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) { if (g_app) g_app->onEditReturn(); return 0; }
        if (wp == VK_ESCAPE) { if (g_app) g_app->onEditEscape(); return 0; }
    } else if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        return 0;
    }
    LRESULT r = CallWindowProcW(g_origEditProc, h, msg, wp, lp);
    if (msg == WM_CHAR || msg == WM_KEYUP || msg == WM_IME_ENDCOMPOSITION || msg == WM_PASTE) {
        if (g_app) g_app->onEditTextChanged();
    }
    return r;
}

void App::create(HWND hwnd) {
    g_app = this;
    m_hwnd = hwnd;
    m_cfg = load_config();
    m_projects = load_projects();
    m_plans = load_plans();
    m_history = load_history();
    m_forestRecords = load_forest();
    m_lastSyncDate = today_str();

    if (sync_repeat_plans(m_projects, m_plans, m_history)) {
        save_projects(m_projects);
        save_plans(m_plans);
    }
    sortTodos();
    m_forestRemain = (float)(m_cfg.focus_minutes * 60);

    // 创建隐藏输入控件以完美支持中文输入法与退格
    m_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                             -200, -200, 100, 24, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_origEditProc = (WNDPROC)SetWindowLongPtrW(m_edit, GWLP_WNDPROC, (LONG_PTR)SubEditProc);

    updateWindowRegion();
    focusHiddenEdit(ET_QUICK_INPUT, L"");
}

void App::destroy() {
    save_projects(m_projects);
    save_plans(m_plans);
    save_config(m_cfg);
}

void App::focusHiddenEdit(EditTarget target, const std::wstring& initText) {
    m_editTarget = target;
    if (!m_edit) return;
    SetWindowTextW(m_edit, initText.c_str());
    int len = (int)initText.size();
    SendMessageW(m_edit, EM_SETSEL, len, len);
    if (m_expanded) SetFocus(m_edit);
}

void App::onEditTextChanged() {
    if (!m_edit) return;
    int len = GetWindowTextLengthW(m_edit);
    std::wstring s(len, L'\0');
    if (len > 0) GetWindowTextW(m_edit, &s[0], len + 1);

    if (m_editTarget == ET_QUICK_INPUT) {
        m_quickInput = s;
        auto parsed = parse_quick_input(s);
        m_parsedPreview = parsed.preview;
    } else if (m_editTarget == ET_DLG_TITLE) {
        m_dlgTitle = s;
    } else if (m_editTarget == ET_DLG_MONTH_DAYS) {
        m_dlgMonthDaysStr = s;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::onEditReturn() {
    if (m_editTarget == ET_QUICK_INPUT && m_modal == MODAL_NONE) {
        commitQuickAdd();
    } else if (m_modal == MODAL_EDIT_TASK || m_modal == MODAL_EDIT_PLAN) {
        saveDialogEdit();
    }
}

void App::onEditEscape() {
    if (m_modal != MODAL_NONE) {
        m_modal = MODAL_NONE;
        focusHiddenEdit(ET_QUICK_INPUT, m_quickInput);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else {
        collapseWindow();
    }
}

void App::sortTodos() {
    if (m_projects.empty()) return;
    std::sort(m_projects[0].todos.begin(), m_projects[0].todos.end(), [](const TodoItem& a, const TodoItem& b) {
        std::wstring da = a.due_date.empty() ? L"9999-12-31" : a.due_date;
        std::wstring db = b.due_date.empty() ? L"9999-12-31" : b.due_date;
        if (da != db) return da < db;
        std::wstring ta = a.due_time.empty() ? L"23:59" : a.due_time;
        std::wstring tb = b.due_time.empty() ? L"23:59" : b.due_time;
        return ta < tb;
    });
}

void App::updateWindowRegion() {
    // 关键性能优化：窗口固定在屏幕右上角 360x640，折叠时将窗口响应区域裁剪为右上角 60x60 圆球！
    // 这样既不遮挡桌面其他区域的鼠标点击，又完全避免了每次展开调用 SetWindowPos 引发的卡顿！
    int targetState = (m_expandT > 0.05f) ? 1 : 0;
    if (targetState == m_lastRgnState) return;
    m_lastRgnState = targetState;

    if (targetState == 0) {
        int bx1 = toPx(AppC::PANEL_W - AppC::BALL_SIZE);
        int by1 = 0;
        int bx2 = toPx(AppC::PANEL_W);
        int by2 = toPx(AppC::BALL_SIZE);
        HRGN rgn = CreateEllipticRgn(bx1, by1, bx2 + 1, by2 + 1);
        SetWindowRgn(m_hwnd, rgn, TRUE);
    } else {
        HRGN rgn = CreateRoundRectRgn(0, 0, toPx(AppC::PANEL_W) + 1, toPx(AppC::PANEL_H) + 1, toPx(16), toPx(16));
        SetWindowRgn(m_hwnd, rgn, TRUE);
    }
}

void App::expandWindow() {
    m_collapseCountdown = -1.0f;
    if (!m_expanded) {
        m_expanded = true;
        updateWindowRegion();
        SetForegroundWindow(m_hwnd);
        if (m_modal == MODAL_NONE) focusHiddenEdit(ET_QUICK_INPUT, m_quickInput);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void App::collapseWindow() {
    if (m_modal != MODAL_NONE || m_isSysMenuOpen) return;
    m_expanded = false;
    m_collapseCountdown = -1.0f;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::checkCollapse() {
    if (m_modal != MODAL_NONE || m_isSysMenuOpen) return;
    if (!m_quickInput.empty()) return;
    POINT pt; GetCursorPos(&pt);
    RECT rc; GetWindowRect(m_hwnd, &rc);
    if (!PtInRect(&rc, pt)) {
        collapseWindow();
    }
}

bool App::isAnimating() const {
    if (m_expanded && m_expandT < 0.999f) return true;
    if (!m_expanded && m_expandT > 0.001f) return true;
    if (m_collapseCountdown > 0.0f) return true;
    if (m_toastTimer > 0.0f) return true;
    if (!m_pendingUuid.empty()) return true;
    if (m_forestWitheredAnim) return true;
    return false;
}

void App::tick(float dt) {
    bool needRedraw = false;

    // 1. 丝滑展开/收起插值 (180ms 动画)
    float targetT = m_expanded ? 1.0f : 0.0f;
    if (std::abs(m_expandT - targetT) > 0.001f) {
        float step = dt / 0.16f;
        if (m_expanded) m_expandT = std::min(1.0f, m_expandT + step);
        else m_expandT = std::max(0.0f, m_expandT - step);
        updateWindowRegion();
        needRedraw = true;
    }

    // 2. 鼠标检测与 400ms 延迟收缩
    POINT pt; GetCursorPos(&pt);
    RECT winRc; GetWindowRect(m_hwnd, &winRc);
    bool inside = false;
    if (m_expanded) {
        inside = PtInRect(&winRc, pt) != FALSE;
    } else {
        RECT ballRc = { winRc.right - toPx(AppC::BALL_SIZE), winRc.top, winRc.right, winRc.top + toPx(AppC::BALL_SIZE) };
        inside = PtInRect(&ballRc, pt) != FALSE;
    }

    if (inside) {
        m_collapseCountdown = -1.0f;
        if (!m_expanded) expandWindow();
    } else if (m_expanded && m_modal == MODAL_NONE && !m_isSysMenuOpen && m_quickInput.empty()) {
        if (m_collapseCountdown < 0.0f) m_collapseCountdown = 0.40f;
        else {
            m_collapseCountdown -= dt;
            if (m_collapseCountdown <= 0.0f) {
                collapseWindow();
                needRedraw = true;
            }
        }
    }

    // 3. 番茄钟计时器 (每秒精确递减)
    if (m_forestRunning) {
        int prevSec = (int)std::ceil(m_forestRemain);
        m_forestRemain -= dt;
        int curSec = (int)std::ceil(m_forestRemain);
        if (curSec != prevSec) needRedraw = true;

        if (m_forestRemain <= 0.0f) {
            m_forestRunning = false;
            if (!m_forestResting) {
                // 专注成功！记录健康大树并弹出沉浸式大弹窗
                ForestRecord rec;
                rec.timestamp = now_iso();
                rec.status = L"healthy";
                rec.task = m_forestTaskTitle.empty() ? L"自由专注" : m_forestTaskTitle;
                rec.minutes = m_cfg.focus_minutes;
                m_forestRecords.push_back(rec);
                save_forest(m_forestRecords);

                if (!m_forestTaskUuid.empty()) {
                    completeTodoByUuid(m_forestTaskUuid);
                }
                expandWindow();
                m_modal = MODAL_FOCUS_SUCCESS;
                needRedraw = true;
            } else {
                // 休息结束，复位
                m_forestResting = false;
                m_forestRemain = (float)(m_cfg.focus_minutes * 60);
                showToast(L"☕ 休息结束，满血复活！");
                needRedraw = true;
            }
        }
    }

    // 4. 枯萎动画复位
    if (m_forestWitheredAnim) {
        m_forestWitheredTimer -= dt;
        if (m_forestWitheredTimer <= 0.0f) {
            m_forestWitheredAnim = false;
            m_forestRemain = (float)(m_cfg.focus_minutes * 60);
            needRedraw = true;
        }
    }

    // 5. 待确认完成 2.5s 自动回退
    if (!m_pendingUuid.empty()) {
        m_pendingTimer -= dt;
        if (m_pendingTimer <= 0.0f) {
            m_pendingUuid.clear();
            needRedraw = true;
        }
    }

    // 6. Toast 消失
    if (m_toastTimer > 0.0f) {
        m_toastTimer -= dt;
        needRedraw = true;
    }

    if (needRedraw) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void App::addHit(D2D1_RECT_F rc, HitType type, int index) {
    m_hits.push_back({rc, type, index});
}

void App::render() {
    Gfx& g = g_gfx;
    g.rt->BeginDraw();
    g.rt->Clear(D2D1::ColorF(0, 0, 0, 0));

    m_hits.clear();

    if (m_expandT < 0.98f) {
        drawBall(g);
    }
    if (m_expandT > 0.02f) {
        // 右上角丝滑缩放矩阵动画
        float scale = 0.2f + 0.8f * (1.0f - (1.0f - m_expandT) * (1.0f - m_expandT));
        D2D1_POINT_2F center = D2D1::Point2F(AppC::PANEL_W - 30.0f, 30.0f);
        g.rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, center));
        drawPanel(g);
        if (m_modal != MODAL_NONE) {
            drawModal(g);
        }
        g.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    if (m_toastTimer > 0.0f) {
        drawToast(g);
    }

    if (g.rt->EndDraw() == D2DERR_RECREATE_TARGET) {
        g_gfx.recreate(m_hwnd);
    }
}

void App::drawBall(Gfx& g) {
    float alpha = 1.0f - m_expandT;
    float bx = AppC::PANEL_W - AppC::BALL_SIZE;
    float by = 0.0f;
    float cx = bx + AppC::BALL_SIZE * 0.5f;
    float cy = by + AppC::BALL_SIZE * 0.5f;
    float r = AppC::BALL_SIZE * 0.5f - 2.0f;

    g.fillEllipse(cx, cy, r, r, D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.72f * alpha));
    g.drawEllipse(cx, cy, r, r, D2D1::ColorF(1.0f, 0.84f, 0.0f, 0.85f * alpha), 2.0f);

    const wchar_t* icon = L"🥚";
    if (m_forestWitheredAnim) icon = L"🥀";
    else if (m_forestResting) icon = L"☕";
    else if (m_forestRunning) {
        float total = (float)(m_cfg.focus_minutes * 60);
        float ratio = (total - m_forestRemain) / std::max(1.0f, total);
        icon = (ratio < 0.25f) ? L"🌰" : (ratio < 0.6f) ? L"🌱" : (ratio < 0.9f) ? L"🌿" : L"🌲";
    }

    if (m_forestRunning || m_forestResting) {
        g.drawText(icon, D2D1::RectF(bx, by + 4, bx + AppC::BALL_SIZE, by + 32), F_SYM_TITLE,
                   D2D1::ColorF(1, 1, 1, alpha), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        int rem = (int)std::ceil(m_forestRemain);
        wchar_t tbuf[16]; swprintf_s(tbuf, L"%02d:%02d", rem / 60, rem % 60);
        g.drawText(tbuf, D2D1::RectF(bx, by + 30, bx + AppC::BALL_SIZE, by + 54), F_HINT,
                   D2D1::ColorF(1.0f, 0.84f, 0.31f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    } else {
        g.drawText(icon, D2D1::RectF(bx, by, bx + AppC::BALL_SIZE, by + AppC::BALL_SIZE), F_SYM_BOTTOM,
                   D2D1::ColorF(1, 1, 1, alpha), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    addHit(D2D1::RectF(bx, by, bx + AppC::BALL_SIZE, by + AppC::BALL_SIZE), H_BALL);
}

void App::drawPanel(Gfx& g) {
    float W = AppC::PANEL_W, H = AppC::PANEL_H;
    D2D1_RECT_F panelRc = D2D1::RectF(1, 1, W - 1, H - 1);
    g.fillRoundedRect(panelRc, 14.0f, Hex(0xFAFAFC, 0.98f));
    g.strokeRoundedRect(panelRc, 14.0f, Hex(0xB4B4B4, 0.85f), 1.2f);

    // 1. 顶部栏：标题 + 4个功能按钮（带热区绑定）
    g.drawText(m_cfg.title_prefix, D2D1::RectF(14, 10, 120, 38), F_PROJ_NAME, Hex(0x222222));

    struct TopBtn { const wchar_t* label; HitType ht; float x; float w; };
    TopBtn topBtns[] = {
        { L"🔄刷新", H_BTN_REFRESH, 148, 48 },
        { L"📋交接", H_BTN_HANDOVER, 198, 48 },
        { L"📚历史", H_BTN_HISTORY, 248, 48 },
        { L"📜森林", H_BTN_FOREST_STATS, 298, 50 }
    };
    for (const auto& b : topBtns) {
        D2D1_RECT_F rc = D2D1::RectF(b.x, 10, b.x + b.w, 36);
        g.drawText(b.label, rc, F_HINT, Hex(0x444444), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (m_modal == MODAL_NONE) addHit(rc, b.ht);
    }

    // 2. 四视图导航 Tab
    float navY = AppC::TITLE_H + 2.0f;
    float tabW = (W - 24.0f - 24.0f) / 4.0f;
    const wchar_t* tabLabels[] = { L"📋 当前任务", L"🎯 象限", L"🗓️ 周期", L"🌲 森林" };
    HitType tabHits[] = { H_NAV_ALL, H_NAV_QUAD, H_NAV_PLANS, H_NAV_FOREST };
    for (int i = 0; i < 4; ++i) {
        float x = 12.0f + i * (tabW + 8.0f);
        D2D1_RECT_F rc = D2D1::RectF(x, navY, x + tabW, navY + 28.0f);
        bool sel = ((int)m_view == i);
        g.fillRoundedRect(rc, 6.0f, sel ? Hex(0x2E7D32) : Hex(0xE0E0E0));
        g.drawText(tabLabels[i], rc, F_HINT, sel ? C::WHITE : Hex(0x555555),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (m_modal == MODAL_NONE) addHit(rc, tabHits[i]);
    }

    // 3. 中间内容视图区
    float cTop = navY + 36.0f;
    float cBot = H - AppC::INPUT_H - 10.0f;
    g.rt->PushAxisAlignedClip(D2D1::RectF(8, cTop, W - 8, cBot), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (m_view == VIEW_ALL) drawViewAll(g, cTop, cBot);
    else if (m_view == VIEW_QUADRANTS) drawViewQuadrants(g, cTop, cBot);
    else if (m_view == VIEW_PLANS) drawViewPlans(g, cTop, cBot);
    else if (m_view == VIEW_FOREST) drawViewForest(g, cTop, cBot);

    g.rt->PopAxisAlignedClip();

    // 4. 底部智能语法预览 + 快速输入框
    float inpY = H - 46.0f;
    if (!m_parsedPreview.empty()) {
        g.drawText(L"智能识别: " + m_parsedPreview, D2D1::RectF(14, inpY - 18, W - 14, inpY - 2), F_FOOTER, Hex(0x2E7D32));
    }
    D2D1_RECT_F inpRc = D2D1::RectF(12, inpY, W - 12, inpY + 34);
    g.fillRoundedRect(inpRc, 6.0f, C::WHITE);
    g.strokeRoundedRect(inpRc, 6.0f, (m_editTarget == ET_QUICK_INPUT && m_modal == MODAL_NONE) ? Hex(0x2E7D32) : Hex(0xBDBDBD), 1.2f);

    if (m_quickInput.empty()) {
        g.drawText(L"💡 快速新增 (如: ! 每月1号, 15号交租)", D2D1::RectF(18, inpY, W - 18, inpY + 34), F_HINT, Hex(0x888888));
    } else {
        g.drawText(m_quickInput + L"|", D2D1::RectF(18, inpY, W - 18, inpY + 34), F_TODO, Hex(0x222222));
    }
    if (m_modal == MODAL_NONE) addHit(inpRc, H_INPUT_BOX);
}

void App::drawViewAll(Gfx& g, float top, float bot) {
    if (m_projects.empty()) return;
    float y = top + 2.0f - m_scrollAll;
    std::wstring tdate = today_str();
    std::wstring ttime = now_time_str();

    int visibleCount = 0;
    for (size_t i = 0; i < m_projects[0].todos.size(); ++i) {
        const auto& t = m_projects[0].todos[i];
        if (!t.hide_until.empty() && tdate < t.hide_until) continue;
        visibleCount++;

        if (y + AppC::ROW_H >= top && y <= bot) {
            D2D1_RECT_F rowRc = D2D1::RectF(12, y, AppC::PANEL_W - 12, y + AppC::ROW_H - 4);
            g.fillRoundedRect(rowRc, 6.0f, C::WHITE);
            g.drawLine(16, y + AppC::ROW_H - 4, AppC::PANEL_W - 16, y + AppC::ROW_H - 4, Hex(0xEEEEEE), 1.0f);

            bool isPending = (t.uuid == m_pendingUuid);
            g.drawText(isPending ? L"✔️" : L"◯", D2D1::RectF(16, y, 38, y + AppC::ROW_H - 4), F_SYM_TITLE,
                       isPending ? Hex(0x1565C0) : Hex(0x888888),
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            std::wstring titleText = isPending ? (L"【再次点击确认完成】 " + t.text) : t.text;
            g.drawText(titleText, D2D1::RectF(40, y + 4, AppC::PANEL_W - 115, y + AppC::ROW_H - 8), F_TODO,
                       isPending ? Hex(0x1565C0) : Hex(0x333333));

            // 右侧双行徽标（逾期警示 + 日期 + 时间 + 周期）
            bool overdue = false;
            if (!t.due_date.empty()) {
                if (t.due_date < tdate) overdue = true;
                else if (t.due_date == tdate && !t.due_time.empty() && t.due_time < ttime) overdue = true;
            }

            float rightEdge = AppC::PANEL_W - 16.0f;
            // 第一行徽标：日期 + 重要星号
            if (!t.due_date.empty()) {
                std::wstring dlabel = overdue ? L"⚠️逾期 📅" : L"📅";
                if (!t.original_due.empty() && t.original_due != t.due_date)
                    dlabel += (t.original_due.size() >= 10 ? t.original_due.substr(5) : t.original_due) + L"(延)";
                else
                    dlabel += (t.due_date.size() >= 10 ? t.due_date.substr(5) : t.due_date);

                float bw = g.measureTextW(dlabel, F_FOOTER) + 10.0f;
                D2D1_RECT_F bRc = D2D1::RectF(rightEdge - bw, y + 6, rightEdge, y + 23);
                g.fillRoundedRect(bRc, 4.0f, overdue ? Hex(0xFFEBEE) : Hex(0xE3F2FD));
                g.drawText(dlabel, bRc, F_FOOTER, overdue ? Hex(0xD32F2F) : Hex(0x1565C0),
                           DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                rightEdge -= (bw + 4.0f);
            }
            if (t.priority == 1) {
                D2D1_RECT_F sRc = D2D1::RectF(rightEdge - 20.0f, y + 6, rightEdge, y + 23);
                g.fillRoundedRect(sRc, 4.0f, Hex(0xFFF8E1));
                g.drawText(L"⭐", sRc, F_FOOTER, Hex(0xF57F17), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }

            // 第二行徽标：周期 + 时刻
            float rightEdge2 = AppC::PANEL_W - 16.0f;
            if (!t.repeat_rule.empty()) {
                std::wstring rdisp = L"🔁" + format_rule_display(t.repeat_rule, t.repeat_days);
                float bw = g.measureTextW(rdisp, F_FOOTER) + 10.0f;
                D2D1_RECT_F bRc = D2D1::RectF(rightEdge2 - bw, y + 26, rightEdge2, y + 43);
                g.fillRoundedRect(bRc, 4.0f, Hex(0xE8F5E9));
                g.drawText(rdisp, bRc, F_FOOTER, Hex(0x2E7D32), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                rightEdge2 -= (bw + 4.0f);
            }
            if (!t.due_time.empty()) {
                std::wstring tdisp = L"🕒" + t.due_time;
                float bw = g.measureTextW(tdisp, F_FOOTER) + 10.0f;
                D2D1_RECT_F bRc = D2D1::RectF(rightEdge2 - bw, y + 26, rightEdge2, y + 43);
                g.fillRoundedRect(bRc, 4.0f, Hex(0xE8F5E9));
                g.drawText(tdisp, bRc, F_FOOTER, Hex(0x2E7D32), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }

            if (m_modal == MODAL_NONE) addHit(rowRc, H_TODO_ITEM, (int)i);
        }
        y += AppC::ROW_H;
    }

    if (visibleCount == 0) {
        g.drawText(L"🎉 当前暂无待办事项，在下方输入框快速添加吧！",
                   D2D1::RectF(20, top + 80, AppC::PANEL_W - 20, top + 140), F_HINT, Hex(0x888888),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void App::drawViewQuadrants(Gfx& g, float top, float bot) {
    float W = AppC::PANEL_W;
    float qW = (W - 30.0f) * 0.5f;
    float qH = (bot - top - 10.0f) * 0.5f;
    std::wstring tdate = today_str();

    struct QuadMeta { int q; float x, y; const wchar_t* title; DWORD headBg; DWORD bodyBg; };
    QuadMeta metas[] = {
        { 1, 12.0f, top, L"🔥 重要且紧急", 0xFFCDD2, 0xFFEBEE },
        { 2, 18.0f + qW, top, L"⭐ 重要不紧急", 0xC8E6C9, 0xE8F5E9 },
        { 3, 12.0f, top + qH + 6.0f, L"⚡ 紧急不重要", 0xFFE082, 0xFFF8E1 },
        { 4, 18.0f + qW, top + qH + 6.0f, L"🍃 不重要不紧急", 0xE0E0E0, 0xF5F5F5 }
    };

    for (const auto& m : metas) {
        D2D1_RECT_F boxRc = D2D1::RectF(m.x, m.y, m.x + qW, m.y + qH);
        g.fillRoundedRect(boxRc, 6.0f, Hex(m.bodyBg));
        D2D1_RECT_F headRc = D2D1::RectF(m.x, m.y, m.x + qW, m.y + 24.0f);
        g.fillRoundedRect(headRc, 4.0f, Hex(m.headBg));
        g.drawText(m.title, D2D1::RectF(m.x + 6, m.y, m.x + qW - 6, m.y + 24.0f), F_HINT, Hex(0x222222));

        float iy = m.y + 28.0f;
        if (!m_projects.empty()) {
            for (size_t i = 0; i < m_projects[0].todos.size(); ++i) {
                const auto& t = m_projects[0].todos[i];
                if (!t.hide_until.empty() && tdate < t.hide_until) continue;
                if (compute_quadrant(t) != m.q) continue;
                if (iy + 22.0f > m.y + qH) break;

                D2D1_RECT_F itemRc = D2D1::RectF(m.x + 4, iy, m.x + qW - 4, iy + 22.0f);
                bool isPending = (t.uuid == m_pendingUuid);
                std::wstring line = (isPending ? L"✔️ 待确认: " : L"◯ ") + t.text;
                if (!t.due_time.empty()) line += L" 🕒" + t.due_time;
                g.drawText(line, itemRc, F_FOOTER, isPending ? Hex(0x1565C0) : Hex(0x333333));
                if (m_modal == MODAL_NONE) addHit(itemRc, H_QUAD_ITEM, (int)i);
                iy += 23.0f;
            }
        }
    }
}

void App::drawViewPlans(Gfx& g, float top, float bot) {
    float W = AppC::PANEL_W;
    D2D1_RECT_F addBtn = D2D1::RectF(12, top + 2, W - 12, top + 36);
    g.fillRoundedRect(addBtn, 6.0f, Hex(0xE8F5E9));
    g.strokeRoundedRect(addBtn, 6.0f, Hex(0xC8E6C9), 1.0f);
    g.drawText(L"➕ 添加周期计划 (设置名称/时间/重复周期)", addBtn, F_HINT, Hex(0x2E7D32),
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (m_modal == MODAL_NONE) addHit(addBtn, H_PLAN_ADD);

    float y = top + 44.0f;
    for (size_t i = 0; i < m_plans.size(); ++i) {
        if (y + 48.0f > bot) break;
        const auto& p = m_plans[i];
        D2D1_RECT_F rowRc = D2D1::RectF(12, y, W - 12, y + 46.0f);
        g.fillRoundedRect(rowRc, 6.0f, C::WHITE);
        g.strokeRoundedRect(rowRc, 6.0f, Hex(0xEEEEEE), 1.0f);

        std::wstring title = (p.priority == 1 ? L"⭐ " : L"🗓️ ") + p.text;
        g.drawText(title, D2D1::RectF(20, y + 4, W - 130, y + 42.0f), F_TODO, Hex(0x333333));

        std::wstring badge = L"🔁" + format_rule_display(p.repeat_rule, p.repeat_days);
        if (!p.due_time.empty()) badge = L"🕒" + p.due_time + L" " + badge;
        float bw = g.measureTextW(badge, F_FOOTER) + 12.0f;
        D2D1_RECT_F bRc = D2D1::RectF(W - 18.0f - bw, y + 12.0f, W - 18.0f, y + 34.0f);
        g.fillRoundedRect(bRc, 4.0f, Hex(0xE8F5E9));
        g.drawText(badge, bRc, F_FOOTER, Hex(0x2E7D32), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (m_modal == MODAL_NONE) addHit(rowRc, H_PLAN_ITEM, (int)i);
        y += 50.0f;
    }

    if (m_plans.empty()) {
        g.drawText(L"点击上方按钮添加周期计划（每天/工作日/每周/每月）\n左键点击已有计划可编辑，右键可删除。",
                   D2D1::RectF(20, top + 100, W - 20, top + 160), F_HINT, Hex(0x888888),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void App::drawViewForest(Gfx& g, float top, float bot) {
    (void)bot;
    float W = AppC::PANEL_W;
    // 顶部栏：当前专注对象 + 时长加减器
    std::wstring focusLabel = m_forestTaskTitle.empty() ? L"当前专注：自由沉浸" : (L"专注: " + m_forestTaskTitle.substr(0, 10));
    g.drawText(focusLabel, D2D1::RectF(18, top + 8, 200, top + 34), F_HINT, Hex(0x2E7D32));

    g.drawText(L"时长:", D2D1::RectF(202, top + 8, 238, top + 34), F_HINT, Hex(0x444444));
    D2D1_RECT_F btnMinus = D2D1::RectF(240, top + 8, 264, top + 32);
    D2D1_RECT_F btnPlus  = D2D1::RectF(318, top + 8, 342, top + 32);
    g.fillRoundedRect(btnMinus, 4.0f, Hex(0xE0E0E0));
    g.fillRoundedRect(btnPlus, 4.0f, Hex(0xE0E0E0));
    g.drawText(L"-", btnMinus, F_PROJ_NAME, Hex(0x333333), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g.drawText(L"+", btnPlus, F_PROJ_NAME, Hex(0x333333), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    std::wstring minStr = std::to_wstring(m_cfg.focus_minutes) + L"分";
    g.drawText(minStr, D2D1::RectF(264, top + 8, 318, top + 32), F_HINT, Hex(0x222222),
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (m_modal == MODAL_NONE && !m_forestRunning && !m_forestResting) {
        addHit(btnMinus, H_FOREST_MINUS);
        addHit(btnPlus, H_FOREST_PLUS);
    }

    // 中央大树展示卡片
    D2D1_RECT_F treeCard = D2D1::RectF(20, top + 46, W - 20, top + 206);
    g.fillRoundedRect(treeCard, 16.0f, Hex(0xE8F5E9, 0.9f));

    const wchar_t* treeIcon = L"🌰";
    if (m_forestWitheredAnim) treeIcon = L"🥀";
    else if (m_forestResting) treeIcon = L"☕";
    else if (m_forestRunning) {
        float total = (float)(m_cfg.focus_minutes * 60);
        float ratio = (total - m_forestRemain) / std::max(1.0f, total);
        treeIcon = (ratio < 0.25f) ? L"🌰" : (ratio < 0.6f) ? L"🌱" : (ratio < 0.9f) ? L"🌿" : L"🌲";
    }
    g.drawText(treeIcon, D2D1::RectF(20, top + 56, W - 20, top + 150), F_ABOUT_TITLE, Hex(0x2E7D32),
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g.drawText(m_forestResting ? L"☕ 休息充电中..." : (m_forestRunning ? L"🌱 树苗正在茁壮成长..." : L"准备开始一段沉浸专注"),
               D2D1::RectF(20, top + 154, W - 20, top + 196), F_HINT, Hex(0x388E3C),
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // 倒计时大数字
    int rem = (int)std::ceil(m_forestRemain);
    wchar_t tbuf[32]; swprintf_s(tbuf, L"%02d:%02d", rem / 60, rem % 60);
    g.drawText(tbuf, D2D1::RectF(20, top + 216, W - 20, top + 264), F_ABOUT_TITLE, Hex(0x1B5E20),
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // 进度条
    float totalSecs = m_forestResting ? 300.0f : (float)(m_cfg.focus_minutes * 60);
    float progress = std::max(0.0f, std::min(1.0f, (totalSecs - m_forestRemain) / std::max(1.0f, totalSecs)));
    D2D1_RECT_F barBg = D2D1::RectF(24, top + 272, W - 24, top + 280);
    g.fillRoundedRect(barBg, 4.0f, Hex(0xC8E6C9));
    if (progress > 0.01f) {
        D2D1_RECT_F barFg = D2D1::RectF(24, top + 272, 24 + (W - 48) * progress, top + 280);
        g.fillRoundedRect(barFg, 4.0f, Hex(0x43A047));
    }

    // 底部双按钮（所见即所得热区绑定！）
    float btnY = top + 300.0f;
    D2D1_RECT_F btnToggle = D2D1::RectF(24, btnY, W * 0.5f - 6, btnY + 40);
    D2D1_RECT_F btnGiveup = D2D1::RectF(W * 0.5f + 6, btnY, W - 24, btnY + 40);

    g.fillRoundedRect(btnToggle, 6.0f, Hex(0x4CAF50));
    const wchar_t* toggleTxt = m_forestRunning ? L"⏸️ 暂停" : (m_forestRemain < totalSecs ? L"▶️ 继续" : L"🌱 开始种植");
    g.drawText(toggleTxt, btnToggle, F_PROJ_NAME, C::WHITE, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    bool canGiveUp = m_forestRunning || m_forestResting || (m_forestRemain < totalSecs);
    g.fillRoundedRect(btnGiveup, 6.0f, canGiveUp ? Hex(0xE53935) : Hex(0xBDBDBD));
    g.drawText(m_forestResting ? L"⏭️ 跳过休息" : L"⚠️ 放弃", btnGiveup, F_PROJ_NAME, C::WHITE,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (m_modal == MODAL_NONE) {
        addHit(btnToggle, H_FOREST_TOGGLE);
        if (canGiveUp) addHit(btnGiveup, H_FOREST_GIVEUP);
    }
}

void App::drawModal(Gfx& g) {
    float W = AppC::PANEL_W, H = AppC::PANEL_H;
    g.fillRoundedRect(D2D1::RectF(1, 1, W - 1, H - 1), 14.0f, D2D1::ColorF(0, 0, 0, 0.45f));

    if (m_modal == MODAL_FOCUS_SUCCESS) {
        D2D1_RECT_F box = D2D1::RectF(40, 160, W - 40, 440);
        g.fillRoundedRect(box, 16.0f, Hex(0xF1F8E9));
        g.strokeRoundedRect(box, 16.0f, Hex(0x81C784), 2.0f);
        g.drawText(L"🌲", D2D1::RectF(40, 180, W - 40, 240), F_ABOUT_TITLE, Hex(0x2E7D32), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        g.drawText(L"专注完成！", D2D1::RectF(40, 245, W - 40, 280), F_ABOUT_TITLE, Hex(0x2E7D32), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        g.drawText(L"路漫漫其修远兮\n\n休息一下吧！", D2D1::RectF(40, 290, W - 40, 355), F_TODO, Hex(0x555555), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        D2D1_RECT_F btnOk = D2D1::RectF(65, 375, W - 65, 420);
        g.fillRoundedRect(btnOk, 8.0f, Hex(0x4CAF50));
        g.drawText(L"开始休息 ☕", btnOk, F_PROJ_NAME, C::WHITE, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        addHit(btnOk, H_SUCCESS_OK);
        return;
    }

    if (m_modal == MODAL_HISTORY || m_modal == MODAL_FOREST_STATS) {
        D2D1_RECT_F box = D2D1::RectF(14, 50, W - 14, H - 50);
        g.fillRoundedRect(box, 12.0f, Hex(0xFAFAFA));
        g.strokeRoundedRect(box, 12.0f, Hex(0xCCCCCC), 1.0f);

        g.drawText(m_modal == MODAL_HISTORY ? L"📚 历史复盘中心 (按完成日期)" : L"🌳 个人专注森林统计",
                   D2D1::RectF(26, 60, W - 60, 90), F_PROJ_NAME, Hex(0x222222));
        D2D1_RECT_F btnClose = D2D1::RectF(W - 52, 58, W - 20, 88);
        g.drawText(L"✖", btnClose, F_PROJ_NAME, Hex(0x666666), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        addHit(btnClose, H_MODAL_CLOSE);

        g.rt->PushAxisAlignedClip(D2D1::RectF(18, 96, W - 18, H - 60), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float y = 100.0f - m_scrollModal;

        if (m_modal == MODAL_FOREST_STATS) {
            int healthy = 0, withered = 0, totalMins = 0;
            for (const auto& r : m_forestRecords) {
                if (r.status == L"healthy") { healthy++; totalMins += r.minutes; }
                else withered++;
            }
            D2D1_RECT_F sumBox = D2D1::RectF(24, y, W - 24, y + 72);
            g.fillRoundedRect(sumBox, 8.0f, Hex(0xE8F5E9));
            wchar_t sumBuf[256];
            swprintf_s(sumBuf, L"🌲 成活大树：%d 棵    🥀 枯萎灌木：%d 棵\n⏳ 累计深度专注：%d 分钟", healthy, withered, totalMins);
            g.drawText(sumBuf, D2D1::RectF(34, y + 8, W - 34, y + 64), F_HINT, Hex(0x1B5E20));
            y += 82.0f;

            for (int i = (int)m_forestRecords.size() - 1; i >= 0; --i) {
                const auto& r = m_forestRecords[i];
                std::wstring icon = (r.status == L"healthy") ? L"🌲" : L"🥀";
                std::wstring ts = (r.timestamp.size() >= 16) ? (r.timestamp.substr(5, 5) + L" " + r.timestamp.substr(11, 5)) : r.timestamp;
                std::wstring line = icon + L" [" + ts + L"] " + r.task + L" (" + std::to_wstring(r.minutes) + L"m)";
                g.drawText(line, D2D1::RectF(26, y, W - 26, y + 26), F_HINT, Hex(0x333333));
                g.drawLine(26, y + 26, W - 26, y + 26, Hex(0xEEEEEE), 1.0f);
                y += 28.0f;
            }
        } else {
            // 按完成日期分组展示历史
            std::map<std::wstring, std::vector<HistoryItem>, std::greater<std::wstring>> groups;
            for (const auto& h : m_history) {
                std::wstring d = (h.completed.size() >= 10) ? h.completed.substr(0, 10) : L"历史沉淀";
                groups[d].push_back(h);
            }
            if (groups.empty()) {
                g.drawText(L"暂无已完成的历史记录", D2D1::RectF(26, y + 40, W - 26, y + 80), F_HINT, Hex(0x888888),
                           DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            for (const auto& kv : groups) {
                D2D1_RECT_F grpRc = D2D1::RectF(22, y, W - 22, y + 26);
                g.fillRoundedRect(grpRc, 4.0f, Hex(0xE8F5E9));
                std::wstring hdr = L"📂 " + kv.first + L" (完成 " + std::to_wstring(kv.second.size()) + L" 项)";
                g.drawText(hdr, D2D1::RectF(28, y, W - 28, y + 26), F_HINT, Hex(0x2E7D32));
                y += 30.0f;
                for (const auto& item : kv.second) {
                    std::wstring tm = (item.completed.size() >= 16) ? item.completed.substr(11, 5) : L"--";
                    g.drawText(L"✅ " + item.text, D2D1::RectF(28, y, W - 75, y + 24), F_FOOTER, Hex(0x333333));
                    g.drawText(tm, D2D1::RectF(W - 72, y, W - 24, y + 24), F_FOOTER, Hex(0x888888));
                    y += 25.0f;
                }
                y += 6.0f;
            }
        }
        g.rt->PopAxisAlignedClip();
        return;
    }

    // ============================================================
    // 完整复刻 Python 版 TaskEditDialog (新建/编辑计划与事项弹窗)
    // ============================================================
    if (m_modal == MODAL_EDIT_TASK || m_modal == MODAL_EDIT_PLAN) {
        D2D1_RECT_F box = D2D1::RectF(14, 75, W - 14, H - 75);
        g.fillRoundedRect(box, 12.0f, Hex(0xFAFAFA));
        g.strokeRoundedRect(box, 12.0f, Hex(0xBDBDBD), 1.2f);

        std::wstring dlgTitle = m_dlgIsPlan ? (m_dlgIsNew ? L"➕ 添加周期计划" : L"✏️ 编辑周期计划") : L"✏️ 编辑待办事项";
        g.drawText(dlgTitle, D2D1::RectF(26, 86, W - 26, 114), F_PROJ_NAME, Hex(0x2E7D32));

        float y = 124.0f;
        // 1. 事项名称输入框
        g.drawText(L"事项名称:", D2D1::RectF(26, y, 95, y + 28), F_HINT, Hex(0x333333));
        D2D1_RECT_F titleRc = D2D1::RectF(96, y, W - 26, y + 28);
        g.fillRoundedRect(titleRc, 4.0f, C::WHITE);
        g.strokeRoundedRect(titleRc, 4.0f, (m_editTarget == ET_DLG_TITLE) ? Hex(0x2E7D32) : Hex(0xCCCCCC), 1.2f);
        std::wstring showT = m_dlgTitle + (m_editTarget == ET_DLG_TITLE ? L"|" : L"");
        g.drawText(showT.empty() ? L"点击输入名称..." : showT, D2D1::RectF(102, y, W - 30, y + 28), F_HINT,
                   m_dlgTitle.empty() ? Hex(0x999999) : Hex(0x222222));
        addHit(titleRc, H_DLG_TITLE_EDIT);
        y += 38.0f;

        // 2. 优先级复选框
        g.drawText(L"优先级:", D2D1::RectF(26, y, 95, y + 26), F_HINT, Hex(0x333333));
        D2D1_RECT_F prioRc = D2D1::RectF(96, y, W - 26, y + 26);
        g.drawText(m_dlgPriority == 1 ? L"☑️ ⭐ 标记为重要事项" : L"⬜ 标记为重要事项", prioRc, F_HINT,
                   m_dlgPriority == 1 ? Hex(0xD32F2F) : Hex(0x444444));
        addHit(prioRc, H_DLG_PRIO_CHK);
        y += 36.0f;

        // 3. 截止日期
        g.drawText(L"截止日期:", D2D1::RectF(26, y, 95, y + 26), F_HINT, Hex(0x333333));
        if (m_dlgIsPlan) {
            g.drawText(L"(周期日期由引擎自动派发)", D2D1::RectF(96, y, W - 26, y + 26), F_FOOTER, Hex(0x777777));
        } else {
            D2D1_RECT_F chkDue = D2D1::RectF(96, y, 165, y + 26);
            g.drawText(m_dlgHasDue ? L"☑️ 启用" : L"⬜ 启用", chkDue, F_HINT, Hex(0x333333));
            addHit(chkDue, H_DLG_DUE_CHK);
            if (m_dlgHasDue) {
                D2D1_RECT_F dPrev = D2D1::RectF(170, y, 194, y + 26);
                D2D1_RECT_F dNext = D2D1::RectF(286, y, 310, y + 26);
                g.fillRoundedRect(dPrev, 4.0f, Hex(0xE0E0E0));
                g.fillRoundedRect(dNext, 4.0f, Hex(0xE0E0E0));
                g.drawText(L"<", dPrev, F_HINT, Hex(0x333333), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                g.drawText(L">", dNext, F_HINT, Hex(0x333333), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                g.drawText(m_dlgDueDate, D2D1::RectF(194, y, 286, y + 26), F_FOOTER, Hex(0x1565C0),
                           DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                addHit(dPrev, H_DLG_DUE_PREV);
                addHit(dNext, H_DLG_DUE_NEXT);
            }
        }
        y += 36.0f;

        // 4. 截止时刻
        g.drawText(L"截止时刻:", D2D1::RectF(26, y, 95, y + 26), F_HINT, Hex(0x333333));
        D2D1_RECT_F chkTime = D2D1::RectF(96, y, 165, y + 26);
        g.drawText(m_dlgHasTime ? L"☑️ 指定" : L"⬜ 指定", chkTime, F_HINT, Hex(0x333333));
        addHit(chkTime, H_DLG_TIME_CHK);
        if (m_dlgHasTime) {
            D2D1_RECT_F hPrev = D2D1::RectF(170, y, 190, y + 26);
            D2D1_RECT_F hNext = D2D1::RectF(222, y, 242, y + 26);
            D2D1_RECT_F mPrev = D2D1::RectF(250, y, 270, y + 26);
            D2D1_RECT_F mNext = D2D1::RectF(302, y, 322, y + 26);
            for (const auto& r : {hPrev, hNext, mPrev, mNext}) g.fillRoundedRect(r, 4.0f, Hex(0xE0E0E0));
            g.drawText(L"-", hPrev, F_HINT, Hex(0x333333), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            g.drawText(L"+", hNext, F_HINT, Hex(0x333333), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            g.drawText(L"-", mPrev, F_HINT, Hex(0x333333), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            g.drawText(L"+", mNext, F_HINT, Hex(0x333333), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            wchar_t hb[8], mb[8]; swprintf_s(hb, L"%02d时", m_dlgHour); swprintf_s(mb, L"%02d分", m_dlgMinute);
            g.drawText(hb, D2D1::RectF(190, y, 222, y + 26), F_FOOTER, Hex(0x2E7D32), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            g.drawText(mb, D2D1::RectF(270, y, 302, y + 26), F_FOOTER, Hex(0x2E7D32), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            addHit(hPrev, H_DLG_HOUR_PREV); addHit(hNext, H_DLG_HOUR_NEXT);
            addHit(mPrev, H_DLG_MIN_PREV);  addHit(mNext, H_DLG_MIN_NEXT);
        }
        y += 38.0f;

        // 5. 重复周期设置
        if (!m_dlgIsPeriodicInst) {
            g.drawText(L"重复周期:", D2D1::RectF(26, y, 95, y + 26), F_HINT, Hex(0x333333));
            struct RuleBtn { const wchar_t* name; const wchar_t* rule; HitType ht; };
            RuleBtn rules[] = {
                { L"不重复", L"", H_DLG_RULE_NONE },
                { L"每天", L"daily", H_DLG_RULE_DAILY },
                { L"工作日", L"weekdays", H_DLG_RULE_WEEKDAYS },
                { L"每周", L"weekly", H_DLG_RULE_WEEKLY },
                { L"每月", L"monthly", H_DLG_RULE_MONTHLY }
            };
            float rx = 96.0f;
            for (int i = (m_dlgIsPlan ? 1 : 0); i < 5; ++i) {
                float rw = (i == 0 || i == 2) ? 48.0f : 40.0f;
                D2D1_RECT_F rRc = D2D1::RectF(rx, y, rx + rw, y + 26);
                bool sel = (m_dlgRepeatRule == rules[i].rule);
                g.fillRoundedRect(rRc, 4.0f, sel ? Hex(0x2E7D32) : Hex(0xE0E0E0));
                g.drawText(rules[i].name, rRc, F_FOOTER, sel ? C::WHITE : Hex(0x333333),
                           DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                addHit(rRc, rules[i].ht);
                rx += rw + 4.0f;
            }
            y += 36.0f;

            if (m_dlgRepeatRule == L"weekly") {
                g.drawText(L"具体星期:", D2D1::RectF(26, y, 95, y + 26), F_HINT, Hex(0x333333));
                const wchar_t* wnames[] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
                for (int w = 0; w < 7; ++w) {
                    D2D1_RECT_F wRc = D2D1::RectF(96.0f + w * 32.0f, y, 96.0f + w * 32.0f + 28.0f, y + 26);
                    bool checked = std::find(m_dlgRepeatDays.begin(), m_dlgRepeatDays.end(), w) != m_dlgRepeatDays.end();
                    g.fillRoundedRect(wRc, 4.0f, checked ? Hex(0x2E7D32) : Hex(0xE0E0E0));
                    g.drawText(wnames[w], wRc, F_FOOTER, checked ? C::WHITE : Hex(0x333333),
                               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    addHit(wRc, H_DLG_WEEKDAY_BTN, w);
                }
                y += 36.0f;
            } else if (m_dlgRepeatRule == L"monthly") {
                g.drawText(L"每月几号:", D2D1::RectF(26, y, 95, y + 28), F_HINT, Hex(0x333333));
                D2D1_RECT_F mRc = D2D1::RectF(96, y, W - 26, y + 28);
                g.fillRoundedRect(mRc, 4.0f, C::WHITE);
                g.strokeRoundedRect(mRc, 4.0f, (m_editTarget == ET_DLG_MONTH_DAYS) ? Hex(0x2E7D32) : Hex(0xCCCCCC), 1.2f);
                std::wstring showM = m_dlgMonthDaysStr + (m_editTarget == ET_DLG_MONTH_DAYS ? L"|" : L"");
                g.drawText(showM.empty() ? L"如: 1, 15, 31" : showM, D2D1::RectF(102, y, W - 30, y + 28), F_HINT,
                           m_dlgMonthDaysStr.empty() ? Hex(0x999999) : Hex(0x222222));
                addHit(mRc, H_DLG_MONTH_EDIT);
                y += 36.0f;
            }
        }

        // 保存与取消按钮
        float btnY = H - 125.0f;
        D2D1_RECT_F btnSave = D2D1::RectF(W - 170, btnY, W - 98, btnY + 34);
        D2D1_RECT_F btnCancel = D2D1::RectF(W - 90, btnY, W - 26, btnY + 34);
        g.fillRoundedRect(btnSave, 6.0f, Hex(0x2E7D32));
        g.fillRoundedRect(btnCancel, 6.0f, Hex(0x9E9E9E));
        g.drawText(L"保存", btnSave, F_HINT, C::WHITE, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        g.drawText(L"取消", btnCancel, F_HINT, C::WHITE, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        addHit(btnSave, H_DLG_SAVE);
        addHit(btnCancel, H_DLG_CANCEL);
    }
}

void App::drawToast(Gfx& g) {
    float alpha = std::min(1.0f, m_toastTimer / 0.3f);
    float tw = g.measureTextW(m_toastText, F_HINT) + 40.0f;
    float cx = AppC::PANEL_W * 0.5f;
    float cy = AppC::PANEL_H * 0.5f + 80.0f;
    D2D1_RECT_F rc = D2D1::RectF(cx - tw * 0.5f, cy - 20.0f, cx + tw * 0.5f, cy + 20.0f);
    g.fillRoundedRect(rc, 8.0f, D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.88f * alpha));
    g.drawText(m_toastText, rc, F_HINT, D2D1::ColorF(1, 1, 1, alpha),
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void App::showToast(const std::wstring& msg) {
    m_toastText = msg;
    m_toastTimer = 2.2f;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::onMouseMove(float x, float y) {
    (void)x; (void)y;
    if (!m_expanded) expandWindow();
}

void App::onLButtonDown(float x, float y) {
    // 反向遍历热区（优先响应顶层按钮）
    for (int i = (int)m_hits.size() - 1; i >= 0; --i) {
        const auto& h = m_hits[i];
        if (x >= h.rc.left && x <= h.rc.right && y >= h.rc.top && y <= h.rc.bottom) {
            switch (h.type) {
                case H_BALL: expandWindow(); return;
                case H_NAV_ALL: m_view = VIEW_ALL; InvalidateRect(m_hwnd, nullptr, FALSE); return;
                case H_NAV_QUAD: m_view = VIEW_QUADRANTS; InvalidateRect(m_hwnd, nullptr, FALSE); return;
                case H_NAV_PLANS: m_view = VIEW_PLANS; InvalidateRect(m_hwnd, nullptr, FALSE); return;
                case H_NAV_FOREST: m_view = VIEW_FOREST; InvalidateRect(m_hwnd, nullptr, FALSE); return;
                case H_BTN_REFRESH:
                    if (sync_repeat_plans(m_projects, m_plans, m_history)) {
                        save_projects(m_projects);
                        save_plans(m_plans);
                    }
                    sortTodos();
                    showToast(L"🔄 已同步周期任务并刷新列表");
                    return;
                case H_BTN_HANDOVER:
                    copyHandover();
                    return;
                case H_BTN_HISTORY:
                    m_modal = MODAL_HISTORY;
                    m_scrollModal = 0.0f;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_BTN_FOREST_STATS:
                    m_modal = MODAL_FOREST_STATS;
                    m_scrollModal = 0.0f;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_INPUT_BOX:
                    focusHiddenEdit(ET_QUICK_INPUT, m_quickInput);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_TODO_ITEM:
                case H_QUAD_ITEM:
                    if (!m_projects.empty() && h.index >= 0 && h.index < (int)m_projects[0].todos.size()) {
                        handleTodoClick(m_projects[0].todos[h.index].uuid);
                    }
                    return;
                case H_PLAN_ADD:
                    openTaskEditor(nullptr, nullptr, true, true);
                    return;
                case H_PLAN_ITEM:
                    if (h.index >= 0 && h.index < (int)m_plans.size()) {
                        openTaskEditor(nullptr, &m_plans[h.index], true, false);
                    }
                    return;
                case H_FOREST_MINUS:
                    m_cfg.focus_minutes = std::max(1, m_cfg.focus_minutes - 5);
                    m_forestRemain = (float)(m_cfg.focus_minutes * 60);
                    save_config(m_cfg);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_FOREST_PLUS:
                    m_cfg.focus_minutes = std::min(120, m_cfg.focus_minutes + 5);
                    m_forestRemain = (float)(m_cfg.focus_minutes * 60);
                    save_config(m_cfg);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_FOREST_TOGGLE:
                    m_forestRunning = !m_forestRunning;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_FOREST_GIVEUP: {
                    if (m_forestResting) {
                        m_forestResting = false;
                        m_forestRunning = false;
                        m_forestRemain = (float)(m_cfg.focus_minutes * 60);
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }
                    m_isSysMenuOpen = true;
                    int ret = MessageBoxW(m_hwnd, L"中途放弃会导致树苗枯萎！确认放弃吗？", L"放弃专注？", MB_YESNO | MB_ICONWARNING);
                    m_isSysMenuOpen = false;
                    if (ret == IDYES) {
                        m_forestRunning = false;
                        m_forestWitheredAnim = true;
                        m_forestWitheredTimer = 2.0f;
                        ForestRecord rec;
                        rec.timestamp = now_iso();
                        rec.status = L"withered";
                        rec.task = m_forestTaskTitle.empty() ? L"自由专注" : m_forestTaskTitle;
                        rec.minutes = m_cfg.focus_minutes;
                        m_forestRecords.push_back(rec);
                        save_forest(m_forestRecords);
                        showToast(L"🥀 树苗枯萎了...");
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                }
                case H_SUCCESS_OK:
                    m_modal = MODAL_NONE;
                    m_forestResting = true;
                    m_forestRemain = 5.0f * 60.0f;
                    m_forestRunning = true;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_MODAL_CLOSE:
                case H_DLG_CANCEL:
                    m_modal = MODAL_NONE;
                    focusHiddenEdit(ET_QUICK_INPUT, m_quickInput);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_DLG_TITLE_EDIT:
                    focusHiddenEdit(ET_DLG_TITLE, m_dlgTitle);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_DLG_MONTH_EDIT:
                    focusHiddenEdit(ET_DLG_MONTH_DAYS, m_dlgMonthDaysStr);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_DLG_PRIO_CHK:
                    m_dlgPriority = (m_dlgPriority == 1) ? 0 : 1;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_DLG_DUE_CHK:
                    m_dlgHasDue = !m_dlgHasDue;
                    if (m_dlgHasDue && m_dlgDueDate.empty()) m_dlgDueDate = today_str();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_DLG_DUE_PREV:
                    m_dlgDueDate = add_days_str(m_dlgDueDate, -1);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_DLG_DUE_NEXT:
                    m_dlgDueDate = add_days_str(m_dlgDueDate, 1);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_DLG_TIME_CHK:
                    m_dlgHasTime = !m_dlgHasTime;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_DLG_HOUR_PREV: m_dlgHour = (m_dlgHour + 23) % 24; InvalidateRect(m_hwnd, nullptr, FALSE); return;
                case H_DLG_HOUR_NEXT: m_dlgHour = (m_dlgHour + 1) % 24; InvalidateRect(m_hwnd, nullptr, FALSE); return;
                case H_DLG_MIN_PREV:  m_dlgMinute = (m_dlgMinute + 55) % 60; InvalidateRect(m_hwnd, nullptr, FALSE); return;
                case H_DLG_MIN_NEXT:  m_dlgMinute = (m_dlgMinute + 5) % 60; InvalidateRect(m_hwnd, nullptr, FALSE); return;
                case H_DLG_RULE_NONE: m_dlgRepeatRule = L""; InvalidateRect(m_hwnd, nullptr, FALSE); return;
                case H_DLG_RULE_DAILY: m_dlgRepeatRule = L"daily"; InvalidateRect(m_hwnd, nullptr, FALSE); return;
                case H_DLG_RULE_WEEKDAYS: m_dlgRepeatRule = L"weekdays"; InvalidateRect(m_hwnd, nullptr, FALSE); return;
                case H_DLG_RULE_WEEKLY:
                    m_dlgRepeatRule = L"weekly";
                    if (m_dlgRepeatDays.empty()) m_dlgRepeatDays.push_back(0);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_DLG_RULE_MONTHLY:
                    m_dlgRepeatRule = L"monthly";
                    if (m_dlgMonthDaysStr.empty()) m_dlgMonthDaysStr = L"1";
                    focusHiddenEdit(ET_DLG_MONTH_DAYS, m_dlgMonthDaysStr);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                case H_DLG_WEEKDAY_BTN: {
                    auto it = std::find(m_dlgRepeatDays.begin(), m_dlgRepeatDays.end(), h.index);
                    if (it != m_dlgRepeatDays.end()) {
                        if (m_dlgRepeatDays.size() > 1) m_dlgRepeatDays.erase(it);
                    } else {
                        m_dlgRepeatDays.push_back(h.index);
                        std::sort(m_dlgRepeatDays.begin(), m_dlgRepeatDays.end());
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                }
                case H_DLG_SAVE:
                    saveDialogEdit();
                    return;
            }
        }
    }
}

void App::onRButtonUp(float x, float y) {
    if (m_modal != MODAL_NONE) return;
    for (int i = (int)m_hits.size() - 1; i >= 0; --i) {
        const auto& h = m_hits[i];
        if (x >= h.rc.left && x <= h.rc.right && y >= h.rc.top && y <= h.rc.bottom) {
            if ((h.type == H_TODO_ITEM || h.type == H_QUAD_ITEM) && !m_projects.empty() && h.index < (int)m_projects[0].todos.size()) {
                TodoItem taskCopy = m_projects[0].todos[h.index];
                m_isSysMenuOpen = true;
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 1, L"➡️ 延后一天 (智能视线隐藏)");
                AppendMenuW(menu, MF_STRING, 2, L"✏️ 编辑事项");
                AppendMenuW(menu, MF_STRING, 3, L"🌲 对此事项开启专注森林");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, 4, L"🗑️ 删除事项");
                POINT pt; GetCursorPos(&pt);
                int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
                DestroyMenu(menu);
                m_isSysMenuOpen = false;

                if (cmd == 1) postponeTodoByUuid(taskCopy.uuid);
                else if (cmd == 2) openTaskEditor(&taskCopy, nullptr, false, false);
                else if (cmd == 3) {
                    m_forestTaskUuid = taskCopy.uuid;
                    m_forestTaskTitle = taskCopy.text;
                    m_view = VIEW_FOREST;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (cmd == 4) deleteTodoByUuid(taskCopy.uuid);
                return;
            }
            if (h.type == H_PLAN_ITEM && h.index < (int)m_plans.size()) {
                Plan planCopy = m_plans[h.index];
                m_isSysMenuOpen = true;
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 1, L"✏️ 修改计划蓝图");
                AppendMenuW(menu, MF_STRING, 2, L"🗑️ 停止并删除计划");
                POINT pt; GetCursorPos(&pt);
                int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
                DestroyMenu(menu);
                m_isSysMenuOpen = false;

                if (cmd == 1) openTaskEditor(nullptr, &planCopy, true, false);
                else if (cmd == 2) {
                    m_plans.erase(m_plans.begin() + h.index);
                    if (!m_projects.empty()) {
                        auto& ts = m_projects[0].todos;
                        ts.erase(std::remove_if(ts.begin(), ts.end(), [&](const TodoItem& t) {
                            return t.plan_uuid == planCopy.plan_uuid;
                        }), ts.end());
                    }
                    save_plans(m_plans);
                    save_projects(m_projects);
                    showToast(L"🗑️ 周期计划已停止并移除");
                }
                return;
            }
        }
    }
}

void App::openTaskEditor(const TodoItem* task, const Plan* plan, bool isPlan, bool isNew) {
    m_dlgIsPlan = isPlan;
    m_dlgIsNew = isNew;
    m_dlgRepeatDays.clear();
    m_dlgMonthDaysStr.clear();

    if (isPlan) {
        m_modal = MODAL_EDIT_PLAN;
        m_dlgIsPeriodicInst = false;
        if (plan) {
            m_dlgTargetUuid = plan->plan_uuid;
            m_dlgTitle = plan->text;
            m_dlgPriority = plan->priority;
            m_dlgHasTime = !plan->due_time.empty();
            if (m_dlgHasTime && plan->due_time.size() >= 5) {
                m_dlgHour = _wtoi(plan->due_time.substr(0, 2).c_str());
                m_dlgMinute = _wtoi(plan->due_time.substr(3, 2).c_str());
            } else { m_dlgHour = 18; m_dlgMinute = 0; }
            m_dlgRepeatRule = plan->repeat_rule.empty() ? L"daily" : plan->repeat_rule;
            m_dlgRepeatDays = plan->repeat_days;
        } else {
            m_dlgTargetUuid.clear();
            m_dlgTitle.clear();
            m_dlgPriority = 0;
            m_dlgHasTime = false;
            m_dlgHour = 18; m_dlgMinute = 0;
            m_dlgRepeatRule = L"daily";
            m_dlgRepeatDays = {0};
        }
    } else if (task) {
        m_modal = MODAL_EDIT_TASK;
        m_dlgTargetUuid = task->uuid;
        m_dlgIsPeriodicInst = !task->plan_uuid.empty();
        m_dlgTitle = task->text;
        m_dlgPriority = task->priority;
        m_dlgHasDue = !task->due_date.empty();
        m_dlgDueDate = m_dlgHasDue ? task->due_date : today_str();
        m_dlgHasTime = !task->due_time.empty();
        if (m_dlgHasTime && task->due_time.size() >= 5) {
            m_dlgHour = _wtoi(task->due_time.substr(0, 2).c_str());
            m_dlgMinute = _wtoi(task->due_time.substr(3, 2).c_str());
        } else { m_dlgHour = 18; m_dlgMinute = 0; }
        m_dlgRepeatRule = task->repeat_rule;
        m_dlgRepeatDays = task->repeat_days;
    }

    for (size_t i = 0; i < m_dlgRepeatDays.size(); ++i) {
        if (i > 0) m_dlgMonthDaysStr += L", ";
        m_dlgMonthDaysStr += std::to_wstring(m_dlgRepeatDays[i]);
    }
    focusHiddenEdit(ET_DLG_TITLE, m_dlgTitle);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::saveDialogEdit() {
    while (!m_dlgTitle.empty() && m_dlgTitle.front() == L' ') m_dlgTitle.erase(0, 1);
    if (m_dlgTitle.empty()) {
        showToast(L"⚠️ 名称不能为空！");
        return;
    }

    if (m_dlgRepeatRule == L"monthly") {
        std::set<int> nums;
        std::wstring cur;
        for (wchar_t c : m_dlgMonthDaysStr) {
            if (c >= L'0' && c <= L'9') cur += c;
            else if (!cur.empty()) {
                int v = _wtoi(cur.c_str());
                if (v >= 1 && v <= 31) nums.insert(v);
                cur.clear();
            }
        }
        if (!cur.empty()) {
            int v = _wtoi(cur.c_str());
            if (v >= 1 && v <= 31) nums.insert(v);
        }
        if (nums.empty()) nums.insert(1);
        m_dlgRepeatDays.assign(nums.begin(), nums.end());
    }

    std::wstring timeStr;
    if (m_dlgHasTime) {
        wchar_t tb[16]; swprintf_s(tb, L"%02d:%02d", m_dlgHour, m_dlgMinute);
        timeStr = tb;
    }

    if (m_dlgIsPlan) {
        if (m_dlgIsNew) {
            Plan p;
            p.plan_uuid = new_uuid();
            p.text = m_dlgTitle;
            p.priority = m_dlgPriority;
            p.due_time = timeStr;
            p.repeat_rule = m_dlgRepeatRule.empty() ? L"daily" : m_dlgRepeatRule;
            p.repeat_days = m_dlgRepeatDays;
            p.base_date = today_str();
            m_plans.push_back(p);
        } else {
            for (auto& p : m_plans) {
                if (p.plan_uuid == m_dlgTargetUuid) {
                    p.text = m_dlgTitle;
                    p.priority = m_dlgPriority;
                    p.due_time = timeStr;
                    p.repeat_rule = m_dlgRepeatRule;
                    p.repeat_days = m_dlgRepeatDays;
                    break;
                }
            }
            if (!m_projects.empty()) {
                for (auto& t : m_projects[0].todos) {
                    if (t.plan_uuid == m_dlgTargetUuid) {
                        t.text = m_dlgTitle;
                        t.priority = m_dlgPriority;
                        t.due_time = timeStr;
                        t.repeat_rule = m_dlgRepeatRule;
                        t.repeat_days = m_dlgRepeatDays;
                    }
                }
            }
        }
        save_plans(m_plans);
        sync_repeat_plans(m_projects, m_plans, m_history);
        sortTodos();
        save_projects(m_projects);
        showToast(L"✅ 周期计划已保存");
    } else {
        if (!m_projects.empty()) {
            for (auto& t : m_projects[0].todos) {
                if (t.uuid == m_dlgTargetUuid) {
                    t.text = m_dlgTitle;
                    t.priority = m_dlgPriority;
                    t.due_date = m_dlgHasDue ? m_dlgDueDate : L"";
                    t.due_time = timeStr;
                    if (!m_dlgIsPeriodicInst) {
                        t.repeat_rule = m_dlgRepeatRule;
                        t.repeat_days = m_dlgRepeatDays;
                    }
                    break;
                }
            }
            sortTodos();
            save_projects(m_projects);
            showToast(L"✅ 事项修改已保存");
        }
    }

    m_modal = MODAL_NONE;
    focusHiddenEdit(ET_QUICK_INPUT, m_quickInput);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::commitQuickAdd() {
    auto p = parse_quick_input(m_quickInput);
    if (p.title.empty()) return;

    std::wstring planUuid;
    if (!p.repeat_rule.empty()) {
        planUuid = new_uuid();
        Plan pl;
        pl.plan_uuid = planUuid;
        pl.text = p.title;
        pl.priority = p.priority;
        pl.due_time = p.due_time;
        pl.repeat_rule = p.repeat_rule;
        pl.repeat_days = p.repeat_days;
        pl.base_date = p.due_date.empty() ? today_str() : p.due_date;
        m_plans.push_back(pl);
        save_plans(m_plans);
    }

    TodoItem item;
    item.uuid = new_uuid();
    item.plan_uuid = planUuid;
    item.cycle_date = p.due_date.empty() ? today_str() : p.due_date;
    item.text = p.title;
    item.priority = p.priority;
    item.due_date = p.due_date;
    item.due_time = p.due_time;
    item.reminder_time = p.due_time;
    item.repeat_rule = p.repeat_rule;
    item.repeat_days = p.repeat_days;
    item.created = now_iso();
    item.done = false;

    if (m_projects.empty()) m_projects.push_back({L"默认项目", {}});
    m_projects[0].todos.insert(m_projects[0].todos.begin(), item);
    sortTodos();
    save_projects(m_projects);

    m_quickInput.clear();
    m_parsedPreview.clear();
    focusHiddenEdit(ET_QUICK_INPUT, L"");
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::handleTodoClick(const std::wstring& uuid) {
    if (m_pendingUuid == uuid) {
        completeTodoByUuid(uuid);
        m_pendingUuid.clear();
    } else {
        m_pendingUuid = uuid;
        m_pendingTimer = 2.5f;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void App::completeTodoByUuid(const std::wstring& uuid) {
    if (m_projects.empty()) return;
    auto& ts = m_projects[0].todos;
    for (auto it = ts.begin(); it != ts.end(); ++it) {
        if (it->uuid == uuid) {
            HistoryItem h;
            h.plan_uuid = it->plan_uuid;
            h.cycle_date = it->cycle_date;
            h.text = it->text;
            h.due_date = it->due_date;
            h.created = it->created;
            h.completed = now_iso();
            m_history.push_back(h);
            save_history(m_history);

            ts.erase(it);
            save_projects(m_projects);
            showToast(L"✅ 事项已完成并归档！");
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
}

void App::postponeTodoByUuid(const std::wstring& uuid) {
    if (m_projects.empty()) return;
    std::wstring tmr = tomorrow_str();
    for (auto& t : m_projects[0].todos) {
        if (t.uuid == uuid) {
            if (!t.due_date.empty() && t.original_due.empty()) t.original_due = t.due_date;
            if (t.due_date.empty() || t.due_date < tmr) t.due_date = tmr;
            else t.due_date = add_days_str(t.due_date, 1);
            t.hide_until = tmr;
            break;
        }
    }
    sortTodos();
    save_projects(m_projects);
    showToast(L"➡️ 已延后一天并在今日视线中隐藏");
}

void App::deleteTodoByUuid(const std::wstring& uuid) {
    if (m_projects.empty()) return;
    auto& ts = m_projects[0].todos;
    ts.erase(std::remove_if(ts.begin(), ts.end(), [&](const TodoItem& t) { return t.uuid == uuid; }), ts.end());
    save_projects(m_projects);
    showToast(L"🗑️ 事项已删除");
}

void App::copyHandover() {
    std::wstring tmr = tomorrow_str();
    std::wstring out = L"【前台未完成待办交接】\r\n交接时间：" + today_str() + L" " + now_time_str() + L"\r\n------------------------\r\n";
    int idx = 1;
    if (!m_projects.empty()) {
        for (const auto& t : m_projects[0].todos) {
            if (t.due_date.empty() || t.due_date <= tmr) {
                std::wstring tinfo;
                if (!t.due_date.empty()) tinfo += (t.due_date.size() >= 10 ? t.due_date.substr(5) : t.due_date);
                if (!t.due_time.empty()) tinfo += (tinfo.empty() ? L"" : L" ") + t.due_time;
                if (tinfo.empty()) tinfo = L"不限期";

                if (!t.repeat_rule.empty()) {
                    out += std::to_wstring(idx++) + L". " + t.text + L" [⚠️" + format_rule_display(t.repeat_rule, t.repeat_days) + L"任务：应于 " + tinfo + L" 前完成，暂未处理]\r\n";
                } else {
                    out += std::to_wstring(idx++) + L". " + t.text + L" （截止：" + tinfo + L"）\r\n";
                }
            }
        }
    }
    if (idx == 1) out += L"当前无紧急未完成事项，辛苦了！🎉\r\n";

    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        size_t bytes = (out.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            memcpy(GlobalLock(hMem), out.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
        showToast(L"✅ 交接事项已成功复制！");
    }
}

void App::checkReminders() {
    std::wstring curDate = today_str();
    if (curDate != m_lastSyncDate) {
        m_lastSyncDate = curDate;
        if (sync_repeat_plans(m_projects, m_plans, m_history)) {
            sortTodos();
            save_projects(m_projects);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
    std::wstring curTime = now_time_str();
    if (!m_projects.empty()) {
        for (auto& t : m_projects[0].todos) {
            if (t.due_date == curDate && t.due_time == curTime && !t.notified) {
                t.notified = true;
                showToast(L"⏰ 待办提醒：" + t.text);
            }
        }
    }
}

void App::toggleAutoStartFromTray() {
    bool target = !is_auto_start();
    if (set_auto_start(target)) {
        showToast(target ? L"✅ 已开启开机自动启动" : L"🚫 已取消开机自动启动");
    } else {
        showToast(L"❌ 权限不足，设置失败");
    }
}

void App::onWheel(int delta) {
    float step = (delta / 120.0f) * 40.0f;
    if (m_modal == MODAL_HISTORY || m_modal == MODAL_FOREST_STATS) {
        m_scrollModal = std::max(0.0f, m_scrollModal - step);
    } else if (m_view == VIEW_ALL) {
        m_scrollAll = std::max(0.0f, m_scrollAll - step);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}
