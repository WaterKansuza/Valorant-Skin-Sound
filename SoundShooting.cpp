#pragma comment(lib, "winmm.lib")
#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <mmsystem.h>
#include <cstdlib>
#include <ctime>

// clear screen
#define clearScreen() printf("\033[H\033[J")
// move cursor
#define gotoXy(x, y) printf("\033[%d;%dH", (x), (y))
// hide cursor
#define hideCursor() printf("\033[?25l");
// show cursor
#define showCursor() printf("\033[?25h");
// reset text color to defaults
#define textReset() printf("\033[0m");
// Invert color
#define invertColor() printf("\033[7m");

// Biến trạng thái
bool isShooting = false;
bool isWeaponEquipped = true;
int currentTransientIndex = 1;
int currentVolume = 10;
std::thread audioThread;

// Biến lưu tên thư mục skin hiện tại (Thay đổi thủ công tại đây)
std::string currentSkin = "Aquarium";

// UI
void InitUI()
{
    hideCursor();
    clearScreen();
    gotoXy(1, 1);
    std::cout << "--- VALORANT VANDAL SKIN SOUND ---";
    gotoXy(2, 1);
    std::cout << "Skin Name :";
    gotoXy(3, 1);
    std::cout << "Volume    :";
    gotoXy(4, 1);
    std::cout << "\nF1: Start, F3: Stop \nUp: Volumn Up, Dn: Volumn Down";
    std::fflush(stdout);
}
void UpdateSkinUI()
{
    gotoXy(2, 13);
    std::cout << currentSkin << "          " << std::flush;
}

void UpdateVolumeUI()
{
    gotoXy(3, 13);
    std::cout << currentVolume << "%  " << std::flush;
}


// Hàm chỉnh âm lượng cấp độ ứng dụng (0 - 100)
void SetAppVolume(int volumeLevel)
{
    if (volumeLevel < 0)
        volumeLevel = 0;
    if (volumeLevel > 100)
        volumeLevel = 100;

    DWORD vol = (DWORD)(volumeLevel * 0xFFFF / 100);
    DWORD newVolume = (vol & 0xFFFF) | (vol << 16);

    waveOutSetVolume(NULL, newVolume);
    UpdateVolumeUI();
    //std::cout << "Am luong: " << volumeLevel << "%\n";
}

// Lấy thư mục gốc chứa file .cpp
std::string GetCppDirectory()
{
    std::string fullPath(__FILE__);
    size_t pos = fullPath.find_last_of("\\/");
    return fullPath.substr(0, pos);
}

// Nối thư mục gốc với thư mục Sound, thư mục skin và tên file
std::string GetSoundPath(const std::string &fileName)
{
    return GetCppDirectory() + "\\Sound\\" + currentSkin + "\\" + fileName;
}

// Phát âm thanh tiếng nổ súng (Transient)
void PlayAudio(const std::string &fileName)
{
    std::string fullPath = GetSoundPath(fileName);
    PlaySoundA(fullPath.c_str(), NULL, SND_FILENAME | SND_ASYNC);
}

void StopAudio()
{
    PlaySoundA(NULL, 0, 0);
}

// Phát âm thanh dội lại (Tail) trên kênh độc lập
void PlayTailAudio(const std::string &fileName)
{
    std::string fullPath = GetSoundPath(fileName);
    mciSendStringA("close TailSound", NULL, 0, NULL);
    std::string openCmd = "open \"" + fullPath + "\" type waveaudio alias TailSound";
    mciSendStringA(openCmd.c_str(), NULL, 0, NULL);
    mciSendStringA("play TailSound", NULL, 0, NULL);
}

// Luồng xử lý âm thanh
void ShootingLogic()
{
    auto nextShotTime = std::chrono::steady_clock::now();

    while (isShooting)
    {
        // Sinh số ngẫu nhiên và phát đạn
        if (currentSkin == "HypeDragon")
        {
            currentTransientIndex = (rand() % 3) + 1;
        }
        else
        {
            currentTransientIndex = (rand() % 5) + 1;
        }
        std::string transientFile = "Transient_" + std::to_string(currentTransientIndex) + " (SFX).wav";
        PlayAudio(transientFile);

        nextShotTime += std::chrono::milliseconds(102); // Chu kỳ bắn chính xác (102ms)

        // Chờ đến thời điểm viên đạn tiếp theo hoặc bị ngắt khi nhả chuột
        while (isShooting && std::chrono::steady_clock::now() < nextShotTime)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Nếu bắn từ 2 viên trở lên (sấy), chờ chu kỳ viên cuối hoàn tất và phát Tail

    while (std::chrono::steady_clock::now() < nextShotTime)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::string tailFile = "Tail_" + std::to_string(currentTransientIndex) + " (SFX).wav";
    PlayTailAudio(tailFile);
}

// Luồng lắng nghe sự kiện chuột
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
                    audioThread.join();
                audioThread = std::thread(ShootingLogic);
            }
        }
        else if (wParam == WM_LBUTTONUP)
        {
            isShooting = false;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// Luồng lắng nghe sự kiện bàn phím
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && wParam == WM_KEYDOWN)
    {
        KBDLLHOOKSTRUCT *kbdStruct = (KBDLLHOOKSTRUCT *)lParam;

        if (kbdStruct->vkCode == 'R')
        {
            // PlayAudio("Reload.wav");
        }
        else if (kbdStruct->vkCode == VK_F1)
        {
            isWeaponEquipped = !isWeaponEquipped;
            // std::cout << "\rTrang thai sung: " << (isWeaponEquipped ? "On " : "Off") << std::flush;
        }
        else if (kbdStruct->vkCode == VK_F3)
        {
            PostQuitMessage(0);
        }
        else if (kbdStruct->vkCode == VK_UP)
        {
            currentVolume += 5;
            if (currentVolume > 100)
                currentVolume = 100;
            SetAppVolume(currentVolume);
            //UpdateVolumeUI();
        }
        else if (kbdStruct->vkCode == VK_DOWN)
        {
            currentVolume -= 5;
            if (currentVolume < 0)
                currentVolume = 0;
            SetAppVolume(currentVolume);
            //UpdateVolumeUI();
        }

        else if (kbdStruct->vkCode == '1')
        {
            isWeaponEquipped = true;
        }

        // Hotkey is capitalize
        else if (kbdStruct->vkCode == 'Q' || kbdStruct->vkCode == 'E' || kbdStruct->vkCode == 'C' || kbdStruct->vkCode == '2' || kbdStruct->vkCode == '4')
        {
            isWeaponEquipped = false;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int main()
{
    // If you wish to fully hide the console window, then un-comment this line in main() :
    // ShowWindow(FindWindowA("ConsoleWindowClass", NULL), false)

    // Kích hoạt môi trường ANSI trên Windows
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= 0x0004; // Kích hoạt ENABLE_VIRTUAL_TERMINAL_PROCESSING
    SetConsoleMode(hOut, dwMode);

    srand(static_cast<unsigned int>(time(NULL)));

    SetAppVolume(currentVolume);
    InitUI();
    UpdateSkinUI();
    UpdateVolumeUI();

    HHOOK mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, NULL, 0);
    HHOOK keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, NULL, 0);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(mouseHook);
    UnhookWindowsHookEx(keyboardHook);

    isShooting = false;
    if (audioThread.joinable())
        audioThread.join();
    StopAudio();
    mciSendStringA("close TailSound", NULL, 0, NULL);

    return 0;
}