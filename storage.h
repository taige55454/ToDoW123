#pragma once
#include <string>
#include <vector>

struct TodoItem { 
    std::wstring text; 
    bool done = false; 
    std::wstring created;
    std::wstring uuid;           // 唯一标识
    std::wstring plan_uuid;      // 关联的周期计划 ID
    std::wstring cycle_date;     // 周期实例锚点日期
    std::wstring hide_until;     // 隐藏截止日期（延期功能）
    std::wstring original_due;   // 原始截止日期（记录延期前的日期）
};

struct Project { 
    std::wstring name; 
    std::vector<TodoItem> todos; 
};

struct Config {
    std::wstring title_prefix = L"\u4f60\u597d"; 
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
    std::wstring plan_uuid;      // 关联周期计划
    std::wstring cycle_date;     // 周期锚点
};

struct ForestRecord {
    std::wstring timestamp;
    std::wstring status;         // healthy / withered
    std::wstring task;
    int minutes;
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

// 时间工具
std::wstring now_iso();
std::wstring iso_to_display(const std::wstring& iso);
std::wstring duration_text(const std::wstring& created, const std::wstring& completed);

// 文件写入
bool write_utf8_file(const std::wstring& filename, const std::wstring& text);

// 自启动
bool is_auto_start();
void set_auto_start(bool en);

// 编码转换
std::wstring utf8_to_w(const std::string& s);
std::string w_to_utf8(const std::wstring& ws);
std::wstring exe_dir();
