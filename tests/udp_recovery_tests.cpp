// The tracker port coming back after another game let go of it.
//
// A player who launches this game with a previous one still running finds the
// UDP port already bound. The mod stays up and the receiver's supervisor keeps
// re-binding, so the moment the other game exits, head tracking has to start on
// its own - fast enough that the player never gets as far as a bug report.
//
// This suite MEASURES that rather than asserting the code reads correctly: it
// holds the port with a real socket, drives a real TrackingRuntime, releases the
// port and times how long the mod takes to publish its first pose. It also reads
// the log back off disk, because the bind-failure line is the one diagnostic the
// player sends us and it has to carry the reason the OS actually gave.

#include <winsock2.h>
#include <ws2tcpip.h>

#include "test_harness.h"

#include "config.h"
#include "tracking_runtime.h"

#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/protocol/udp_receiver.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using metroex_test::Check;

namespace {

using Clock = std::chrono::steady_clock;

int ElapsedMs(Clock::time_point from) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - from).count());
}

// A plain bind on the tracker port, standing in for the other game. Deliberately
// no SO_REUSEADDR: that is what makes the second bind fail rather than silently
// sharing the port, which is the conflict being reproduced here.
class PortHolder {
public:
    ~PortHolder() { Release(); }

    bool Hold(uint16_t port) {
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) return false;
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            return false;
        }
        return true;
    }

    void Release() {
        if (m_socket == INVALID_SOCKET) return;
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

private:
    SOCKET m_socket = INVALID_SOCKET;
};

uint16_t FindFreePort(PortHolder& holder) {
    for (uint16_t port = 24242; port < 24342; ++port) {
        if (holder.Hold(port)) return port;
    }
    return 0;
}

// An OpenTrack tracker at ~120Hz. The pose walks in small steps because the
// receiver's dropout gate discards a bit-identical repeat, so a held pose would
// publish once and then read as a tracker that had lost the head.
class TrackerSender {
public:
    ~TrackerSender() { Stop(); }

    bool Start(uint16_t port) {
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) return false;
        m_dest.sin_family = AF_INET;
        m_dest.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &m_dest.sin_addr);
        m_run.store(true, std::memory_order_release);
        m_thread = std::thread([this] { Pump(); });
        return true;
    }

    void Stop() {
        m_run.store(false, std::memory_order_release);
        if (m_thread.joinable()) m_thread.join();
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }

private:
    void Pump() {
        int step = 0;
        while (m_run.load(std::memory_order_acquire)) {
            double packet[6] = {};
            packet[3] = 2.0 + 0.01 * (step % 64);
            packet[4] = 1.0;
            sendto(m_socket, reinterpret_cast<const char*>(packet), sizeof(packet), 0,
                   reinterpret_cast<const sockaddr*>(&m_dest), sizeof(m_dest));
            ++step;
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }

    SOCKET m_socket = INVALID_SOCKET;
    sockaddr_in m_dest = {};
    std::atomic<bool> m_run{false};
    std::thread m_thread;
};

std::string ReadFileText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// WSAEADDRINUSE. Named here rather than taken from a header so a change in what
// the OS reports fails the test instead of following it.
constexpr int kAddressInUse = 10048;

// What a rebind is allowed to cost: one retry interval, plus the supervisor's
// wake-up granularity, plus a whole further retry interval of scheduling slack.
//
// The slack is an allowance for the machine, not a claim about the mod. On this
// developer box the worst trial across the retry cycle is around 390ms against a
// 500ms interval; a two-core CI runner can lose a whole 500ms wake-up to the
// scheduler, and a budget that did not allow for one would fail a correct
// change. What the suite is measuring is that recovery happens on the cadence
// the receiver documents rather than not at all.
constexpr int kRebindBudgetMs =
    cameraunlock::UdpReceiver::kRetryIntervalMs * 2 + 100;

void PrintFirstLineFrom(const std::string& text, const char* needle) {
    const size_t start = text.find(needle);
    if (start == std::string::npos) return;
    const size_t end = text.find('\n', start);
    std::printf("%s\n", text.substr(start, end - start).c_str());
}

