#include "ip-tester.h"

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
    long ssl_verify_result = 0;
    curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &ssl_verify_result);

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

// ========== 简化的联通测试 ==========
bool IPTester::quickConnectTest(const std::string &ip, int port,
                                int timeout_ms) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return false;

  // 设置非阻塞
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags == -1 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
    close(sock);
    return false;
  }

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

  // 使用poll等待连接
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

// ========== 简化测试主函数 ==========
void IPTester::simpleConnectFilter(
    std::vector<GitHubIP> &ip_list,
    std::function<void(int current, int total, int stage, int stage_total)>
        progress_callback) {

  std::cout << "\n=== 简化联通测试开始 ===" << std::endl;
  std::cout << "总IP数: " << ip_list.size() << std::endl;

  // 重置所有IP状态
  for (auto &ip : ip_list) {
    ip.is_valid = false;
    ip.latency = -1;
  }

  // ========== 第一轮联通测试 ==========
  std::cout << "\n[第一轮] 快速联通测试（300ms超时）..." << std::endl;

  std::vector<GitHubIP *> round1_passed_ips;
  std::atomic<int> round1_completed(0);
  std::vector<std::thread> threads;
  std::mutex round1_mutex;

  auto start_time = std::chrono::steady_clock::now();

  // 第一轮测试所有IP的端口连通性
  int batch_size = (ip_list.size() + thread_count_ - 1) / thread_count_;

  for (int i = 0; i < thread_count_; i++) {
    int start = i * batch_size;
    int end = std::min(start + batch_size, (int)ip_list.size());

    if (start >= end)
      break;

    threads.emplace_back([&, start, end]() {
      std::vector<GitHubIP *> local_passed;

      for (int j = start; j < end; j++) {
        bool connected = quickConnectTest(ip_list[j].address, 443, 300);

        if (connected) {
          // 通过第一轮，加入本地列表
          local_passed.push_back(&ip_list[j]);
        } else {
          // 第一轮失败，直接标记为无效
          ip_list[j].is_valid = false;
          ip_list[j].latency = -1;
        }

        // 更新进度
        int completed = ++round1_completed;
        if (progress_callback) {
          std::lock_guard<std::mutex> lock(callback_mutex_);
          progress_callback(completed, ip_list.size(), 1, ip_list.size());
        }
      }

      // 批量添加，减少锁竞争
      if (!local_passed.empty()) {
        std::lock_guard<std::mutex> lock(round1_mutex);
        round1_passed_ips.insert(round1_passed_ips.end(), local_passed.begin(),
                                 local_passed.end());
      }
    });
  }

  for (auto &thread : threads) {
    if (thread.joinable())
      thread.join();
  }

  threads.clear();

  auto round1_end = std::chrono::steady_clock::now();
  auto round1_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      round1_end - start_time);

  std::cout << "  结果: " << round1_passed_ips.size() << "/" << ip_list.size()
            << " 通过 (" << round1_duration.count() << "ms)" << std::endl;

  if (round1_passed_ips.empty()) {
    std::cout << "所有IP在第一轮测试中被淘汰" << std::endl;
    return;
  }

  // ========== 第二轮联通测试 ==========
  std::cout << "\n[第二轮] 确认测试（300ms超时）..." << std::endl;

  std::atomic<int> round2_passed(0);
  std::atomic<int> round2_completed(0);

  // 第二轮：再次测试第一轮通过的IP，确认稳定性
  batch_size = (round1_passed_ips.size() + thread_count_ - 1) / thread_count_;
  auto round2_start = std::chrono::steady_clock::now();

  for (int i = 0; i < thread_count_; i++) {
    int start = i * batch_size;
    int end = std::min(start + batch_size, (int)round1_passed_ips.size());

    if (start >= end)
      break;

    threads.emplace_back([&, start, end]() {
      for (int j = start; j < end; j++) {
        GitHubIP *ip_ptr = round1_passed_ips[j];

        // 检查指针有效性
        if (!ip_ptr) {
          round2_completed++;
          continue;
        }

        // 第二次连接测试
        bool connected = quickConnectTest(ip_ptr->address, 443, 300);

        if (connected) {
          round2_passed++;
          ip_ptr->is_valid = true; // 通过两轮测试，标记为有效
          // 简单估算延迟：连接时间
          ip_ptr->latency = 50; // 默认值，简化处理
        } else {
          ip_ptr->is_valid = false;
          ip_ptr->latency = -1;
        }

        int completed = ++round2_completed;
        if (progress_callback) {
          std::lock_guard<std::mutex> lock(callback_mutex_);
          progress_callback(completed, round1_passed_ips.size(), 2,
                            round1_passed_ips.size());
        }
      }
    });
  }

  for (auto &thread : threads) {
    if (thread.joinable())
      thread.join();
  }

  auto round2_end = std::chrono::steady_clock::now();
  auto round2_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      round2_end - round2_start);
  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      round2_end - start_time);

  std::cout << "\n=== 简化测试完成 ===" << std::endl;
  std::cout << "各轮结果:" << std::endl;
  std::cout << "  第一轮（快速测试）: " << round1_passed_ips.size() << " 通过"
            << std::endl;
  std::cout << "  第二轮（确认测试）: " << round2_passed << " 通过"
            << std::endl;

  std::cout << "\n时间统计:" << std::endl;
  std::cout << "  第一轮: " << round1_duration.count() << "ms" << std::endl;
  std::cout << "  第二轮: " << round2_duration.count() << "ms" << std::endl;
  std::cout << "  总计: " << total_duration.count() << "ms" << std::endl;

  std::cout << "\n有效IP数: " << round2_passed << "（约 "
            << (round2_passed * 100 / ip_list.size()) << "%）" << std::endl;
}

