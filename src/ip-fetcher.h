#ifndef IP_FETCHER_H
#define IP_FETCHER_H

#include <memory>
#include <string>
#include <vector>

struct GitHubIP {
  std::string address; // IP地址
  std::string domain;  // 对应的域名
  int latency;         // 延迟(ms)，-1表示未知
  bool is_valid;       // 是否有效

  GitHubIP(const std::string &addr, const std::string &dom)
      : address(addr), domain(dom), latency(-1), is_valid(false) {}
};

class IPFetcher {
public:
  IPFetcher();
  ~IPFetcher();

  // 从GitHub API获取IP列表
  bool fetchFromGitHubAPI(std::vector<GitHubIP> &ip_list);

  // 从备用源获取IP列表
  bool fetchFromBackupSource(std::vector<GitHubIP> &ip_list);

  // 展开CIDR格式的IP段
  static std::vector<std::string> expandCIDR(const std::string &cidr);

private:
  std::string user_agent_;

  // 发送HTTP请求
  std::string httpGet(const std::string &url);

  // 解析GitHub API的JSON响应
  bool parseGitHubAPIResponse(const std::string &json_str,
                              std::vector<GitHubIP> &ip_list);
};

#endif
