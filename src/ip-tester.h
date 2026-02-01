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

  // 测试单个IP的延迟（保持，可能其他地方需要）
  int testLatency(const std::string &ip);

  // 测试IP是否服务于GitHub
  bool testGitHubService(const GitHubIP &ip_info);

  // 批量测试IP列表（简化版）
  void batchTest(
      std::vector<GitHubIP> &ip_list,
      std::function<void(int current, int total, int stage, int stage_total)>
          progress_callback = nullptr);

  // 对IP列表按质量排序
  static void sortByQuality(std::vector<GitHubIP> &ip_list);

private:
  int timeout_ms_;
  int thread_count_;
  std::mutex callback_mutex_;

  // 线程函数：测试一批IP
  void testBatch(const std::vector<GitHubIP *> &batch,
                 std::function<void(int)> completion_callback);

  // ========== 简化后的联通测试 ==========

  // 快速端口联通测试（简化版，只检查端口是否开放）
  bool quickConnectTest(const std::string &ip, int port = 443,
                        int timeout_ms = 300);

  // 简化测试主函数
  void simpleConnectFilter(
      std::vector<GitHubIP> &ip_list,
      std::function<void(int current, int total, int stage, int stage_total)>
          progress_callback = nullptr);
};

#endif
