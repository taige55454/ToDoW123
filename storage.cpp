#include "storage.h"
#include "json.h"
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <fstream>
#include <sstream>
#include <cwchar>
#include <algorithm>
#include <set>

#pragma comment(lib, "ole32.lib")

std::wstring utf8_to_w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], n);
    return ws;
}

std::string w_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring exe_dir() {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    size_t bs = p.find_last_of(L'\\');
    return bs != std::wstring::npos ? p.substr(0, bs + 1) : p;
}

static bool read_file(const std::wstring& path, std::string& out) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool write_file(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f.write(data.data(), (std::streamsize)data.size());
    f.close();
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    return true;
}

std::wstring new_uuid() {
    GUID g;
    CoCreateGuid(&g);
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
               g.Data1, g.Data2, g.Data3,
               g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
               g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

std::wstring now_iso() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring today_str() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return buf;
}

std::wstring add_days_str(const std::wstring& base_ymd, int days) {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    int y = 0, m = 0, d = 0;
    if (base_ymd.size() >= 10 && swscanf_s(base_ymd.c_str(), L"%d-%d-%d", &y, &m, &d) == 3) {
        st.wYear = (WORD)y; st.wMonth = (WORD)m; st.wDay = (WORD)d;
    }
    st.wHour = 12; st.wMinute = 0; st.wSecond = 0; st.wMilliseconds = 0;
    FILETIME ft; SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    u.QuadPart += (__int64)days * 864000000000LL;
    ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
    FileTimeToSystemTime(&ft, &st);
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return buf;
}

std::wstring tomorrow_str() {
    return add_days_str(today_str(), 1);
}

std::wstring now_time_str() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%02d:%02d", st.wHour, st.wMinute);
    return buf;
}

std::wstring format_rule_display(const std::wstring& rule, const std::vector<int>& days) {
    if (rule == L"daily") return L"每天";
    if (rule == L"weekdays") return L"工作日";
    if (rule == L"weekly") {
        if (days.empty()) return L"每周";
        const wchar_t* wnames[] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
        std::wstring s = L"每周";
        for (int d : days) if (d >= 0 && d < 7) s += wnames[d];
        return s;
    }
    if (rule == L"monthly") {
        if (days.empty()) return L"每月";
        std::wstring s = L"每月";
        for (size_t i = 0; i < days.size(); ++i) {
            if (i > 0) s += L",";
            s += std::to_wstring(days[i]);
        }
        s += L"日";
        return s;
    }
    return L"";
}

static void parse_todo_obj(const JsonValue& tt, TodoItem& t) {
    const JsonValue* tx = tt.find(L"text");
    const JsonValue* dn = tt.find(L"done");
    const JsonValue* cr = tt.find(L"created");
    const JsonValue* uid = tt.find(L"uuid");
    const JsonValue* puid = tt.find(L"plan_uuid");
    const JsonValue* cdate = tt.find(L"cycle_date");
    const JsonValue* hide = tt.find(L"hide_until");
    const JsonValue* orig = tt.find(L"original_due");
    const JsonValue* pri = tt.find(L"priority");
    const JsonValue* ddate = tt.find(L"due_date");
    const JsonValue* dtime = tt.find(L"due_time");
    const JsonValue* rrule = tt.find(L"repeat_rule");
    const JsonValue* rdays = tt.find(L"repeat_days");

    if (tx && tx->type == JsonValue::Str) t.text = tx->str;
    if (dn && dn->type == JsonValue::Bool) t.done = dn->b;
    if (cr && cr->type == JsonValue::Str) t.created = cr->str;
    t.uuid = (uid && uid->type == JsonValue::Str && !uid->str.empty()) ? uid->str : new_uuid();
    if (puid && puid->type == JsonValue::Str) t.plan_uuid = puid->str;
    if (ddate && ddate->type == JsonValue::Str) t.due_date = ddate->str;
    t.cycle_date = (cdate && cdate->type == JsonValue::Str && !cdate->str.empty())
                   ? cdate->str : (!t.due_date.empty() ? t.due_date : today_str());
    if (hide && hide->type == JsonValue::Str) t.hide_until = hide->str;
    if (orig && orig->type == JsonValue::Str) t.original_due = orig->str;
    if (pri && pri->type == JsonValue::Num) t.priority = (int)pri->num;
    if (dtime && dtime->type == JsonValue::Str) t.due_time = dtime->str;
    if (rrule && rrule->type == JsonValue::Str) t.repeat_rule = rrule->str;
    if (rdays && rdays->type == JsonValue::Arr) {
        for (const auto& d : rdays->arr) if (d.type == JsonValue::Num) t.repeat_days.push_back((int)d.num);
    }
}

