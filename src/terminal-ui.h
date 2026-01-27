#ifndef TERMINAL_UI_H
#define TERMINAL_UI_H

#include "ip-fetcher.h"
#include <functional>
#include <string>
#include <vector>

class TerminalUI {
public:
  TerminalUI();

  // 显示主菜单
  void showMainMenu();

  // 显示IP列表供用户选择
  std::vector<GitHubIP>
  selectIPsInteractive(const std::vector<GitHubIP> &ip_list,
                       const std::string &title = "选择要应用的IP地址");

  // 显示进度条
  void showProgressBar(int current, int total, const std::string &message = "");

  // 显示彩色输出
  static void printColored(const std::string &text,
                           const std::string &color = "default",
                           bool bold = false);

  // 确认对话框
  bool confirmDialog(const std::string &question, bool default_yes = false);

  // 清屏
  static void clearScreen();

private:
  int terminal_width_;

  // 获取终端宽度
  void getTerminalSize();

  // 显示表格
  void displayTable(const std::vector<GitHubIP> &ip_list,
                    const std::vector<bool> &selected);

  // 设置终端颜色
  static void setColor(const std::string &color, bool bold = false);
  static void resetColor();
};

// 颜色定义
namespace TerminalColors {
const std::string RED = "\033[31m";
const std::string GREEN = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string BLUE = "\033[34m";
const std::string MAGENTA = "\033[35m";
const std::string CYAN = "\033[36m";
const std::string WHITE = "\033[37m";
const std::string RESET = "\033[0m";
const std::string BOLD = "\033[1m";
} // namespace TerminalColors

#endif
