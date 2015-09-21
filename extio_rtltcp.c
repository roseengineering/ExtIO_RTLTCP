// Copyright 2015 George Magiros
// Use subject to the terms of the MIT License
// version 1.0.0

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "resource.h"

#define HOSTENT struct hostent

// rtl_tcp constants

#define SET_CENTER_FREQ      1
#define SET_SAMPLE_RATE      2
#define SET_GAIN_MODE        3  // 0 (auto) or 1 (manual)
#define SET_GAIN             4  // db * 10
#define SET_FREQ_CORRECTION  5  // khz
#define SET_DIRECT_SAMPLING  9  // 0, 1(I), 2(Q)
#define SET_GAIN_INDEX       13 // 0 ...

// ExtIO constants

#define LIBAPI __declspec(dllexport) __stdcall
#define STATUS_SAMPLE_RATE 100

// constants

#define SAMPLE_PAIRS (4 * 1024)
#define BUF_SIZE 1024
#define TITLE "ExtIO_RTLTCP"

#define HOSTNAME   "localhost"
#define PORT       "1234"
#define SAMPLERATE "2400000"

// types

typedef char buf_t[BUF_SIZE];

// variables

static void (* callback)(int, int, float, void *);
static HMODULE hinst;

static volatile bool active;
static SOCKET sock;
static HANDLE thread;

static buf_t hostname;
static buf_t port;
static buf_t samplerate;
static buf_t correction;
static buf_t directsampling;

static uint8_t raw[2 * SAMPLE_PAIRS];
static int16_t iq[2 * SAMPLE_PAIRS];

static DWORD WINAPI consumer(LPVOID lpParam)
{
    char *data = (char *) raw;
    int index, bytesleft, n;

    while (active) {
        index = 0;
        bytesleft = sizeof(raw);
        while(bytesleft > 0) {
            n = recv(sock, &data[index], bytesleft, 0);
            if (n <= 0) return 0;
            bytesleft -= n;
            index += n;
        }
        for (n = 0; n < 2 * SAMPLE_PAIRS; n++) {
            iq[n] = (raw[n] - 127) << 7;
        }
        callback(SAMPLE_PAIRS, 0, 0, iq);
    }
    return 0;
}

static void issue_command(int cmd, int param)
{
    #pragma pack(push, 1)
    struct {
        unsigned char cmd;
        unsigned int param;
    } command;
    #pragma pack(pop)
    
    if (active) {
        command.cmd = cmd;
        command.param = htonl(param);
        send(sock, (char *) &command, sizeof(command), 0);
    }
}

static void trim(char *dest, char *s) {
    int l = strlen(s);
    while(l && isspace(s[l - 1])) l--;
    while(l && *s && isspace(*s)) s++, l--;
    strncpy(dest, s, l);
    dest[l] = 0;
}

static void validate(void)
{
    if (strlen(hostname) == 0) strncpy(hostname, HOSTNAME, BUF_SIZE);
    if (atoi(port) == 0) strncpy(port, PORT, BUF_SIZE);
    if (atoi(samplerate) == 0) strncpy(samplerate, SAMPLERATE, BUF_SIZE);
}

static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    buf_t buf;

    switch(uMsg)
    {
        case WM_COMMAND:
            switch(LOWORD(wParam))
            {
            case IDCANCEL:
                EndDialog(hDlg, wParam); 
                return TRUE;

            case IDOK:
                GetDlgItemText(hDlg, IDC_HOSTNAME, buf, BUF_SIZE);
                trim(hostname, buf);
                GetDlgItemText(hDlg, IDC_PORT, buf, BUF_SIZE);
                trim(port, buf);
                GetDlgItemText(hDlg, IDC_SAMPLE_RATE, buf, BUF_SIZE);
                trim(samplerate, buf);
                GetDlgItemText(hDlg, IDC_CORRECTION, buf, BUF_SIZE);
                trim(correction, buf);
                GetDlgItemText(hDlg, IDC_DIRECT_SAMPLING, buf, BUF_SIZE);
                trim(directsampling, buf);
                validate();
                EndDialog(hDlg, wParam); 
                return TRUE;
            }
            break;

        case WM_INITDIALOG:
            SetDlgItemText(hDlg, IDC_HOSTNAME, hostname);
            SetDlgItemText(hDlg, IDC_PORT, port);
            SetDlgItemText(hDlg, IDC_SAMPLE_RATE, samplerate);
            SetDlgItemText(hDlg, IDC_CORRECTION, correction);
            SetDlgItemText(hDlg, IDC_DIRECT_SAMPLING, directsampling);
            return TRUE;
        
        case WM_CLOSE:
            DestroyWindow(hDlg);
            return TRUE;

        case WM_DESTROY:
            PostQuitMessage(0);
            return TRUE;
    }

    return FALSE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        hinst=hModule;
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}

