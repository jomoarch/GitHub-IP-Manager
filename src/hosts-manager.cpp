#include "hosts-manager.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

HostsManager::HostsManager() {
  // 设置默认备份路径
  char *home_dir = getenv("HOME");
  if (home_dir) {
    default_backup_path_ = std::string(home_dir) + "/.GHIP.backup/hosts.backup";
  } else {
    default_backup_path_ = "/tmp/hosts.backup";
  }
}

bool HostsManager::hasRootPrivilege() { return geteuid() == 0; }

std::string HostsManager::executeCommand(const std::string &cmd) {
  char buffer[128];
  std::string result = "";
  FILE *pipe = popen(cmd.c_str(), "r");

  if (!pipe) {
    return "ERROR";
  }

  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result += buffer;
  }

  pclose(pipe);
  return result;
}

bool HostsManager::backupHosts(const std::string &backup_path) {
  std::string backup_file =
      backup_path.empty() ? default_backup_path_ : backup_path;

  // 创建备份目录
  std::filesystem::path backup_dir =
      std::filesystem::path(backup_file).parent_path();
  if (!backup_dir.empty() && !std::filesystem::exists(backup_dir)) {
    if (!std::filesystem::create_directories(backup_dir)) {
      std::cerr << "无法创建备份目录: " << backup_dir << std::endl;
      return false;
    }
  }

  // 复制hosts文件
  std::ifstream src("/etc/hosts", std::ios::binary);
  std::ofstream dst(backup_file, std::ios::binary);

  if (!src.is_open()) {
    std::cerr << "无法打开源文件 /etc/hosts" << std::endl;
    return false;
  }

  if (!dst.is_open()) {
    std::cerr << "无法打开目标文件 " << backup_file << std::endl;
    return false;
  }

  dst << src.rdbuf();

  src.close();
  dst.close();

  std::cout << "hosts文件已备份到: " << backup_file << std::endl;
  return true;
}

bool HostsManager::restoreHosts(const std::string &backup_path) {
  std::string backup_file =
      backup_path.empty() ? default_backup_path_ : backup_path;

  if (!std::filesystem::exists(backup_file)) {
    std::cerr << "备份文件不存在: " << backup_file << std::endl;
    return false;
  }

  // 检查权限
  if (!hasRootPrivilege()) {
    std::cerr << "需要root权限来恢复hosts文件" << std::endl;
    return false;
  }

  // 恢复hosts文件
  std::ifstream src(backup_file, std::ios::binary);
  std::ofstream dst("/etc/hosts", std::ios::binary);

  if (!src.is_open()) {
    std::cerr << "无法打开备份文件 " << backup_file << std::endl;
    return false;
  }

  if (!dst.is_open()) {
    std::cerr << "无法打开目标文件 /etc/hosts" << std::endl;
    return false;
  }

  dst << src.rdbuf();

  src.close();
  dst.close();

  std::cout << "hosts文件已从备份恢复: " << backup_file << std::endl;
  return true;
}