std::vector<Project> load_projects() {
    std::string raw;
    if (!read_file(exe_dir() + L"todos.json", raw)) {
        return {{L"默认项目", {}}};
    }
    JsonValue root = json_parse(utf8_to_w(raw));
    Project defProj; defProj.name = L"默认项目";
    if (root.type == JsonValue::Arr) {
        for (const auto& it : root.arr) {
            if (it.type != JsonValue::Obj) continue;
            // 兼容 Python 版扁平数组与 C++ 版带 todos 的结构
            const JsonValue* todos = it.find(L"todos");
            if (todos && todos->type == JsonValue::Arr) {
                for (const auto& tt : todos->arr) {
                    TodoItem t; parse_todo_obj(tt, t);
                    if (!t.text.empty()) defProj.todos.push_back(t);
                }
            } else {
                TodoItem t; parse_todo_obj(it, t);
                if (!t.text.empty()) defProj.todos.push_back(t);
            }
        }
    }
    return {defProj};
}

void save_projects(const std::vector<Project>& projs) {
    // 统一保存为兼容 Python 版 main.py 的扁平数组结构
    JsonValue root; root.type = JsonValue::Arr;
    for (const auto& proj : projs) {
        for (const auto& t : proj.todos) {
            JsonValue to; to.type = JsonValue::Obj;
            to.obj.emplace_back(L"uuid", JsonValue{JsonValue::Str, false, 0, t.uuid});
            if (!t.plan_uuid.empty()) to.obj.emplace_back(L"plan_uuid", JsonValue{JsonValue::Str, false, 0, t.plan_uuid});
            to.obj.emplace_back(L"cycle_date", JsonValue{JsonValue::Str, false, 0, t.cycle_date});
            to.obj.emplace_back(L"text", JsonValue{JsonValue::Str, false, 0, t.text});
            to.obj.emplace_back(L"priority", JsonValue{JsonValue::Num, false, (double)t.priority, L""});
            if (!t.due_date.empty()) to.obj.emplace_back(L"due_date", JsonValue{JsonValue::Str, false, 0, t.due_date});
            if (!t.due_time.empty()) to.obj.emplace_back(L"due_time", JsonValue{JsonValue::Str, false, 0, t.due_time});
            if (!t.repeat_rule.empty()) to.obj.emplace_back(L"repeat_rule", JsonValue{JsonValue::Str, false, 0, t.repeat_rule});
            if (!t.repeat_days.empty()) {
                JsonValue rda; rda.type = JsonValue::Arr;
                for (int d : t.repeat_days) rda.arr.push_back(JsonValue{JsonValue::Num, false, (double)d, L""});
                to.obj.emplace_back(L"repeat_days", rda);
            }
            if (!t.hide_until.empty()) to.obj.emplace_back(L"hide_until", JsonValue{JsonValue::Str, false, 0, t.hide_until});
            if (!t.original_due.empty()) to.obj.emplace_back(L"original_due", JsonValue{JsonValue::Str, false, 0, t.original_due});
            to.obj.emplace_back(L"created", JsonValue{JsonValue::Str, false, 0, t.created});
            to.obj.emplace_back(L"done", JsonValue{JsonValue::Bool, t.done, 0, L""});
            root.arr.push_back(to);
        }
    }
    write_file(exe_dir() + L"todos.json", w_to_utf8(json_stringify(root, true)));
}

