#include "terminal-ui.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
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

std::vector<GitHubIP>
TerminalUI::selectIPsInteractive(const std::vector<GitHubIP> &ip_list,
                                 const std::string &title) {

  clearScreen();
  printColored(title, "yellow", true);
  std::cout << std::endl;

  std::vector<GitHubIP> valid_ips;
  std::copy_if(ip_list.begin(), ip_list.end(), std::back_inserter(valid_ips),
               [](const GitHubIP &ip) { return ip.is_valid; });

  if (valid_ips.empty()) {
    printColored("没有找到有效的IP地址！", "red", true);
    return {};
  }

  // 显示IP列表表格
  std::cout << std::left << std::setw(6) << "序号" << std::setw(18) << "IP地址"
            << std::setw(25) << "域名" << std::setw(10) << "延迟(ms)"
            << std::setw(8) << "状态" << std::endl;

  std::cout << std::string(terminal_width_, '-') << std::endl;

  for (size_t i = 0; i < valid_ips.size(); i++) {
    std::cout << std::left << std::setw(6) << (i + 1) << std::setw(18)
              << valid_ips[i].address << std::setw(25)
              << (valid_ips[i].domain.length() > 24
                      ? valid_ips[i].domain.substr(0, 21) + "..."
                      : valid_ips[i].domain)
              << std::setw(10);

    if (valid_ips[i].latency >= 0) {
      std::cout << valid_ips[i].latency;
    } else {
      std::cout << "N/A";
    }

    std::cout << std::setw(8);
    if (valid_ips[i].is_valid) {
      printColored("✓ 可用", "green");
    } else {
      printColored("✗ 无效", "red");
    }
    std::cout << std::endl;
  }

  std::cout << std::endl;
  std::cout << "选择要应用的IP（输入序号，多个用逗号分隔，0选择所有）: ";

  std::string input;
  std::getline(std::cin, input);

  std::vector<GitHubIP> selected_ips;

  if (input == "0") {
    // 选择所有
    return valid_ips;
  }

  std::istringstream iss(input);
  std::string token;
  while (std::getline(iss, token, ',')) {
    try {
      int index = std::stoi(token) - 1;
      if (index >= 0 && index < static_cast<int>(valid_ips.size())) {
        selected_ips.push_back(valid_ips[index]);
      }
    } catch (...) {
      // 忽略无效输入
    }
  }

  return selected_ips;
}

void TerminalUI::showProgressBar(int current, int total,
                                 const std::string &message) {
  int bar_width = 50;
  float progress = static_cast<float>(current) / total;
  int pos = bar_width * progress;

  std::cout << "\r" << message << " [";
  for (int i = 0; i < bar_width; ++i) {
    if (i < pos) {
      printColored("=", "green");
    } else if (i == pos) {
      printColored(">", "green");
    } else {
      std::cout << " ";
    }
  }
  std::cout << "] " << int(progress * 100.0) << "% (" << current << "/" << total
            << ")";
  std::cout.flush();

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
  std::cout << "   - 可能需要测试数百个IP\n\n";

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

// terminal-ui.cpp - 在文件末尾添加
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
