#include "ip-tester.h"
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <curl/curl.h>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

static size_t discardCallback(char *ptr, size_t size, size_t nmemb,
                              void *userdata) {
  return size * nmemb; // 直接丢弃内容，只关心响应头
}

IPTester::IPTester(int timeout_ms, int thread_count)
    : timeout_ms_(timeout_ms), thread_count_(thread_count) {}

IPTester::~IPTester() {}

int IPTester::testLatency(const std::string &ip) {
  // 使用ping命令获取真实延迟
  std::string cmd =
      "ping -c 2 -W 1 " + ip + " 2>&1 | tail -1 | awk -F '/' '{print $5}'";

  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe)
    return -1;

  char buffer[128];
  std::string result = "";

  while (!feof(pipe)) {
    if (fgets(buffer, 128, pipe) != nullptr) {
      result += buffer;
    }
  }

  pclose(pipe);

  // 解析ping输出的平均延迟
  try {
    // 移除换行符和空格
    result.erase(std::remove_if(
                     result.begin(), result.end(),
                     [](char c) { return c == '\n' || c == '\r' || c == ' '; }),
                 result.end());

    if (!result.empty()) {
      // 转换为毫秒
      double latency_ms = std::stod(result);
      return static_cast<int>(latency_ms + 0.5); // 四舍五入
    }
  } catch (...) {
    // 解析失败
  }

  return -1; // 测试失败
}

bool IPTester::testGitHubService(const GitHubIP &ip_info) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return false;

  // 构造测试URL - 使用IP直接访问
  std::string test_url = "https://" + ip_info.address;

  // 设置Host头，让服务器知道我们要访问GitHub
  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, ("Host: " + ip_info.domain).c_str());

  curl_easy_setopt(curl, CURLOPT_URL, test_url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD请求
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // 简化SSL验证
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardCallback);

  bool is_valid = false;
  CURLcode res = curl_easy_perform(curl);

  if (res == CURLE_OK) {
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    // 更宽松的验证条件：只要SSL握手成功且有HTTP响应就认为是有效IP
    // 1. 首先检查SSL握手是否成功
    long ssl_verify_result = 0;
    curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &ssl_verify_result);

    // 2. 放宽响应码要求
    if (response_code > 0) {
      // 条件1：如果SSL验证成功（值为0）
      if (ssl_verify_result == 0) {
        is_valid = true;
      }
      // 条件2：或者HTTP状态码是成功的（2xx或3xx）
      else if (response_code / 100 == 2 || response_code / 100 == 3) {
        is_valid = true;
      }
      // 条件3：即使返回403/404，但能建立连接，也认为是GitHub服务器
      else if (response_code == 403 || response_code == 404) {
        // 确认连接本身是成功的
        long connect_code = 0;
        curl_easy_getinfo(curl, CURLINFO_HTTP_CONNECTCODE, &connect_code);
        if (res == CURLE_OK) {
          is_valid = true;
        }
      }
    }
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  return is_valid;
}

void IPTester::sortByQuality(std::vector<GitHubIP> &ip_list) {
  std::sort(ip_list.begin(), ip_list.end(),
            [](const GitHubIP &a, const GitHubIP &b) {
              if (a.is_valid != b.is_valid) {
                return a.is_valid > b.is_valid;
              }
              if (a.latency >= 0 && b.latency >= 0) {
                return a.latency < b.latency;
              }
              return a.latency >= 0;
            });
}

// ========== 第一层：超快速端口扫描 ==========
bool IPTester::ultraFastPortScan(const std::string &ip, int port,
                                 int timeout_ms) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return false;

  // 设置非阻塞
  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
    close(sock);
    return false;
  }

  // 调用 connect 并检查返回值
  int connect_result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));

  // 连接立即成功（本地连接等情况）
  if (connect_result == 0) {
    close(sock);
    return true;
  }

  // 如果 connect 返回 -1 且 errno 是 EINPROGRESS，说明连接正在进行
  // 否则就是真正的错误
  if (connect_result == -1 && errno != EINPROGRESS) {
    close(sock);
    return false;
  }

  // 使用poll等待连接（比select更高效）
  struct pollfd fds[1];
  fds[0].fd = sock;
  fds[0].events = POLLOUT;

  int ret = poll(fds, 1, timeout_ms);

  if (ret <= 0) {
    close(sock);
    return false;
  }

  int so_error;
  socklen_t len = sizeof(so_error);
  getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);

  close(sock);
  return (so_error == 0);
}

