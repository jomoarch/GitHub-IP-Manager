#include "hosts-manager.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <sys/stat.h>
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
    std::cerr << "需要root权限来更新hosts文件" << std::endl;
    std::cerr << "请使用 sudo 运行程序" << std::endl;
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
      "github.com",
      "api.github.com",
      "assets-cdn.github.com",
      "raw.githubusercontent.com",
      "user-images.githubusercontent.com",
      "codeload.github.com",
      "github.global.ssl.fastly.net"};

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
      "github.com",
      "api.github.com",
      "assets-cdn.github.com",
      "raw.githubusercontent.com",
      "user-images.githubusercontent.com",
      "codeload.github.com",
      "github.global.ssl.fastly.net"};

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

  // 根据不同的系统使用不同的命令
  bool success = false;

  // 检查系统类型
  std::string system_info = executeCommand("uname -s");

  if (system_info.find("Linux") != std::string::npos) {
    // 对于Arch Linux和其他使用systemd-resolved的系统
    std::string result = executeCommand("systemctl is-active systemd-resolved");
    if (result.find("active") != std::string::npos) {
      std::cout << "使用 systemd-resolved..." << std::endl;
      if (system("sudo systemd-resolve --flush-caches") == 0) {
        success = true;
      }
    }

    // 尝试通用的nscd
    if (!success) {
      std::string nscd_result = executeCommand("systemctl is-active nscd");
      if (nscd_result.find("active") != std::string::npos) {
        std::cout << "使用 nscd..." << std::endl;
        if (system("sudo systemctl restart nscd") == 0) {
          success = true;
        }
      }
    }

    // 如果都没有，尝试通用方法
    if (!success) {
      std::cout << "使用通用DNS刷新方法..." << std::endl;
      if (system("sudo rcnscd restart") == 0 ||
          system("sudo /etc/init.d/nscd restart") == 0) {
        success = true;
      }
    }
  }

  if (success) {
    std::cout << "DNS缓存刷新成功" << std::endl;
  } else {
    std::cout << "DNS缓存刷新失败，可能需要手动刷新" << std::endl;
    std::cout << "对于Arch Linux，可以尝试: sudo systemd-resolve --flush-caches"
              << std::endl;
  }

  return success;
}
