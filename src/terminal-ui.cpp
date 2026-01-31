#include "terminal-ui.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

TerminalUI::TerminalUI() { getTerminalSize(); }

void TerminalUI::getTerminalSize() {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
    terminal_width_ = w.ws_col;
  } else {
    terminal_width_ = 80; // 默认宽度
  }
}

void TerminalUI::showMainMenu() {
  clearScreen();

  printColored("=", "cyan", true);
  for (int i = 0; i < terminal_width_ - 2; i++) {
    std::cout << "=";
  }
  std::cout << "=\n";

  printColored("  GitHub IP 优化工具", "cyan", true);
  std::cout << std::endl << std::endl;

  std::cout << "1. 自动检测并优化GitHub访问" << std::endl;
  std::cout << "2. 手动选择IP地址" << std::endl;
  std::cout << "3. 查看当前GitHub IP设置" << std::endl;
  std::cout << "4. 恢复原始hosts文件" << std::endl;
  std::cout << "5. 退出" << std::endl << std::endl;

  printColored("=", "cyan", true);
  for (int i = 0; i < terminal_width_ - 2; i++) {
    std::cout << "=";
  }
  std::cout << "=\n";

  std::cout << "请选择操作 (1-5): ";
}

void TerminalUI::showProgressBar(int current, int total,
                                 const std::string &message) {
  if (total <= 0) {
    std::cout << message << " [等待数据...]" << std::endl;
    return;
  }

  // 计算进度百分比
  float progress = static_cast<float>(current) / total;
  if (progress < 0)
    progress = 0;
  if (progress > 1)
    progress = 1;

  int bar_width = 50;
  int pos = bar_width * progress;
  int percent = static_cast<int>(progress * 100.0);

  // 使用回车符和清除行尾，确保整行刷新
  std::cout << "\r\033[K"; // \r回到行首，\033[K 清除从光标到行尾

  std::cout << message << " [";

  // 绘制进度条
  for (int i = 0; i < bar_width; ++i) {
    if (i < pos) {
      printColored("=", "green");
    } else if (i == pos) {
      printColored(">", "green");
    } else {
      std::cout << " ";
    }
  }

  std::cout << "] " << percent << "% (" << current << "/" << total << ")";

  // 强制刷新输出缓冲区
  std::cout.flush();

  // 当完成时换行
  if (current >= total) {
    std::cout << std::endl;
  }
}

void TerminalUI::printColored(const std::string &text, const std::string &color,
                              bool bold) {
  setColor(color, bold);
  std::cout << text;
  resetColor();
}

void TerminalUI::setColor(const std::string &color, bool bold) {
  if (color == "red") {
    std::cout << TerminalColors::RED;
  } else if (color == "green") {
    std::cout << TerminalColors::GREEN;
  } else if (color == "yellow") {
    std::cout << TerminalColors::YELLOW;
  } else if (color == "blue") {
    std::cout << TerminalColors::BLUE;
  } else if (color == "magenta") {
    std::cout << TerminalColors::MAGENTA;
  } else if (color == "cyan") {
    std::cout << TerminalColors::CYAN;
  }

  if (bold) {
    std::cout << TerminalColors::BOLD;
  }
}

void TerminalUI::resetColor() { std::cout << TerminalColors::RESET; }

void TerminalUI::clearScreen() { std::cout << "\033[2J\033[1;1H"; }

bool TerminalUI::confirmDialog(const std::string &question, bool default_yes) {
  std::cout << question << " [" << (default_yes ? "Y/n" : "y/N") << "]: ";
  std::string answer;
  std::getline(std::cin, answer);

  if (answer.empty()) {
    return default_yes;
  }

  return (answer[0] == 'Y' || answer[0] == 'y');
}