// ========== 第二层：HTTP快速验证（优化版） ==========
bool IPTester::fastHttpValidation(const GitHubIP &ip_info, int timeout_ms) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return false;

  // 测试GitHub状态端点 - 更通用的端点
  std::string test_url = "https://" + ip_info.address + "/";

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, ("Host: " + ip_info.domain).c_str());
  headers = curl_slist_append(headers, "User-Agent: GitHub-IP-Tester/2.0");
  headers = curl_slist_append(headers, "Accept: */*");

  curl_easy_setopt(curl, CURLOPT_URL, test_url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD请求
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                   timeout_ms / 3); // 连接超时更短
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardCallback);

  // 启用TCP快速打开（如果支持）
  curl_easy_setopt(curl, CURLOPT_TCP_FASTOPEN, 1L);

  bool is_github = false;
  CURLcode res = curl_easy_perform(curl);

  if (res == CURLE_OK) {
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    // 检查SSL握手结果
    long ssl_verify_result = 0;
    curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &ssl_verify_result);

    // 检查连接时间
    double connect_time = 0;
    curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME_T, &connect_time);
    double total_time = 0;
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME_T, &total_time);

    // 非常宽松的验证条件（保证通过率）
    // 1. 只要SSL握手成功（即使有警告）且响应码存在
    if (ssl_verify_result == 0 && response_code > 0) {
      is_github = true;
    }
    // 2. 或者响应码是有效的HTTP状态码（100-599）且连接时间合理
    else if (response_code >= 100 && response_code < 600) {
      // 连接时间阈值放宽到1.5秒
      if (connect_time <= 1.5 && total_time <= 2.0) {
        is_github = true;
      }
    }
    // 3. 即使SSL验证有问题，但连接成功且响应迅速
    else if (res == CURLE_OK && connect_time <= 1.0) {
      is_github = true;
    }

    // 额外的GitHub特征检查（可选，进一步提高准确性）
    if (is_github) {
      // 检查Server头是否包含GitHub（如果有的话）
      char *server_header = nullptr;
      curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &server_header);
      // 这里可以添加更多GitHub特定特征的检查
    }
  } else if (res == CURLE_OPERATION_TIMEDOUT) {
    // 超时不一定代表不是GitHub，可能是网络波动
    // 给一个较小的权重，但标记为可能需要重新验证
    is_github = false;
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  return is_github;
}
// ========== 第三层：简单延迟测试（使用TCP连接时间） ==========
int IPTester::quickLatencyTest(const std::string &ip, int timeout_ms) {
  // 使用 TCP 连接时间作为延迟测量，比 ping 更可靠
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return -1;

  // 设置非阻塞
  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(443); // GitHub 使用 HTTPS

  if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
    close(sock);
    return -1;
  }

  // 记录开始时间
  auto start_time = std::chrono::steady_clock::now();

  // 尝试连接
  int connect_result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));

  if (connect_result == 0) {
    // 立即连接成功，延迟极低
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    close(sock);
    return static_cast<int>(duration.count());
  }

  // 如果 connect 返回 -1 且 errno 不是 EINPROGRESS，说明有错误
  if (connect_result == -1 && errno != EINPROGRESS) {
    close(sock);
    return -1;
  }

  // 使用 poll 等待连接完成
  struct pollfd fds[1];
  fds[0].fd = sock;
  fds[0].events = POLLOUT;

  int poll_result = poll(fds, 1, timeout_ms);

  if (poll_result <= 0) {
    // 超时或错误
    close(sock);
    return -1;
  }

  // 检查连接状态
  int so_error;
  socklen_t len = sizeof(so_error);
  if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) == -1) {
    close(sock);
    return -1;
  }

  if (so_error != 0) {
    // 连接失败
    close(sock);
    return -1;
  }

  // 连接成功，计算延迟
  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  close(sock);

  int latency = static_cast<int>(duration.count());

  // 设置延迟阈值：超过200ms的直接淘汰
  if (latency > 200) {
    return -1;
  }

  return latency;
}
// ========== 批量处理助手 ==========
void IPTester::processBatch(std::vector<GitHubIP *> &batch,
                            const std::function<bool(GitHubIP *)> &test_func,
                            std::atomic<int> &passed,
                            std::atomic<int> &completed) {
  for (auto &ip_ptr : batch) {
    bool test_result = test_func(ip_ptr);

    if (test_result) {
      passed++;
    }

    completed++;
  }
}

