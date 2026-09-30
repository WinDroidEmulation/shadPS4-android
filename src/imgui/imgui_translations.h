// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <map>
#include <string>

namespace ImguiTranslate {

std::string tr(std::string input);

///////////// ImGui Translation Tables

// disable clang line limits for ease of translation
// clang-format off

const std::map<std::string, std::string> JapaneseMap = {
    {"Trophy Earned", "トロフィー獲得"},
};

const std::map<std::string, std::string> FrenchMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> FrenchCanadaMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> SpanishMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> SpanishLatinAmericanMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> GermanMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> ItalianMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> DutchMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> PortugesePtMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> PortugeseBrMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> RussianMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> KoreanMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> ChineseTraditionalMap = {
    {"Trophy Earned", "獲得獎盃"},
    // Big Picture top-level
    {"Select Game", "選擇遊戲"},
    {"Settings", "設定"},
    {"Exit", "退出"},
    {"Confirm Exit", "確認退出"},
    {"This will exit shadPS4!\nAre you sure?", "這將退出 shadPS4！\n確定要繼續嗎？"},
    {"OK", "確定"},
    {"Cancel", "取消"},
    {"Save", "儲存"},
    {"Apply", "套用"},
    // Settings categories
    {"Profiles", "設定檔"},
    {"General", "一般"},
    {"Graphics", "圖形"},
    {"Input", "輸入"},
    {"Trophy", "獎盃"},
    {"Game Folders", "遊戲資料夾"},
    {"Log", "日誌"},
    {"Experimental", "實驗功能"},
    // Save profile dialog
    {"Save Confirmation", "確認儲存"},
    {"Profile Saved:\n", "設定檔已儲存：\n"},
    // Delete profile dialog
    {"Confirm Delete", "確認刪除"},
    {"Delete this profile?", "刪除此設定檔？"},
    // Game folders
    {"Add Folder", "新增資料夾"},
    {"Add shadPS4 game folder", "新增 shadPS4 遊戲資料夾"},
    // Settings entries
    {"Console Language", "主機語言"},
    {"Audio Backend", "音訊後端"},
    {"Show Splash Screen When Launching Game", "啟動遊戲時顯示啟動畫面"},
    {"Display Mode", "顯示模式"},
    {"Present Mode", "呈現模式"},
    {"Window Width", "視窗寬度"},
    {"Window Height", "視窗高度"},
    {"Enable HDR", "啟用 HDR"},
    {"Enable FSR", "啟用 FSR"},
    {"Enable RCAS", "啟用 RCAS"},
    {"Enable Motion Controls", "啟用體感控制"},
    {"Enable Background Controller Input", "啟用背景控制器輸入"},
    {"Hide Cursor", "隱藏游標"},
    {"Hide Cursor Idle Timeout", "隱藏游標閒置逾時"},
    {"Disable Trophy Notification", "停用獎盃通知"},
    {"Trophy Notification Position", "獎盃通知位置"},
    {"Enable Logging", "啟用日誌"},
    {"Separate Log Files", "分離日誌檔案"},
    {"Log Sync", "日誌同步"},
    {"Readbacks Mode", "回讀模式"},
    {"Enable Readback Linear Images", "啟用線性圖像回讀"},
    {"Enable Direct Memory Access", "啟用直接記憶體存取"},
    {"Windows Guest Red Zone Protection (Requires Restart)", "Windows 客體紅區保護（需重啟）"},
    {"Enable Devkit Console Mode", "啟用開發機模式"},
    {"Enable PS4 Neo Mode", "啟用 PS4 Neo 模式"},
    {"Enable ShadNet", "啟用 ShadNet"},
    {"Set Network Connected to True", "將網路狀態設為已連線"},
    {"Enable Shader Cache", "啟用著色器快取"},
    {"Compress Shader Cache to Zip File", "將著色器快取壓縮為 ZIP"},
    {"Volume", "音量"},
    {"RCAS Attenuation", "RCAS 衰減"},
    {"Trophy Notification Duration", "獎盃通知持續時間"},
    {"Additional DMem Allocation", "額外 DMEM 分配"},
    {"Vblank Frequency", "Vblank 頻率"},
    // Touch overlay / compatibility-mode UI
    {"Compatibility mode — game loaded, but x86-64 → ARM64 "
     "interpreter is still in development (see ANDROID_PORT.md §6).",
     "相容模式 — 遊戲已載入，但 x86-64 → ARM64 解譯器仍在開發中"
     "（詳見 ANDROID_PORT.md §6）。"},
    {"Touch the on-screen buttons to test the virtual gamepad. "
     "When the interpreter backend lands, games will execute here.",
     "觸碰螢幕上的按鈕以測試虛擬手把。"
     "解譯器後端完成後，遊戲將在此執行。"},
    {"Hide message", "隱藏提示"},
    {"Back to Big Picture", "返回大圖模式"},
};

const std::map<std::string, std::string> ChineseSimplifiedMap = {
    {"Trophy Earned", "获得奖杯"},
    // Big Picture top-level
    {"Select Game", "选择游戏"},
    {"Settings", "设置"},
    {"Exit", "退出"},
    {"Confirm Exit", "确认退出"},
    {"This will exit shadPS4!\nAre you sure?", "这将退出 shadPS4！\n确定要继续吗？"},
    {"OK", "确定"},
    {"Cancel", "取消"},
    {"Save", "保存"},
    {"Apply", "应用"},
    // Settings window
    {"Settings", "设置"},
    {"Game Window", "游戏窗口"},
    // Settings categories
    {"Profiles", "配置档"},
    {"General", "常规"},
    {"Graphics", "图形"},
    {"Input", "输入"},
    {"Trophy", "奖杯"},
    {"Game Folders", "游戏文件夹"},
    {"Log", "日志"},
    {"Experimental", "实验功能"},
    // Save profile dialog
    {"Save Confirmation", "确认保存"},
    {"Profile Saved:\n", "配置档已保存：\n"},
    // Delete profile dialog
    {"Confirm Delete", "确认删除"},
    {"Delete this profile?", "删除此配置档？"},
    // Game folders
    {"Add Folder", "添加文件夹"},
    {"Add shadPS4 game folder", "添加 shadPS4 游戏文件夹"},
    // Settings entries - General
    {"Console Language", "主机语言"},
    {"Audio Backend", "音频后端"},
    {"Show Splash Screen When Launching Game", "启动游戏时显示启动画面"},
    // Settings entries - Graphics
    {"Display Mode", "显示模式"},
    {"Present Mode", "呈现模式"},
    {"Window Width", "窗口宽度"},
    {"Window Height", "窗口高度"},
    {"Enable HDR", "启用 HDR"},
    {"Enable FSR", "启用 FSR"},
    {"Enable RCAS", "启用 RCAS"},
    // Settings entries - Input
    {"Enable Motion Controls", "启用体感控制"},
    {"Enable Background Controller Input", "启用后台控制器输入"},
    {"Hide Cursor", "隐藏光标"},
    {"Hide Cursor Idle Timeout", "隐藏光标空闲超时"},
    // Settings entries - Trophy
    {"Disable Trophy Notification", "禁用奖杯通知"},
    {"Trophy Notification Position", "奖杯通知位置"},
    // Settings entries - Log
    {"Enable Logging", "启用日志"},
    {"Separate Log Files", "分离日志文件"},
    {"Log Sync", "日志同步"},
    // Settings entries - Experimental
    {"Readbacks Mode", "回读模式"},
    {"Enable Readback Linear Images", "启用线性图像回读"},
    {"Enable Direct Memory Access", "启用直接内存访问"},
    {"Windows Guest Red Zone Protection (Requires Restart)", "Windows 客体红区保护（需重启）"},
    {"Enable Devkit Console Mode", "启用开发机模式"},
    {"Enable PS4 Neo Mode", "启用 PS4 Neo 模式"},
    {"Enable ShadNet", "启用 ShadNet"},
    {"Set Network Connected to True", "将网络状态设为已连接"},
    {"Enable Shader Cache", "启用着色器缓存"},
    {"Compress Shader Cache to Zip File", "将着色器缓存压缩为 ZIP"},
    {"Volume", "音量"},
    {"RCAS Attenuation", "RCAS 衰减"},
    {"Trophy Notification Duration", "奖杯通知持续时间"},
    {"Additional DMem Allocation", "额外 DMEM 分配"},
    {"Vblank Frequency", "Vblank 频率"},
    // Touch overlay / compatibility-mode UI
    {"Compatibility mode — game loaded, but x86-64 → ARM64 "
     "interpreter is still in development (see ANDROID_PORT.md §6).",
     "兼容模式 — 游戏已加载，但 x86-64 → ARM64 解释器仍在开发中"
     "（详见 ANDROID_PORT.md §6）。"},
    {"Touch the on-screen buttons to test the virtual gamepad. "
     "When the interpreter backend lands, games will execute here.",
     "触摸屏幕上的按钮以测试虚拟手柄。"
     "解释器后端完成后，游戏将在此处运行。"},
    {"Hide message", "隐藏提示"},
    {"Back to Big Picture", "返回大图模式"},
};

const std::map<std::string, std::string> FinnishMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> SwedishMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> DanishMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> NorwegianMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> PolishMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> TurkishMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> ArabicMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> CzechMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> HungarianMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> GreekMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> RomanianMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> ThaiMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> VietnameseMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> IndonesianMap = {
    {"Trophy Earned", "Trophy Earned"},
};

const std::map<std::string, std::string> UkranianMap = {
    {"Trophy Earned", "Trophy Earned"},
};

// clang-format on

///////////// End ImGui Translation Tables

} // namespace ImguiTranslate