Config load_config() {
    Config cfg;
    std::string raw;
    if (!read_file(exe_dir() + L"config.json", raw)) return cfg;
    JsonValue root = json_parse(utf8_to_w(raw));
    if (root.type != JsonValue::Obj) return cfg;
    const JsonValue* tp = root.find(L"title_prefix"); if (tp && tp->type == JsonValue::Str && !tp->str.empty()) cfg.title_prefix = tp->str;
    const JsonValue* fm = root.find(L"focus_minutes"); if (fm && fm->type == JsonValue::Num) cfg.focus_minutes = std::max(1, std::min(120, (int)fm->num));
    return cfg;
}

void save_config(const Config& cfg) {
    JsonValue root; root.type = JsonValue::Obj;
    root.obj.emplace_back(L"title_prefix", JsonValue{JsonValue::Str, false, 0, cfg.title_prefix});
    root.obj.emplace_back(L"focus_minutes", JsonValue{JsonValue::Num, false, (double)cfg.focus_minutes, L""});
    write_file(exe_dir() + L"config.json", w_to_utf8(json_stringify(root, true)));
}

std::vector<Plan> load_plans() {
    std::string raw;
    if (!read_file(exe_dir() + L"plans.json", raw)) return {};
    JsonValue root = json_parse(utf8_to_w(raw));
    std::vector<Plan> plans;
    if (root.type != JsonValue::Arr) return plans;
    for (const auto& it : root.arr) {
        if (it.type != JsonValue::Obj) continue;
        Plan p;
        const JsonValue* puid = it.find(L"plan_uuid");
        const JsonValue* tx = it.find(L"text");
        const JsonValue* pri = it.find(L"priority");
        const JsonValue* dt = it.find(L"due_time");
        const JsonValue* rr = it.find(L"repeat_rule");
        const JsonValue* rd = it.find(L"repeat_days");
        const JsonValue* bd = it.find(L"base_date");
        p.plan_uuid = (puid && puid->type == JsonValue::Str && !puid->str.empty()) ? puid->str : new_uuid();
        if (tx && tx->type == JsonValue::Str) p.text = tx->str;
        if (pri && pri->type == JsonValue::Num) p.priority = (int)pri->num;
        if (dt && dt->type == JsonValue::Str) p.due_time = dt->str;
        p.repeat_rule = (rr && rr->type == JsonValue::Str && !rr->str.empty()) ? rr->str : L"daily";
        p.base_date = (bd && bd->type == JsonValue::Str && !bd->str.empty()) ? bd->str : today_str();
        if (rd && rd->type == JsonValue::Arr) {
            for (const auto& d : rd->arr) if (d.type == JsonValue::Num) p.repeat_days.push_back((int)d.num);
        }
        if (!p.text.empty()) plans.push_back(p);
    }
    return plans;
}

void save_plans(const std::vector<Plan>& plans) {
    JsonValue root; root.type = JsonValue::Arr;
    for (const auto& p : plans) {
        JsonValue o; o.type = JsonValue::Obj;
        o.obj.emplace_back(L"plan_uuid", JsonValue{JsonValue::Str, false, 0, p.plan_uuid});
        o.obj.emplace_back(L"text", JsonValue{JsonValue::Str, false, 0, p.text});
        o.obj.emplace_back(L"priority", JsonValue{JsonValue::Num, false, (double)p.priority, L""});
        if (!p.due_time.empty()) o.obj.emplace_back(L"due_time", JsonValue{JsonValue::Str, false, 0, p.due_time});
        o.obj.emplace_back(L"repeat_rule", JsonValue{JsonValue::Str, false, 0, p.repeat_rule});
        o.obj.emplace_back(L"base_date", JsonValue{JsonValue::Str, false, 0, p.base_date});
        JsonValue rda; rda.type = JsonValue::Arr;
        for (int d : p.repeat_days) rda.arr.push_back(JsonValue{JsonValue::Num, false, (double)d, L""});
        o.obj.emplace_back(L"repeat_days", rda);
        root.arr.push_back(o);
    }
    write_file(exe_dir() + L"plans.json", w_to_utf8(json_stringify(root, true)));
}

