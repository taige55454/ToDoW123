#pragma once
#include <string>
#include <vector>

struct TodoItem { 
    std::wstring uuid;
    std::wstring plan_uuid;
    std::wstring cycle_date;     // YYYY-MM-DD
    std::wstring text; 
    int priority = 0;            // 0: 普通, 1: 重要
    std::wstring due_date;       // YYYY-MM-DD
    std::wstring due_time;       // HH:MM
    std::wstring reminder_time;
    std::wstring repeat_rule;    // daily, weekdays, weekly, monthly
    std::vector<int> repeat_days;
    std::wstring original_due;
    std::wstring hide_until;
    std::wstring created;
    bool done = false; 
    bool notified = false;
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
    int focus_minutes = 25;
};

struct Plan {
    std::wstring plan_uuid;
    std::wstring text;
    int priority = 0;
    std::wstring due_time;
    std::wstring repeat_rule;    // daily / weekdays / weekly / monthly
    std::vector<int> repeat_days;
    std::wstring base_date;
};

struct HistoryItem { 
    std::wstring project;
    std::wstring text;
    std::wstring created;
    std::wstring completed;
    std::wstring plan_uuid;
    std::wstring cycle_date;
    std::wstring due_date;
};

struct ForestRecord {
    std::wstring timestamp;
    std::wstring status;         // healthy / withered
    std::wstring task;
    int minutes = 0;
};

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

std::wstring now_iso();
std::wstring today_str();
std::wstring tomorrow_str();
std::wstring add_days_str(const std::wstring& base_ymd, int days);
std::wstring now_time_str();
std::wstring new_uuid();
std::wstring format_rule_display(const std::wstring& rule, const std::vector<int>& days);

bool is_auto_start();
bool set_auto_start(bool en);

std::wstring utf8_to_w(const std::string& s);
std::string w_to_utf8(const std::wstring& ws);
std::wstring exe_dir();

bool sync_repeat_plans(std::vector<Project>& projs, std::vector<Plan>& plans, const std::vector<HistoryItem>& hist);
