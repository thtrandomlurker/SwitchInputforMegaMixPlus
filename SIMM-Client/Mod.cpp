// dllmain.cpp : Defines the entry point for the DLL application.
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <detours.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define _USE_MATH_DEFINES
#include <math.h>

#include <Helpers.h>
#include <SigScan.h>

//#include <JoyShockLibrary.h>

#include <chrono>
#include <atomic>
#include <thread>
#include <algorithm>
#include <mutex>

#include <cstdio>
#ifdef USE_NETWORK
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <libusb-1.0/libusb.h>
#endif
#include <iostream>
#include <toml++/toml.hpp>
#include <ViGEm/Client.h>
#include <XInput.h>

#pragma comment(lib, "setupapi.lib")

#include "libnx-helpers.h"

typedef enum REQUEST_TYPE {
	REQUEST_NONE,
	REQUEST_BUTTONS,
	REQUEST_ANGLE,
	REQUEST_DIVA,
	REQUEST_RUMBLE,
	REQUEST_QUIT,
	REQUEST_INVALID
} REQUEST_TYPE;

typedef struct {
	REQUEST_TYPE type;
	uint64_t downButtons;
	uint64_t heldButtons;
	uint64_t upButtons;
} RESPONSE_BUTTONS;

typedef struct {
	REQUEST_TYPE type;
	float angleLeft[3];
	float angleRight[3];
} RESPONSE_ANGLE;

typedef struct {
	REQUEST_TYPE type;
	uint64_t heldButtons;
	int32_t stickLX;
	int32_t stickLY;
	int32_t stickRX;
	int32_t stickRY;
	float directionLeft[3][3];
	float directionRight[3][3];
} RESPONSE_DIVA;

typedef struct {
	REQUEST_TYPE type;
	float leftMotorSmallStrength;
	float leftMotorLargeStrength;
	float rightMotorSmallStrength;
	float rightMotorLargeStrength;
} S_REQUEST_RUMBLE;

toml::table config;
const char* ip = "";

static int* sensitivity = (int*)0x1412B639C;
static bool* rumble = (bool*)0x1412B63A8;

WNDPROC oWndProc;

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

typedef enum GameButton : uint32_t {
	DIVA_BUTTON_NONE		= 0x00000000,
	DIVA_BUTTON_UP			= 0x00000008,
	DIVA_BUTTON_DOWN		= 0x00000010,
	DIVA_BUTTON_LEFT		= 0x00000020,
	DIVA_BUTTON_RIGHT		= 0x00000040,
	DIVA_BUTTON_SQUARE		= 0x00000080,
	DIVA_BUTTON_TRIANGLE	= 0x00000100,
	DIVA_BUTTON_CIRCLE		= 0x00000200,
	DIVA_BUTTON_CROSS		= 0x00000400,
	DIVA_BUTTON_L1			= 0x00000800,
	DIVA_BUTTON_R1			= 0x00001000,
	DIVA_BUTTON_L2			= 0x00002000,
	DIVA_BUTTON_R2			= 0x00004000,
	DIVA_BUTTON_L3			= 0x00008000,
	DIVA_BUTTON_R3			= 0x00010000,
	DIVA_BUTTON_TOUCHPAD	= 0x00020000,
	DIVA_BUTTON_START		= 0x01000000,
	DIVA_BUTTON_SELECT		= 0x40000000
} GameButton;

typedef enum GameStick : uint32_t {
	STICK_NONE		= 0x00000000,
	STICK_L_UP		= 0x01000000,
	STICK_L_DOWN	= 0x02000000,
	STICK_L_LEFT	= 0x04000000,
	STICK_L_RIGHT	= 0x08000000,
	STICK_R_UP		= 0x10000000,
	STICK_R_DOWN	= 0x20000000,
	STICK_R_LEFT	= 0x40000000,
	STICK_R_RIGHT	= 0x80000000
} GameStick;

typedef enum MixModeCursor : uint32_t {
	CURSOR_LEFT,
	CURSOR_RIGHT,
	CURSOR_MAX
} MixModeCursor;

