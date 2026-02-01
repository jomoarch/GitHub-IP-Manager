#include "ip-tester.h"

static size_t discardCallback(char *ptr, size_t size, size_t nmemb,
                              void *userdata) {
  return size * nmemb; // 直接丢弃内容，只关心响应头
}

IPTester::IPTester(int timeout_ms, int thread_count)
    : timeout_ms_(timeout_ms), thread_count_(thread_count) {}

IPTester::~IPTester() {}

// ========== 统一的批量测试函数 ==========
void IPTester::unifiedTest(
    std::vector<GitHubIP> &ip_list, TestMode mode, int connect_test_times,
    std::function<void(int current, int total, int stage, int stage_total)>
        progress_callback) {

  if (ip_list.empty()) {
    std::cout << "IP列表为空，跳过测试" << std::endl;
    return;
  }

  std::cout << "开始测试 " << ip_list.size() << " 个IP地址..." << std::endl;
  std::cout << "测试模式: ";
  switch (mode) {
  case TEST_MODE_FULL:
    std::cout << "完整测试" << std::endl;
    break;
  case TEST_MODE_REFRESH:
    std::cout << "刷新测试" << std::endl;
    break;
  case TEST_MODE_CONNECT:
    std::cout << "仅联通测试" << std::endl;
    break;
  }

  // 根据模式执行不同的测试流程
  switch (mode) {
  case TEST_MODE_FULL: {
    // 1. 多次联通测试
    multipleConnectFilter(ip_list, connect_test_times, progress_callback);

    // 询问用户是否进行深度测试
    std::cout << "\n联通测试完成，是否进行深度测试？[Y/n]: ";
    std::string answer;
    std::getline(std::cin, answer);

    if (answer.empty() || answer[0] == 'Y' || answer[0] == 'y') {
      // 2. 深度测试（只测试通过联通测试的IP）
      depthTestWithLatency(ip_list, false, progress_callback);
    } else {
      std::cout << "跳过深度测试，仅使用联通测试结果。" << std::endl;
      // 为联通成功的IP设置默认延迟
      for (auto &ip : ip_list) {
        if (ip.is_valid && ip.latency == -1) {
          ip.latency = 100; // 默认延迟值
        }
      }
    }
    break;
  }
  case TEST_MODE_REFRESH:
    // 刷新测试：只深度测试当前有效的IP
    depthTestWithLatency(ip_list, false, progress_callback);
    break;

  case TEST_MODE_CONNECT:
    // 仅联通测试
    multipleConnectFilter(ip_list, connect_test_times, progress_callback);
    break;
  }

  // 最后按质量排序
  sortByQuality(ip_list);
}

// ========== 刷新测试专用函数 ==========
void IPTester::refreshTest(
    std::vector<GitHubIP> &ip_list,
    std::function<void(int current, int total, int stage, int stage_total)>
        progress_callback) {

  // 收集当前有效的IP
  std::vector<GitHubIP *> valid_ips;
  for (auto &ip : ip_list) {
    if (ip.is_valid) {
      valid_ips.push_back(&ip);
    }
  }

  if (valid_ips.empty()) {
    std::cout << "没有有效的IP需要刷新" << std::endl;
    return;
  }

  std::cout << "\n正在刷新 " << valid_ips.size() << " 个有效IP..." << std::endl;

  // 对有效IP进行深度测试
  depthTestWithLatency(ip_list, false, progress_callback);
}