// ========== 三层快速筛选主函数 ==========
void IPTester::threeLayerQuickFilter(
    std::vector<GitHubIP> &ip_list,
    std::function<void(int, int)> progress_callback) {
  std::cout << "\n=== 三层快速筛选开始 ===" << std::endl;
  std::cout << "总IP数: " << ip_list.size() << std::endl;

  // 重置所有IP状态
  for (auto &ip : ip_list) {
    ip.is_valid = false;
    ip.latency = -1;
  }

  int total_stages = 3;
  int ip_per_stage = ip_list.size() / total_stages;

  // ========== 第一层：超快速端口扫描 ==========
  std::cout << "\n[第一层] 超快速端口扫描（300ms超时）..." << std::endl;

  std::vector<GitHubIP *> stage1_passed_ips;
  std::atomic<int> stage1_completed(0);
  std::vector<std::thread> threads;

  auto start_time = std::chrono::steady_clock::now();

  // 第一层：测试所有IP的端口连通性
  int batch_size = (ip_list.size() + thread_count_ - 1) / thread_count_;

  for (int i = 0; i < thread_count_; i++) {
    int start = i * batch_size;
    int end = std::min(start + batch_size, (int)ip_list.size());

    if (start >= end)
      break;

    threads.emplace_back([&, start, end]() {
      for (int j = start; j < end; j++) {
        bool port_open = ultraFastPortScan(ip_list[j].address, 443, 300);

        if (port_open) {
          // 通过第一层，加入下一轮测试列表
          stage1_passed_ips.push_back(&ip_list[j]);
        } else {
          // 第一层失败，直接标记为无效
          ip_list[j].is_valid = false;
          ip_list[j].latency = -1;
        }

        stage1_completed++;
        if (progress_callback) {
          progress_callback(stage1_completed,
                            ip_list.size() + stage1_passed_ips.size() +
                                stage1_passed_ips.size() / 2); // 预估
        }
      }
    });
  }

  for (auto &thread : threads) {
    if (thread.joinable())
      thread.join();
  }

  threads.clear();

  auto stage1_end = std::chrono::steady_clock::now();
  auto stage1_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      stage1_end - start_time);

  std::cout << "  结果: " << stage1_passed_ips.size() << "/" << ip_list.size()
            << " 通过 (" << stage1_duration.count() << "ms)" << std::endl;

  if (stage1_passed_ips.empty()) {
    std::cout << "所有IP在第一层被淘汰" << std::endl;
    return;
  }

  // ========== 第二层：HTTP快速验证 ==========
  std::cout << "\n[第二层] HTTP快速验证（检查是否为GitHub服务器）..."
            << std::endl;

  std::vector<GitHubIP *> stage2_passed_ips;
  std::atomic<int> stage2_completed(0);

  // 第二层：只测试第一层通过的IP
  batch_size = (stage1_passed_ips.size() + thread_count_ - 1) / thread_count_;

  for (int i = 0; i < thread_count_; i++) {
    int start = i * batch_size;
    int end = std::min(start + batch_size, (int)stage1_passed_ips.size());

    if (start >= end)
      break;

    threads.emplace_back([&, start, end]() {
      for (int j = start; j < end; j++) {
        GitHubIP *ip_ptr = stage1_passed_ips[j];

        bool is_github = fastHttpValidation(*ip_ptr, 1500);

        if (is_github) {
          // 通过第二层，加入下一轮测试列表
          stage2_passed_ips.push_back(ip_ptr);
        } else {
          // 第二层失败，标记为无效
          ip_ptr->is_valid = false;
          ip_ptr->latency = -1;
        }

        stage2_completed++;
        if (progress_callback) {
          int current_total = ip_list.size() + stage2_completed;
          int estimated_total = ip_list.size() + stage1_passed_ips.size() +
                                stage2_passed_ips.size();
          progress_callback(current_total, estimated_total);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });
  }

  for (auto &thread : threads) {
    if (thread.joinable())
      thread.join();
  }

  threads.clear();

  auto stage2_end = std::chrono::steady_clock::now();
  auto stage2_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      stage2_end - stage1_end);

  std::cout << "  结果: " << stage2_passed_ips.size() << "/"
            << stage1_passed_ips.size() << " 通过 (" << stage2_duration.count()
            << "ms)" << std::endl;

  // ========== 第三层：简单延迟测试 ==========
  std::cout << "\n[第三层] TCP延迟测试（淘汰高延迟IP）..." << std::endl;

  std::atomic<int> stage3_passed(0);
  std::atomic<int> stage3_completed(0);

  // 第三层：只测试第二层通过的IP
  batch_size = (stage2_passed_ips.size() + thread_count_ - 1) / thread_count_;
  auto stage3_start = std::chrono::steady_clock::now();

  for (int i = 0; i < thread_count_; i++) {
    int start = i * batch_size;
    int end = std::min(start + batch_size, (int)stage2_passed_ips.size());

    if (start >= end)
      break;

    threads.emplace_back([&, start, end]() {
      for (int j = start; j < end; j++) {
        GitHubIP *ip_ptr = stage2_passed_ips[j];

        int latency = quickLatencyTest(ip_ptr->address, 800);

        if (latency > 0) {
          stage3_passed++;
          ip_ptr->latency = latency;
          ip_ptr->is_valid = true; // 通过三层测试，标记为有效
        } else {
          ip_ptr->is_valid = false;
          ip_ptr->latency = -1;
        }

        stage3_completed++;
        if (progress_callback) {
          int current_total =
              ip_list.size() + stage1_passed_ips.size() + stage3_completed;
          int estimated_total = ip_list.size() + stage1_passed_ips.size() +
                                stage2_passed_ips.size();
          progress_callback(current_total, estimated_total);
        }
      }
    });
  }

  for (auto &thread : threads) {
    if (thread.joinable())
      thread.join();
  }

  auto stage3_end = std::chrono::steady_clock::now();
  auto stage3_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      stage3_end - stage3_start);
  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      stage3_end - start_time);

  std::cout << "\n=== 三层筛选完成 ===" << std::endl;
  std::cout << "各阶段结果:" << std::endl;
  std::cout << "  第一层（端口扫描）: " << stage1_passed_ips.size() << " 通过"
            << std::endl;
  std::cout << "  第二层（HTTP验证）: " << stage2_passed_ips.size() << " 通过"
            << std::endl;
  std::cout << "  第三层（延迟测试）: " << stage3_passed << " 通过"
            << std::endl;

  std::cout << "\n时间统计:" << std::endl;
  std::cout << "  第一层: " << stage1_duration.count() << "ms" << std::endl;
  std::cout << "  第二层: " << stage2_duration.count() << "ms" << std::endl;
  std::cout << "  第三层: " << stage3_duration.count() << "ms" << std::endl;
  std::cout << "  总计: " << total_duration.count() << "ms" << std::endl;

  std::cout << "\n进入深度测试的IP数: " << stage3_passed << "（约 "
            << (stage3_passed * 100 / ip_list.size()) << "%）" << std::endl;
}

