#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <shellapi.h>
#include "app.h"
#include "gfx.h"
#include "storage.h"
#include <cmath>
#include <algorithm>
#include <sstream>

App* g_app = nullptr;

// 自然语言解析器（无缝迁移 Python QuickAddParser）
static void ParseNaturalLanguage(const std::wstring& raw, std::wstring& cleanTitle, std::wstring& previewTags, 
                                 std::wstring& outDue, std::wstring& outTime, std::wstring& outRepeat, 
                                 std::vector<int>& outRepeatDays, int& outPriority) {
    cleanTitle = raw;
    previewTags.clear();
    outDue.clear();
    outTime.clear();
    outRepeat.clear();
    outRepeatDays.clear();
    outPriority = 0;

    if (cleanTitle.empty()) return;

    // 优先级
    if (cleanTitle.find(L"!") == 0 || cleanTitle.find(L"！") == 0) {
        outPriority = 1;
        previewTags += L"⭐重要 ";
        cleanTitle = cleanTitle.substr(1);
    } else if (cleanTitle.find(L"重要") != std::wstring::npos) {
        outPriority = 1;
        previewTags += L"⭐重要 ";
        size_t p = cleanTitle.find(L"重要");
        cleanTitle.erase(p, 2);
    }

    // 获取当前时间
    SYSTEMTIME st; GetLocalTime(&st);

    auto addDays = [](const SYSTEMTIME& base, int days) -> std::wstring {
        FILETIME ft; SystemTimeToFileTime(&base, &ft);
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
        u.QuadPart += (__int64)days * 864000000000ULL;
        ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
        SYSTEMTIME out; FileTimeToSystemTime(&ft, &out);
        wchar_t buf[16];
        swprintf_s(buf, L"%04d-%02d-%02d", out.wYear, out.wMonth, out.wDay);
        return buf;
    };

    // 日期
    if (cleanTitle.find(L"大后天") != std::wstring::npos) {
        outDue = addDays(st, 3); previewTags += L"大后天 ";
        cleanTitle.erase(cleanTitle.find(L"大后天"), 3);
    } else if (cleanTitle.find(L"后天") != std::wstring::npos) {
        outDue = addDays(st, 2); previewTags += L"后天 ";
        cleanTitle.erase(cleanTitle.find(L"后天"), 2);
    } else if (cleanTitle.find(L"明天") != std::wstring::npos) {
        outDue = addDays(st, 1); previewTags += L"明天 ";
        cleanTitle.erase(cleanTitle.find(L"明天"), 2);
    } else if (cleanTitle.find(L"今天") != std::wstring::npos) {
        outDue = addDays(st, 0); previewTags += L"今天 ";
        cleanTitle.erase(cleanTitle.find(L"今天"), 2);
    }

    // 周期
    if (cleanTitle.find(L"每天") != std::wstring::npos) {
        outRepeat = L"daily"; previewTags += L"🔁每天 ";
        cleanTitle.erase(cleanTitle.find(L"每天"), 2);
    } else if (cleanTitle.find(L"工作日") != std::wstring::npos) {
        outRepeat = L"weekdays"; previewTags += L"🔁工作日 ";
        cleanTitle.erase(cleanTitle.find(L"工作日"), 3);
    }

    // 时间匹配：HH:MM
    for (size_t i = 0; i + 4 < cleanTitle.size(); ++i) {
        if (iswdigit(cleanTitle[i]) && iswdigit(cleanTitle[i + 1]) &&
            (cleanTitle[i + 2] == L':' || cleanTitle[i + 2] == L'：') &&
            iswdigit(cleanTitle[i + 3]) && iswdigit(cleanTitle[i + 4])) {
            outTime = cleanTitle.substr(i, 2) + L":" + cleanTitle.substr(i + 3, 2);
            previewTags += L"🕒" + outTime + L" ";
            cleanTitle.erase(i, 5);
            break;
        }
    }

    // 清洗首尾空白
    size_t f = cleanTitle.find_first_not_of(L" \t\r\n");
    size_t e = cleanTitle.find_last_not_of(L" \t\r\n");
    if (f != std::wstring::npos && e >= f) cleanTitle = cleanTitle.substr(f, e - f + 1);
    else cleanTitle = raw;
}

static int CalculateQuadrant(const TodoItem& item) {
    bool imp = item.text.find(L"[⭐]") != std::wstring::npos;
    bool urgent = false;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t today[16]; swprintf_s(today, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);

    if (!item.cycle_date.empty() && item.cycle_date <= today) urgent = true;
    if (item.text.find(L"[今天]") != std::wstring::npos) urgent = true;

    if (imp && urgent) return 1;
    if (imp && !urgent) return 2;
    if (!imp && urgent) return 3;
    return 4;
}

float App::toDip(int px) const { return g_gfx.dpiScale > 0 ? px / g_gfx.dpiScale : (float)px; }
int App::toPx(float dip) const { return (int)(dip * g_gfx.dpiScale + 0.5f); }
void App::requestRedraw() { if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE); }

