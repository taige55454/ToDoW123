#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include "app.h"
#include "gfx.h"
#include <cmath>
#include <algorithm>

App* g_app = nullptr;

float App::toDip(int px) const { return g_gfx.dpiScale > 0 ? px / g_gfx.dpiScale : (float)px; }
int App::toPx(float dip) const { return (int)(dip * g_gfx.dpiScale + 0.5f); }

// ============================================================
// 快捷语法与自然语言解析器（高精度匹配）
// ============================================================
struct ParsedQuick {
    std::wstring title;
    int priority = 0;
    std::wstring due_date;
    std::wstring due_time;
    std::wstring repeat_rule;
    std::vector<int> repeat_days;
    std::wstring previewLabel;
};

static ParsedQuick ParseQuickAdd(const std::wstring& raw) {
    ParsedQuick q;
    std::wstring text = raw;
    std::wstring labels;

    // 1. 优先级
    if (text.find(L"!") == 0 || text.find(L"！") == 0 || text.find(L"重要") != std::wstring::npos) {
        q.priority = 1;
        labels += L"⭐ 重要 | ";
        if (text.find(L"!") == 0 || text.find(L"！") == 0) text.erase(0, 1);
        size_t p = text.find(L"重要");
        if (p != std::wstring::npos) text.erase(p, 2);
    }

    // 2. 日期解析
    SYSTEMTIME st; GetLocalTime(&st);
    auto makeDateStr = [](SYSTEMTIME s, int addDays) {
        FILETIME ft; SystemTimeToFileTime(&s, &ft);
        ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
        u.QuadPart += (__int64)addDays * 864000000000ULL;
        ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
        FileTimeToSystemTime(&ft, &s);
        wchar_t b[16] = {}; swprintf_s(b, L"%04d-%02d-%02d", s.wYear, s.wMonth, s.wDay);
        return std::wstring(b);
    };

    if (text.find(L"大后天") != std::wstring::npos) {
        q.due_date = makeDateStr(st, 3); labels += L"大后天 | ";
        text.erase(text.find(L"大后天"), 3);
    } else if (text.find(L"后天") != std::wstring::npos) {
        q.due_date = makeDateStr(st, 2); labels += L"后天 | ";
        text.erase(text.find(L"后天"), 2);
    } else if (text.find(L"明天") != std::wstring::npos) {
        q.due_date = makeDateStr(st, 1); labels += L"明天 | ";
        text.erase(text.find(L"明天"), 2);
    } else if (text.find(L"今天") != std::wstring::npos) {
        q.due_date = makeDateStr(st, 0); labels += L"今天 | ";
        text.erase(text.find(L"今天"), 2);
    }

    // 3. 周期解析
    if (text.find(L"每天") != std::wstring::npos) {
        q.repeat_rule = L"daily"; labels += L"🔁 每天 | ";
        text.erase(text.find(L"每天"), 2);
    } else if (text.find(L"工作日") != std::wstring::npos) {
        q.repeat_rule = L"weekdays"; labels += L"🔁 工作日 | ";
        text.erase(text.find(L"工作日"), 3);
    }

    // 4. 时间提取 (HH:MM)
    for (size_t i = 0; i + 4 < text.size(); ++i) {
        if (text[i] >= L'0' && text[i] <= L'2' && (text[i+2] == L':' || text[i+2] == L'：')) {
            wchar_t tb[8] = {};
            wcsncpy_s(tb, text.c_str() + i, 5);
            if (tb[2] == L'：') tb[2] = L':';
            q.due_time = tb;
            labels += L"🕒 " + q.due_time + L" | ";
            text.erase(i, 5);
            if (q.due_date.empty()) q.due_date = today_str();
            break;
        }
    }

    while (!text.empty() && (text.front() == L' ' || text.front() == L'\t')) text.erase(0, 1);
    while (!text.empty() && (text.back() == L' ' || text.back() == L'\t')) text.pop_back();
    q.title = text.empty() ? raw : text;
    if (!labels.empty() && labels.size() >= 3) labels = labels.substr(0, labels.size() - 3);
    q.previewLabel = labels;
    return q;
}

