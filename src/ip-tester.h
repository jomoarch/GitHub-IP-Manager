#ifndef IP_TESTER_H
#define IP_TESTER_H

#include "ip-fetcher.h"
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class IPTester {
public:
  IPTester(int timeout_ms = 1000, int thread_count = 10);
  ~IPTester();

  // 测试单个IP的延迟
  int testLatency(const std::string &ip);

  // 测试IP是否服务于GitHub
  bool testGitHubService(const GitHubIP &ip_info);

  // 批量测试IP列表
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

  // ========== 三层快速检测方法 ==========

  // 第一层：超快速端口扫描（300ms超时）
  bool ultraFastPortScan(const std::string &ip, int port = 443,
                         int timeout_ms = 300);

  // 第二层：简单延迟测试
  int quickLatencyTest(const std::string &ip, int timeout_ms = 800);

  // 两层快速筛选主函数
  void twoLayerQuickFilter(
      std::vector<GitHubIP> &ip_list,
      std::function<void(int current, int total, int stage, int stage_total)>
          progress_callback = nullptr);
};

#endif
