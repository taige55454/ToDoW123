#include "storage.h"
#include "json.h"
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <sstream>
#include <cwchar>
#include <algorithm>
#include <ctime>

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
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f.write(data.data(), (std::streamsize)data.size());
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

std::wstring now_time_str() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%02d:%02d", st.wHour, st.wMinute);
    return buf;
}

std::vector<Project> load_projects() {
    std::string raw;
    if (!read_file(exe_dir() + L"todos.json", raw)) {
        return {{L"默认项目", {}}};
    }
    JsonValue root = json_parse(utf8_to_w(raw));
    std::vector<Project> projs;
    if (root.type != JsonValue::Arr) return {{L"默认项目", {}}};
    
    for (const auto& it : root.arr) {
        if (it.type != JsonValue::Obj) continue;
        Project p;
        const JsonValue* name = it.find(L"name");
        const JsonValue* todos = it.find(L"todos");
        p.name = (name && name->type == JsonValue::Str) ? name->str : L"未命名项目";
        if (todos && todos->type == JsonValue::Arr) {
            for (const auto& tt : todos->arr) {
                TodoItem t;
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

                if (tx && tx->type == JsonValue::Str) t.text = tx->str;
                if (dn && dn->type == JsonValue::Bool) t.done = dn->b;
                if (cr && cr->type == JsonValue::Str) t.created = cr->str;
                t.uuid = (uid && uid->type == JsonValue::Str) ? uid->str : new_uuid();
                if (puid && puid->type == JsonValue::Str) t.plan_uuid = puid->str;
                t.cycle_date = (cdate && cdate->type == JsonValue::Str) ? cdate->str : today_str();
                if (hide && hide->type == JsonValue::Str) t.hide_until = hide->str;
                if (orig && orig->type == JsonValue::Str) t.original_due = orig->str;
                if (pri && pri->type == JsonValue::Num) t.priority = (int)pri->num;
                if (ddate && ddate->type == JsonValue::Str) t.due_date = ddate->str;
                if (dtime && dtime->type == JsonValue::Str) t.due_time = dtime->str;
                if (rrule && rrule->type == JsonValue::Str) t.repeat_rule = rrule->str;

                if (!t.text.empty()) p.todos.push_back(t);
            }
        }
        projs.push_back(p);
    }
    if (projs.empty()) projs.push_back({L"默认项目", {}});
    return projs;
}

void save_projects(const std::vector<Project>& projs) {
    JsonValue root;
    root.type = JsonValue::Arr;
    for (const auto& proj : projs) {
        JsonValue po; po.type = JsonValue::Obj;
        po.obj.emplace_back(L"name", JsonValue{JsonValue::Str, false, 0, proj.name});
        JsonValue ts; ts.type = JsonValue::Arr;
        for (const auto& t : proj.todos) {
            JsonValue to; to.type = JsonValue::Obj;
            to.obj.emplace_back(L"uuid", JsonValue{JsonValue::Str, false, 0, t.uuid});
            to.obj.emplace_back(L"plan_uuid", JsonValue{JsonValue::Str, false, 0, t.plan_uuid});
            to.obj.emplace_back(L"cycle_date", JsonValue{JsonValue::Str, false, 0, t.cycle_date});
            to.obj.emplace_back(L"text", JsonValue{JsonValue::Str, false, 0, t.text});
            to.obj.emplace_back(L"priority", JsonValue{JsonValue::Num, false, (double)t.priority, L""});
            to.obj.emplace_back(L"due_date", JsonValue{JsonValue::Str, false, 0, t.due_date});
            to.obj.emplace_back(L"due_time", JsonValue{JsonValue::Str, false, 0, t.due_time});
            to.obj.emplace_back(L"repeat_rule", JsonValue{JsonValue::Str, false, 0, t.repeat_rule});
            to.obj.emplace_back(L"hide_until", JsonValue{JsonValue::Str, false, 0, t.hide_until});
            to.obj.emplace_back(L"original_due", JsonValue{JsonValue::Str, false, 0, t.original_due});
            to.obj.emplace_back(L"created", JsonValue{JsonValue::Str, false, 0, t.created});
            to.obj.emplace_back(L"done", JsonValue{JsonValue::Bool, t.done, 0, L""});
            ts.arr.push_back(to);
        }
        po.obj.emplace_back(L"todos", ts);
        root.arr.push_back(po);
    }
    write_file(exe_dir() + L"todos.json", w_to_utf8(json_stringify(root, true)));
}

