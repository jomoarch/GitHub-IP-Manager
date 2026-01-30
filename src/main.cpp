#include "hosts-manager.h"
#include "ip-fetcher.h"
#include "ip-tester.h"
#include "terminal-ui.h"
#include <cstring>
#include <iostream>
#include <unistd.h>

bool isRunningAsRoot() { return geteuid() == 0; }

void restartWithSudo(char *argv[]) {
  std::cout << "\n检测到需要root权限，正在尝试使用sudo重新运行..." << std::endl;

  // 获取当前程序路径
  std::string program_path = "/proc/self/exe"; // Linux特定方法

  // 构建sudo命令
  std::vector<char *> args;
  args.push_back(strdup("sudo"));
  args.push_back(strdup(argv[0]));

  // 复制原始参数（如果有的话）
  for (int i = 1; argv[i] != nullptr; i++) {
    args.push_back(strdup(argv[i]));
  }
  args.push_back(nullptr); // execvp要求以nullptr结尾

  // 执行sudo命令
  execvp("sudo", args.data());

  // 如果execvp失败
  std::cerr << "无法使用sudo重新运行: " << strerror(errno) << std::endl;
  std::cout << "请手动使用 'sudo " << argv[0] << "' 运行程序" << std::endl;

  // 清理内存
  for (auto arg : args) {
    if (arg)
      free(arg);
  }
  exit(1);
}

int main(int argc, char *argv[]) {
  // 检查是否以root权限运行
  if (!isRunningAsRoot()) {
    std::cout << "\n╔════════════════════════════════════════════════╗\n";
    std::cout << "║            GitHub IP 优化工具                  ║\n";
    std::cout << "║                                                ║\n";
    std::cout << "║    警告：此工具需要root权限来修改系统文件！    ║\n";
    std::cout << "║                                                ║\n";
    std::cout << "╚════════════════════════════════════════════════╝\n";

    // 询问用户是否使用sudo重新运行
    std::cout << "\n检测到当前没有root权限。";
    std::cout << "\n\n选项:";
    std::cout << "\n  1. 使用sudo重新运行（推荐）";
    std::cout << "\n  2. 继续运行但功能受限";
    std::cout << "\n  3. 退出程序";
    std::cout << "\n\n请选择 (1-3): ";

    int choice;
    std::string input;
    std::getline(std::cin, input);

    try {
      choice = std::stoi(input);
    } catch (...) {
      choice = 0;
    }

    if (choice == 1) {
      // 重新运行
      restartWithSudo(argv);
      return 0; // 不会执行到这里
    } else if (choice == 2) {
      std::cout << "\n继续以非root权限运行，部分功能将受限。" << std::endl;
      std::cout << "按回车键继续...";
      std::cin.get();
    } else {
      std::cout << "\n程序已退出。" << std::endl;
      return 0;
    }
  }

  std::cout << "GitHub IP优化工具 v1.0" << std::endl;
  std::cout << "正在初始化..." << std::endl;

  // 初始化组件
  TerminalUI ui;
  IPFetcher fetcher;
  IPTester tester(1000, 10); // 1秒超时，10个线程
  HostsManager hosts_manager;

  while (true) {
    ui.showMainMenu();

    int choice;
    std::cin >> choice;
    std::cin.ignore(); // 清除换行符

    switch (choice) {
    case 1: { // 自动检测并优化
      // 让用户选择IP来源
      int source_choice = ui.selectIPSource();

      std::vector<GitHubIP> ip_list;
      bool api_success = false;
      bool backup_success = false;

      // 根据用户选择获取IP
      switch (source_choice) {
      case 1: { // 仅使用GitHub API
        std::cout << "\n使用GitHub官方API获取IP..." << std::endl;
        api_success = fetcher.fetchFromGitHubAPI(ip_list);
        if (!api_success) {
          ui.printColored("从API获取失败！\n", "red");
          break;
        }
        break;
      }

      case 2: { // 仅使用GitHub520
        std::cout << "\n使用GitHub520备用源获取IP..." << std::endl;
        backup_success = fetcher.fetchFromBackupSource(ip_list);
        if (!backup_success) {
          ui.printColored("从备用源获取失败！\n", "red");
          break;
        }
        break;
      }

      case 3: { // 两者合并
        std::cout << "\n同时获取两种来源的IP..." << std::endl;

        std::vector<GitHubIP> api_list;
        std::vector<GitHubIP> backup_list;

        // 并行获取（这里简化，实际可考虑多线程）
        api_success = fetcher.fetchFromGitHubAPI(api_list);
        backup_success = fetcher.fetchFromBackupSource(backup_list);

        if (!api_success && !backup_success) {
          ui.printColored("两个来源都获取失败！\n", "red");
          break;
        }

        if (api_success) {
          std::cout << "从API获取到 " << api_list.size() << " 个IP"
                    << std::endl;
        }
        if (backup_success) {
          std::cout << "从备用源获取到 " << backup_list.size() << " 个IP"
                    << std::endl;
        }

        // 合并去重
        if (api_success && backup_success) {
          // 优先使用备用源的IP（质量更高）
          ip_list = backup_list;
          IPFetcher::mergeIPLists(ip_list, api_list);
        } else if (api_success) {
          ip_list = api_list;
        } else {
          ip_list = backup_list;
        }

        std::cout << "合并去重后共有 " << ip_list.size() << " 个IP"
                  << std::endl;
        break;
      }
      }

      // 如果没有获取到IP，退出
      if (ip_list.empty()) {
        std::cout << "按回车键返回主菜单...";
        std::cin.get();
        break;
      }

      // 批量测试IP质量 - 使用三层检测
      tester.batchTest(ip_list, [&](int current, int total) {
        // 总进度 = 三层筛选 + 深度测试
        int filter_stages = ip_list.size() * 3;   // 每层都测试所有IP
        int depth_stages = total - filter_stages; // 深度测试的IP数

        if (current <= filter_stages) {
          // 还在筛选阶段
          int stage = (current - 1) / ip_list.size() + 1;
          ui.showProgressBar(current % ip_list.size(), ip_list.size(),
                             "快速筛选第" + std::to_string(stage) + "层");
        } else {
          // 进入深度测试阶段
          int depth_current = current - filter_stages;
          ui.showProgressBar(depth_current, depth_stages, "深度测试");
        }
      });

      // 按质量排序
      tester.sortByQuality(ip_list);

      // 让用户选择IP（使用ncdu风格的交互界面）
      auto selected_ips = ui.selectIPsNcduMode(ip_list);

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