bool HostsManager::updateGitHubHosts(const std::vector<GitHubIP> &selected_ips,
                                     bool backup) {
  // 检查权限
  if (!hasRootPrivilege()) {
    std::cerr << "\n错误：需要root权限来更新hosts文件！" << std::endl;
    std::cerr << "当前可用的hosts条目（预览）：" << std::endl;
    std::cerr << "==========================================" << std::endl;

    for (const auto &ip : selected_ips) {
      if (ip.is_valid) {
        std::cout << ip.address << "\t" << ip.domain;
        if (ip.latency >= 0) {
          std::cout << " # 延迟: " << ip.latency << "ms";
        }
        std::cout << std::endl;
      }
    }

    std::cerr << "==========================================" << std::endl;
    std::cerr << "请使用 'sudo " << getenv("_")
              << "' 重新运行程序来应用这些更改。" << std::endl;
    return false;
  }

  // 如果需要，先备份
  if (backup && !backupHosts()) {
    std::cerr << "备份失败，操作取消" << std::endl;
    return false;
  }

  // 读取当前hosts文件
  std::ifstream hosts_file("/etc/hosts");
  if (!hosts_file.is_open()) {
    std::cerr << "无法打开 /etc/hosts 文件" << std::endl;
    return false;
  }

  std::vector<std::string> lines;
  std::string line;

  while (std::getline(hosts_file, line)) {
    lines.push_back(line);
  }
  hosts_file.close();

  // 移除旧的GitHub相关条目
  std::vector<std::string> github_domains = {
      "alive.github.com",
      "api.github.com",
      "api.individual.githubcopilot.com",
      "avatars.githubusercontent.com",
      "avatars0.githubusercontent.com",
      "avatars1.githubusercontent.com",
      "avatars2.githubusercontent.com",
      "avatars3.githubusercontent.com",
      "avatars4.githubusercontent.com",
      "avatars5.githubusercontent.com",
      "camo.githubusercontent.com",
      "central.github.com",
      "cloud.githubusercontent.com",
      "codeload.github.com",
      "collector.github.com",
      "desktop.githubusercontent.com",
      "favicons.githubusercontent.com",
      "gist.github.com",
      "github-cloud.s3.amazonaws.com",
      "github-com.s3.amazonaws.com",
      "github-production-release-asset-2e65be.s3.amazonaws.com",
      "github-production-repository-file-5c1aeb.s3.amazonaws.com",
      "github-production-user-asset-6210df.s3.amazonaws.com",
      "github.blog",
      "github.com",
      "github.community",
      "github.githubassets.com",
      "github.global.ssl.fastly.net",
      "github.io",
      "github.map.fastly.net",
      "githubstatus.com",
      "live.github.com",
      "media.githubusercontent.com",
      "objects.githubusercontent.com",
      "pipelines.actions.githubusercontent.com",
      "raw.githubusercontent.com",
      "user-images.githubusercontent.com",
      "private-user-images.githubusercontent.com",
      "vscode.dev", // 注意：文档中标注了此域名可能超时
      "education.github.com"};

  std::vector<std::string> new_lines;
  for (const auto &line : lines) {
    bool is_github_line = false;

    // 检查这一行是否包含GitHub域名
    for (const auto &domain : github_domains) {
      if (line.find(domain) != std::string::npos && !line.empty() &&
          line[0] != '#') {
        is_github_line = true;
        break;
      }
    }

    if (!is_github_line) {
      new_lines.push_back(line);
    }
  }

  // 添加新的GitHub条目
  // 按域名分组，每个域名选择最快的几个IP
  std::map<std::string, std::vector<GitHubIP>> ips_by_domain;
  for (const auto &ip : selected_ips) {
    if (ip.is_valid) {
      ips_by_domain[ip.domain].push_back(ip);
    }
  }

  // 对每个域名的IP按延迟排序
  for (auto &pair : ips_by_domain) {
    std::sort(pair.second.begin(), pair.second.end(),
              [](const GitHubIP &a, const GitHubIP &b) {
                return a.latency < b.latency;
              });

    // 为每个域名添加注释和IP映射
    new_lines.push_back("");
    new_lines.push_back("# GitHub " + pair.first + " 优化条目 - 由GHIP生成");

    // 添加前3个最快的IP（如果有的话）
    int count = std::min(3, static_cast<int>(pair.second.size()));
    for (int i = 0; i < count; i++) {
      std::ostringstream oss;
      oss << pair.second[i].address << "\t" << pair.first;
      if (i == 0 && pair.second[i].latency >= 0) {
        oss << " # 最快 (" << pair.second[i].latency << "ms)";
      }
      new_lines.push_back(oss.str());
    }
  }

  // 写入新的hosts文件
  std::ofstream out_file("/etc/hosts");
  if (!out_file.is_open()) {
    std::cerr << "无法写入 /etc/hosts 文件" << std::endl;
    return false;
  }

  for (const auto &line : new_lines) {
    out_file << line << std::endl;
  }

  out_file.close();

  std::cout << "已更新hosts文件，添加了 " << selected_ips.size() << " 个IP条目"
            << std::endl;

  return true;
}

