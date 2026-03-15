#ifndef IP_TESTER_H
#define IP_TESTER_H

#include "ip-fetcher.h"
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <curl/curl.h>
#include <fcntl.h>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

class IPTester {
public:
  IPTester(int timeout_ms = 1000, int thread_count = 10);
  ~IPTester();

  // 统一的测试方法
  enum TestMode {
    TEST_MODE_FULL,    // 完整测试：联通测试 + 深度测试
    TEST_MODE_REFRESH, // 刷新测试：仅深度测试（已有有效IP）
    TEST_MODE_CONNECT  // 仅联通测试
  };

  // 测试单个IP的延迟
  int testLatency(const std::string &ip);

  // 测试IP是否服务于GitHub
  bool testGitHubService(const GitHubIP &ip_info);

  // 统一的批量测试函数（用户可自定义测试次数）
  void unifiedTest(
      std::vector<GitHubIP> &ip_list, TestMode mode = TEST_MODE_FULL,
      int connect_test_times = 2, // 新增：联通测试次数
      std::function<void(int current, int total, int stage, int stage_total)>
          progress_callback = nullptr);

  // 对IP列表按质量排序
  static void sortByQuality(std::vector<GitHubIP> &ip_list);

  // 刷新测试：仅重新测试已标记为有效的IP
  void refreshTest(
      std::vector<GitHubIP> &ip_list,
      std::function<void(int current, int total, int stage, int stage_total)>
          progress_callback = nullptr);

private:
  int timeout_ms_;
  int thread_count_;
  std::mutex callback_mutex_;

  // ========== 内部测试方法 ==========

  // 快速端口联通测试
  bool quickConnectTest(const std::string &ip, int port = 443,
                        int timeout_ms = 300);

  // 多次联通测试主函数
  void multipleConnectFilter(
      std::vector<GitHubIP> &ip_list,
      int test_times, // 测试次数
      std::function<void(int current, int total, int stage, int stage_total)>
          progress_callback = nullptr);

  // 深度测试（包含延迟检测）
  void depthTestWithLatency(
      std::vector<GitHubIP> &ip_list,
      bool test_all =
          false, // true: 测试所有IP, false: 只测试ip.is_valid=true的IP
      std::function<void(int current, int total, int stage, int stage_total)>
          progress_callback = nullptr);
};

#endif
