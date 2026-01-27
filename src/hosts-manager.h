#ifndef HOSTS_MANAGER_H
#define HOSTS_MANAGER_H

#include "ip-fetcher.h"
#include <string>
#include <vector>

class HostsManager {
public:
  HostsManager();

  // 备份当前hosts文件
  bool backupHosts(const std::string &backup_path = "");

  // 恢复hosts文件
  bool restoreHosts(const std::string &backup_path);

  // 更新hosts文件中的GitHub条目
  bool updateGitHubHosts(const std::vector<GitHubIP> &selected_ips,
                         bool backup = true);

  // 获取当前hosts文件中的GitHub条目
  std::vector<std::string> getCurrentGitHubEntries();

  // 刷新DNS缓存
  bool flushDNSCache();

private:
  std::string default_backup_path_;

  // 检查是否具有root权限
  bool hasRootPrivilege();

  // 执行系统命令
  std::string executeCommand(const std::string &cmd);
};

#endif