std::vector<std::string> HostsManager::getCurrentGitHubEntries() {
  std::vector<std::string> github_entries;
  std::vector<std::string> github_domains = {
      "alive.github.com",
      "api.github.com",
      "api.individual.githubcopilot.com",
      "avatars.githubusercontent.com",
      "avatars0.githubusercontent.com",
      "avatars1.githubusercontent.com",
      "avatars2.githubusercontent.com",
      "avatars3.githubusercontent.com",
      "avatars4.githubusercontent.com",
      "avatars5.githubusercontent.com",
      "camo.githubusercontent.com",
      "central.github.com",
      "cloud.githubusercontent.com",
      "codeload.github.com",
      "collector.github.com",
      "desktop.githubusercontent.com",
      "favicons.githubusercontent.com",
      "gist.github.com",
      "github-cloud.s3.amazonaws.com",
      "github-com.s3.amazonaws.com",
      "github-production-release-asset-2e65be.s3.amazonaws.com",
      "github-production-repository-file-5c1aeb.s3.amazonaws.com",
      "github-production-user-asset-6210df.s3.amazonaws.com",
      "github.blog",
      "github.com",
      "github.community",
      "github.githubassets.com",
      "github.global.ssl.fastly.net",
      "github.io",
      "github.map.fastly.net",
      "githubstatus.com",
      "live.github.com",
      "media.githubusercontent.com",
      "objects.githubusercontent.com",
      "pipelines.actions.githubusercontent.com",
      "raw.githubusercontent.com",
      "user-images.githubusercontent.com",
      "private-user-images.githubusercontent.com",
      "vscode.dev", // 注意：文档中标注了此域名可能超时
      "education.github.com"};

  std::ifstream hosts_file("/etc/hosts");
  if (!hosts_file.is_open()) {
    std::cerr << "无法打开 /etc/hosts 文件" << std::endl;
    return github_entries;
  }

  std::string line;
  while (std::getline(hosts_file, line)) {
    // 跳过空行和注释
    if (line.empty() || line[0] == '#') {
      continue;
    }

    // 检查是否包含GitHub域名
    for (const auto &domain : github_domains) {
      if (line.find(domain) != std::string::npos) {
        github_entries.push_back(line);
        break;
      }
    }
  }

  hosts_file.close();
  return github_entries;
}