#ifdef USE_NETWORK
SOCKET sock;
bool socketOpen = false;
#else
libusb_context* ctx;
std::atomic<libusb_device_handle*> simm;
std::atomic<libusb_device_descriptor> simmDesc;
std::atomic<bool> usbConnected;
std::mutex device_mutex;
int simmInputEndpoint;
int simmOutputEndpoint;
#endif
//FUNCTION_PTR(void*, __fastcall, DivaGetInputState, 0x1402AC970, int32_t a1);  // still not sigscanning

std::thread* updateInputThread;
std::thread* checkSIMMThread;

std::atomic<float> joyconTiltL;
std::atomic<float> joyconTiltR;

std::atomic<bool> runSIMMCheckThread;
std::atomic<bool> runUpdateInputThread;

PVIGEM_CLIENT client;
PVIGEM_TARGET pad;

RESPONSE_DIVA getInput() {
	// setup our request
	unsigned char request[32] = { '\0' };
	unsigned char recvBuffer[256] = { '\0' };
	*(REQUEST_TYPE*)request = REQUEST_DIVA;
#ifdef USE_NETWORK
	if (send(sock, (char*)request, 32, 0) == SOCKET_ERROR) {
		printf("Failed to send request, %d\n", WSAGetLastError());
	}

	int bytesReceived = recv(sock, (char*)recvBuffer, 256, 0);
#else
	//libusb_handle_events(ctx);

	libusb_device_handle* simmTemp;

	{
		std::lock_guard<std::mutex> lock(device_mutex);
		simmTemp = simm.load();
	}

	if (simmTemp != nullptr) {
		int transferred;
		int r;

		r = libusb_bulk_transfer(simmTemp,
			0x01,
			request,
			sizeof(request),
			&transferred,
			1000);  // Timeout in milliseconds

		r = libusb_bulk_transfer(simmTemp, 0x81, recvBuffer, 256, &transferred, 1000);
	}
#endif

	return *(RESPONSE_DIVA*)(recvBuffer);
}

std::atomic<RESPONSE_DIVA> lastInput;

void checkSIMM() {
	libusb_device** device_list = nullptr;
	ssize_t count;

	while (runSIMMCheckThread.load()) {
		count = libusb_get_device_list(ctx, &device_list);
		if (count < 0) {
			std::cerr << "Error getting device list" << std::endl;
			break;
		}

		bool found = false;
		for (ssize_t i = 0; i < count; i++) {
			libusb_device* device = device_list[i];
			libusb_device_descriptor desc;

			if (libusb_get_device_descriptor(device, &desc) == 0) {
				if (desc.idVendor == 0x057E) {
					found = true;
					if (!usbConnected.load()) {
						libusb_device_handle* handle;
						if (libusb_open(device, &handle) == 0) {
							if (libusb_claim_interface(handle, 0) == 0) {
								std::cout << "Device connected and interface claimed" << std::endl;

								// Lock and update shared device handle
								{
									std::lock_guard<std::mutex> lock(device_mutex);
									simm.store(handle);
									usbConnected.store(true);
								}
							}
							else {
								std::cerr << "Failed to claim interface" << std::endl;
								libusb_close(handle);
							}
						}
						else {
							std::cerr << "Failed to open device" << std::endl;
						}
					}
					break;
				}
			}
		}

		if (!found && usbConnected.load()) {
			// Device was previously connected but is now not found
			std::lock_guard<std::mutex> lock(device_mutex);
			libusb_device_handle* handle = simm.exchange(nullptr);
			if (handle) {
				libusb_release_interface(handle, 0);
				libusb_close(handle);
			}
			usbConnected.store(false);
			std::cout << "Device disconnected and interface released" << std::endl;
		}

		libusb_free_device_list(device_list, 1);
		std::this_thread::sleep_for(std::chrono::seconds(2)); // Polling interval
	}
}