int TerminalUI::selectIPSource() {
  clearScreen();

  printColored("=", "cyan", true);
  for (int i = 0; i < terminal_width_ - 2; i++) {
    std::cout << "=";
  }
  std::cout << "=\n";

  printColored("  GitHub IP 获取来源选择", "cyan", true);
  std::cout << std::endl << std::endl;

  std::cout << "请选择IP获取方式:\n";
  std::cout << "1. GitHub官方API（完整，但测试慢）\n";
  std::cout << "   - 获取官方CIDR段，展开后测试\n";
  std::cout << "   - 包含所有可能的IP地址\n";
  std::cout << "   - 可能需要测试上千个IP\n\n";

  std::cout << "2. GitHub520备用源（快速，推荐）\n";
  std::cout << "   - 获取预验证的高质量IP列表\n";
  std::cout << "   - 通常包含30-50个已验证IP\n";
  std::cout << "   - 测试速度快，成功率高\n\n";

  std::cout << "3. 同时使用两者（最全面）\n";
  std::cout << "   - 合并两个来源的IP\n";
  std::cout << "   - 去重后统一测试\n";
  std::cout << "   - 最全面但测试时间最长\n";

  std::cout << std::endl;
  printColored("=", "cyan", true);
  for (int i = 0; i < terminal_width_ - 2; i++) {
    std::cout << "=";
  }
  std::cout << "=\n";

  std::cout << "请选择 (1-3): ";

  int choice;
  std::string input;
  std::getline(std::cin, input);

  try {
    choice = std::stoi(input);
  } catch (...) {
    choice = 0;
  }

  // 验证输入
  while (choice < 1 || choice > 3) {
    std::cout << "输入无效，请重新输入 (1-3): ";
    std::getline(std::cin, input);
    try {
      choice = std::stoi(input);
    } catch (...) {
      choice = 0;
    }
  }

  return choice;
}

void TerminalUI::showPermissionWarning() {
  clearScreen();

  printColored("╔════════════════════════════════════════════════════════╗\n",
               "red", true);
  printColored("║                      权限不足警告                      ║\n",
               "red", true);
  printColored("╠════════════════════════════════════════════════════════╣\n",
               "red", true);
  printColored("║                                                        ║\n",
               "red");
  printColored("║  当前程序没有root权限，以下功能将无法使用：            ║\n",
               "red");
  printColored("║                                                        ║\n",
               "red");
  printColored("║  • 更新hosts文件                                       ║\n",
               "red");
  printColored("║  • 恢复hosts文件                                       ║\n",
               "red");
  printColored("║  • 刷新DNS缓存                                         ║\n",
               "red");
  printColored("║                                                        ║\n",
               "red");
  printColored("║  请退出并使用 'sudo'. . .                              ║\n",
               "red");
  printColored("║                                                        ║\n",
               "red");
  printColored("╚════════════════════════════════════════════════════════╝\n",
               "red", true);

  std::cout << "\n按回车键继续（功能受限）...";
}