bool HostsManager::flushDNSCache() {
  std::cout << "正在刷新DNS缓存..." << std::endl;

  bool success = false;
  std::vector<std::pair<std::string, std::string>> methods;

  // Arch Linux 推荐的 DNS 缓存刷新方法
  methods = {
      {"systemd-resolve", "sudo systemd-resolve --flush-caches"},
      {"直接重启 systemd-resolved", "sudo systemctl restart systemd-resolved"},
      {"NetworkManager", "sudo systemctl restart NetworkManager"},
      {"dhclient", "sudo dhclient -r && sudo dhclient"},
      {"nscd", "sudo systemctl restart nscd"}};

  for (const auto &[name, cmd] : methods) {
    std::cout << "尝试方法: " << name << "..." << std::endl;

    std::string test_cmd = cmd.substr(0, cmd.find(' '));
    if (test_cmd == "sudo") {
      test_cmd = cmd.substr(5, cmd.find(' ', 5) - 5);
    }

    // 检查命令是否存在
    if (system(("command -v " + test_cmd + " > /dev/null 2>&1").c_str()) != 0) {
      std::cout << "  → 跳过 (" << test_cmd << " 未安装)" << std::endl;
      continue;
    }

    // 执行命令
    int result = system(cmd.c_str());
    if (result == 0) {
      std::cout << "  → 成功" << std::endl;
      success = true;
      break;
    } else {
      std::cout << "  → 失败 (返回码: " << result << ")" << std::endl;
    }

    // 短暂延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  if (success) {
    std::cout << "✓ DNS缓存刷新成功" << std::endl;
  } else {
    std::cout << "\n✗ 自动DNS缓存刷新失败" << std::endl;
    std::cout << "\n对于 Arch Linux，请尝试以下方法之一：" << std::endl;
    std::cout
        << "══════════════════════════════════════════════════════════════"
        << std::endl;
    std::cout << "方法 1: 安装并启用 systemd-resolved" << std::endl;
    std::cout << "  sudo pacman -S systemd-resolvconf" << std::endl;
    std::cout << "  sudo systemctl enable --now systemd-resolved" << std::endl;
    std::cout << "  sudo ln -sf /run/systemd/resolve/stub-resolv.conf "
                 "/etc/resolv.conf"
              << std::endl;
    std::cout << std::endl;
    std::cout << "方法 2: 使用 NetworkManager（如果已安装）" << std::endl;
    std::cout << "  sudo systemctl restart NetworkManager" << std::endl;
    std::cout << std::endl;
    std::cout << "方法 3: 重启网络服务" << std::endl;
    std::cout << "  sudo systemctl restart systemd-networkd" << std::endl;
    std::cout << std::endl;
    std::cout << "方法 4: 手动清除程序缓存" << std::endl;
    std::cout << "  # Chrome/Chromium" << std::endl;
    std::cout << "  chrome://net-internals/#dns" << std::endl;
    std::cout << std::endl;
    std::cout << "  # Firefox" << std::endl;
    std::cout << "  about:networking#dns" << std::endl;
    std::cout
        << "══════════════════════════════════════════════════════════════"
        << std::endl;

    // 询问用户是否要安装 systemd-resolved
    std::cout << "\n是否希望我帮你安装并配置 systemd-resolved？ [y/N]: ";
    std::string answer;
    std::getline(std::cin, answer);

    if (!answer.empty() && (answer[0] == 'Y' || answer[0] == 'y')) {
      std::cout << "正在安装 systemd-resolved..." << std::endl;
      system("sudo pacman -S --noconfirm systemd-resolvconf");
      system("sudo systemctl enable --now systemd-resolved");
      system(
          "sudo ln -sf /run/systemd/resolve/stub-resolv.conf /etc/resolv.conf");
      std::cout << "安装完成！重新运行DNS刷新..." << std::endl;

      // 重新尝试刷新
      if (system("sudo systemd-resolve --flush-caches") == 0) {
        success = true;
        std::cout << "✓ DNS缓存刷新成功" << std::endl;
      }
    }
  }

  return success;
}

std::string
HostsManager::generateHostsContent(const std::vector<GitHubIP> &selected_ips) {
  std::ostringstream oss;

  oss << "# GitHub IP优化 - 由gh_ip_optimizer生成\n";
  oss << "# 生成时间: " << __TIME__ << "\n";
  oss << "# 请将以下内容添加到 /etc/hosts 文件末尾\n";
  oss << "# ========================================\n\n";

  // 按域名分组
  std::map<std::string, std::vector<GitHubIP>> ips_by_domain;
  for (const auto &ip : selected_ips) {
    if (ip.is_valid) {
      ips_by_domain[ip.domain].push_back(ip);
    }
  }

  // 生成内容
  for (auto &pair : ips_by_domain) {
    // 按延迟排序
    std::sort(pair.second.begin(), pair.second.end(),
              [](const GitHubIP &a, const GitHubIP &b) {
                return a.latency < b.latency;
              });

    oss << "# " << pair.first << "\n";
    int count = std::min(3, static_cast<int>(pair.second.size()));
    for (int i = 0; i < count; i++) {
      oss << pair.second[i].address << "\t" << pair.first;
      if (i == 0 && pair.second[i].latency >= 0) {
        oss << " # 最快 (" << pair.second[i].latency << "ms)";
      }
      oss << "\n";
    }
    oss << "\n";
  }

  oss << "# ========================================\n";
  oss << "# 使用方法: sudo nano /etc/hosts\n";
  oss << "# 然后将上述内容粘贴到文件末尾\n";

  return oss.str();
}