Config load_config() {
    Config cfg;
    std::string raw;
    if (!read_file(exe_dir() + L"config.json", raw)) return cfg;
    JsonValue root = json_parse(utf8_to_w(raw));
    if (root.type != JsonValue::Obj) return cfg;
    const JsonValue* tp = root.find(L"title_prefix"); if (tp && tp->type == JsonValue::Str) cfg.title_prefix = tp->str;
    const JsonValue* as = root.find(L"auto_start"); if (as && as->type == JsonValue::Bool) cfg.auto_start = as->b;
    const JsonValue* sa = root.find(L"snap_anim"); if (sa && sa->type == JsonValue::Num) cfg.snap_anim = (int)sa->num;
    const JsonValue* dp = root.find(L"default_pos"); if (dp && dp->type == JsonValue::Num) cfg.default_pos = (int)dp->num;
    const JsonValue* ts = root.find(L"card_text_size"); if (ts && ts->type == JsonValue::Num) cfg.card_text_size = (int)ts->num;
    const JsonValue* fm = root.find(L"focus_minutes"); if (fm && fm->type == JsonValue::Num) cfg.focus_minutes = (int)fm->num;
    return cfg;
}

void save_config(const Config& cfg) {
    JsonValue root; root.type = JsonValue::Obj;
    root.obj.emplace_back(L"title_prefix", JsonValue{JsonValue::Str, false, 0, cfg.title_prefix});
    root.obj.emplace_back(L"auto_start", JsonValue{JsonValue::Bool, cfg.auto_start, 0, L""});
    root.obj.emplace_back(L"snap_anim", JsonValue{JsonValue::Num, false, (double)cfg.snap_anim, L""});
    root.obj.emplace_back(L"default_pos", JsonValue{JsonValue::Num, false, (double)cfg.default_pos, L""});
    root.obj.emplace_back(L"card_text_size", JsonValue{JsonValue::Num, false, (double)cfg.card_text_size, L""});
    root.obj.emplace_back(L"focus_minutes", JsonValue{JsonValue::Num, false, (double)cfg.focus_minutes, L""});
    write_file(exe_dir() + L"config.json", w_to_utf8(json_stringify(root, false)));
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
        if (puid && puid->type == JsonValue::Str) p.plan_uuid = puid->str;
        if (tx && tx->type == JsonValue::Str) p.text = tx->str;
        if (pri && pri->type == JsonValue::Num) p.priority = (int)pri->num;
        if (dt && dt->type == JsonValue::Str) p.due_time = dt->str;
        if (rr && rr->type == JsonValue::Str) p.repeat_rule = rr->str;
        if (bd && bd->type == JsonValue::Str) p.base_date = bd->str;
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
        o.obj.emplace_back(L"due_time", JsonValue{JsonValue::Str, false, 0, p.due_time});
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
        if (pj && pj->type == JsonValue::Str) h.project = pj->str;
        if (tx && tx->type == JsonValue::Str) h.text = tx->str;
        if (cr && cr->type == JsonValue::Str) h.created = cr->str;
        if (cp && cp->type == JsonValue::Str) h.completed = cp->str;
        if (puid && puid->type == JsonValue::Str) h.plan_uuid = puid->str;
        if (cdate && cdate->type == JsonValue::Str) h.cycle_date = cdate->str;
        if (!h.text.empty()) items.push_back(h);
    }
    return items;
}

