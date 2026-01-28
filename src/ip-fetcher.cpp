#include "ip-fetcher.h"
#include <curl/curl.h>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>

// libcurl写回调函数
size_t writeCallback(char *ptr, size_t size, size_t nmemb, std::string *data) {
  data->append(ptr, size * nmemb);
  return size * nmemb;
}

IPFetcher::IPFetcher() : user_agent_("GHIP/1.0") {}

IPFetcher::~IPFetcher() {}

bool IPFetcher::fetchFromBackupSource(std::vector<GitHubIP> &ip_list) {
  std::cout << "正在从备用源 (GitHub520) 获取IP列表..." << std::endl;

  // GitHub520 的 hosts 文件地址
  std::string response = httpGet("https://hosts.gitcdn.top/hosts.txt");
  if (response.empty()) {
    std::cerr << "无法从备用源获取数据" << std::endl;
    return false;
  }

  // 解析 hosts 文件格式
  // 格式示例: 140.82.113.3 github.com
  std::istringstream iss(response);
  std::string line;

  // GitHub 相关域名列表
  std::vector<std::string> target_domains = {
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

  int ip_count = 0;
  while (std::getline(iss, line)) {
    // 跳过注释和空行
    if (line.empty() || line[0] == '#') {
      continue;
    }

    // 移除行尾的\r（Windows换行符）
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    std::istringstream line_stream(line);
    std::string ip, domain;

    line_stream >> ip >> domain;

    // 检查是否是我们关心的GitHub域名
    if (!ip.empty() && !domain.empty()) {
      for (const auto &target_domain : target_domains) {
        if (domain == target_domain) {
          // 验证IP地址格式
          std::regex ip_regex("^([0-9]{1,3}\\.){3}[0-9]{1,3}$");
          if (std::regex_match(ip, ip_regex)) {
            ip_list.emplace_back(ip, domain);
            ip_count++;
          }
          break;
        }
      }
    }
  }

  if (ip_count > 0) {
    std::cout << "从备用源获取到 " << ip_count << " 个IP条目" << std::endl;
    return true;
  }

  std::cerr << "从备用源解析失败或未找到相关IP" << std::endl;
  return false;
}

bool IPFetcher::fetchFromGitHubAPI(std::vector<GitHubIP> &ip_list) {
  std::cout << "正在从 GitHub API 获取IP列表..." << std::endl;

  // 发送请求到 GitHub Meta API
  std::string response = httpGet("https://api.github.com/meta");
  if (response.empty()) {
    std::cerr << "无法从GitHub API获取数据" << std::endl;
    return false;
  }

  // 调用解析函数处理返回的JSON
  return parseGitHubAPIResponse(response, ip_list);
}

std::string IPFetcher::httpGet(const std::string &url) {
  CURL *curl = curl_easy_init();
  std::string response_data;

  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent_.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res)
                << std::endl;
      response_data.clear();
    }

    curl_easy_cleanup(curl);
  }

  return response_data;
}

bool IPFetcher::parseGitHubAPIResponse(const std::string &json_str,
                                       std::vector<GitHubIP> &ip_list) {
  // 简化的JSON解析 - 实际使用中建议使用json库
  // 这里解析GitHub API返回的CIDR格式

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

  // 提取web字段的CIDR
  std::regex cidr_regex("\"web\":\\s*\\[([^\\]]+)\\]");
  std::smatch match;

  if (std::regex_search(json_str, match, cidr_regex) && match.size() > 1) {
    std::string cidr_list_str = match[1].str();
    std::regex ip_regex("\"([0-9./]+)\"");
    auto words_begin = std::sregex_iterator(cidr_list_str.begin(),
                                            cidr_list_str.end(), ip_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
      std::string cidr = (*i)[1].str();
      std::vector<std::string> ips = expandCIDR(cidr);

      // 为每个IP创建条目
      for (const auto &ip : ips) {
        for (const auto &domain : github_domains) {
          ip_list.emplace_back(ip, domain);
        }
      }
    }

    std::cout << "从API获取到 " << ip_list.size() << " 个IP条目" << std::endl;
    return true;
  }

  return false;
}

std::vector<std::string> IPFetcher::expandCIDR(const std::string &cidr) {
  std::vector<std::string> result;

  // 简化的CIDR展开 - 实际项目中应使用专门的CIDR库
  // 这里仅处理简单的 /24, /16 等常见情况

  size_t slash_pos = cidr.find('/');
  if (slash_pos == std::string::npos) {
    result.push_back(cidr);
    return result;
  }

  std::string ip_str = cidr.substr(0, slash_pos);
  int prefix_len = std::stoi(cidr.substr(slash_pos + 1));

  // 对于演示，我们只返回几个示例IP
  // 实际实现需要完整的CIDR展开算法
  result.push_back(ip_str);

  // 生成几个相邻IP作为示例
  std::regex ip_regex("(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)");
  std::smatch match;
  if (std::regex_match(ip_str, match, ip_regex) && match.size() == 5) {
    int a = std::stoi(match[1].str());
    int b = std::stoi(match[2].str());
    int c = std::stoi(match[3].str());
    int d = std::stoi(match[4].str());

    // 添加几个示例IP
    for (int i = 1; i <= 3 && d + i < 256; i++) {
      std::ostringstream oss;
      oss << a << "." << b << "." << c << "." << (d + i);
      result.push_back(oss.str());
    }
  }

  return result;
}

void IPFetcher::mergeIPLists(std::vector<GitHubIP> &dest,
                             const std::vector<GitHubIP> &src) {
  // 使用set记录已存在的IP-域名组合
  std::set<std::pair<std::string, std::string>> existing_ips;

  // 记录目标列表中已有的组合
  for (const auto &ip : dest) {
    existing_ips.insert({ip.address, ip.domain});
  }

  // 添加源列表中不重复的组合
  for (const auto &ip : src) {
    auto key = std::make_pair(ip.address, ip.domain);
    if (existing_ips.find(key) == existing_ips.end()) {
      dest.push_back(ip);
      existing_ips.insert(key);
    }
  }

  std::cout << "合并后IP总数: " << dest.size() << std::endl;
}