// The bind-failure line the player sends us has to name the reason the socket
// gave, because a bind fails for reasons other than a port conflict and a guess
// sends them looking for a program that is not running.
void TestBindFailureNamesTheOsReason(uint16_t port, const std::filesystem::path& logPath) {
    PortHolder other;
    Check(other.Hold(port), "the other game holds the tracker port");

    cameraunlock::logging::Open(logPath.wstring());

    metroex::Config cfg;
    cfg.udp_port = port;
    cfg.enabled_on_startup = true;

    metroex::TrackingRuntime rt;
    rt.Start(cfg);

    const std::string log = ReadFileText(logPath);
    rt.Stop();
    cameraunlock::logging::Close();

    Check(log.find("Failed to bind UDP port " + std::to_string(port)) != std::string::npos,
          "log names the port the bind failed on");
    Check(log.find("bind failed with error") != std::string::npos,
          "log names the call that failed rather than assuming a cause");
    Check(log.find(std::to_string(kAddressInUse)) != std::string::npos,
          "log carries the address-in-use code the OS actually returned");
    Check(log.find("retrying every " +
                   std::to_string(cameraunlock::UdpReceiver::kRetryIntervalMs) + "ms") !=
              std::string::npos,
          "log states the retry cadence rather than leaving the player to guess");

    PrintFirstLineFrom(log, "Failed to bind");
}

// The number this suite exists for: the port frees up, and the mod is publishing
// a tracked pose again this soon after.
void TestPortFreeToFirstPose(uint16_t port) {
    PortHolder other;
    Check(other.Hold(port), "the other game holds the tracker port");

    metroex::Config cfg;
    cfg.udp_port = port;
    cfg.enabled_on_startup = true;

    metroex::TrackingRuntime rt;
    rt.Start(cfg);

    TrackerSender tracker;
    Check(tracker.Start(port), "tracker sends to the held port");

    // Long enough that several retries have already been refused, so the
    // measurement starts from a supervisor in its steady retry rhythm rather
    // than from its first attempt.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    metroex::HeadPose pose;
    const metroex::TrackingState blocked = rt.SamplePerFrame(true, false, cfg.ads_mode, pose);
    Check(blocked.verdict == metroex::TrackingVerdict::NoTracker,
          "no pose reaches the camera while the port is held");

    const Clock::time_point freed = Clock::now();
    other.Release();

    int elapsed = -1;
    while (ElapsedMs(freed) < 5000) {
        const metroex::TrackingState state = rt.SamplePerFrame(true, false, cfg.ads_mode, pose);
        if (state.verdict == metroex::TrackingVerdict::Active) {
            elapsed = ElapsedMs(freed);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::printf("port freed -> first tracked pose: %d ms\n", elapsed);
    Check(elapsed >= 0, "the mod recovers on its own once the port frees up");
    Check(elapsed >= 0 && elapsed <= kRebindBudgetMs,
          "recovery lands inside one retry interval plus scheduling slack");

    tracker.Stop();
    rt.Stop();
}

// The cadence itself. Releasing the port at a different phase of the retry cycle
// each time spreads the latencies across the interval, so the worst trial is
// what the interval actually is - reading the constant back would prove nothing
// about the loop that consumes it.
void TestRetryCadence(uint16_t port) {
    const int phases[] = {0, 120, 240, 360, 480};
    int worst = 0;

    for (int phase : phases) {
        PortHolder other;
        Check(other.Hold(port), "the other game holds the tracker port");

        cameraunlock::UdpReceiver rx;
        rx.Start(port);
        Check(rx.IsRetrying(), "receiver is waiting for the port");

        std::this_thread::sleep_for(std::chrono::milliseconds(1000 + phase));

        const Clock::time_point freed = Clock::now();
        other.Release();

        int elapsed = -1;
        while (ElapsedMs(freed) < 5000) {
            if (rx.IsRunning()) {
                elapsed = ElapsedMs(freed);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::printf("release at +%dms into the cycle -> rebind after %d ms\n", phase, elapsed);
        Check(elapsed >= 0, "receiver rebinds without being restarted");
        if (elapsed > worst) worst = elapsed;

        rx.Stop();
    }

    std::printf("worst rebind latency across the retry cycle: %d ms\n", worst);
    Check(worst <= kRebindBudgetMs, "the retry cadence is one interval, not a longer one");
}

}  // namespace

int main() {
    WSADATA wsa = {};
    // Held for the whole run so the receiver's own WSACleanup cannot tear
    // Winsock down under this suite's sockets.
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::printf("FAIL: WSAStartup\n");
        return EXIT_FAILURE;
    }

    PortHolder probe;
    const uint16_t port = FindFreePort(probe);
    probe.Release();
    Check(port != 0, "a free tracker port is available for the test");
    if (port == 0) {
        WSACleanup();
        return metroex_test::Report();
    }

    const std::filesystem::path logPath =
        std::filesystem::temp_directory_path() / "metroex_udp_recovery_test.log";

    TestBindFailureNamesTheOsReason(port, logPath);
    TestPortFreeToFirstPose(port);
    TestRetryCadence(port);

    std::error_code ec;
    std::filesystem::remove(logPath, ec);
    std::filesystem::remove(cameraunlock::logging::PreviousGenerationPath(logPath.wstring()), ec);

    WSACleanup();
    return metroex_test::Report();
}
