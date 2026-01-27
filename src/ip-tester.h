#ifndef IP_TESTER_H
#define IP_TESTER_H

#include "ip-fetcher.h"
#include <functional>
#include <string>
#include <vector>

class IPTester {
public:
  IPTester(int timeout_ms = 3000, int thread_count = 10);
  ~IPTester();

  // 测试单个IP的延迟
  int testLatency(const std::string &ip);

  // 测试IP是否服务于GitHub
  bool testGitHubService(const GitHubIP &ip_info);

  // 批量测试IP列表
  void batchTest(std::vector<GitHubIP> &ip_list,
                 std::function<void(int, int)> progress_callback = nullptr);

  // 对IP列表按质量排序
  static void sortByQuality(std::vector<GitHubIP> &ip_list);

private:
  int timeout_ms_;
  int thread_count_;

  // 线程函数：测试一批IP
  void testBatch(const std::vector<GitHubIP *> &batch,
                 std::function<void(int)> completion_callback);
};

#endif