void updateInput() {

	XINPUT_STATE state;
	RESPONSE_DIVA input;

	while (runUpdateInputThread.load()) {

		// setup our request
		/*char request[64] = {'\0'};
		*(REQUEST_TYPE*)request = REQUEST_ANGLE;
		if (send(sock, request, 4, 0) == SOCKET_ERROR) {
			printf("Failed to send request, %d\n", WSAGetLastError());
		}

		char recvBuffer[64] = { '\0' };
		int bytesReceived = recv(sock, recvBuffer, 64, 0);

		joyconAngle.store(*(RESPONSE_ANGLE*)(recvBuffer));*/
		input = getInput();

		//XUSB_REPORT rp;

		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_A) > 0) ? XUSB_GAMEPAD_A : 0;
		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_B) > 0) ? XUSB_GAMEPAD_B : 0;
		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_X) > 0) ? XUSB_GAMEPAD_X : 0;
		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_Y) > 0) ? XUSB_GAMEPAD_Y : 0;

		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_Up) > 0) ? XUSB_GAMEPAD_DPAD_UP : 0;
		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_Down) > 0) ? XUSB_GAMEPAD_DPAD_DOWN : 0;
		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_Left) > 0) ? XUSB_GAMEPAD_DPAD_LEFT : 0;
		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_Right) > 0) ? XUSB_GAMEPAD_DPAD_RIGHT : 0;

		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_L) > 0) ? XUSB_GAMEPAD_LEFT_SHOULDER : 0;
		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_R) > 0) ? XUSB_GAMEPAD_RIGHT_SHOULDER : 0;

		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_Plus) > 0) ? XUSB_GAMEPAD_START : 0;
		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_Minus) > 0) ? XUSB_GAMEPAD_BACK : 0;

		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_StickL) > 0) ? XUSB_GAMEPAD_LEFT_THUMB : 0;
		state.Gamepad.wButtons |= ((input.heldButtons & HidNpadButton_StickR) > 0) ? XUSB_GAMEPAD_RIGHT_THUMB : 0;

		state.Gamepad.bLeftTrigger = ((input.heldButtons & HidNpadButton_ZL) > 0) ? 255 : 0;
		state.Gamepad.bRightTrigger = ((input.heldButtons & HidNpadButton_ZR) > 0) ? 255 : 0;

		printf("%d\r", input.stickLX);

		state.Gamepad.sThumbLX = input.stickLX;
		state.Gamepad.sThumbLY = input.stickLY;
		state.Gamepad.sThumbRX = input.stickRX;
		state.Gamepad.sThumbRY = input.stickRY;

		joyconTiltL.store(input.directionLeft[2][2]);
		joyconTiltR.store(input.directionRight[2][2]);

		//printf("%llx\n", input.heldButtons);

		vigem_target_x360_update(client, pad, *reinterpret_cast<XUSB_REPORT*>(&state.Gamepad));

		state.Gamepad.wButtons = 0;
		state.Gamepad.sThumbLX = 0;
		state.Gamepad.sThumbLY = 0;
		state.Gamepad.sThumbRX = 0;
		state.Gamepad.sThumbRY = 0;
		state.Gamepad.bLeftTrigger = 0;
		state.Gamepad.bRightTrigger = 0;

		lastInput.store(input);

		//std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}
}

int currentCursor = CURSOR_LEFT;

HOOK(void, __fastcall, UpdateMixModeInput, 0x14063E810, uint32_t a1) {
	originalUpdateMixModeInput(a1);
	void* mixData = reinterpret_cast<void*>((uintptr_t)a1);

	if (currentCursor == CURSOR_LEFT) {
		float joyconTilt = joyconTiltL.load();
		float cursor = max(-1.f, min(1.f, ((joyconTilt * 0.333334) + (0.666667 * joyconTilt * joyconTilt * joyconTilt) * (1.55f + (0.1f * *sensitivity)))));
		cursor = ((cursor + 1.f) / 2.f);


		*(float_t*)((char*)mixData + 0x28) = 1.f - max(0.075f, min(0.95f, cursor));

		// ensure it doesn't exceed 1.0f
		//*(float_t*)((char*)mixData + 0x28) = max(0.0f, min(1.0f, *(float_t*)((char*)mixData + 0x28)));
	}
	
	if (currentCursor == CURSOR_RIGHT) {
		float joyconTilt = joyconTiltR.load();
		float cursor = max(-1.f, min(1.f, ((joyconTilt * 0.333334) + (0.666667 * joyconTilt * joyconTilt * joyconTilt) * (1.55f + (0.1f * *sensitivity)))));
		cursor = ((cursor + 1.f) / 2.f);

		// and this should hopefully take us to the right range.

		*(float_t*)((char*)mixData + 0x28) = max(0.075f, min(0.95f, cursor));

		// ensure it doesn't exceed 1.0f
		//*(float_t*)((char*)mixData + 0x28) = max(0.0f, min(1.0f, *(float_t*)((char*)mixData + 0x28)));
	}
	//printf("%s Cursor Position is %1.6f\n", currentCursor == CURSOR_LEFT? "Left" : "Right", * (float_t*)((char*)mixData + 0x28));
	// swap active cursor
	currentCursor = !currentCursor;
}