std::vector<HistoryItem> load_history() {
    std::string raw;
    if (!read_file(exe_dir() + L"history.json", raw)) return {};
    JsonValue root = json_parse(utf8_to_w(raw));
    std::vector<HistoryItem> items;
    if (root.type != JsonValue::Arr) return items;
    for (const auto& it : root.arr) {
        if (it.type != JsonValue::Obj) continue;
        HistoryItem h;
        const JsonValue* pj = it.find(L"project");
        const JsonValue* tx = it.find(L"text");
        const JsonValue* cr = it.find(L"created");
        const JsonValue* cp = it.find(L"completed");
        const JsonValue* puid = it.find(L"plan_uuid");
        const JsonValue* cdate = it.find(L"cycle_date");
        const JsonValue* ddate = it.find(L"due_date");
        if (pj && pj->type == JsonValue::Str) h.project = pj->str;
        if (tx && tx->type == JsonValue::Str) h.text = tx->str;
        if (cr && cr->type == JsonValue::Str) h.created = cr->str;
        if (cp && cp->type == JsonValue::Str) h.completed = cp->str;
        if (puid && puid->type == JsonValue::Str) h.plan_uuid = puid->str;
        if (cdate && cdate->type == JsonValue::Str) h.cycle_date = cdate->str;
        if (ddate && ddate->type == JsonValue::Str) h.due_date = ddate->str;
        if (!h.text.empty()) items.push_back(h);
    }
    return items;
}

void save_history(const std::vector<HistoryItem>& items) {
    JsonValue root; root.type = JsonValue::Arr;
    for (const auto& h : items) {
        JsonValue o; o.type = JsonValue::Obj;
        if (!h.plan_uuid.empty()) o.obj.emplace_back(L"plan_uuid", JsonValue{JsonValue::Str, false, 0, h.plan_uuid});
        if (!h.cycle_date.empty()) o.obj.emplace_back(L"cycle_date", JsonValue{JsonValue::Str, false, 0, h.cycle_date});
        o.obj.emplace_back(L"text", JsonValue{JsonValue::Str, false, 0, h.text});
        if (!h.due_date.empty()) o.obj.emplace_back(L"due_date", JsonValue{JsonValue::Str, false, 0, h.due_date});
        o.obj.emplace_back(L"created", JsonValue{JsonValue::Str, false, 0, h.created});
        o.obj.emplace_back(L"completed", JsonValue{JsonValue::Str, false, 0, h.completed});
        root.arr.push_back(o);
    }
    write_file(exe_dir() + L"history.json", w_to_utf8(json_stringify(root, true)));
}

std::vector<ForestRecord> load_forest() {
    std::string raw;
    if (!read_file(exe_dir() + L"forest.json", raw)) return {};
    JsonValue root = json_parse(utf8_to_w(raw));
    std::vector<ForestRecord> records;
    if (root.type != JsonValue::Arr) return records;
    for (const auto& it : root.arr) {
        if (it.type != JsonValue::Obj) continue;
        ForestRecord r;
        const JsonValue* ts = it.find(L"timestamp");
        const JsonValue* st = it.find(L"status");
        const JsonValue* tk = it.find(L"task");
        const JsonValue* mn = it.find(L"minutes");
        if (ts && ts->type == JsonValue::Str) r.timestamp = ts->str;
        if (st && st->type == JsonValue::Str) r.status = st->str;
        if (tk && tk->type == JsonValue::Str) r.task = tk->str;
        if (mn && mn->type == JsonValue::Num) r.minutes = (int)mn->num;
        records.push_back(r);
    }
    return records;
}

void save_forest(const std::vector<ForestRecord>& records) {
    JsonValue root; root.type = JsonValue::Arr;
    for (const auto& r : records) {
        JsonValue o; o.type = JsonValue::Obj;
        o.obj.emplace_back(L"timestamp", JsonValue{JsonValue::Str, false, 0, r.timestamp});
        o.obj.emplace_back(L"status", JsonValue{JsonValue::Str, false, 0, r.status});
        o.obj.emplace_back(L"task", JsonValue{JsonValue::Str, false, 0, r.task});
        o.obj.emplace_back(L"minutes", JsonValue{JsonValue::Num, false, (double)r.minutes, L""});
        root.arr.push_back(o);
    }
    write_file(exe_dir() + L"forest.json", w_to_utf8(json_stringify(root, true)));
}