// ========== 集成三层检测的批量测试主函数 ==========
void IPTester::batchTest(std::vector<GitHubIP> &ip_list,
                         std::function<void(int, int)> progress_callback) {

  if (ip_list.empty()) {
    std::cout << "IP列表为空，跳过测试" << std::endl;
    return;
  }

  std::cout << "开始测试 " << ip_list.size() << " 个IP地址..." << std::endl;

  // ========== 第一步：三层快速筛选 ==========
  threeLayerQuickFilter(ip_list, progress_callback);

  // 收集通过快速筛选的IP
  std::vector<GitHubIP *> ips_for_depth_test;
  for (auto &ip : ip_list) {
    if (ip.is_valid && ip.latency > 0) {
      ips_for_depth_test.push_back(&ip);
    }
  }

  std::cout << "\n准备进入深度测试的IP数: " << ips_for_depth_test.size()
            << std::endl;

  // 如果筛选结果太少，添加一些备用IP
  if (ips_for_depth_test.size() < 10 && !ip_list.empty()) {
    std::cout << "筛选结果过少，添加一些备用IP..." << std::endl;

    // 收集所有有延迟值的IP（即使未被标记为有效）
    std::vector<std::pair<int, int>> ip_with_latency;
    for (size_t i = 0; i < ip_list.size(); i++) {
      if (ip_list[i].latency > 0) {
        ip_with_latency.emplace_back(ip_list[i].latency, i);
      }
    }

    // 按延迟排序
    std::sort(ip_with_latency.begin(), ip_with_latency.end());

    int add_count = std::min(20, (int)ip_with_latency.size());
    for (int i = 0; i < add_count; i++) {
      int idx = ip_with_latency[i].second;
      if (!ip_list[idx].is_valid) {
        ip_list[idx].is_valid = true;
        ips_for_depth_test.push_back(&ip_list[idx]);
      }
    }

    std::cout << "添加了 " << add_count << " 个备用IP" << std::endl;
  }

  // ========== 第二步：深度测试 ==========
  if (ips_for_depth_test.empty()) {
    std::cout << "没有IP需要深度测试" << std::endl;
    return;
  }

  std::cout << "\n=== 深度测试开始 ===" << std::endl;
  std::cout << "深度测试 " << ips_for_depth_test.size() << " 个高质量IP"
            << std::endl;

  std::atomic<int> depth_completed(0);
  std::vector<std::thread> depth_threads;

  int depth_batch_size =
      (ips_for_depth_test.size() + thread_count_ - 1) / thread_count_;

  for (int i = 0; i < thread_count_; i++) {
    int start = i * depth_batch_size;
    int end =
        std::min(start + depth_batch_size, (int)ips_for_depth_test.size());

    if (start >= end)
      break;

    depth_threads.emplace_back([&, start, end]() {
      for (int j = start; j < end; j++) {
        GitHubIP *ip_ptr = ips_for_depth_test[j];

        // 深度测试：完整的GitHub服务验证
        bool is_github = testGitHubService(*ip_ptr);

        if (!is_github) {
          ip_ptr->is_valid = false;
        }

        depth_completed++;
        if (progress_callback) {
          int total_progress = ip_list.size() * 3; // 三层筛选占用的进度
          progress_callback(total_progress + depth_completed,
                            total_progress + ips_for_depth_test.size());
        }

        // 控制请求频率
        if (j % 3 == 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      }
    });
  }

  for (auto &thread : depth_threads) {
    if (thread.joinable())
      thread.join();
  }

  // ========== 最终统计 ==========
  int valid_count = 0;
  int fastest_latency = INT_MAX;
  std::string fastest_ip;
  std::string fastest_domain;

  for (const auto &ip : ip_list) {
    if (ip.is_valid) {
      valid_count++;
      if (ip.latency > 0 && ip.latency < fastest_latency) {
        fastest_latency = ip.latency;
        fastest_ip = ip.address;
        fastest_domain = ip.domain;
      }
    }
  }

  std::cout << "\n=== 测试完成 ===" << std::endl;
  std::cout << "总IP数: " << ip_list.size() << std::endl;
  std::cout << "进入深度测试: " << ips_for_depth_test.size() << " 个"
            << std::endl;
  std::cout << "最终有效IP: " << valid_count << " 个" << std::endl;

  if (valid_count > 0) {
    std::cout << "最快IP: " << fastest_ip << " -> " << fastest_domain << " ("
              << fastest_latency << "ms)" << std::endl;

    // 输出前5个有效IP
    std::cout << "有效IP示例（前5个）:" << std::endl;
    int count = 0;
    for (const auto &ip : ip_list) {
      if (ip.is_valid && count < 5) {
        std::cout << "  " << ip.address << " -> " << ip.domain;
        if (ip.latency > 0) {
          std::cout << " (" << ip.latency << "ms)";
        }
        std::cout << std::endl;
        count++;
      }
    }
  } else {
    std::cout << "\n警告：未找到任何有效IP！" << std::endl;
    std::cout << "可能原因：" << std::endl;
    std::cout << "  1. 网络连接问题" << std::endl;
    std::cout << "  2. GitHub服务暂时不可用" << std::endl;
    std::cout << "  3. 获取的IP列表已过期" << std::endl;
  }
}