// ========== 多次联通测试 ==========
void IPTester::multipleConnectFilter(
    std::vector<GitHubIP> &ip_list, int test_times,
    std::function<void(int current, int total, int stage, int stage_total)>
        progress_callback) {

  if (test_times <= 0) {
    std::cout << "跳过联通测试" << std::endl;
    return;
  }

  std::cout << "\n=== 开始 " << test_times << " 次联通测试 ===" << std::endl;
  std::cout << "总IP数: " << ip_list.size() << std::endl;

  // 重置所有IP状态
  for (auto &ip : ip_list) {
    ip.is_valid = false;
    ip.latency = -1;
  }

  // 用于记录每轮测试后的有效IP
  std::vector<GitHubIP *> current_valid_ips;
  for (auto &ip : ip_list) {
    current_valid_ips.push_back(&ip);
  }

  auto start_time = std::chrono::steady_clock::now();
  int remaining_valid_count = current_valid_ips.size();

  // 进行多次联通测试
  for (int round = 1; round <= test_times; round++) {
    if (remaining_valid_count == 0) {
      std::cout << "\n第 " << round << " 轮测试跳过：所有IP已被淘汰"
                << std::endl;
      break;
    }

    std::cout << "\n[第 " << round << " 轮] 联通测试（300ms超时）..."
              << std::endl;

    std::vector<GitHubIP *> round_passed_ips;
    std::atomic<int> round_completed(0);
    std::vector<std::thread> threads;
    std::mutex round_mutex;

    // 本轮测试的批量大小
    int batch_size =
        (current_valid_ips.size() + thread_count_ - 1) / thread_count_;

    // 设置进度条显示
    int total_for_round = current_valid_ips.size();
    if (progress_callback) {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      progress_callback(0, total_for_round, round, test_times);
    }

    for (int i = 0; i < thread_count_; i++) {
      int start = i * batch_size;
      int end = std::min(start + batch_size, (int)current_valid_ips.size());

      if (start >= end)
        break;

      threads.emplace_back([&, start, end, round]() {
        std::vector<GitHubIP *> local_passed;

        for (int j = start; j < end; j++) {
          GitHubIP *ip_ptr = current_valid_ips[j];
          if (!ip_ptr) {
            round_completed++;
            continue;
          }

          bool connected = quickConnectTest(ip_ptr->address, 443, 300);

          if (connected) {
            local_passed.push_back(ip_ptr);
          } else {
            ip_ptr->is_valid = false;
          }

          // 更新进度
          int completed = ++round_completed;
          if (progress_callback) {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            progress_callback(completed, total_for_round, round, test_times);
          }
        }

        // 批量添加，减少锁竞争
        if (!local_passed.empty()) {
          std::lock_guard<std::mutex> lock(round_mutex);
          round_passed_ips.insert(round_passed_ips.end(), local_passed.begin(),
                                  local_passed.end());
        }
      });
    }

    for (auto &thread : threads) {
      if (thread.joinable())
        thread.join();
    }

    // 更新有效IP列表
    current_valid_ips = round_passed_ips;
    remaining_valid_count = current_valid_ips.size();

    std::cout << "  结果: " << remaining_valid_count << " 个IP通过本轮测试"
              << std::endl;

    // 为通过本轮测试的IP设置有效标记
    for (auto ip_ptr : current_valid_ips) {
      ip_ptr->is_valid = true;
    }

    // 如果本轮测试淘汰了所有IP，提前结束
    if (remaining_valid_count == 0) {
      std::cout << "所有IP均被淘汰，测试提前结束" << std::endl;
      break;
    }

    // 控制轮次间隔
    if (round < test_times && remaining_valid_count > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  auto end_time = std::chrono::steady_clock::now();
  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  std::cout << "\n=== " << test_times << " 次联通测试完成 ===" << std::endl;
  std::cout << "时间统计: " << total_duration.count() << "ms" << std::endl;
  std::cout << "最终有效IP数: " << remaining_valid_count << "（约 "
            << (remaining_valid_count * 100 / ip_list.size()) << "%）"
            << std::endl;
}

// ========== 快速端口联通测试 ==========
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

// ========== 深度测试（包含延迟检测） ==========
void IPTester::depthTestWithLatency(
    std::vector<GitHubIP> &ip_list, bool test_all,
    std::function<void(int current, int total, int stage, int stage_total)>
        progress_callback) {

  // 收集需要深度测试的IP
  std::vector<GitHubIP *> ips_to_test;
  for (auto &ip : ip_list) {
    if (test_all || ip.is_valid) {
      ips_to_test.push_back(&ip);
    }
  }

  if (ips_to_test.empty()) {
    std::cout << "没有IP需要深度测试" << std::endl;
    return;
  }

  std::cout << "\n=== 深度测试开始 ===" << std::endl;
  std::cout << "测试 " << ips_to_test.size() << " 个IP" << std::endl;
  std::cout << "（包含GitHub服务验证和真实延迟检测）" << std::endl;

  std::atomic<int> depth_completed(0);
  std::vector<std::thread> depth_threads;

  int depth_batch_size =
      (ips_to_test.size() + thread_count_ - 1) / thread_count_;

  // 初始化深度测试进度显示
  if (progress_callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    progress_callback(0, ips_to_test.size(), 0, 0); // stage=0表示深度测试
  }

  // 存储每个IP的延迟信息
  struct IPResult {
    GitHubIP *ip_ptr;
    int latency;
    bool is_github;
  };
  std::vector<IPResult> depth_results(ips_to_test.size());

  for (int i = 0; i < thread_count_; i++) {
    int start = i * depth_batch_size;
    int end = std::min(start + depth_batch_size, (int)ips_to_test.size());

    if (start >= end)
      break;

    depth_threads.emplace_back([&, start, end]() {
      for (int j = start; j < end; j++) {
        GitHubIP *ip_ptr = ips_to_test[j];

        if (!ip_ptr) {
          depth_completed++;
          if (progress_callback) {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            progress_callback(depth_completed.load(), ips_to_test.size(), 0, 0);
          }
          continue;
        }

        // ========== 步骤1：测试真实延迟 ==========
        int real_latency = -1;

        // 优先使用ping测试延迟（更准确）
        real_latency = testLatency(ip_ptr->address);

        // 如果ping失败，使用TCP连接时间
        if (real_latency < 0) {
          int sock = socket(AF_INET, SOCK_STREAM, 0);
          if (sock >= 0) {
            // 设置超时
            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                       sizeof(timeout));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                       sizeof(timeout));

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(443);

            if (inet_pton(AF_INET, ip_ptr->address.c_str(), &addr.sin_addr) >
                0) {
              auto start_time = std::chrono::steady_clock::now();

              if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                auto end_time = std::chrono::steady_clock::now();
                auto duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time);
                real_latency = static_cast<int>(duration.count());
              }
            }
            close(sock);
          }
        }

        // ========== 步骤2：测试是否为GitHub服务 ==========
        bool is_github = testGitHubService(*ip_ptr);

        // 保存结果
        depth_results[j] = {ip_ptr, real_latency, is_github};

        // 更新IP信息
        if (is_github && real_latency > 0) {
          ip_ptr->is_valid = true;
          ip_ptr->latency = real_latency;
        } else {
          ip_ptr->is_valid = false;
          ip_ptr->latency = -1;
        }

        int completed = ++depth_completed;
        if (progress_callback) {
          std::lock_guard<std::mutex> lock(callback_mutex_);
          progress_callback(completed, ips_to_test.size(), 0, 0);
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

  // ========== 延迟统计和输出 ==========
  std::cout << "\n=== 深度测试完成 ===" << std::endl;

  // 统计结果
  int valid_count = 0;
  int github_valid_count = 0;
  int total_latency = 0;
  int min_latency = INT_MAX;
  int max_latency = 0;
  GitHubIP *fastest_ip = nullptr;

  // 计算统计数据
  for (const auto &result : depth_results) {
    if (result.ip_ptr && result.is_github) {
      github_valid_count++;

      if (result.latency > 0) {
        valid_count++;
        total_latency += result.latency;

        if (result.latency < min_latency) {
          min_latency = result.latency;
          fastest_ip = result.ip_ptr;
        }

        if (result.latency > max_latency) {
          max_latency = result.latency;
        }
      }
    }
  }

  std::cout << "\n统计信息：" << std::endl;
  std::cout << "================================" << std::endl;
  std::cout << "测试IP总数: " << ips_to_test.size() << std::endl;
  std::cout << "GitHub有效IP: " << github_valid_count << std::endl;

  if (valid_count > 0) {
    double avg_latency = static_cast<double>(total_latency) / valid_count;

    std::cout << "\n延迟统计：" << std::endl;
    std::cout << "  平均延迟: " << std::fixed << std::setprecision(1)
              << avg_latency << "ms" << std::endl;
    std::cout << "  最低延迟: " << min_latency << "ms" << std::endl;
    std::cout << "  最高延迟: " << max_latency << "ms" << std::endl;

    if (fastest_ip) {
      std::cout << "\n最快IP：" << std::endl;
      std::cout << "  " << fastest_ip->address << " -> " << fastest_ip->domain;
      std::cout << " (" << min_latency << "ms)" << std::endl;
    }
  }
}

// ========== 保持原有的辅助函数 ==========
int IPTester::testLatency(const std::string &ip) {
  // 使用ping命令获取真实延迟
  std::string cmd =
      "ping -c 3 -W 1 " + ip + " 2>&1 | tail -1 | awk -F '/' '{print $5}'";

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
    long ssl_verify_result = 0;
    curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &ssl_verify_result);

    if (response_code > 0) {
      if (ssl_verify_result == 0) {
        is_valid = true;
      } else if (response_code / 100 == 2 || response_code / 100 == 3) {
        is_valid = true;
      } else if (response_code == 403 || response_code == 404) {
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