//////////// DLL exports ///////////////

bool LIBAPI InitHW(char *name, char *model, int *type)
{
    validate();
    *type = 3; // 16 bit, little endian
    strcpy(name, TITLE);
    strcpy(model, "");
    return true; // success
}

bool LIBAPI OpenHW(void)
{
    WSADATA wsd;
    WSAStartup(MAKEWORD(2,2), &wsd);
    return true;
}

void LIBAPI CloseHW(void)
{
    WSACleanup();
}

int LIBAPI StartHW(long freq)
{
    buf_t buf;
    SOCKADDR_IN server;
    HOSTENT *host;

    // open socket
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return 0;

    // set hostname
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(port));
    server.sin_addr.s_addr = inet_addr(hostname);

    // resolve host
    if (server.sin_addr.s_addr == INADDR_NONE) {
        host = gethostbyname(hostname);
        if (host == NULL) {
            snprintf(buf, BUF_SIZE, "Cannot resolve '%s', winsock error: %d.", 
                     hostname, WSAGetLastError());
            MessageBox(NULL, buf, TITLE, 0);
            return 0;
        }
        CopyMemory(&server.sin_addr, host->h_addr_list[0], host->h_length);
    }

    // connect to hostname
    if (connect(sock, (SOCKADDR*) &server, sizeof(server)) == SOCKET_ERROR){
        snprintf(buf, BUF_SIZE, "Cannot connect to %s:%s, winsock error: %d.", 
                 hostname, port, WSAGetLastError());
        MessageBox(NULL, buf, TITLE, 0);
        return 0;
    }    

    // have an active connection now
    active = true;

    // create consumer thread
    // thread = CreateThread(NULL, 0, consumer, 0, 0, 0);
    thread = (HANDLE) _beginthread(consumer, 0, NULL );
    if (thread == NULL) return 0;

    // set sample rate
    issue_command(SET_SAMPLE_RATE, atoi(samplerate));
    callback(-1, STATUS_SAMPLE_RATE, 0, NULL);

    // set frequency correction
    if (strlen(correction)) {
        issue_command(SET_FREQ_CORRECTION, atoi(correction));
    }
    
    // set direct sampling mode
    if (strlen(directsampling)) {
        issue_command(SET_DIRECT_SAMPLING, atoi(directsampling));
    }

    // IQ sample pairs (divisible by 512)
    return SAMPLE_PAIRS;
}

void LIBAPI StopHW(void)
{
    active = false;
    closesocket(sock);
    if (thread != NULL) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        thread = NULL;
    }
}

int LIBAPI SetHWLO(long freq)  // same freq as starthw
{
    issue_command(SET_CENTER_FREQ, freq);
    return 0; // within the limits of the hardware
}

int LIBAPI GetStatus(void)
{
    return 0;
}

void LIBAPI SetCallback(void (* Callback)(int, int, float, void *))
{
    callback = Callback;
}

long LIBAPI GetHWSR(void)
{
    return atoi(samplerate);
}

void LIBAPI ShowGUI(void)
{
    HWND hDlg = CreateDialog(hinst, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DialogProc);
    ShowWindow(hDlg, SW_SHOW);
}

int LIBAPI GetAttenuators(int atten_idx, float *attenuation)
{
    if (atten_idx < 30) {
        *attenuation = atten_idx;
        return 0;
    }
    return 1;
}

int LIBAPI SetAttenuator(int atten_idx)
{
    if (atten_idx == 0) {
        issue_command(SET_GAIN_MODE, 0);
    } else {
        issue_command(SET_GAIN_MODE, 1);
        issue_command(SET_GAIN_INDEX, atten_idx - 1);
    }
    return 0;
}


