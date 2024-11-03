#include <string.h>
#include <stdio.h>
#ifdef USE_NETWORK
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#include <sys/errno.h>
#include <unistd.h>
#include <cstdio>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <switch.h>
static char sendTest[] = "This is the captain of the pike.\nAre you receiving me all right?\n\0";

typedef enum REQUEST_TYPE : uint32_t {
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
    float angleLeft[3];
    float angleRight[3];
} RESPONSE_ANGLE;

typedef struct {
    REQUEST_TYPE type;
    float leftMotorSmallStrength;
    float leftMotorLargeStrength;
    float rightMotorSmallStrength;
    float rightMotorLargeStrength;
} S_REQUEST_RUMBLE;

int main() {
    consoleInit(NULL);

    // Configure our supported input layout: a single player with standard controller styles
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    // Initialize the default gamepad (which reads handheld mode inputs as well as the first connected controller)
    PadState pad;
    padInitializeDefault(&pad);

    // It's necessary to initialize these separately as they all have different handle values
    HidSixAxisSensorHandle handles[4];
    hidGetSixAxisSensorHandles(&handles[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
    hidGetSixAxisSensorHandles(&handles[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
    hidGetSixAxisSensorHandles(&handles[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
    hidStartSixAxisSensor(handles[0]);
    hidStartSixAxisSensor(handles[1]);
    hidStartSixAxisSensor(handles[2]);
    hidStartSixAxisSensor(handles[3]);

#ifdef USE_NETWORK

    socketInitializeDefault();

    // Create socket
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        printf("Socket creation failed.\n");
        consoleUpdate(NULL);
        return 1;
    }

    int yes = 0;
    setsockopt(serverSock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int));

    // Set up server address
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);  // Server will listen on port 8080
    serverAddr.sin_addr.s_addr = INADDR_ANY;  // Bind to any address

    // Bind the socket
    if (bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        printf("Failed to bind to Socket.\n");
        consoleUpdate(NULL);
        close(serverSock);
        return 1;
    }

    // Start listening
    if (listen(serverSock, 5) < 0) {
        printf("Failed to listen on Socket.\n");
        consoleUpdate(NULL);
        close(serverSock);
        return 1;
    }
    printf("Listening on port 8080...\n");
    consoleUpdate(NULL);

    // Accept connection
    sockaddr_in clientAddr;
    socklen_t clientSize = sizeof(clientAddr);
    int clientSock = accept(serverSock, (sockaddr*)&clientAddr, &clientSize);
    if (clientSock < 0) {
        printf("Failed to bind to accept connection.\n");
        consoleUpdate(NULL);
        close(serverSock);
        return 1;
    }
    printf("Client Connected. Welcome to SIMM-Server.\n");
    consoleUpdate(NULL);
#else

    usbCommsInitialize();

#endif

    bool quitSignal = false;

    // Main loop
    while (appletMainLoop())
    {
        // Scan the gamepad. This should be done once for each frame

        padUpdate(&pad);

        // padGetButtonsDown returns the set of buttons that have been newly pressed in this frame compared to the previous one
        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);
        u64 kUp = padGetButtonsUp(&pad);
        
        HidAnalogStickState lStick = padGetStickPos(&pad, 0);
        HidAnalogStickState rStick = padGetStickPos(&pad, 1);

#ifdef USE_NETWORK
        char recvBuffer[32] = { '\0' };
        int bytesReceived = recv(clientSock, recvBuffer, 32, 0);
        if (bytesReceived <= 0) {
            printf("ERROR: NO BYTES RECEIVED.\n");
            consoleUpdate(NULL);
            break; // Exit the loop if the client disconnects or an error occurs
        }
        else {
            REQUEST_TYPE req = *(REQUEST_TYPE*)recvBuffer;
            char buffer[256] = { '\0' };
            HidSixAxisSensorState sixaxisL = { 0 };
            HidSixAxisSensorState sixaxisR = { 0 };
            u64 style_set = padGetStyleSet(&pad);
            u64 attrib = padGetAttributes(&pad);
            switch (req) {
                case REQUEST_NONE:
                    send(clientSock, buffer, 32, 0);
                    printf("Sent %d to Client.\n", req);
                    consoleUpdate(NULL);
                    break;
                case REQUEST_BUTTONS:
                    ((RESPONSE_BUTTONS*)(buffer))->type = req;
                    ((RESPONSE_BUTTONS*)(buffer))->downButtons = kDown;
                    ((RESPONSE_BUTTONS*)(buffer))->heldButtons = kHeld;
                    ((RESPONSE_BUTTONS*)(buffer))->upButtons = kUp;
                    send(clientSock, buffer, 64, 0);
                    printf("Sent %d, %lx to Client.\n", req, kHeld);
                    consoleUpdate(NULL);
                    break;
                case REQUEST_ANGLE:
                    if (style_set & HidNpadStyleTag_NpadJoyDual) {
                        // For JoyDual, read from either the Left or Right Joy-Con depending on which is/are connected
                        if (attrib & HidNpadAttribute_IsLeftConnected)
                            hidGetSixAxisSensorStates(handles[2], &sixaxisL, 1);
                        if (attrib & HidNpadAttribute_IsRightConnected)
                            hidGetSixAxisSensorStates(handles[3], &sixaxisR, 1);

                        *(HidVector*)((char*)buffer + 4) = sixaxisL.angle;
                        *(HidVector*)((char*)buffer + 4 + sizeof(HidVector)) = sixaxisR.angle;
                    }
                    ((RESPONSE_ANGLE*)(buffer))->type = req;
                    memcpy(((RESPONSE_ANGLE*)(buffer))->angleLeft, &sixaxisL.angle, 12);
                    memcpy(((RESPONSE_ANGLE*)(buffer))->angleRight, &sixaxisR.angle, 12);
                    send(clientSock, buffer, 64, 0);
                    printf("Left sent %d, %1.6f %1.6f %1.6f to Client.\n", req, sixaxisL.angle.x, sixaxisL.angle.y, sixaxisL.angle.z);
                    printf("Right sent % d, % 1.6f % 1.6f % 1.6f to Client.\n", req, sixaxisR.angle.x, sixaxisR.angle.y, sixaxisR.angle.z);
                    break;
                case REQUEST_DIVA:
                    if (style_set & HidNpadStyleTag_NpadJoyDual) {
                        // For JoyDual, read from either the Left or Right Joy-Con depending on which is/are connected
                        if (attrib & HidNpadAttribute_IsLeftConnected)
                            hidGetSixAxisSensorStates(handles[2], &sixaxisL, 1);
                        if (attrib & HidNpadAttribute_IsRightConnected)
                            hidGetSixAxisSensorStates(handles[3], &sixaxisR, 1);
                        //((RESPONSE_DIVA*)(buffer))->pitchLeft = sixaxisL.angle.x;

                        memcpy(((RESPONSE_DIVA*)(buffer))->directionLeft, sixaxisL.direction.direction, sizeof(float) * 3 * 3);
                        memcpy(((RESPONSE_DIVA*)(buffer))->directionRight, sixaxisR.direction.direction, sizeof(float) * 3 * 3);
                        //((RESPONSE_DIVA*)(buffer))->pitchRight = sixaxisR.angle.x;
                    }
                    ((RESPONSE_DIVA*)(buffer))->type = req;
                    ((RESPONSE_DIVA*)(buffer))->heldButtons = kHeld;
                    ((RESPONSE_DIVA*)(buffer))->stickLX = state.sticks[0].x;
                    ((RESPONSE_DIVA*)(buffer))->stickLY = state.sticks[0].y;
                    ((RESPONSE_DIVA*)(buffer))->stickRX = state.sticks[1].x;
                    ((RESPONSE_DIVA*)(buffer))->stickRY = state.sticks[1].y;

                    //printf("Angle states = %1.6f;%1.6f.\r", ((RESPONSE_DIVA*)(buffer))->pitchLeft, ((RESPONSE_DIVA*)(buffer))->pitchRight);

                    send(clientSock, buffer, 256, 0);
                    break;
                case REQUEST_INVALID:
                    printf("Received invalid request %d\n", req);
                    consoleUpdate(NULL);
                    break;
                default:
                    printf("Received invalid request %d\n", req);
                    consoleUpdate(NULL);
                    break;
            }
        }
#else
        char recvBuffer[32] = { '\0' };
        //int bytesReceived = recv(clientSock, recvBuffer, 4, 0);
        int bytesReceived = usbCommsRead(recvBuffer, 32);
        if (bytesReceived <= 0) {
            printf("ERROR: NO BYTES RECEIVED.\n");
            consoleUpdate(NULL);
            break; // Exit the loop if the client disconnects or an error occurs
        }
        else {
            REQUEST_TYPE req = *(REQUEST_TYPE*)recvBuffer;
            char buffer[256] = { '\0' };
            HidSixAxisSensorState sixaxisL = { 0 };
            HidSixAxisSensorState sixaxisR = { 0 };
            u64 style_set = padGetStyleSet(&pad);
            u64 attrib = padGetAttributes(&pad);
            switch (req) {
            case REQUEST_NONE:
                usbCommsWrite(buffer, 256);
                printf("Sent %d to Client.\n", req);
                consoleUpdate(NULL);
                break;
            case REQUEST_BUTTONS:
                ((RESPONSE_BUTTONS*)(buffer))->type = req;
                ((RESPONSE_BUTTONS*)(buffer))->downButtons = kDown;
                ((RESPONSE_BUTTONS*)(buffer))->heldButtons = kHeld;
                ((RESPONSE_BUTTONS*)(buffer))->upButtons = kUp;
                usbCommsWrite(buffer, 256);
                printf("Sent %d, %lx to Client.\n", req, kHeld);
                consoleUpdate(NULL);
                break;
            case REQUEST_ANGLE:
                if (style_set & HidNpadStyleTag_NpadJoyDual) {
                    // For JoyDual, read from either the Left or Right Joy-Con depending on which is/are connected
                    if (attrib & HidNpadAttribute_IsLeftConnected)
                        hidGetSixAxisSensorStates(handles[2], &sixaxisL, 1);
                    if (attrib & HidNpadAttribute_IsRightConnected)
                        hidGetSixAxisSensorStates(handles[3], &sixaxisR, 1);

                    *(HidVector*)((char*)buffer + 4) = sixaxisL.angle;
                    *(HidVector*)((char*)buffer + 4 + sizeof(HidVector)) = sixaxisR.angle;
                }
                ((RESPONSE_ANGLE*)(buffer))->type = req;
                memcpy(((RESPONSE_ANGLE*)(buffer))->angleLeft, &sixaxisL.angle, 12);
                memcpy(((RESPONSE_ANGLE*)(buffer))->angleRight, &sixaxisR.angle, 12);
                usbCommsWrite(buffer, 256);
                printf("Left sent %d, %1.6f %1.6f %1.6f to Client.\n", req, sixaxisL.angle.x, sixaxisL.angle.y, sixaxisL.angle.z);
                printf("Right sent % d, % 1.6f % 1.6f % 1.6f to Client.\n", req, sixaxisR.angle.x, sixaxisR.angle.y, sixaxisR.angle.z);
                break;
            case REQUEST_DIVA:
                if (style_set & HidNpadStyleTag_NpadJoyDual) {
                    // For JoyDual, read from either the Left or Right Joy-Con depending on which is/are connected
                    if (attrib & HidNpadAttribute_IsLeftConnected)
                        hidGetSixAxisSensorStates(handles[2], &sixaxisL, 1);
                    if (attrib & HidNpadAttribute_IsRightConnected)
                        hidGetSixAxisSensorStates(handles[3], &sixaxisR, 1);
                    //((RESPONSE_DIVA*)(buffer))->pitchLeft = sixaxisL.angle.x;

                    memcpy(((RESPONSE_DIVA*)(buffer))->directionLeft, sixaxisL.direction.direction, sizeof(float) * 3 * 3);
                    memcpy(((RESPONSE_DIVA*)(buffer))->directionRight, sixaxisR.direction.direction, sizeof(float) * 3 * 3);
                    //((RESPONSE_DIVA*)(buffer))->pitchRight = sixaxisR.angle.x;
                }
                ((RESPONSE_DIVA*)(buffer))->type = req;
                ((RESPONSE_DIVA*)(buffer))->heldButtons = kHeld;
                ((RESPONSE_DIVA*)(buffer))->stickLX = lStick.x;
                ((RESPONSE_DIVA*)(buffer))->stickLY = lStick.y;
                ((RESPONSE_DIVA*)(buffer))->stickRX = rStick.x;
                ((RESPONSE_DIVA*)(buffer))->stickRY = rStick.y;

                //printf("Angle states = %1.6f;%1.6f.\r", ((RESPONSE_DIVA*)(buffer))->pitchLeft, ((RESPONSE_DIVA*)(buffer))->pitchRight);

                usbCommsWrite(buffer, 256);
                break;
            case REQUEST_RUMBLE:
                break;
            case REQUEST_INVALID:
                printf("Received invalid request %d\n", req);
                consoleUpdate(NULL);
                break;
            default:
                printf("Received invalid request %d\n", req);
                consoleUpdate(NULL);
                break;
            }
        }
#endif


        if (kHeld & HidNpadButton_Minus && kHeld & HidNpadButton_L && kHeld & HidNpadButton_R) break; // break in order to return to hbmenu

        if (quitSignal) {
            break;
        }

        consoleUpdate(NULL);
    }
#ifdef USE_NETWORK

    char buf[4] = { (char)REQUEST_QUIT, 0, 0, 0 };
    send(clientSock, buf, 4, 0);

    socketExit();
#else
    /*usbDsExit();

    endpointIn = nullptr;
    endpointOut = nullptr;
    interface = nullptr;

    initialized = false;*/

    usbHsExit();
#endif
    consoleExit(NULL);
    return 0;
}