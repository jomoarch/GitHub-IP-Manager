#include "ip-tester.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <curl/curl.h>
#include <iostream>
#include <thread>

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

void IPTester::batchTest(std::vector<GitHubIP> &ip_list,
                         std::function<void(int, int)> progress_callback) {
  std::cout << "开始测试 " << ip_list.size() << " 个IP地址..." << std::endl;

  std::atomic<int> completed(0);
  std::vector<std::thread> threads;
  int batch_size = (ip_list.size() + thread_count_ - 1) / thread_count_;

  for (int i = 0; i < thread_count_; i++) {
    int start = i * batch_size;
    int end = std::min(start + batch_size, (int)ip_list.size());

    if (start >= end)
      break;

    threads.emplace_back([&, start, end]() {
      for (int j = start; j < end; j++) {
        // 测试是否为有效的GitHub服务器
        ip_list[j].is_valid = testGitHubService(ip_list[j]);

        if (ip_list[j].is_valid) {
          // 测试延迟
          ip_list[j].latency = testLatency(ip_list[j].address);
        } else {
          ip_list[j].latency = -1;
        }

        completed++;
        if (progress_callback) {
          progress_callback(completed, ip_list.size());
        }

        // 避免请求过快
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });
  }

  // 等待所有线程完成
  for (auto &thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  std::cout << "测试完成，有效IP: "
            << std::count_if(ip_list.begin(), ip_list.end(),
                             [](const GitHubIP &ip) { return ip.is_valid; })
            << " 个" << std::endl;
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
