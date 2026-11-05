#pragma once
#include <string>
#include <vector>

struct TodoItem { 
    std::wstring uuid;           // 唯一标识
    std::wstring plan_uuid;      // 关联的周期计划 ID
    std::wstring cycle_date;     // 周期实例锚点日期 (YYYY-MM-DD)
    std::wstring text; 
    int priority = 0;            // 0: 普通, 1: 重要
    std::wstring due_date;       // 截止日期 (YYYY-MM-DD)
    std::wstring due_time;       // 截止时刻 (HH:MM)
    std::wstring reminder_time;
    std::wstring repeat_rule;    // daily, weekdays, weekly, monthly
    std::vector<int> repeat_days;
    std::wstring original_due;   // 原始截止日期
    std::wstring hide_until;     // 隐藏截止日期
    std::wstring created;
    bool done = false; 
};

struct Project { 
    std::wstring name; 
    std::vector<TodoItem> todos; 
};

struct Config {
    std::wstring title_prefix = L"待办"; 
    bool auto_start = false;
    int snap_anim = 1;
    int default_pos = 0; 
    int card_text_size = 1; 
    int focus_minutes = 25;      // 专注时长（分钟）
};

struct Plan {
    std::wstring plan_uuid;      // 计划唯一 ID
    std::wstring text;           // 计划名称
    int priority = 0;            // 优先级
    std::wstring due_time;       // 截止时刻
    std::wstring repeat_rule;    // 周期规则：daily/weekdays/weekly/monthly
    std::vector<int> repeat_days;// 具体日期（周几 0-6 或 月几号 1-31）
    std::wstring base_date;      // 基准日期
};

struct HistoryItem { 
    std::wstring project;
    std::wstring text;
    std::wstring created;
    std::wstring completed;
    std::wstring plan_uuid;
    std::wstring cycle_date;
};

struct ForestRecord {
    std::wstring timestamp;
    std::wstring status;         // healthy / withered
    std::wstring task;
    int minutes = 0;
};

// 数据加载与保存
std::vector<Project> load_projects();
void save_projects(const std::vector<Project>& projs);

Config load_config();
void save_config(const Config& cfg);

std::vector<Plan> load_plans();
void save_plans(const std::vector<Plan>& plans);

std::vector<HistoryItem> load_history();
void save_history(const std::vector<HistoryItem>& items);

std::vector<ForestRecord> load_forest();
void save_forest(const std::vector<ForestRecord>& records);

// 业务与时间工具
std::wstring now_iso();
std::wstring today_str();
std::wstring now_time_str();
std::wstring iso_to_display(const std::wstring& iso);
std::wstring duration_text(const std::wstring& created, const std::wstring& completed);
std::wstring new_uuid();

bool write_utf8_file(const std::wstring& filename, const std::wstring& text);
bool is_auto_start();
void set_auto_start(bool en);

std::wstring utf8_to_w(const std::string& s);
std::string w_to_utf8(const std::wstring& ws);
std::wstring exe_dir();

// 周期计划同步引擎
bool sync_repeat_plans(std::vector<Project>& projs, const std::vector<Plan>& plans, const std::vector<HistoryItem>& hist);