void App::saveAll() {
    save_projects(m_projects);
    save_plans(m_plans);
    save_history(m_history);
    save_forest(m_forestLogs);
    save_config(m_cfg);
}

void App::create(HWND hwnd) {
    g_app = this;
    m_hwnd = hwnd;
    m_projects = load_projects();
    if (m_projects.empty()) m_projects.push_back({L"默认清单", {}});
    m_plans = load_plans();
    m_history = load_history();
    m_forestLogs = load_forest();
    m_cfg = load_config();

    m_forestTotalSecs = (float)(m_cfg.focus_minutes * 60);
    m_forestRemainSecs = m_forestTotalSecs;

    syncPlansToToday();
    updateWindowShape(0.0f);
}

void App::destroy() {
    saveAll();
}

void App::showToast(const std::wstring& msg) {
    m_toastText = msg;
    m_toastAlpha = 1.0f;
    m_toastTimer = 2.0f;
    requestRedraw();
}

void App::updateWindowShape(float p) {
    // 缓动从 60x60 插值到 360x640
    float ease = 1.0f - std::pow(1.0f - p, 3.0f);
    m_w = AppC::BALL_SIZE + (AppC::PANEL_W - AppC::BALL_SIZE) * ease;
    m_h = AppC::BALL_SIZE + (AppC::PANEL_H - AppC::BALL_SIZE) * ease;

    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int curW = toPx(m_w);
    int curH = toPx(m_h);
    int posX = wa.right - curW - toPx(20);
    int posY = wa.top + toPx(60);

    SetWindowPos(m_hwnd, HWND_TOPMOST, posX, posY, curW, curH, SWP_NOACTIVATE);

    // 异形圆角裁切
    HRGN rgn = CreateRoundRectRgn(0, 0, curW + 1, curH + 1, 
                                  (int)(30.0f * g_gfx.dpiScale * (1.0f - ease) + 14.0f * g_gfx.dpiScale * ease),
                                  (int)(30.0f * g_gfx.dpiScale * (1.0f - ease) + 14.0f * g_gfx.dpiScale * ease));
    SetWindowRgn(m_hwnd, rgn, TRUE);
    DeleteObject(rgn);

    g_gfx.resize();
    rebuildHits();
}

void App::expandWindow() {
    m_expanded = true;
    m_collapseTimer = 0.0f;
}

void App::collapseWindow() {
    m_expanded = false;
    m_inputFocused = false;
}

void App::syncPlansToToday() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t todayBuf[16];
    swprintf_s(todayBuf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    std::wstring today = todayBuf;

    bool added = false;
    for (const auto& plan : m_plans) {
        bool hit = false;
        if (plan.repeat_rule == L"daily") hit = true;
        else if (plan.repeat_rule == L"weekdays" && (st.wDayOfWeek >= 1 && st.wDayOfWeek <= 5)) hit = true;
        else if (plan.repeat_rule == L"weekly") {
            for (int d : plan.repeat_days) {
                int pyWk = (st.wDayOfWeek == 0) ? 6 : st.wDayOfWeek - 1;
                if (d == pyWk) { hit = true; break; }
            }
        } else if (plan.repeat_rule == L"monthly") {
            for (int d : plan.repeat_days) {
                if (d == st.wDay) { hit = true; break; }
            }
        }

        if (hit) {
            bool exists = false;
            for (const auto& item : m_projects[0].todos) {
                if (item.plan_uuid == plan.plan_uuid && item.cycle_date == today) {
                    exists = true; break;
                }
            }
            if (!exists) {
                for (const auto& hist : m_history) {
                    if (hist.plan_uuid == plan.plan_uuid && hist.cycle_date == today) {
                        exists = true; break;
                    }
                }
            }
            if (!exists) {
                TodoItem ni;
                ni.text = plan.text;
                if (plan.priority == 1) ni.text = L"[⭐] " + ni.text;
                if (!plan.due_time.empty()) ni.text += L" [🕒" + plan.due_time + L"]";
                ni.created = now_iso();
                ni.plan_uuid = plan.plan_uuid;
                ni.cycle_date = today;
                m_projects[0].todos.push_back(ni);
                added = true;
            }
        }
    }
    if (added) saveAll();
}

void App::clampScroll() {
    float viewH = m_h - (AppC::TITLE_H + AppC::TAB_H + AppC::BOT_INPUT_H);
    float maxScroll = m_contentH - viewH;
    if (maxScroll < 0) maxScroll = 0;
    if (m_scrollTarget < 0) m_scrollTarget = 0;
    if (m_scrollTarget > maxScroll) m_scrollTarget = maxScroll;
    if (m_scroll < 0) m_scroll = 0;
    if (m_scroll > maxScroll) m_scroll = maxScroll;
}