static int ComputeQuadrant(const TodoItem& t) {
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

void App::create(HWND hwnd) {
    g_app = this;
    m_hwnd = hwnd;
    m_projects = load_projects();
    m_plans = load_plans();
    m_history = load_history();
    m_cfg = load_config();
    m_forestRemain = (float)(m_cfg.focus_minutes * 60);
    m_forestTotal = m_forestRemain;
    
    // 自动派发周期计划
    if (sync_repeat_plans(m_projects, m_plans, m_history)) {
        save_projects(m_projects);
    }
}

void App::destroy() {
    save_projects(m_projects);
    save_plans(m_plans);
    save_config(m_cfg);
}

void App::expandWindow() {
    if (m_expanded) return;
    m_expanded = true;
    m_collapseTimer = 0.0f;
}

void App::collapseWindow() {
    if (!m_expanded) return;
    m_expanded = false;
    m_collapseTimer = 0.0f;
    m_inputText.clear();
    m_parsedPreview.clear();
}

void App::showToast(const std::wstring& msg) {
    m_toast.text = msg;
    m_toast.life = m_toast.maxLife;
    m_toast.alpha = 1.0f;
}

void App::copyHandover() {
    std::wstring txt = L"【前台未完成待办交接】\n交接时间：" + today_str() + L" " + now_time_str() + L"\n";
    txt += L"------------------------\n";
    int idx = 1;
    for (const auto& p : m_projects) {
        for (const auto& t : p.todos) {
            txt += std::to_wstring(idx++) + L". " + t.text;
            if (!t.due_time.empty()) txt += L" (截止: " + t.due_time + L")";
            txt += L"\n";
        }
    }
    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        size_t bytes = (txt.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            memcpy(GlobalLock(hMem), txt.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
        showToast(L"✅ 交接事项已成功复制！");
    }
}

void App::checkReminders() {
    std::wstring tdate = today_str();
    std::wstring ttime = now_time_str();
    for (const auto& p : m_projects) {
        for (const auto& t : p.todos) {
            if (t.due_date == tdate && t.due_time == ttime) {
                showToast(L"⏰ 待办提醒: " + t.text);
            }
        }
    }
}

void App::tick(float dt) {
    // 1. 平滑展开/收缩过渡
    float targetT = m_expanded ? 1.0f : 0.0f;
    float speed = m_expanded ? 14.0f : 10.0f;
    m_expandT += (targetT - m_expandT) * std::min(1.0f, dt * speed);

    // 2. 窗口尺寸与定位响应动画
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    float curW = AppC::BALL_SIZE + (AppC::PANEL_W - AppC::BALL_SIZE) * m_expandT;
    float curH = AppC::BALL_SIZE + (AppC::PANEL_H - AppC::BALL_SIZE) * m_expandT;
    int targetX = wa.right - toPx(curW) - 20;
    int targetY = wa.top + 60;

    SetWindowPos(m_hwnd, HWND_TOPMOST, targetX, targetY, toPx(curW), toPx(curH), SWP_NOACTIVATE);
    m_w = curW;
    m_h = curH;

    // 3. 鼠标悬停离开延迟收起检测
    POINT pt; GetCursorPos(&pt);
    RECT winRc; GetWindowRect(m_hwnd, &winRc);
    bool inside = PtInRect(&winRc, pt);

    if (inside && !m_mouseInside) {
        m_mouseInside = true;
        expandWindow();
    } else if (!inside && m_mouseInside) {
        m_mouseInside = false;
        m_collapseTimer = 0.4f; // 400ms 延迟
    }

    if (!m_mouseInside && m_expanded) {
        m_collapseTimer -= dt;
        if (m_collapseTimer <= 0.0f && m_inputText.empty()) {
            collapseWindow();
        }
    }

    // 4. 专注番茄钟计时
    if (m_plantState == PLANT_GROWING) {
        m_forestRemain -= dt;
        if (m_forestRemain <= 0.0f) {
            m_plantState = PLANT_RESTING;
            m_forestRemain = (float)(m_cfg.focus_minutes * 60);
            m_restRemain = m_restTotal;
            m_showSuccessDialog = true;
            
            ForestRecord rec;
            rec.timestamp = now_iso();
            rec.status = L"healthy";
            rec.task = L"专注完成";
            rec.minutes = m_cfg.focus_minutes;
            auto recs = load_forest();
            recs.push_back(rec);
            save_forest(recs);
        }
    } else if (m_plantState == PLANT_RESTING) {
        m_restRemain -= dt;
        if (m_restRemain <= 0.0f) {
            m_plantState = PLANT_IDLE;
            m_forestRemain = (float)(m_cfg.focus_minutes * 60);
        }
    }

    // 5. Toast 渐隐
    if (m_toast.life > 0.0f) {
        m_toast.life -= dt;
        m_toast.alpha = std::min(1.0f, m_toast.life / 0.4f);
    }

    // 6. 待确认状态复位
    if (!m_pendingUuid.empty()) {
        m_pendingTimer -= dt;
        if (m_pendingTimer <= 0.0f) {
            m_pendingUuid.clear();
        }
    }

    m_scroll += (m_scrollTarget - m_scroll) * std::min(1.0f, dt * 16.0f);
    rebuildHits();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::rebuildHits() {
    m_hits.clear();
    if (m_expandT < 0.2f) {
        m_hits.push_back({D2D1::RectF(0, 0, AppC::BALL_SIZE, AppC::BALL_SIZE), H_BALL, 0, 0});
        return;
    }

    // 顶部按钮热区
    m_hits.push_back({D2D1::RectF(160, 6, 200, 32), H_BTN_REFRESH, 0, 0});
    m_hits.push_back({D2D1::RectF(205, 6, 245, 32), H_BTN_HANDOVER, 0, 0});
    m_hits.push_back({D2D1::RectF(250, 6, 290, 32), H_BTN_HISTORY, 0, 0});
    m_hits.push_back({D2D1::RectF(295, 6, 335, 32), H_BTN_FOREST_STATS, 0, 0});

    // 导航热区
    float navY = AppC::TITLE_H + 4.0f;
    float tabW = (AppC::PANEL_W - 24.0f) / 4.0f;
    m_hits.push_back({D2D1::RectF(12 + tabW * 0, navY, 12 + tabW * 1 - 4, navY + 28), H_NAV_ALL, 0, 0});
    m_hits.push_back({D2D1::RectF(12 + tabW * 1, navY, 12 + tabW * 2 - 4, navY + 28), H_NAV_QUAD, 0, 0});
    m_hits.push_back({D2D1::RectF(12 + tabW * 2, navY, 12 + tabW * 3 - 4, navY + 28), H_NAV_PLANS, 0, 0});
    m_hits.push_back({D2D1::RectF(12 + tabW * 3, navY, 12 + tabW * 4 - 4, navY + 28), H_NAV_FOREST, 0, 0});

    // 输入框热区
    float inpY = AppC::PANEL_H - AppC::INPUT_H - 12.0f;
    m_hits.push_back({D2D1::RectF(12, inpY, AppC::PANEL_W - 12, inpY + 36), H_INPUT_BOX, 0, 0});

    if (m_showSuccessDialog) {
        float cx = AppC::PANEL_W * 0.5f, cy = AppC::PANEL_H * 0.5f;
        m_hits.push_back({D2D1::RectF(cx - 80, cy + 50, cx + 80, cy + 90), H_REST_FINISH, 0, 0});
        return;
    }

    // 视图特定热区
    if (m_view == VIEW_ALL && !m_projects.empty()) {
        float y = AppC::TITLE_H + AppC::NAV_H + 16.0f - m_scroll;
        for (size_t ti = 0; ti < m_projects[0].todos.size(); ++ti) {
            const auto& t = m_projects[0].todos[ti];
            if (!t.hide_until.empty() && today_str() < t.hide_until) continue;
            m_hits.push_back({D2D1::RectF(16, y + 4, 38, y + 40), H_TODO_CIRCLE, 0, (int)ti});
            m_hits.push_back({D2D1::RectF(40, y + 4, AppC::PANEL_W - 16, y + 40), H_TODO_TEXT, 0, (int)ti});
            y += AppC::ROW_H;
        }
    } else if (m_view == VIEW_PLANS) {
        float y = AppC::TITLE_H + AppC::NAV_H + 16.0f;
        m_hits.push_back({D2D1::RectF(12, y, AppC::PANEL_W - 12, y + 32), H_PLAN_ADD, 0, 0});
        y += 40.0f;
        for (size_t i = 0; i < m_plans.size(); ++i) {
            m_hits.push_back({D2D1::RectF(12, y, AppC::PANEL_W - 12, y + 44), H_PLAN_ITEM, (int)i, 0});
            y += 48.0f;
        }
    } else if (m_view == VIEW_FOREST) {
        float cy = AppC::PANEL_H * 0.5f + 80.0f;
        m_hits.push_back({D2D1::RectF(40, cy, AppC::PANEL_W * 0.5f - 10, cy + 40), H_FOREST_TOGGLE, 0, 0});
        m_hits.push_back({D2D1::RectF(AppC::PANEL_W * 0.5f + 10, cy, AppC::PANEL_W - 40, cy + 40), H_FOREST_GIVEUP, 0, 0});
    }
}

void App::render() {
    Gfx& g = g_gfx;
    g.rt->BeginDraw();
    g.rt->Clear(D2D1::ColorF(0, 0, 0, 0)); // 全透明基底

    if (m_expandT < 0.99f) {
        drawBall(g);
    }
    if (m_expandT > 0.01f) {
        drawPanel(g);
    }
    if (m_toast.alpha > 0.01f) {
        drawToast(g);
    }
    if (m_showSuccessDialog) {
        drawSuccessDialog(g);
    }

    g.rt->EndDraw();
}

void App::drawBall(Gfx& g) {
    float r = AppC::BALL_SIZE * 0.5f;
    D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(r, r), r - 2.0f, r - 2.0f);
    g.fillEllipse(r, r, r - 2.0f, r - 2.0f, D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.70f * (1.0f - m_expandT)));
    g.drawEllipse(r, r, r - 2.0f, r - 2.0f, D2D1::ColorF(1.0f, 0.84f, 0.0f, 0.85f * (1.0f - m_expandT)), 2.0f);

    const wchar_t* icon = (m_plantState == PLANT_GROWING) ? L"🌲" : L"🥚";
    g.drawText(icon, D2D1::RectF(0, 8, AppC::BALL_SIZE, 32), F_SYM_BOTTOM,
               D2D1::ColorF(1, 1, 1, 1.0f - m_expandT),
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (m_plantState == PLANT_GROWING) {
        int m = (int)(m_forestRemain / 60);
        int s = (int)(m_forestRemain) % 60;
        wchar_t b[16]; swprintf_s(b, L"%02d:%02d", m, s);
        g.drawText(b, D2D1::RectF(0, 32, AppC::BALL_SIZE, 54), F_FOOTER,
                   D2D1::ColorF(1.0f, 0.85f, 0.3f, 1.0f - m_expandT),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void App::drawPanel(Gfx& g) {
    float W = AppC::PANEL_W, H = AppC::PANEL_H;
    D2D1_RECT_F pRc = D2D1::RectF(0, 0, W, H);
    g.fillRoundedRect(pRc, 14.0f, D2D1::ColorF(0.98f, 0.98f, 0.99f, 0.99f * m_expandT));
    g.strokeRoundedRect(pRc, 14.0f, D2D1::ColorF(0.7f, 0.7f, 0.7f, 0.8f * m_expandT), 1.0f);

    // 1. 标题栏
    g.drawText(m_cfg.title_prefix, D2D1::RectF(14, 8, 140, AppC::TITLE_H), F_TITLE_BIG, C::TEXT);
    g.drawText(L"🔄刷新", D2D1::RectF(160, 8, 200, AppC::TITLE_H), F_HINT, C::MUTED);
    g.drawText(L"📋交接", D2D1::RectF(205, 8, 245, AppC::TITLE_H), F_HINT, C::MUTED);
    g.drawText(L"📚历史", D2D1::RectF(250, 8, 290, AppC::TITLE_H), F_HINT, C::MUTED);
    g.drawText(L"📜森林", D2D1::RectF(295, 8, 335, AppC::TITLE_H), F_HINT, C::MUTED);

    // 2. 导航栏
    float navY = AppC::TITLE_H + 4.0f;
    float tabW = (W - 24.0f) / 4.0f;
    const wchar_t* tabs[] = { L"📋 当前", L"🎯 象限", L"🗓️ 周期", L"🌲 森林" };
    for (int i = 0; i < 4; ++i) {
        float x1 = 12 + tabW * i, x2 = x1 + tabW - 4;
        bool sel = ((int)m_view == i);
        D2D1_RECT_F tbRc = D2D1::RectF(x1, navY, x2, navY + 26);
        g.fillRoundedRect(tbRc, 6.0f, sel ? Hex(0x2E7D32) : Hex(0xE0E0E0));
        g.drawText(tabs[i], tbRc, F_HINT, sel ? C::WHITE : Hex(0x444444),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // 3. 内容区
    float cTop = AppC::TITLE_H + AppC::NAV_H + 10.0f;
    float cBot = H - AppC::INPUT_H - 24.0f;
    g.rt->PushAxisAlignedClip(D2D1::RectF(0, cTop, W, cBot), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (m_view == VIEW_ALL && !m_projects.empty()) {
        float y = cTop + 4.0f - m_scroll;
        for (const auto& t : m_projects[0].todos) {
            if (!t.hide_until.empty() && today_str() < t.hide_until) continue;

            D2D1_RECT_F itemRc = D2D1::RectF(12, y, W - 12, y + AppC::ROW_H - 4.0f);
            g.fillRoundedRect(itemRc, 6.0f, Hex(0xFFFFFF));

            bool isPending = (t.uuid == m_pendingUuid);
            g.drawText(isPending ? L"✔️" : L"◯", D2D1::RectF(18, y + 8, 38, y + 36), F_SYM_BOTTOM,
                       isPending ? Hex(0x1565C0) : C::CIRCLE,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            std::wstring dText = isPending ? (L"【点击确认完成】 " + t.text) : t.text;
            g.drawText(dText, D2D1::RectF(42, y + 6, W - 100, y + AppC::ROW_H - 8), F_TODO,
                       isPending ? Hex(0x1565C0) : C::TEXT);

            // 徽标绘制
            float bX = W - 20.0f;
            if (!t.due_time.empty()) {
                std::wstring s = L"🕒" + t.due_time;
                float tw = g.measureTextW(s, F_HINT);
                bX -= (tw + 10.0f);
                g.fillRoundedRect(D2D1::RectF(bX, y + 8, bX + tw + 8.0f, y + 24), 4.0f, Hex(0xE8F5E9));
                g.drawText(s, D2D1::RectF(bX, y + 8, bX + tw + 8.0f, y + 24), F_HINT, Hex(0x2E7D32),
                           DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            if (t.priority == 1) {
                bX -= 26.0f;
                g.drawText(L"⭐", D2D1::RectF(bX, y + 8, bX + 22.0f, y + 24), F_HINT, Hex(0xFFB300),
                           DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            y += AppC::ROW_H;
        }
    } else if (m_view == VIEW_QUADRANTS && !m_projects.empty()) {
        float halfW = (W - 30.0f) * 0.5f;
        float halfH = (cBot - cTop - 12.0f) * 0.5f;

        auto drawQ = [&](int qNum, float left, float top, const wchar_t* qTitle, DWORD headCol, DWORD bgCol) {
            D2D1_RECT_F qRc = D2D1::RectF(left, top, left + halfW, top + halfH);
            g.fillRoundedRect(qRc, 8.0f, Hex(bgCol));
            D2D1_RECT_F hRc = D2D1::RectF(left, top, left + halfW, top + 24.0f);
            g.fillRoundedRect(hRc, 6.0f, Hex(headCol));
            g.drawText(qTitle, hRc, F_HINT, Hex(0x222222),
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            float ty = top + 28.0f;
            for (const auto& t : m_projects[0].todos) {
                if (ComputeQuadrant(t) == qNum) {
                    g.drawText(L"• " + t.text, D2D1::RectF(left + 6, ty, left + halfW - 6, ty + 18.0f), F_FOOTER, Hex(0x333333));
                    ty += 20.0f;
                    if (ty > top + halfH - 18.0f) break;
                }
            }
        };

        drawQ(1, 12, cTop, L"🔥 重要且紧急", 0xFFCDD2, 0xFFEBEE);
        drawQ(2, 18 + halfW, cTop, L"⭐ 重要不紧急", 0xC8E6C9, 0xE8F5E9);
        drawQ(3, 12, cTop + halfH + 8, L"⚡ 紧急不重要", 0xFFE082, 0xFFF8E1);
        drawQ(4, 18 + halfW, cTop + halfH + 8, L"🍃 不重要不紧急", 0xE0E0E0, 0xF5F5F5);
    } else if (m_view == VIEW_PLANS) {
        float y = cTop + 4.0f;
        g.fillRoundedRect(D2D1::RectF(12, y, W - 12, y + 32), 6.0f, Hex(0xE8F5E9));
        g.drawText(L"➕ 添加周期计划", D2D1::RectF(12, y, W - 12, y + 32), F_HINT, Hex(0x2E7D32),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        y += 40.0f;
        for (const auto& p : m_plans) {
            D2D1_RECT_F pRc = D2D1::RectF(12, y, W - 12, y + 42.0f);
            g.fillRoundedRect(pRc, 6.0f, Hex(0xFFFFFF));
            g.drawText(p.text, D2D1::RectF(20, y + 10, W - 80, y + 32.0f), F_TODO, Hex(0x333333));
            g.drawText(L"🔁 " + p.repeat_rule, D2D1::RectF(W - 80, y + 10, W - 20, y + 32.0f), F_HINT, Hex(0x2E7D32));
            y += 48.0f;
        }
    } else if (m_view == VIEW_FOREST) {
        float cx = W * 0.5f;
        const wchar_t* tree = (m_plantState == PLANT_GROWING) ? L"🌱" : (m_plantState == PLANT_RESTING ? L"☕" : L"🌰");
        g.drawText(tree, D2D1::RectF(cx - 50, cTop + 30, cx + 50, cTop + 130), F_SYM_BOTTOM, C::TEXT,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        int m = (int)((m_plantState == PLANT_RESTING ? m_restRemain : m_forestRemain) / 60);
        int s = (int)((m_plantState == PLANT_RESTING ? m_restRemain : m_forestRemain)) % 60;
        wchar_t tbuf[16]; swprintf_s(tbuf, L"%02d:%02d", m, s);
        g.drawText(tbuf, D2D1::RectF(cx - 100, cTop + 140, cx + 100, cTop + 180), F_ABOUT_TITLE, Hex(0x1B5E20),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // 按钮
        float by = cTop + 220.0f;
        D2D1_RECT_F btn1 = D2D1::RectF(40, by, cx - 10, by + 38);
        g.fillRoundedRect(btn1, 6.0f, Hex(0x4CAF50));
        g.drawText(m_plantState == PLANT_GROWING ? L"⏸️ 暂停" : L"🌱 开始种植", btn1, F_HINT, C::WHITE,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F btn2 = D2D1::RectF(cx + 10, by, W - 40, by + 38);
        g.fillRoundedRect(btn2, 6.0f, Hex(0xE53935));
        g.drawText(L"⚠️ 放弃", btn2, F_HINT, C::WHITE,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    g.rt->PopAxisAlignedClip();

    // 4. 输入框及解析提示
    float inpY = H - AppC::INPUT_H - 12.0f;
    if (!m_parsedPreview.empty()) {
        g.drawText(L"智能识别: " + m_parsedPreview, D2D1::RectF(14, inpY - 18, W - 14, inpY), F_FOOTER, Hex(0x2E7D32));
    }
    D2D1_RECT_F inRc = D2D1::RectF(12, inpY, W - 12, inpY + 36);
    g.fillRoundedRect(inRc, 6.0f, Hex(0xFFFFFF));
    g.strokeRoundedRect(inRc, 6.0f, Hex(0xBDBDBD), 1.0f);

    std::wstring showInp = m_inputText.empty() ? L"💡 快速新增 (如: ! 每天 18:00 交租)" : m_inputText;
    g.drawText(showInp, D2D1::RectF(20, inpY + 8, W - 20, inpY + 30), F_HINT,
               m_inputText.empty() ? C::MUTED : C::TEXT);
}

void App::drawToast(Gfx& g) {
    float W = AppC::PANEL_W, H = AppC::PANEL_H;
    float tw = g.measureTextW(m_toast.text, F_HINT) + 36.0f;
    float tx = (W - tw) * 0.5f;
    float ty = H * 0.5f - 20.0f;
    D2D1_RECT_F tRc = D2D1::RectF(tx, ty, tx + tw, ty + 40.0f);
    g.fillRoundedRect(tRc, 8.0f, D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.85f * m_toast.alpha));
    g.drawText(m_toast.text, tRc, F_HINT, D2D1::ColorF(1, 1, 1, m_toast.alpha),
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void App::drawSuccessDialog(Gfx& g) {
    float W = AppC::PANEL_W, H = AppC::PANEL_H;
    g.fillRect(D2D1::RectF(0, 0, W, H), D2D1::ColorF(0, 0, 0, 0.4f));

    float dw = 260.0f, dh = 240.0f;
    float dx = (W - dw) * 0.5f, dy = (H - dh) * 0.5f;
    D2D1_RECT_F dlgRc = D2D1::RectF(dx, dy, dx + dw, dy + dh);
    g.fillRoundedRect(dlgRc, 16.0f, Hex(0xF1F8E9));
    g.strokeRoundedRect(dlgRc, 16.0f, Hex(0x81C784), 2.0f);

    g.drawText(L"🌲", D2D1::RectF(dx, dy + 15, dx + dw, dy + 65), F_SYM_BOTTOM, C::TEXT,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g.drawText(L"专注完成！", D2D1::RectF(dx, dy + 70, dx + dw, dy + 95), F_TITLE_BIG, Hex(0x2E7D32),
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g.drawText(L"路漫漫其修远兮\n休息一下吧！", D2D1::RectF(dx, dy + 105, dx + dw, dy + 150), F_HINT, Hex(0x555555),
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    D2D1_RECT_F btn = D2D1::RectF(dx + 30, dy + 170, dx + dw - 30, dy + 212);
    g.fillRoundedRect(btn, 8.0f, Hex(0x4CAF50));
    g.drawText(L"开始休息 ☕", btn, F_HINT, C::WHITE,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void App::onMouseMove(float x, float y) {
    (void)x; (void)y;
}

void App::onLButtonDown(float x, float y) {
    rebuildHits();
    for (const auto& h : m_hits) {
        if (x >= h.rc.left && x <= h.rc.right && y >= h.rc.top && y <= h.rc.bottom) {
            switch (h.type) {
                case H_BALL: expandWindow(); return;
                case H_NAV_ALL: m_view = VIEW_ALL; return;
                case H_NAV_QUAD: m_view = VIEW_QUADRANTS; return;
                case H_NAV_PLANS: m_view = VIEW_PLANS; return;
                case H_NAV_FOREST: m_view = VIEW_FOREST; return;
                case H_BTN_REFRESH:
                    if (sync_repeat_plans(m_projects, m_plans, m_history)) save_projects(m_projects);
                    showToast(L"✅ 已刷新并派发");
                    return;
                case H_BTN_HANDOVER: copyHandover(); return;
                case H_BTN_HISTORY:
                    ShellExecuteW(m_hwnd, L"open", (exe_dir() + L"history.json").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return;
                case H_BTN_FOREST_STATS:
                    ShellExecuteW(m_hwnd, L"open", (exe_dir() + L"forest.json").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return;
                case H_TODO_CIRCLE:
                case H_TODO_TEXT: {
                    int ti = h.index2;
                    if (ti >= 0 && ti < (int)m_projects[0].todos.size()) {
                        const auto& uuid = m_projects[0].todos[ti].uuid;
                        if (m_pendingUuid == uuid) {
                            completeTodo(uuid);
                            m_pendingUuid.clear();
                        } else {
                            m_pendingUuid = uuid;
                            m_pendingTimer = 2.5f; // 2.5 秒内双击确认完成
                        }
                    }
                    return;
                }
                case H_PLAN_ADD: {
                    Plan p;
                    p.plan_uuid = new_uuid();
                    p.text = L"新建周期任务";
                    p.repeat_rule = L"daily";
                    p.base_date = today_str();
                    m_plans.push_back(p);
                    save_plans(m_plans);
                    sync_repeat_plans(m_projects, m_plans, m_history);
                    save_projects(m_projects);
                    showToast(L"✅ 已增加每日周期");
                    return;
                }
                case H_FOREST_TOGGLE:
                    m_plantState = (m_plantState == PLANT_GROWING) ? PLANT_IDLE : PLANT_GROWING;
                    return;
                case H_FOREST_GIVEUP:
                    if (m_plantState == PLANT_GROWING) {
                        m_plantState = PLANT_IDLE;
                        m_forestRemain = (float)(m_cfg.focus_minutes * 60);
                        ForestRecord rec;
                        rec.timestamp = now_iso();
                        rec.status = L"withered";
                        rec.task = L"中途放弃";
                        rec.minutes = m_cfg.focus_minutes;
                        auto recs = load_forest();
                        recs.push_back(rec);
                        save_forest(recs);
                        showToast(L"🥀 树苗枯萎了");
                    }
                    return;
                case H_REST_FINISH:
                    m_showSuccessDialog = false;
                    return;
                default: break;
            }
        }
    }
}

void App::onRButtonUp(float x, float y) {
    if (m_view != VIEW_ALL) return;
    rebuildHits();
    for (const auto& h : m_hits) {
        if ((h.type == H_TODO_CIRCLE || h.type == H_TODO_TEXT) &&
            x >= h.rc.left && x <= h.rc.right && y >= h.rc.top && y <= h.rc.bottom) {
            int ti = h.index2;
            if (ti >= 0 && ti < (int)m_projects[0].todos.size()) {
                std::wstring uid = m_projects[0].todos[ti].uuid;
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1, L"➡️ 延后一天 (智能视线隐藏)");
                AppendMenuW(hMenu, MF_STRING, 2, L"🗑️ 删除事项");
                POINT pt; GetCursorPos(&pt);
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
                DestroyMenu(hMenu);
                if (cmd == 1) postponeTodo(uid);
                else if (cmd == 2) deleteTodo(uid);
            }
            return;
        }
    }
}

void App::postponeTodo(const std::wstring& uuid) {
    for (auto& t : m_projects[0].todos) {
        if (t.uuid == uuid) {
            SYSTEMTIME st; GetLocalTime(&st);
            FILETIME ft; SystemTimeToFileTime(&st, &ft);
            ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
            u.QuadPart += 864000000000ULL;
            ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
            FileTimeToSystemTime(&ft, &st);
            wchar_t b[16] = {}; swprintf_s(b, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
            t.hide_until = b;
            t.due_date = b;
            save_projects(m_projects);
            showToast(L"➡️ 已延期并隐藏至明日");
            break;
        }
    }
}

void App::deleteTodo(const std::wstring& uuid) {
    auto& ts = m_projects[0].todos;
    for (auto it = ts.begin(); it != ts.end(); ++it) {
        if (it->uuid == uuid) {
            ts.erase(it);
            save_projects(m_projects);
            showToast(L"🗑️ 事项已移除");
            break;
        }
    }
}

void App::completeTodo(const std::wstring& uuid) {
    auto& ts = m_projects[0].todos;
    for (auto it = ts.begin(); it != ts.end(); ++it) {
        if (it->uuid == uuid) {
            HistoryItem h;
            h.project = m_projects[0].name;
            h.text = it->text;
            h.created = it->created;
            h.completed = now_iso();
            h.plan_uuid = it->plan_uuid;
            h.cycle_date = it->cycle_date;
            m_history.push_back(h);
            save_history(m_history);

            ts.erase(it);
            save_projects(m_projects);
            showToast(L"🎉 待办达成！");
            break;
        }
    }
}

void App::commitInput() {
    if (m_inputText.empty()) return;
    auto p = ParseQuickAdd(m_inputText);
    TodoItem item;
    item.uuid = new_uuid();
    item.text = p.title;
    item.priority = p.priority;
    item.due_date = p.due_date;
    item.due_time = p.due_time;
    item.repeat_rule = p.repeat_rule;
    item.repeat_days = p.repeat_days;
    item.created = now_iso();
    item.done = false;

    if (!p.repeat_rule.empty()) {
        Plan pl;
        pl.plan_uuid = new_uuid();
        pl.text = p.title;
        pl.priority = p.priority;
        pl.due_time = p.due_time;
        pl.repeat_rule = p.repeat_rule;
        pl.repeat_days = p.repeat_days;
        pl.base_date = today_str();
        m_plans.push_back(pl);
        save_plans(m_plans);
        item.plan_uuid = pl.plan_uuid;
    }

    m_projects[0].todos.insert(m_projects[0].todos.begin(), item);
    save_projects(m_projects);
    m_inputText.clear();
    m_parsedPreview.clear();
    showToast(L"💡 事项已新增");
}

void App::onChar(wchar_t ch) {
    if (!m_expanded) return;
    if (ch == L'\r' || ch == L'\n') {
        commitInput();
    } else if (ch == L'\b') {
        if (!m_inputText.empty()) {
            m_inputText.pop_back();
            auto p = ParseQuickAdd(m_inputText);
            m_parsedPreview = p.previewLabel;
        }
    } else if (ch >= 32) {
        m_inputText += ch;
        auto p = ParseQuickAdd(m_inputText);
        m_parsedPreview = p.previewLabel;
    }
}

void App::onKeyDown(int vk) {
    if (vk == VK_ESCAPE) {
        collapseWindow();
    }
}

void App::onWheel(int delta) {
    m_scrollTarget -= (delta / 120.0f) * 40.0f;
    if (m_scrollTarget < 0) m_scrollTarget = 0;
}
