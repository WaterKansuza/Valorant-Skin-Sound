#pragma comment(lib, "winmm.lib")
#include <windows.h>
#include <mmsystem.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <atomic>
#include <random>

// ── ANSI terminal macros ──────────────────────────────────────────────────────
#define clearScreen() fputs("\033[H\033[J", stdout)
#define gotoXy(r, c) printf("\033[%d;%dH", (r), (c))
#define hideCursor() fputs("\033[?25l", stdout)
// show cursor
#define showCursor() printf("\033[?25h");

// ── Vandal fire rate ──────────────────────────────────────────────────────────
// 9.75 rounds/sec → period = 1,000,000,000 / 9.75 ≈ 102,564,103 ns
// (original dùng 102ms → lệch ~0.564ms/viên, cộng dồn ~14ms/mag)
static constexpr auto FIRE_INTERVAL = std::chrono::nanoseconds(102'564'103LL);
// Spin-wait 1.5ms cuối để đảm bảo deadline chính xác
static constexpr auto SPIN_THRESHOLD = std::chrono::microseconds(1'500);

// ── Shared atomic state ───────────────────────────────────────────────────────
std::atomic<bool> isShooting{false};
std::atomic<bool> isWeaponEquipped{true};
// Flag để biết audio thread đã return chưa (tránh block hook khi join)
std::atomic<bool> audioThreadDone{true};

// ── UI state (chỉ message-loop thread chạm → không cần sync) ─────────────────
int currentVolume = 15;
int currentIndex = 1; // default index 1 = "Hellfire"
std::string currentSkin = "Hellfire";
const std::vector<std::string> SKINS = {
    "Aquarium", "Hellfire", "HypeDragon", "Ashen", "Cyberpunk", "Rose"};
std::thread audioThread;

// ── Path helpers ──────────────────────────────────────────────────────────────
// Cache exe directory (C++11 static local init thread-safe)
static const std::string &ExeDir()
{
    static const std::string dir = []()
    {
        char buf[MAX_PATH];
        GetModuleFileNameA(NULL, buf, MAX_PATH);
        std::string s(buf);
        return s.substr(0, s.find_last_of("\\/"));
    }();
    return dir;
}

static std::string SoundPath(const std::string &skin, const std::string &file)
{
    return ExeDir() + "\\Sound\\" + skin + "\\" + file;
}

// ── Volume ────────────────────────────────────────────────────────────────────
static void SetVolume(int pct)
{
    if (pct < 0)
        pct = 0;
    else if (pct > 100)
        pct = 100;
    DWORD v = (DWORD)(pct * 0xFFFF / 100);
    waveOutSetVolume(NULL, v | (v << 16));
}

// ── Audio playback ────────────────────────────────────────────────────────────
static void PlayTransient(const std::string &skin, int idx)
{
    PlaySoundA(
        SoundPath(skin, "Transient_" + std::to_string(idx) + " (SFX).wav").c_str(),
        NULL, SND_FILENAME | SND_ASYNC);
}

static void PlayTail(const std::string &skin, int idx)
{
    std::string path = SoundPath(skin, "Tail_" + std::to_string(idx) + " (SFX).wav");
    mciSendStringA("close TailSound", NULL, 0, NULL);
    mciSendStringA(("open \"" + path + "\" type waveaudio alias TailSound").c_str(), NULL, 0, NULL);
    mciSendStringA("play TailSound", NULL, 0, NULL);
}

// ── Precision wait ────────────────────────────────────────────────────────────
// Coarse sleep + spin để đạt độ chính xác cao hơn nhiều so với sleep_for đơn thuần.
// interruptible=true: thoát sớm nếu isShooting thành false (giữa các viên đạn).
// interruptible=false: không bị ngắt (chờ deadline cuối trước khi phát tail).
static void SleepUntil(std::chrono::steady_clock::time_point tp, bool interruptible)
{
    for (;;)
    {
        auto now = std::chrono::steady_clock::now();
        if (now >= tp)
            return;
        if (interruptible && !isShooting)
            return;
        auto rem = tp - now;
        if (rem > SPIN_THRESHOLD)
        {
            std::this_thread::sleep_for(rem - SPIN_THRESHOLD);
        }
        else
        {
            // Spin cuối: độ chính xác sub-millisecond
            while (std::chrono::steady_clock::now() < tp)
            {
                if (interruptible && !isShooting)
                    return;
                YieldProcessor();
            }
            return;
        }
    }
}

// ── Shooting logic (audio thread) ─────────────────────────────────────────────
// currentSkin truyền by value tại lúc spawn thread → không có data race
// dù user đổi skin trong khi đang bắn.
static void ShootingLogic(std::string skin)
{
    // Số lượng transient theo skin
    const int maxIdx = (skin == "HypeDragon") ? 3
                       : (skin == "Ashen")    ? 4
                                              : 5;

    // Per-thread RNG: không dùng global rand() → không cần lock
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(1, maxIdx);

    int shots = 0;
    int lastIdx = 1;
    auto deadline = std::chrono::steady_clock::now();

    while (isShooting)
    {
        lastIdx = dist(rng);
        PlayTransient(skin, lastIdx);
        ++shots;

        deadline += FIRE_INTERVAL;
        SleepUntil(deadline, /*interruptible=*/true);
    }

    // Chờ hết chu kỳ viên cuối rồi mới phát tail (đồng bộ timing với game)
    if (shots > 0)
    {
        SleepUntil(deadline, /*interruptible=*/false);
        PlayTail(skin, lastIdx);
    }

    audioThreadDone.store(true, std::memory_order_release);
}

// ── UI ────────────────────────────────────────────────────────────────────────
static void UpdateSkinUI()
{
    currentSkin = SKINS[currentIndex];
    gotoXy(2, 13);
    printf("%-15s", currentSkin.c_str());
    fflush(stdout);
}

static void UpdateVolumeUI()
{
    gotoXy(3, 13);
    printf("%3d%%  ", currentVolume);
    fflush(stdout);
}

static void InitUI()
{
    hideCursor();
    clearScreen();
    gotoXy(1, 1);
    printf("--- VALORANT VANDAL SKIN SOUND ---\n");
    gotoXy(2, 1);
    printf("Skin Name :");
    gotoXy(3, 1);
    printf("Volume    :");
    gotoXy(5, 1);
    printf("F1: Toggle weapon   F3: Exit\n");
    gotoXy(6, 1);
    printf("Left/Right: Volume -/+5\n");
    gotoXy(7, 1);
    printf("Up/Down: Change Skin\n");
    fflush(stdout);
}

// ── Hook callbacks ────────────────────────────────────────────────────────────
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0)
    {
        if (wParam == WM_LBUTTONDOWN && !isShooting)
        {
            isShooting = true;
            if (isWeaponEquipped)
            {
                if (audioThread.joinable())
                {
                    if (audioThreadDone.load(std::memory_order_acquire))
                    {
                        // Thread đã xong → join tức thì, không block
                        audioThread.join();
                    }
                    else
                    {
                        // Hiếm: click mới trước khi tail phát xong
                        // Detach để hook không bị block; PlayTail mới sẽ close alias cũ
                        audioThread.detach();
                    }
                }
                audioThreadDone.store(false, std::memory_order_relaxed);
                // Truyền skin by value → không race dù user đang đổi skin
                audioThread = std::thread(ShootingLogic, currentSkin);
            }
        }
        else if (wParam == WM_LBUTTONUP)
        {
            isShooting = false;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && wParam == WM_KEYDOWN)
    {
        const DWORD vk = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam)->vkCode;
        switch (vk)
        {
        case VK_F1:
            isWeaponEquipped = !isWeaponEquipped.load(std::memory_order_relaxed);
            break;
        case VK_F3:
            clearScreen();
            showCursor();
            std::cout << "Thanks for using my code!";
            PostQuitMessage(0);
            break;
        case VK_RIGHT:
            if (currentVolume < 100)
            {
                currentVolume += 5;
                SetVolume(currentVolume);
                UpdateVolumeUI();
            }
            break;
        case VK_LEFT:
            if (currentVolume > 0)
            {
                currentVolume -= 5;
                SetVolume(currentVolume);
                UpdateVolumeUI();
            }
            break;
        case VK_UP:
            if (--currentIndex < 0)
                currentIndex = (int)SKINS.size() - 1;
            UpdateSkinUI();
            break;
        case VK_DOWN:
            if (++currentIndex >= (int)SKINS.size())
                currentIndex = 0;
            UpdateSkinUI();
            break;
        case '1':
            isWeaponEquipped = true;
            break;
        case 'Q':
        case 'E':
        case 'C':
        case '2':
        case '3':
            isWeaponEquipped = false;
            break;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main()
{
    // Kích hoạt ANSI escape trên Windows console
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    // Nâng timer resolution lên 1ms (mặc định Windows là 15.6ms)
    // Giúp sleep_for trong SleepUntil chính xác hơn
    timeBeginPeriod(1);

    SetVolume(currentVolume);
    InitUI();
    UpdateSkinUI();
    UpdateVolumeUI();

    HHOOK hMouse = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, NULL, 0);
    HHOOK hKeybd = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, NULL, 0);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hMouse);
    UnhookWindowsHookEx(hKeybd);

    isShooting = false;
    if (audioThread.joinable())
        audioThread.join();
    PlaySoundA(NULL, 0, 0);
    mciSendStringA("close TailSound", NULL, 0, NULL);

    timeEndPeriod(1);
    return 0;
}