int32_t prevCombo = 0;

HOOK(int32_t, __fastcall, UpdateMixModeCombo, 0x140636120, void* a1, void* a2) {
	int32_t val = originalUpdateMixModeCombo(a1, a2);

	/*if (rumble) {
		if (*(int32_t*)((char*)a1 + 40) > prevCombo) {
			uint64_t btn = lastInput.load().heldButtons;
			bool leftHitNote = (btn & HidNpadButton_L || btn & HidNpadButton_ZL) > 0;
			bool rightHitNote = (btn & HidNpadButton_R || btn & HidNpadButton_ZR) > 0;
			unsigned char request[32];

			((S_REQUEST_RUMBLE*)request)->type = REQUEST_RUMBLE;

			if (leftHitNote) {
				((S_REQUEST_RUMBLE*)request)->leftMotorSmallStrength = 1.0f;
			}
			if (rightHitNote) {
				((S_REQUEST_RUMBLE*)request)->rightMotorSmallStrength = 1.0f;
			}

			int transferred;
			int r;

			r = libusb_bulk_transfer(simm,
				0x01,
				request,
				sizeof(request),
				&transferred,
				1000);  // Timeout in milliseconds
		}
	}*/
	return val;
	
}

void cleanup() {
#ifdef USE_NETWORK
	closesocket(sock);
	WSACleanup();
#else
	runSIMMCheckThread.store(false);
	checkSIMMThread->join();
	if (simm != nullptr) {
		libusb_release_interface(simm, 0);
	}
	libusb_exit(ctx);
#endif
	runUpdateInputThread.store(false);
	updateInputThread->join();
	vigem_target_remove(client, pad);
	vigem_target_free(pad);
}

extern "C" __declspec(dllexport) void Init()
{
	atexit(cleanup);
	config = toml::parse_file("config.toml");
#ifdef USE_NETWORK
	ip = config["switch_ip"].value_or("127.0.0.1");

	int res;
	WSADATA wsaData;
	if (WSAStartup(0x0202, &wsaData) != 0) {
		printf("Failed to start WSA\n");
		//return 1;
	}

	printf("Connecting to switch");

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (sock == INVALID_SOCKET) {
		printf("Socket creation failed. Error: %d\n", WSAGetLastError());
		WSACleanup();
		//return 1;
	}

	char yes = 0;
	setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(char));  // disable keepalive

	// Set up server address
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(8080);
	inet_pton(AF_INET, ip, &serverAddr.sin_addr);

	// Connect to server
	if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		printf("Connection failed. Error: %d\n", WSAGetLastError());
		closesocket(sock);
		WSACleanup();
		//return 1;
	}
#else
	// init libusb
	int r = libusb_init(&ctx);
	if (r < 0) {
		printf("Error initializing libusb: %s\n", libusb_error_name(r));
	}

	runSIMMCheckThread.store(true);
	checkSIMMThread = new std::thread(checkSIMM);
#endif

	client = vigem_alloc();

	if (client == nullptr)
	{
		printf("Failed to create client.\n");
	}
	
	const auto retval = vigem_connect(client);

	if (!VIGEM_SUCCESS(retval))
	{
		printf("ViGEm Bus connection failed with error code: 0x%x\n", retval);
	}

	pad = vigem_target_x360_alloc();
	const auto pir = vigem_target_add(client, pad);
	if (!VIGEM_SUCCESS(pir))
	{
		printf("Target plugin failed with error code: 0x%x", pir);
	}

#ifdef USE_NETWORK
	socketOpen = true;
#endif

	runUpdateInputThread.store(true);

	updateInputThread = new std::thread(updateInput);

	//INSTALL_HOOK(DivaGetInputState);
	INSTALL_HOOK(UpdateMixModeInput);
	INSTALL_HOOK(UpdateMixModeCombo);
}


BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	return TRUE;
}