void App::rebuildHits() {
    m_hits.clear();
    if (!m_expanded && m_expandProgress < 0.1f) {
        m_hits.push_back({D2D1::RectF(0, 0, AppC::BALL_SIZE, AppC::BALL_SIZE), H_BALL, -1, -1});
        return;
    }

    float W = m_w, H = m_h;
    // 顶部按钮热区
    m_hits.push_back({D2D1::RectF(160, 0, 205, AppC::TITLE_H), H_BTN_REFRESH, -1, -1});
    m_hits.push_back({D2D1::RectF(210, 0, 255, AppC::TITLE_H), H_BTN_HANDOVER, -1, -1});
    m_hits.push_back({D2D1::RectF(260, 0, 305, AppC::TITLE_H), H_BTN_HISTORY, -1, -1});
    m_hits.push_back({D2D1::RectF(310, 0, 355, AppC::TITLE_H), H_BTN_FOREST_LOG, -1, -1});

    // 4个视图 Tabs
    float tabY = AppC::TITLE_H;
    float tabW = (W - 24.0f) / 4.0f;
    m_hits.push_back({D2D1::RectF(12 + tabW * 0, tabY, 12 + tabW * 1, tabY + AppC::TAB_H), H_TAB_ALL, -1, -1});
    m_hits.push_back({D2D1::RectF(12 + tabW * 1, tabY, 12 + tabW * 2, tabY + AppC::TAB_H), H_TAB_QUAD, -1, -1});
    m_hits.push_back({D2D1::RectF(12 + tabW * 2, tabY, 12 + tabW * 3, tabY + AppC::TAB_H), H_TAB_PLANS, -1, -1});
    m_hits.push_back({D2D1::RectF(12 + tabW * 3, tabY, 12 + tabW * 4, tabY + AppC::TAB_H), H_TAB_FOREST, -1, -1});

    // 底部输入框热区
    m_hits.push_back({D2D1::RectF(12, H - AppC::BOT_INPUT_H, W - 12, H - 8), H_INPUT_BOX, -1, -1});

    // 中间内容热区
    float contentTop = AppC::TITLE_H + AppC::TAB_H + 6.0f;
    float contentY = contentTop - m_scroll;
    m_contentH = 0.0f;

    if (m_curView == VIEW_ALL) {
        for (size_t pi = 0; pi < m_projects.size(); ++pi) {
            for (size_t ti = 0; ti < m_projects[pi].todos.size(); ++ti) {
                float rowY = contentY;
                m_hits.push_back({D2D1::RectF(16, rowY, 44, rowY + AppC::ROW_H), H_TODO_CIRCLE, (int)pi, (int)ti});
                m_hits.push_back({D2D1::RectF(46, rowY, W - 16, rowY + AppC::ROW_H), H_TODO_TEXT, (int)pi, (int)ti});
                contentY += AppC::ROW_H + 4.0f;
                m_contentH += AppC::ROW_H + 4.0f;
            }
        }
    } else if (m_curView == VIEW_PLANS) {
        for (size_t i = 0; i < m_plans.size(); ++i) {
            float rowY = contentY;
            m_hits.push_back({D2D1::RectF(16, rowY, W - 16, rowY + AppC::ROW_H + 6.0f), H_PLAN_ITEM, (int)i, -1});
            contentY += AppC::ROW_H + 10.0f;
            m_contentH += AppC::ROW_H + 10.0f;
        }
    } else if (m_curView == VIEW_FOREST) {
        float btnY = contentTop + 240.0f;
        m_hits.push_back({D2D1::RectF(40, btnY, 170, btnY + 40), H_FOREST_START, -1, -1});
        m_hits.push_back({D2D1::RectF(190, btnY, 320, btnY + 40), H_FOREST_GIVEUP, -1, -1});
    }
}

void App::tick(float dt) {
    // 展开/折叠过渡动效
    float targetProg = m_expanded ? 1.0f : 0.0f;
    if (std::abs(m_expandProgress - targetProg) > 0.005f) {
        m_expandProgress += (targetProg - m_expandProgress) * std::min(1.0f, dt * 14.0f);
        updateWindowShape(m_expandProgress);
        requestRedraw();
    }

    // 鼠标离开延迟收起计数
    if (m_expanded && !m_mouseInside && !m_inputFocused) {
        m_collapseTimer += dt;
        if (m_collapseTimer >= 0.4f) {
            collapseWindow();
        }
    }

    // 专注森林秒级更新
    if (m_forestRunning) {
        m_forestRemainSecs -= dt;
        if (m_forestRemainSecs <= 0.0f) {
            m_forestRemainSecs = 0.0f;
            m_forestRunning = false;
            // 写入成活记录
            ForestRecord rec;
            rec.timestamp = now_iso();
            rec.status = L"healthy";
            rec.task = m_forestTaskText.empty() ? L"自由沉浸" : m_forestTaskText;
            rec.minutes = m_cfg.focus_minutes;
            m_forestLogs.push_back(rec);
            save_forest(m_forestLogs);
            showToast(L"🌲 专注完成！休息一下吧！");
        }
        requestRedraw();
    }

    // 待办双击完成倒计时确认
    if (m_confirmDonePi >= 0) {
        m_confirmDoneTimer -= dt;
        if (m_confirmDoneTimer <= 0.0f) {
            m_confirmDonePi = -1;
            m_confirmDoneTi = -1;
            requestRedraw();
        }
    }

    // Toast 渐隐动画
    if (m_toastAlpha > 0.0f) {
        m_toastTimer -= dt;
        if (m_toastTimer <= 0.0f) {
            m_toastAlpha -= dt * 3.0f;
            if (m_toastAlpha < 0.0f) m_toastAlpha = 0.0f;
        }
        requestRedraw();
    }

    // 输入框光标闪烁
    if (m_inputFocused) {
        m_cursorBlink += dt;
        requestRedraw();
    }

    // 平滑滚动插值
    if (std::abs(m_scroll - m_scrollTarget) > 0.5f) {
        m_scroll += (m_scrollTarget - m_scroll) * std::min(1.0f, dt * 16.0f);
        rebuildHits();
        requestRedraw();
    }
}

