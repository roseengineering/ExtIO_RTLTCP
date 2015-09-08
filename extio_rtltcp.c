
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

#define LIBAPI __declspec(dllexport) __stdcall

#define SET_CENTER_FREQ 1
#define SET_SAMPLE_RATE 2
#define SET_TUNER_GAIN_MODE 3  // 0 or 1
#define SET_TUNER_GAIN 4  // gain = param / 10.0
#define SET_FREQ_CORRECTION 5
#define SET_DIRECT_SAMPLING 9 // 0 or 1

#define STATUS_SAMPLE_RATE 100
    
#define TITLE "ExtIO_RTLTCP"
#define BUF_SIZE 1024
#define SAMPLE_PAIRS (4 * 1024)

#define HOSTNAME   "localhost"
#define PORT       "1234"
#define SAMPLERATE "1800000"

static void (* callback)(int, int, float, void *);
static SOCKET sock;
static HANDLE thread;
static HMODULE hinst;

static char hostname[BUF_SIZE];
static char port[BUF_SIZE];
static char samplerate[BUF_SIZE];
static char gain[BUF_SIZE];
static char correction[BUF_SIZE];
static char directsampling[BUF_SIZE];

DWORD WINAPI consumer(LPVOID lpParam)
{
    typedef int16_t iq_t[2];
    typedef uint8_t raw_t[2]; 

    int ret, i, k, n;
    static iq_t iq[SAMPLE_PAIRS];
    static raw_t raw[SAMPLE_PAIRS];
    
    i = 0;
    n = SAMPLE_PAIRS * sizeof(raw_t);
    while (sock != INVALID_SOCKET) {
        ret = recv(sock, (char *) &raw[i], n - i, 0);
        if (ret == 0 || ret == SOCKET_ERROR) break;
        i += ret;
        if (n - i == 0) {
            i = 0;
            for (k = 0; k < SAMPLE_PAIRS; k++) {
                iq[k][0] = (raw[k][0] - 127) << 7;
                iq[k][1] = (raw[k][1] - 127) << 7;
            }
            callback(SAMPLE_PAIRS, 0, 0, iq);
        }        
    }
    return 0;
}

bool issue_command(int cmd, int param)
{
    #pragma pack(push, 1)
    struct {
        unsigned char cmd;
        unsigned int param;
    } command;
    #pragma pack(pop)
    
    command.cmd = cmd;
    command.param = htonl(param);
    int ret = send(sock, (char *) &command, sizeof(command), 0);
    return ret == sizeof(command);
}

void trim(char *dest, char *s) {
    int l = strlen(s);
    while(isspace(s[l - 1])) --l;
    while(*s && isspace(*s)) ++s, --l;
    strncpy(dest, s, l);
    dest[l] = 0;
}

static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char buf[BUF_SIZE];

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
                GetDlgItemText(hDlg, IDC_GAIN, buf, BUF_SIZE);
                trim(gain, buf);
                GetDlgItemText(hDlg, IDC_CORRECTION, buf, BUF_SIZE);
                trim(correction, buf);
                GetDlgItemText(hDlg, IDC_DIRECT_SAMPLING, buf, BUF_SIZE);
                trim(directsampling, buf);
                EndDialog(hDlg, wParam); 
                return TRUE;
            }
            break;

        case WM_INITDIALOG:
            SetDlgItemText(hDlg, IDC_HOSTNAME, hostname);
            SetDlgItemText(hDlg, IDC_PORT, port);
            SetDlgItemText(hDlg, IDC_SAMPLE_RATE, samplerate);
            SetDlgItemText(hDlg, IDC_GAIN, gain);
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

//////////////////////////////////////

bool LIBAPI InitHW(char *name, char *model, int *type)
{
    strncpy(hostname, HOSTNAME, BUF_SIZE);
    strncpy(port, PORT, BUF_SIZE);
    strncpy(samplerate, SAMPLERATE, BUF_SIZE);
    
    *type = 3; // 16 bit, little endian
    strcpy(name, TITLE);
    strcpy(model, "");
    return true; // success
}

bool LIBAPI OpenHW(void)
{
    WSADATA wsd;
    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0) return false;
    thread = NULL;
    return true;
}

void LIBAPI CloseHW(void)
{
    if (thread != NULL) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    WSACleanup();
}

int LIBAPI StartHW(long freq)
{
    char buf[BUF_SIZE];
    SOCKADDR_IN server;
    struct hostent *host;

    if (thread != NULL) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }

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

    // create consumer thread
    thread = CreateThread(NULL, 0, consumer, 0, 0, 0);
    if (thread == NULL) return 0;

    // set sample rate
    issue_command(SET_SAMPLE_RATE, atoi(samplerate));
    callback(-1, STATUS_SAMPLE_RATE, 0, NULL);

    if (strlen(gain)) {
        int db = (int)(atof(gain) * 10);
        if (db == 0) {
            issue_command(SET_TUNER_GAIN_MODE, 0);
        } else {
            issue_command(SET_TUNER_GAIN_MODE, 1);
            issue_command(SET_TUNER_GAIN, db);
        }
    }

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
    closesocket(sock);
}

int LIBAPI SetHWLO(long freq)  // same freq as starthw
{
    if (thread != NULL) issue_command(SET_CENTER_FREQ, freq);    
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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        hinst=hModule;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

