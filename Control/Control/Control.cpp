#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <tchar.h>
#include <strsafe.h>
// Binn � a biblioteca usada para serializa��o dos dados
#include "binn.h"

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#define DEFAULT_BUFLEN 1024
/* 
	Portas para recebimento e envio de comandos e dados
 O m�dulo de controle consiste em 2 threads de comandos, uma para recebimento do cliente
 e outra para repasse dos comandos para o m�dulo de experimentos, e 2 threads de dados, recebendo 
 dados do m�dulo de experimento e repassando para o cliente
 */
#define RECEIVE_COMMANDS_PORT "27015"
#define SEND_COMMANDS_PORT "27013"
#define RECEIVE_DATA_PORT "27011"
#define SEND_DATA_PORT "27012"

// Estrutura do comando
struct Command {
	int ack;
	int type;
	int command;
};

struct Command command;

WSADATA wsaData;

// Id das respectivas threads
DWORD receive_commands_thread_id;
DWORD send_commands_server_thread_id;
DWORD receive_data_thread_id;
DWORD send_data_thread_id;

// Socket da thread de envio de comandos e dados
SOCKET SendCommandsClientSocket = INVALID_SOCKET;
SOCKET SendDataClientSocket = INVALID_SOCKET;

// Buffers de dados globais
char command_recvbuf[DEFAULT_BUFLEN];
char data_recvbuf[DEFAULT_BUFLEN];
// Declara��o de cada uma das threads
DWORD WINAPI receive_commands_thread(LPVOID lpParam);
DWORD WINAPI send_commands_server_thread(LPVOID lpParam);
DWORD WINAPI receive_data_thread(LPVOID lpParam);
DWORD WINAPI send_data_thread(LPVOID lpParam);

HANDLE commandsMutex;

int main()
{
	int wsa;

	wsa = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (wsa != 0) {
		printf("WSAStartup failed with error: %d\n", wsa);
		return 1;
	}

	commandsMutex = CreateMutex(
		NULL,
		FALSE,
		NULL);
	if (commandsMutex == NULL) {
		printf("CreateMutex error: %d\n", GetLastError());
		return 1;
	}


	HANDLE hndThread1;
	hndThread1 = CreateThread(NULL, 0, send_commands_server_thread, NULL, 0, &receive_commands_thread_id);

	if (NULL != hndThread1)
		WaitForSingleObject(hndThread1, INFINITE);
	CloseHandle(hndThread1);

	CloseHandle(commandsMutex);

	return 0;
}

DWORD WINAPI send_commands_server_thread(LPVOID lpParam) {
	int iResult;

	SOCKET ListenSocket = INVALID_SOCKET;

	struct addrinfo* result = NULL;
	struct addrinfo hints;

	int iSendResult;

	int recvbuflen = DEFAULT_BUFLEN;

	binn* obj;
	obj = binn_object();
	DWORD dwWaitResult;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	iResult = getaddrinfo(NULL, SEND_COMMANDS_PORT, &hints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
		printf("socket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}

	iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	freeaddrinfo(result);

	iResult = listen(ListenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}


	SendCommandsClientSocket = accept(ListenSocket, NULL, NULL);
	if (SendCommandsClientSocket == INVALID_SOCKET) {
		printf("accept failed with error: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}


	do {

		dwWaitResult = WaitForSingleObject(
			commandsMutex,
			INFINITE);

		switch (dwWaitResult)
		{
		case WAIT_OBJECT_0:
			__try {

				command.ack = 4;
				command.type = 2;
				command.command = 12345;
				binn_object_set_int32(obj, (char*)"ack", command.ack);
				binn_object_set_int32(obj, (char*)"type", command.type);
				binn_object_set_int32(obj, (char*)"command", command.command);

				iSendResult = send(SendCommandsClientSocket, (const char*)binn_ptr(obj), binn_size(obj), 0);
				printf("Bytes sent: %d\n", iSendResult);
			}
			__finally {
				if (iSendResult == SOCKET_ERROR) {
					printf("send failed with error: %d\n", WSAGetLastError());
					closesocket(SendCommandsClientSocket);
					WSACleanup();
					return 1;
				}

				if (!ReleaseMutex(commandsMutex))
				{
					printf("Error releasing mutex: %d\n", GetLastError());
					return 1;
				}

			}
			break;

		case WAIT_ABANDONED:
			return 1;
		}

	} while (iResult > 0);
	
	iResult = shutdown(SendCommandsClientSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed with error: %d\n", WSAGetLastError());
		closesocket(SendCommandsClientSocket);
		WSACleanup();
		return 1;
	}

	WSACleanup();
	closesocket(ListenSocket);
	binn_free(obj);

	return 0;
}