void App::render() {
    Gfx& g = g_gfx;
    g.rt->BeginDraw();
    g.rt->Clear(D2D1::ColorF(0, 0, 0, 0));

    // 1. 悬浮球视图 (Collapsed State)
    if (!m_expanded && m_expandProgress < 0.05f) {
        D2D1_RECT_F ballRc = D2D1::RectF(0, 0, AppC::BALL_SIZE, AppC::BALL_SIZE);
        g.fillEllipse(30, 30, 28, 28, D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.70f));
        g.drawEllipse(30, 30, 28, 28, D2D1::ColorF(1.0f, 0.84f, 0.0f, 0.80f), 2.0f);

        if (m_forestRunning) {
            int m = (int)(m_forestRemainSecs / 60);
            int s = (int)(m_forestRemainSecs) % 60;
            wchar_t tb[16]; swprintf_s(tb, L"%02d:%02d", m, s);
            g.drawText(L"🌲", D2D1::RectF(0, 8, AppC::BALL_SIZE, 30), F_HINT, Hex(0xFFD54F), DWRITE_TEXT_ALIGNMENT_CENTER);
            g.drawText(tb, D2D1::RectF(0, 32, AppC::BALL_SIZE, 50), F_FOOTER, Hex(0xFFD54F), DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            g.drawText(L"🥚", ballRc, F_SYM_BOTTOM, C::WHITE, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        g.rt->EndDraw();
        return;
    }

    // 2. 主面板视图 (Expanded State)
    float W = m_w, H = m_h;
    D2D1_RECT_F panelRc = D2D1::RectF(0, 0, W, H);
    g.fillRoundedRect(panelRc, 14.0f, D2D1::ColorF(0.98f, 0.98f, 0.99f, 0.98f));
    g.strokeRoundedRect(panelRc, 14.0f, D2D1::ColorF(0.70f, 0.70f, 0.70f, 0.80f), 1.0f);

    // 标题栏
    g.drawText(m_cfg.title_prefix.empty() ? L"待办" : m_cfg.title_prefix, 
               D2D1::RectF(14, 0, 150, AppC::TITLE_H), F_TITLE_BIG, Hex(0x222222),
               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g.drawText(L"🔄刷新", D2D1::RectF(160, 0, 205, AppC::TITLE_H), F_HINT, Hex(0x555555), DWRITE_TEXT_ALIGNMENT_CENTER);
    g.drawText(L"📋交接", D2D1::RectF(210, 0, 255, AppC::TITLE_H), F_HINT, Hex(0x555555), DWRITE_TEXT_ALIGNMENT_CENTER);
    g.drawText(L"📚历史", D2D1::RectF(260, 0, 305, AppC::TITLE_H), F_HINT, Hex(0x555555), DWRITE_TEXT_ALIGNMENT_CENTER);
    g.drawText(L"📜森林", D2D1::RectF(310, 0, 355, AppC::TITLE_H), F_HINT, Hex(0x555555), DWRITE_TEXT_ALIGNMENT_CENTER);

    // 视图导航栏
    float tabY = AppC::TITLE_H;
    float tabW = (W - 24.0f) / 4.0f;
    const wchar_t* tabTitles[] = { L"📋 当前", L"🎯 象限", L"🗓️ 周期", L"🌲 森林" };
    for (int i = 0; i < 4; ++i) {
        D2D1_RECT_F trc = D2D1::RectF(12 + tabW * i, tabY, 12 + tabW * (i + 1), tabY + AppC::TAB_H - 4);
        bool sel = (m_curView == (ViewMode)i);
        g.fillRoundedRect(trc, 6.0f, sel ? Hex(0x2E7D32) : Hex(0xE0E0E0));
        g.drawText(tabTitles[i], trc, F_HINT, sel ? C::WHITE : Hex(0x666666),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // 中间动态内容裁剪区
    float clipTop = AppC::TITLE_H + AppC::TAB_H;
    float clipBot = H - AppC::BOT_INPUT_H - 18.0f;
    g.rt->PushAxisAlignedClip(D2D1::RectF(12, clipTop, W - 12, clipBot), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (m_curView == VIEW_ALL) {
        float cy = clipTop + 4.0f - m_scroll;
        for (size_t pi = 0; pi < m_projects.size(); ++pi) {
            for (size_t ti = 0; ti < m_projects[pi].todos.size(); ++ti) {
                const auto& item = m_projects[pi].todos[ti];
                D2D1_RECT_F rowRc = D2D1::RectF(14, cy, W - 14, cy + AppC::ROW_H);
                g.fillRoundedRect(rowRc, 6.0f, Hex(0xFFFFFF));
                g.strokeRoundedRect(rowRc, 6.0f, Hex(0xEEEEEE), 1.0f);

                bool isPending = (m_confirmDonePi == (int)pi && m_confirmDoneTi == (int)ti);
                if (isPending) {
                    g.drawText(L"✔️", D2D1::RectF(16, cy, 40, cy + AppC::ROW_H), F_SYM_CHECK, Hex(0x1565C0), DWRITE_TEXT_ALIGNMENT_CENTER);
                    g.drawText(L"【再点一次确认完成】" + item.text, D2D1::RectF(44, cy, W - 20, cy + AppC::ROW_H), F_TODO, Hex(0x1565C0));
                } else {
                    g.drawEllipse(28, cy + AppC::ROW_H / 2, AppC::CIRCLE_R, AppC::CIRCLE_R, Hex(0x888888), 1.5f);
                    g.drawText(item.text, D2D1::RectF(44, cy, W - 20, cy + AppC::ROW_H), F_TODO, Hex(0x333333),
                               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                }
                cy += AppC::ROW_H + 4.0f;
            }
        }
    } else if (m_curView == VIEW_QUADRANTS) {
        float midX = W / 2.0f;
        float midY = clipTop + (clipBot - clipTop) / 2.0f;
        g.fillRoundedRect(D2D1::RectF(14, clipTop, midX - 4, midY - 4), 6.0f, Hex(0xFFEBEE));
        g.drawText(L"🔥 重要且紧急", D2D1::RectF(16, clipTop + 4, midX - 6, clipTop + 20), F_HINT, Hex(0xD32F2F));

        g.fillRoundedRect(D2D1::RectF(midX + 4, clipTop, W - 14, midY - 4), 6.0f, Hex(0xE8F5E9));
        g.drawText(L"⭐ 重要不紧急", D2D1::RectF(midX + 6, clipTop + 4, W - 16, clipTop + 20), F_HINT, Hex(0x2E7D32));

        g.fillRoundedRect(D2D1::RectF(14, midY + 4, midX - 4, clipBot), 6.0f, Hex(0xFFF8E1));
        g.drawText(L"⚡ 紧急不重要", D2D1::RectF(16, midY + 8, midX - 6, midY + 24), F_HINT, Hex(0xF57F17));

        g.fillRoundedRect(D2D1::RectF(midX + 4, midY + 4, W - 14, clipBot), 6.0f, Hex(0xF5F5F5));
        g.drawText(L"🍃 不重要不紧急", D2D1::RectF(midX + 6, midY + 8, W - 16, midY + 24), F_HINT, Hex(0x757575));

        // 渲染象限中的任务
        float qy[5] = {0, clipTop + 24, clipTop + 24, midY + 28, midY + 28};
        for (const auto& item : m_projects[0].todos) {
            int q = CalculateQuadrant(item);
            float qx1 = (q == 1 || q == 3) ? 18.0f : midX + 8.0f;
            float qx2 = (q == 1 || q == 3) ? midX - 8.0f : W - 18.0f;
            if (qy[q] + 16.0f < ((q <= 2) ? midY - 6 : clipBot - 4)) {
                g.drawText(L"• " + item.text, D2D1::RectF(qx1, qy[q], qx2, qy[q] + 16), F_FOOTER, Hex(0x333333));
                qy[q] += 18.0f;
            }
        }
    } else if (m_curView == VIEW_PLANS) {
        float cy = clipTop + 4.0f - m_scroll;
        for (size_t i = 0; i < m_plans.size(); ++i) {
            D2D1_RECT_F prc = D2D1::RectF(14, cy, W - 14, cy + AppC::ROW_H + 6.0f);
            g.fillRoundedRect(prc, 6.0f, Hex(0xE8F5E9));
            std::wstring ruleStr = L"🔁" + m_plans[i].repeat_rule;
            g.drawText(m_plans[i].text, D2D1::RectF(22, cy + 2, W - 90, cy + AppC::ROW_H + 4), F_TODO, Hex(0x2E7D32));
            g.drawText(ruleStr, D2D1::RectF(W - 90, cy + 4, W - 20, cy + AppC::ROW_H), F_HINT, Hex(0x1B5E20), DWRITE_TEXT_ALIGNMENT_TRAILING);
            cy += AppC::ROW_H + 10.0f;
        }
    } else if (m_curView == VIEW_FOREST) {
        D2D1_RECT_F treeBox = D2D1::RectF(W / 2 - 60, clipTop + 20, W / 2 + 60, clipTop + 140);
        g.fillRoundedRect(treeBox, 16.0f, Hex(0xE8F5E9, 0.85f));
        g.drawText(m_forestRunning ? L"🌲" : L"🌰", treeBox, F_ABOUT_TITLE, Hex(0x2E7D32),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        int rm = (int)(m_forestRemainSecs / 60);
        int rs = (int)(m_forestRemainSecs) % 60;
        wchar_t tbuf[16]; swprintf_s(tbuf, L"%02d:%02d", rm, rs);
        g.drawText(tbuf, D2D1::RectF(14, clipTop + 155, W - 14, clipTop + 195), F_ABOUT_TITLE, Hex(0x1B5E20), DWRITE_TEXT_ALIGNMENT_CENTER);

        // 按钮
        g.fillRoundedRect(D2D1::RectF(40, clipTop + 240, 170, clipTop + 280), 8.0f, Hex(0x4CAF50));
        g.drawText(m_forestRunning ? L"⏸️ 暂停" : L"🌱 开始种植", D2D1::RectF(40, clipTop + 240, 170, clipTop + 280),
                   F_HINT, C::WHITE, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        g.fillRoundedRect(D2D1::RectF(190, clipTop + 240, 320, clipTop + 280), 8.0f, Hex(0xE53935));
        g.drawText(L"⚠️ 放弃", D2D1::RectF(190, clipTop + 240, 320, clipTop + 280),
                   F_HINT, C::WHITE, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    g.rt->PopAxisAlignedClip();

    // 智能快捷标签实时识别预览
    if (!m_quickPreview.empty()) {
        g.drawText(L"智能识别: " + m_quickPreview, D2D1::RectF(16, H - AppC::BOT_INPUT_H - 18, W - 16, H - AppC::BOT_INPUT_H),
                   F_FOOTER, Hex(0x2E7D32));
    }

    // 底部快速新增输入框
    D2D1_RECT_F inRc = D2D1::RectF(12, H - AppC::BOT_INPUT_H, W - 12, H - 10);
    g.fillRoundedRect(inRc, 6.0f, C::WHITE);
    g.strokeRoundedRect(inRc, 6.0f, m_inputFocused ? Hex(0x2E7D32) : Hex(0xBDBDBD), 1.2f);
    if (m_quickInput.empty() && !m_inputFocused) {
        g.drawText(L"💡 快速新增 (如: ! 明天 18:00 交租)", D2D1::RectF(20, H - AppC::BOT_INPUT_H, W - 20, H - 10),
                   F_HINT, Hex(0x9E9E9E), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    } else {
        g.drawText(m_quickInput, D2D1::RectF(20, H - AppC::BOT_INPUT_H, W - 20, H - 10),
                   F_TODO, Hex(0x222222), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (m_inputFocused && ((int)(m_cursorBlink * 2) % 2 == 0)) {
            float tw = g.measureTextW(m_quickInput, F_TODO);
            float curX = 20.0f + tw + 1.0f;
            g.drawLine(curX, H - AppC::BOT_INPUT_H + 8, curX, H - 18, Hex(0x2E7D32), 1.5f);
        }
    }

    // Toast 浮层
    if (m_toastAlpha > 0.01f) {
        float tw = g.measureTextW(m_toastText, F_HINT) + 36.0f;
        D2D1_RECT_F trc = D2D1::RectF((W - tw) / 2, H / 2 - 20, (W + tw) / 2, H / 2 + 20);
        g.fillRoundedRect(trc, 8.0f, D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.85f * m_toastAlpha));
        g.drawText(m_toastText, trc, F_HINT, D2D1::ColorF(1.0f, 1.0f, 1.0f, m_toastAlpha),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (g.rt->EndDraw() == D2DERR_RECREATE_TARGET) g_gfx.recreate(m_hwnd);
}

void App::onMouseMove(float x, float y) {
    if (!m_mouseInside) {
        m_mouseInside = true;
        TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0};
        TrackMouseEvent(&tme);
        if (!m_expanded) expandWindow();
    }
    m_collapseTimer = 0.0f;
}

void App::onMouseLeave() {
    m_mouseInside = false;
    m_collapseTimer = 0.0f;
}

void App::commitQuickInput() {
    if (m_quickInput.empty()) return;
    std::wstring title, tags, due, timeStr, repeat;
    std::vector<int> days;
    int prio = 0;
    ParseNaturalLanguage(m_quickInput, title, tags, due, timeStr, repeat, days, prio);

    if (!repeat.empty()) {
        Plan p;
        p.plan_uuid = now_iso();
        p.text = title;
        p.priority = prio;
        p.due_time = timeStr;
        p.repeat_rule = repeat;
        p.repeat_days = days;
        p.base_date = due;
        m_plans.push_back(p);
    }

    TodoItem t;
    t.text = (tags.empty() ? L"" : tags) + title;
    t.created = now_iso();
    t.cycle_date = due;
    m_projects[0].todos.insert(m_projects[0].todos.begin(), t);

    m_quickInput.clear();
    m_quickPreview.clear();
    saveAll();
    syncPlansToToday();
    rebuildHits();
    showToast(L"✅ 事项添加成功！");
}

void App::completeTodo(int pi, int ti) {
    if (pi < 0 || pi >= (int)m_projects.size() || ti < 0 || ti >= (int)m_projects[pi].todos.size()) return;
    HistoryItem h;
    h.project = m_projects[pi].name;
    h.text = m_projects[pi].todos[ti].text;
    h.created = m_projects[pi].todos[ti].created;
    h.completed = now_iso();
    h.plan_uuid = m_projects[pi].todos[ti].plan_uuid;
    h.cycle_date = m_projects[pi].todos[ti].cycle_date;
    m_history.push_back(h);
    m_projects[pi].todos.erase(m_projects[pi].todos.begin() + ti);
    m_confirmDonePi = -1;
    m_confirmDoneTi = -1;
    saveAll();
    rebuildHits();
    showToast(L"🎉 事项已完成！");
}

void App::postponeTodo(int pi, int ti) {
    if (pi < 0 || pi >= (int)m_projects.size() || ti < 0 || ti >= (int)m_projects[pi].todos.size()) return;
    SYSTEMTIME st; GetLocalTime(&st);
    FILETIME ft; SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    u.QuadPart += 864000000000ULL;
    ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
    FileTimeToSystemTime(&ft, &st);
    wchar_t buf[16]; swprintf_s(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);

    m_projects[pi].todos[ti].cycle_date = buf;
    m_projects[pi].todos[ti].text += L" [延后至" + std::wstring(buf + 5) + L"]";
    saveAll();
    rebuildHits();
    showToast(L"➡️ 已智能延后一天");
}

void App::removeTodo(int pi, int ti) {
    if (pi < 0 || pi >= (int)m_projects.size() || ti < 0 || ti >= (int)m_projects[pi].todos.size()) return;
    m_projects[pi].todos.erase(m_projects[pi].todos.begin() + ti);
    saveAll();
    rebuildHits();
    showToast(L"🗑️ 事项已删除");
}

void App::copyHandover() {
    std::wstring out = L"【未完成待办交接】\r\n";
    int idx = 1;
    for (const auto& p : m_projects) {
        for (const auto& t : p.todos) {
            out += std::to_wstring(idx++) + L". " + t.text + L"\r\n";
        }
    }
    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        size_t sz = (out.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
        if (hMem) {
            memcpy(GlobalLock(hMem), out.c_str(), sz);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
        showToast(L"📋 交接事项已复制到剪贴板！");
    }
}

void App::openHistoryReport() {
    std::wstring h = L"<!DOCTYPE html><html><head><meta charset='utf-8'><title>历史复盘</title>";
    h += L"<style>body{font-family:sans-serif;background:#FAFAFA;padding:20px;} table{width:100%;border-collapse:collapse;} th,td{border:1px solid #DDD;padding:8px;text-align:left;} th{background:#E8F5E9;}</style></head><body>";
    h += L"<h2>📚 历史复盘中心</h2><table><tr><th>任务名称</th><th>创建时间</th><th>完成时间</th></tr>";
    for (auto it = m_history.rbegin(); it != m_history.rend(); ++it) {
        h += L"<tr><td>" + it->text + L"</td><td>" + it->created + L"</td><td>" + it->completed + L"</td></tr>";
    }
    h += L"</table></body></html>";
    write_utf8_file(L"history.html", h);
    ShellExecuteW(m_hwnd, L"open", (exe_dir() + L"history.html").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::openForestReport() {
    std::wstring h = L"<!DOCTYPE html><html><head><meta charset='utf-8'><title>专注森林</title>";
    h += L"<style>body{font-family:sans-serif;background:#F1F8E9;padding:20px;} .tree{font-size:32px;}</style></head><body>";
    h += L"<h2>🌲 专注森林档案</h2><ul>";
    for (auto it = m_forestLogs.rbegin(); it != m_forestLogs.rend(); ++it) {
        h += L"<li>" + std::wstring(it->status == L"healthy" ? L"🌲" : L"🥀") + L" [" + it->timestamp + L"] " + it->task + L" (" + std::to_wstring(it->minutes) + L"分钟)</li>";
    }
    h += L"</ul></body></html>";
    write_utf8_file(L"forest.html", h);
    ShellExecuteW(m_hwnd, L"open", (exe_dir() + L"forest.html").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::triggerForestToggle() {
    m_forestRunning = !m_forestRunning;
    requestRedraw();
}

void App::triggerForestGiveup() {
    if (!m_forestRunning) return;
    m_forestRunning = false;
    ForestRecord rec;
    rec.timestamp = now_iso();
    rec.status = L"withered";
    rec.task = L"中途放弃";
    rec.minutes = m_cfg.focus_minutes;
    m_forestLogs.push_back(rec);
    save_forest(m_forestLogs);
    m_forestRemainSecs = m_forestTotalSecs;
    showToast(L"🥀 树苗枯萎了！");
}

void App::onLButtonDown(float x, float y) {
    if (!m_expanded) {
        expandWindow();
        return;
    }
    for (const auto& h : m_hits) {
        if (x >= h.rc.left && x <= h.rc.right && y >= h.rc.top && y <= h.rc.bottom) {
            switch (h.type) {
                case H_TAB_ALL: m_curView = VIEW_ALL; rebuildHits(); requestRedraw(); return;
                case H_TAB_QUAD: m_curView = VIEW_QUADRANTS; rebuildHits(); requestRedraw(); return;
                case H_TAB_PLANS: m_curView = VIEW_PLANS; rebuildHits(); requestRedraw(); return;
                case H_TAB_FOREST: m_curView = VIEW_FOREST; rebuildHits(); requestRedraw(); return;
                case H_BTN_REFRESH: syncPlansToToday(); showToast(L"🔄 计划已同步！"); return;
                case H_BTN_HANDOVER: copyHandover(); return;
                case H_BTN_HISTORY: openHistoryReport(); return;
                case H_BTN_FOREST_LOG: openForestReport(); return;
                case H_INPUT_BOX: m_inputFocused = true; m_cursorBlink = 0; requestRedraw(); return;
                case H_TODO_CIRCLE: {
                    if (m_confirmDonePi == h.pi && m_confirmDoneTi == h.ti) {
                        completeTodo(h.pi, h.ti);
                    } else {
                        m_confirmDonePi = h.pi;
                        m_confirmDoneTi = h.ti;
                        m_confirmDoneTimer = 2.5f;
                        requestRedraw();
                    }
                    return;
                }
                case H_FOREST_START: triggerForestToggle(); return;
                case H_FOREST_GIVEUP: triggerForestGiveup(); return;
                default: break;
            }
        }
    }
    m_inputFocused = false;
}

void App::onLButtonDblClk(float, float) {}

void App::onRButtonUp(float x, float y) {
    for (const auto& h : m_hits) {
        if (x >= h.rc.left && x <= h.rc.right && y >= h.rc.top && y <= h.rc.bottom) {
            if (h.type == H_TODO_TEXT || h.type == H_TODO_CIRCLE) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 1, L"➡️ 延后一天 (智能视线隐藏)");
                AppendMenuW(menu, MF_STRING, 2, L"🗑️ 删除事项");
                POINT pt; GetCursorPos(&pt);
                int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
                DestroyMenu(menu);
                if (cmd == 1) postponeTodo(h.pi, h.ti);
                else if (cmd == 2) removeTodo(h.pi, h.ti);
                return;
            }
        }
    }
}

void App::onWheel(int delta) {
    m_scrollTarget -= (delta / 120.0f) * 45.0f;
    clampScroll();
    requestRedraw();
}

void App::onChar(wchar_t ch) {
    if (!m_inputFocused) return;
    if (ch == L'\r' || ch == L'\n') {
        commitQuickInput();
        return;
    }
    if (ch == 27) {
        m_quickInput.clear();
        m_quickPreview.clear();
        m_inputFocused = false;
        requestRedraw();
        return;
    }
    if (ch == L'\b') {
        if (!m_quickInput.empty()) m_quickInput.pop_back();
    } else {
        m_quickInput += ch;
    }

    // 实时语义标签分析
    std::wstring t, tags, due, tm, rep;
    std::vector<int> days;
    int p = 0;
    ParseNaturalLanguage(m_quickInput, t, tags, due, tm, rep, days, p);
    m_quickPreview = tags;
    m_cursorBlink = 0;
    requestRedraw();
}

void App::onKeyDown(int vk) {
    if (vk == VK_RETURN) commitQuickInput();
    else if (vk == VK_ESCAPE) {
        m_quickInput.clear();
        m_inputFocused = false;
        requestRedraw();
    }
}

LRESULT App::onNcHitTest(POINT screenPt) {
    POINT pt = screenPt; ScreenToClient(m_hwnd, &pt);
    if (!m_expanded) return HTCLIENT;
    if (pt.y < AppC::TITLE_H && pt.x < 150) return HTCAPTION; // 仅左上角空白区可拖动
    return HTCLIENT;
}

void App::showFromTray(bool) {
    ShowWindow(m_hwnd, SW_SHOW);
    expandWindow();
    SetForegroundWindow(m_hwnd);
}

void App::hideToTray() {
    ShowWindow(m_hwnd, SW_HIDE);
}

void App::exitFromTray() {
    saveAll();
    DestroyWindow(m_hwnd);
}