static int max_days_in_month(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 30;
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) return 29;
    return days[month - 1];
}

bool sync_repeat_plans(std::vector<Project>& projs, std::vector<Plan>& plans, const std::vector<HistoryItem>& hist) {
    if (projs.empty()) projs.push_back({L"默认项目", {}});
    bool changed = false;

    // 兼容孤立的 repeat_rule 任务升级为 Plan
    for (auto& t : projs[0].todos) {
        if (!t.repeat_rule.empty() && t.plan_uuid.empty()) {
            t.plan_uuid = new_uuid();
            Plan p;
            p.plan_uuid = t.plan_uuid;
            p.text = t.text;
            p.priority = t.priority;
            p.due_time = t.due_time;
            p.repeat_rule = t.repeat_rule;
            p.repeat_days = t.repeat_days;
            p.base_date = !t.due_date.empty() ? t.due_date : today_str();
            plans.push_back(p);
            changed = true;
        }
    }

    SYSTEMTIME st; GetLocalTime(&st);
    int curWk = (st.wDayOfWeek == 0) ? 6 : (st.wDayOfWeek - 1); // 0=周一..6=周日
    std::wstring tdate = today_str();
    int maxD = max_days_in_month(st.wYear, st.wMonth);

    std::set<std::pair<std::wstring, std::wstring>> existing;
    for (const auto& t : projs[0].todos) {
        if (!t.plan_uuid.empty() && !t.cycle_date.empty())
            existing.insert({t.plan_uuid, t.cycle_date});
    }
    for (const auto& h : hist) {
        if (!h.plan_uuid.empty() && !h.cycle_date.empty())
            existing.insert({h.plan_uuid, h.cycle_date});
    }

    for (const auto& plan : plans) {
        if (!plan.base_date.empty() && tdate < plan.base_date) continue;
        bool hit = false;
        if (plan.repeat_rule == L"daily") hit = true;
        else if (plan.repeat_rule == L"weekdays" && curWk < 5) hit = true;
        else if (plan.repeat_rule == L"weekly") {
            if (plan.repeat_days.empty()) hit = (curWk == 0);
            else for (int d : plan.repeat_days) if (d == curWk) { hit = true; break; }
        } else if (plan.repeat_rule == L"monthly") {
            if (plan.repeat_days.empty()) hit = (st.wDay == 1);
            else for (int d : plan.repeat_days) if (std::min(d, maxD) == (int)st.wDay) { hit = true; break; }
        }

        if (hit && existing.find({plan.plan_uuid, tdate}) == existing.end()) {
            TodoItem item;
            item.uuid = new_uuid();
            item.plan_uuid = plan.plan_uuid;
            item.cycle_date = tdate;
            item.text = plan.text;
            item.priority = plan.priority;
            item.due_date = tdate;
            item.due_time = plan.due_time;
            item.reminder_time = plan.due_time;
            item.repeat_rule = plan.repeat_rule;
            item.repeat_days = plan.repeat_days;
            item.created = now_iso();
            item.done = false;
            projs[0].todos.push_back(item);
            existing.insert({plan.plan_uuid, tdate});
            changed = true;
        }
    }
    return changed;
}

bool is_auto_start() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    wchar_t buf[1024] = {0};
    DWORD sz = sizeof(buf);
    LONG r = RegQueryValueExW(hKey, L"EggDoneTodo", nullptr, nullptr, (BYTE*)buf, &sz);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

bool set_auto_start(bool en) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return false;
    bool ok = false;
    if (en) {
        wchar_t path[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring val = L"\"" + std::wstring(path) + L"\"";
        ok = (RegSetValueExW(hKey, L"EggDoneTodo", 0, REG_SZ, (BYTE*)val.c_str(), (DWORD)((val.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
    } else {
        LONG r = RegDeleteValueW(hKey, L"EggDoneTodo");
        ok = (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(hKey);
    return ok;
}
