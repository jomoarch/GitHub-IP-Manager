#include "hosts-manager.h"
#include "ip-fetcher.h"
#include "ip-tester.h"
#include "terminal-ui.h"
#include <iostream>

int main() {
  std::cout << "GitHub IP优化工具 v1.0" << std::endl;
  std::cout << "正在初始化..." << std::endl;

  // 初始化组件
  TerminalUI ui;
  IPFetcher fetcher;
  IPTester tester(3000, 8); // 3秒超时，8个线程
  HostsManager hosts_manager;

  while (true) {
    ui.showMainMenu();

    int choice;
    std::cin >> choice;
    std::cin.ignore(); // 清除换行符

    switch (choice) {
    case 1: { // 自动检测并优化
      std::vector<GitHubIP> ip_list;

      // 从GitHub API获取IP
      if (!fetcher.fetchFromGitHubAPI(ip_list)) {
        ui.printColored("从API获取失败，尝试备用源...\n", "yellow");
        if (!fetcher.fetchFromBackupSource(ip_list)) {
          ui.printColored("无法获取IP列表！\n", "red");
          break;
        }
      }

      // 批量测试IP质量
      ui.showProgressBar(0, ip_list.size(), "测试IP地址");
      tester.batchTest(ip_list, [&](int current, int total) {
        ui.showProgressBar(current, total, "测试IP地址");
      });

      // 按质量排序
      tester.sortByQuality(ip_list);

      // 让用户选择IP
      auto selected_ips = ui.selectIPsInteractive(ip_list);

      if (!selected_ips.empty()) {
        ui.printColored("您选择了 " + std::to_string(selected_ips.size()) +
                            " 个IP地址\n",
                        "green");

        if (ui.confirmDialog("确定要更新hosts文件吗？", true)) {
          if (hosts_manager.updateGitHubHosts(selected_ips)) {
            ui.printColored("hosts文件更新成功！\n", "green");

            if (ui.confirmDialog("是否刷新DNS缓存？", true)) {
              hosts_manager.flushDNSCache();
            }
          } else {
            ui.printColored("更新失败，请检查权限\n", "red");
          }
        }
      }

      std::cout << "按回车键继续...";
      std::cin.get();
      break;
    }

    case 2: { // 手动选择IP
      std::cout << "请输入GitHub域名（多个用空格分隔，回车使用默认）: ";
      std::string domains_input;
      std::getline(std::cin, domains_input);

      std::cout << "请输入IP地址（多个用空格分隔）: ";
      std::string ips_input;
      std::getline(std::cin, ips_input);

      // 这里可以添加手动输入的IP处理逻辑
      ui.printColored("手动输入功能开发中...\n", "yellow");
      break;
    }

    case 3: { // 查看当前设置
      auto current_entries = hosts_manager.getCurrentGitHubEntries();
      std::cout << "当前hosts文件中的GitHub条目:\n";
      std::cout << "================================\n";
      for (const auto &entry : current_entries) {
        std::cout << entry << std::endl;
      }
      std::cout << "================================\n";
      std::cout << "按回车键继续...";
      std::cin.get();
      break;
    }

    case 4: { // 恢复hosts
      if (ui.confirmDialog("确定要恢复原始hosts文件吗？", false)) {
        if (hosts_manager.restoreHosts("")) {
          ui.printColored("恢复成功！\n", "green");
          hosts_manager.flushDNSCache();
        } else {
          ui.printColored("恢复失败！\n", "red");
        }
      }
      std::cout << "按回车键继续...";
      std::cin.get();
      break;
    }

    case 5: // 退出
      ui.printColored("感谢使用！\n", "cyan");
      return 0;

    default:
      ui.printColored("无效的选择！\n", "red");
      break;
    }
  }

  return 0;
}