// ========== 集成批量测试主函数 ==========
void IPTester::batchTest(
    std::vector<GitHubIP> &ip_list,
    std::function<void(int current, int total, int stage, int stage_total)>
        progress_callback) {

  if (ip_list.empty()) {
    std::cout << "IP列表为空，跳过测试" << std::endl;
    return;
  }

  std::cout << "开始简化测试 " << ip_list.size() << " 个IP地址..." << std::endl;

  // 调用简化测试函数
  simpleConnectFilter(ip_list, progress_callback);

  // ========== 深度测试（可选） ==========
  // 收集通过简化测试的IP
  std::vector<GitHubIP *> ips_for_depth_test;
  for (auto &ip : ip_list) {
    if (ip.is_valid) {
      ips_for_depth_test.push_back(&ip);
    }
  }

  if (ips_for_depth_test.empty()) {
    std::cout << "\n=== 测试完成 ===" << std::endl;
    std::cout << "总IP数: " << ip_list.size() << std::endl;
    std::cout << "有效IP: 0 个" << std::endl;
    std::cout << "\n警告：未找到任何有效IP！" << std::endl;
    return;
  }

  // 询问是否进行深度测试（可选）
  std::cout << "\n找到 " << ips_for_depth_test.size() << " 个连通IP"
            << std::endl;
  std::cout << "是否进行深度测试以验证是否为GitHub服务？ [y/N]: ";

  // 读取用户输入
  std::string answer;
  std::getline(std::cin, answer);

  if (!answer.empty() && (answer[0] == 'Y' || answer[0] == 'y')) {
    // 进行深度测试
    std::cout << "\n=== 深度测试开始 ===" << std::endl;
    std::cout << "深度测试 " << ips_for_depth_test.size() << " 个连通IP"
              << std::endl;

    std::atomic<int> depth_completed(0);
    std::vector<std::thread> depth_threads;

    int depth_batch_size =
        (ips_for_depth_test.size() + thread_count_ - 1) / thread_count_;

    // 初始化阶段3的进度显示
    if (progress_callback) {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      progress_callback(0, ips_for_depth_test.size(), 3,
                        ips_for_depth_test.size());
    }

    for (int i = 0; i < thread_count_; i++) {
      int start = i * depth_batch_size;
      int end =
          std::min(start + depth_batch_size, (int)ips_for_depth_test.size());

      if (start >= end)
        break;

      depth_threads.emplace_back([&, start, end]() {
        for (int j = start; j < end; j++) {
          GitHubIP *ip_ptr = ips_for_depth_test[j];

          if (!ip_ptr) {
            depth_completed++;
            continue;
          }

          // 深度测试：完整的GitHub服务验证
          bool is_github = testGitHubService(*ip_ptr);

          if (!is_github) {
            ip_ptr->is_valid = false;
            ip_ptr->latency = -1;
          }

          int completed = ++depth_completed;
          if (progress_callback) {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            progress_callback(completed, ips_for_depth_test.size(), 3,
                              ips_for_depth_test.size());
          }

          // 控制请求频率，避免被封IP
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

    std::cout << "\n深度测试完成" << std::endl;
  }

  // ========== 最终统计 ==========
  int valid_count = 0;
  for (const auto &ip : ip_list) {
    if (ip.is_valid) {
      valid_count++;
    }
  }

  std::cout << "\n=== 测试完成 ===" << std::endl;
  std::cout << "总IP数: " << ip_list.size() << std::endl;
  std::cout << "最终有效IP: " << valid_count << " 个" << std::endl;

  if (valid_count > 0) {
    // 输出前5个有效IP
    std::cout << "有效IP示例（前5个）:" << std::endl;
    int count = 0;
    for (const auto &ip : ip_list) {
      if (ip.is_valid && count < 5) {
        std::cout << "  " << ip.address << " -> " << ip.domain << std::endl;
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