std::vector<GitHubIP>
TerminalUI::selectIPsNcduMode(const std::vector<GitHubIP> &ip_list,
                              const std::string &title) {

  // 过滤出有效IP
  std::vector<GitHubIP> valid_ips;
  std::copy_if(ip_list.begin(), ip_list.end(), std::back_inserter(valid_ips),
               [](const GitHubIP &ip) { return ip.is_valid; });

  if (valid_ips.empty()) {
    printColored("没有找到有效的IP地址！", "red", true);
    return {};
  }

  // 状态变量
  int cursor_pos = 0;
  int scroll_offset = 0;
  bool batch_mode = false;
  int batch_start = -1;
  std::vector<bool> selected(valid_ips.size(), false);
  bool quit = false;
  bool confirm = false;

  // 计算可显示行数
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int visible_rows = w.ws_row - 10; // 减去标题和状态栏

  // 设置终端为原始模式（读取单个字符）
  struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  // 清屏并隐藏光标
  std::cout << "\033[?25l"; // 隐藏光标

  while (!quit && !confirm) {
    // 清屏
    clearScreen();

    // 显示标题
    printColored("╔════════════════════════════════════════════════════════╗\n",
                 "cyan");
    printColored("║ ", "cyan");
    printColored(title, "cyan", true);
    for (int i = 0; i < 63 - title.length(); i++) {
      std::cout << " ";
    }
    printColored("║\n", "cyan");
    printColored("╚════════════════════════════════════════════════════════╝\n",
                 "cyan");

    // 显示统计信息
    int selected_count = std::count(selected.begin(), selected.end(), true);
    std::cout << "有效IP: " << valid_ips.size() << " 个 | ";
    std::cout << "已选择: " << selected_count << " 个 | ";
    std::cout << "光标: " << (cursor_pos + 1) << "/" << valid_ips.size();
    if (batch_mode) {
      printColored(" | 批量选择模式", "yellow", true);
    }
    std::cout << "\n";

    std::cout << std::string(terminal_width_, '-') << "\n";

    // 计算滚动位置
    if (cursor_pos < scroll_offset) {
      scroll_offset = cursor_pos;
    } else if (cursor_pos >= scroll_offset + visible_rows) {
      scroll_offset = cursor_pos - visible_rows + 1;
    }

    int line_num_width = std::to_string(valid_ips.size()).length();

    // 显示IP列表
    int end_row = std::min(scroll_offset + visible_rows, (int)valid_ips.size());
    for (int i = scroll_offset; i < end_row; i++) {
      const auto &ip = valid_ips[i];

      bool is_current = (i == cursor_pos);

      // ============ 左侧：行号区域 ============
      // 打印行号（右对齐，不补零）
      std::string line_num_str = std::to_string(i + 1);
      int padding = line_num_width - line_num_str.length();

      if (is_current) {
        std::cout << TerminalColors::UNDERLINE;
      }
      for (int p = 0; p < padding; p++) {
        std::cout << " ";
      }
      std::cout << line_num_str << " ║ ";
      if (is_current) {
        std::cout << TerminalColors::NO_UNDERLINE;
      }

      // ============ 右侧：内容区域 ============

      if (is_current) {
        std::cout << TerminalColors::UNDERLINE;
      }
      // 绘制行前缀
      if (batch_mode && batch_start != -1) {
        int start = std::min(batch_start, cursor_pos);
        int end = std::max(batch_start, cursor_pos);
        if (i >= start && i <= end) {
          printColored("│ ", "yellow"); // 批量选择竖线
        } else {
          std::cout << "  ";
        }
      } else {
        std::cout << "  ";
      }
      if (is_current) {
        std::cout << TerminalColors::NO_UNDERLINE;
      }

      // 光标指示
      if (is_current) {
        std::cout << TerminalColors::UNDERLINE;
        printColored("▶ ", "green", true); // 光标
        std::cout << TerminalColors::NO_UNDERLINE;
      } else {
        std::cout << "  ";
      }

      if (is_current) {
        std::cout << TerminalColors::UNDERLINE;
      }
      // 选择状态
      if (selected[i]) {
        printColored("● ", "green"); // 已选中
      } else {
        std::cout << "○ ";
      }
      if (is_current) {
        std::cout << TerminalColors::NO_UNDERLINE;
      }

      if (is_current) {
        std::cout << TerminalColors::UNDERLINE;
      }

      // IP信息
      std::cout << std::left << std::setw(18) << ip.address;

      // 域名（截断处理）
      std::string domain_display = ip.domain;
      if (domain_display.length() > 25) {
        domain_display = domain_display.substr(0, 22) + "...";
      }
      std::cout << std::setw(28) << domain_display;

      // 延迟
      if (ip.latency >= 0) {
        std::cout << std::setw(8) << ip.latency << "ms";
      } else {
        std::cout << std::setw(8) << "N/A";
      }

      // 质量指示器
      if (ip.latency >= 0) {
        if (ip.latency < 50) {
          printColored(" ██████", "green");
        } else if (ip.latency < 100) {
          printColored(" ████", "yellow");
        } else if (ip.latency < 200) {
          printColored(" ██", "red");
        }
      }

      if (is_current) {
        std::cout << TerminalColors::NO_UNDERLINE;
      }

      std::cout << "\n";
    }

    // 显示状态栏
    std::cout << std::string(terminal_width_, '-') << "\n";

    // 显示操作提示
    std::cout << "j:下移 k:上移 p:选择/取消 v:批选模式 o:跳转 e:确认 q:退出";
    if (batch_mode) {
      std::cout << " [批量选择模式中]";
    }
    std::cout << "\n";

    // 如果有跳转输入提示
    static bool jump_mode = false;
    static std::string jump_input;

    if (jump_mode) {
      std::cout << "跳转到行号 (1-" << valid_ips.size() << "): " << jump_input;
      std::cout.flush();
    }

    // 读取用户输入
    char ch;
    read(STDIN_FILENO, &ch, 1);

    if (jump_mode) {
      if (ch == '\n' || ch == '\r') { // 回车
        if (!jump_input.empty()) {
          try {
            int target = std::stoi(jump_input) - 1;
            if (target < 0)
              target = 0;
            if (target >= valid_ips.size())
              target = valid_ips.size() - 1;
            cursor_pos = target;
          } catch (...) {
            // 忽略无效输入
          }
        }
        jump_mode = false;
        jump_input.clear();
      } else if (ch == 27) { // ESC 取消
        jump_mode = false;
        jump_input.clear();
      } else if (ch == 127 || ch == 8) { // 退格
        if (!jump_input.empty()) {
          jump_input.pop_back();
        }
      } else if (isdigit(ch)) {
        jump_input += ch;
      }
      continue;
    }

    // 处理按键
    switch (ch) {
    case 'j': // 下移
      if (cursor_pos < valid_ips.size() - 1) {
        cursor_pos++;
        if (batch_mode && batch_start != -1) {
          // 更新批量标记
          int start = std::min(batch_start, cursor_pos);
          int end = std::max(batch_start, cursor_pos);
        }
      }
      break;

    case 'k': // 上移
      if (cursor_pos > 0) {
        cursor_pos--;
        if (batch_mode && batch_start != -1) {
          int start = std::min(batch_start, cursor_pos);
          int end = std::max(batch_start, cursor_pos);
        }
      }
      break;

    case 'p': // 选择/取消
      if (batch_mode && batch_start != -1) {
        // 批量选择/取消
        int start = std::min(batch_start, cursor_pos);
        int end = std::max(batch_start, cursor_pos);
        bool all_selected = true;
        for (int i = start; i <= end; i++) {
          if (!selected[i]) {
            all_selected = false;
            break;
          }
        }

        for (int i = start; i <= end; i++) {
          selected[i] = !all_selected;
        }
      } else {
        // 单个选择/取消
        selected[cursor_pos] = !selected[cursor_pos];
      }
      break;

    case 'v': // 批量选择模式
      batch_mode = !batch_mode;
      if (batch_mode) {
        batch_start = cursor_pos;
      } else {
        batch_start = -1;
      }
      break;

    case 'o': // 跳转
      jump_mode = true;
      jump_input.clear();
      break;

    case 'e': // 确认
      confirm = true;
      break;

    case 'q': // 退出
    case 27:  // ESC
      quit = true;
      break;

    case 'g': // 跳转到顶部
      cursor_pos = 0;
      break;

    case 'G': // 跳转到底部
      cursor_pos = valid_ips.size() - 1;
      break;

    case ' ': // 空格键也支持选择
      if (batch_mode && batch_start != -1) {
        // 批量选择/取消
        int start = std::min(batch_start, cursor_pos);
        int end = std::max(batch_start, cursor_pos);
        bool all_selected = true;
        for (int i = start; i <= end; i++) {
          if (!selected[i]) {
            all_selected = false;
            break;
          }
        }

        for (int i = start; i <= end; i++) {
          selected[i] = !all_selected;
        }
      } else {
        // 单个选择/取消
        selected[cursor_pos] = !selected[cursor_pos];
      }
      break;

    default:
      break;
    }
  }

  // 恢复终端设置
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  std::cout << "\033[?25h"; // 显示光标

  if (quit) {
    return {}; // 用户退出
  }

  // 收集选中的IP
  std::vector<GitHubIP> result;
  for (size_t i = 0; i < valid_ips.size(); i++) {
    if (selected[i]) {
      result.push_back(valid_ips[i]);
    }
  }

  // 显示选择结果
  clearScreen();
  if (!result.empty()) {
    printColored("已选择 " + std::to_string(result.size()) + " 个IP地址\n",
                 "green", true);
  } else {
    printColored("未选择任何IP地址\n", "yellow", true);
  }

  return result;
}