void save_history(const std::vector<HistoryItem>& items) {
    JsonValue root; root.type = JsonValue::Arr;
    for (const auto& h : items) {
        JsonValue o; o.type = JsonValue::Obj;
        o.obj.emplace_back(L"project", JsonValue{JsonValue::Str, false, 0, h.project});
        o.obj.emplace_back(L"text", JsonValue{JsonValue::Str, false, 0, h.text});
        o.obj.emplace_back(L"created", JsonValue{JsonValue::Str, false, 0, h.created});
        o.obj.emplace_back(L"completed", JsonValue{JsonValue::Str, false, 0, h.completed});
        o.obj.emplace_back(L"plan_uuid", JsonValue{JsonValue::Str, false, 0, h.plan_uuid});
        o.obj.emplace_back(L"cycle_date", JsonValue{JsonValue::Str, false, 0, h.cycle_date});
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

bool sync_repeat_plans(std::vector<Project>& projs, const std::vector<Plan>& plans, const std::vector<HistoryItem>& hist) {
    if (projs.empty()) projs.push_back({L"默认项目", {}});
    SYSTEMTIME st; GetLocalTime(&st);
    int curWk = (st.wDayOfWeek == 0) ? 6 : (st.wDayOfWeek - 1); // 0=Mon..6=Sun
    std::wstring tdate = today_str();

    bool added = false;
    for (const auto& plan : plans) {
        bool hit = false;
        if (plan.repeat_rule == L"daily") {
            hit = true;
        } else if (plan.repeat_rule == L"weekdays") {
            if (curWk < 5) hit = true;
        } else if (plan.repeat_rule == L"weekly") {
            for (int d : plan.repeat_days) {
                if (d == curWk) { hit = true; break; }
            }
        } else if (plan.repeat_rule == L"monthly") {
            for (int d : plan.repeat_days) {
                if (d == (int)st.wDay) { hit = true; break; }
            }
        }

        if (hit) {
            bool exists = false;
            for (const auto& p : projs) {
                for (const auto& t : p.todos) {
                    if (t.plan_uuid == plan.plan_uuid && t.cycle_date == tdate) {
                        exists = true; break;
                    }
                }
                if (exists) break;
            }
            if (!exists) {
                for (const auto& h : hist) {
                    if (h.plan_uuid == plan.plan_uuid && h.cycle_date == tdate) {
                        exists = true; break;
                    }
                }
            }

            if (!exists) {
                TodoItem item;
                item.uuid = new_uuid();
                item.plan_uuid = plan.plan_uuid;
                item.cycle_date = tdate;
                item.text = plan.text;
                item.priority = plan.priority;
                item.due_date = tdate;
                item.due_time = plan.due_time;
                item.repeat_rule = plan.repeat_rule;
                item.repeat_days = plan.repeat_days;
                item.created = now_iso();
                item.done = false;
                projs[0].todos.insert(projs[0].todos.begin(), item);
                added = true;
            }
        }
    }
    return added;
}

static bool parse_iso(const std::wstring& s, SYSTEMTIME& st) {
    st = {};
    if (s.size() < 19) return false;
    int y=0, mo=0, d=0, h=0, mi=0, se=0;
    if (swscanf_s(s.c_str(), L"%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return false;
    st.wYear = (WORD)y; st.wMonth = (WORD)mo; st.wDay = (WORD)d;
    st.wHour = (WORD)h; st.wMinute = (WORD)mi; st.wSecond = (WORD)se;
    return true;
}

std::wstring iso_to_display(const std::wstring& iso) {
    if (iso.empty()) return L"—";
    if (iso.size() >= 16) return iso.substr(0, 10) + L" " + iso.substr(11, 5);
    return iso;
}

std::wstring duration_text(const std::wstring& created, const std::wstring& completed) {
    SYSTEMTIME a, b;
    if (!parse_iso(created, a) || !parse_iso(completed, b)) return L"—";
    FILETIME fa, fb;
    if (!SystemTimeToFileTime(&a, &fa) || !SystemTimeToFileTime(&b, &fb)) return L"—";
    ULARGE_INTEGER ua, ub;
    ua.LowPart = fa.dwLowDateTime; ua.HighPart = fa.dwHighDateTime;
    ub.LowPart = fb.dwLowDateTime; ub.HighPart = fb.dwHighDateTime;
    __int64 diff = (__int64)((ub.QuadPart - ua.QuadPart) / 10000000ULL);
    if (diff < 0) diff = 0;
    long totalH = (long)(diff / 3600.0 + 0.5);
    if (totalH < 1) return L"不足1小时";
    long days = totalH / 24, hrs = totalH % 24;
    wchar_t buf[64] = {};
    if (days > 0 && hrs > 0) swprintf_s(buf, L"%ld天%ld小时", days, hrs);
    else if (days > 0) swprintf_s(buf, L"%ld天", days);
    else swprintf_s(buf, L"%ld小时", hrs);
    return buf;
}

bool write_utf8_file(const std::wstring& filename, const std::wstring& text) {
    return write_file(exe_dir() + filename, w_to_utf8(text));
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

void set_auto_start(bool en) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return;
    if (en) {
        wchar_t path[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring val = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(hKey, L"EggDoneTodo", 0, REG_SZ, (BYTE*)val.c_str(), (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"EggDoneTodo");
    }
    RegCloseKey(hKey);
}
