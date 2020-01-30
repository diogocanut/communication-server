#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <tchar.h>
#include <strsafe.h>
#include "binn.h"

#pragma comment (lib, "Ws2_32.lib")

#define DEFAULT_BUFLEN 1024
#define RECEIVE_COMMANDS_FROM_CONTROL_PORT "27013"
#define SEND_COMMANDS_SERVER_PORT "27016"

// Estrutura do comando
struct Command {
	int ack;
	int type;
	int command;
};

struct Command command;

WSADATA wsaData;

DWORD receive_commands_thread_id;
DWORD send_commands_server_thread_id;

SOCKET ExperimentClientSocket = INVALID_SOCKET;

char ack_recvbuf[DEFAULT_BUFLEN];

DWORD WINAPI receive_commands_thread(LPVOID lpParam);
DWORD WINAPI send_commands_server_thread(LPVOID lpParam);

char* args;

int main(int argc, char** argv)
{
	int wsa;

	// Initialize Winsock
	wsa = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (wsa != 0) {
		printf("WSAStartup failed with error: %d\n", wsa);
		return 1;
	}
	/*
	if (argc != 2) {
		printf("usage: %s experiment-server-name\n", argv[0]);
		return 1;
	}

	args = argv[1];
	*/
	HANDLE hndThread1;
	HANDLE hndThread2;
	hndThread1 = CreateThread(NULL, 0, receive_commands_thread, NULL, 0, &receive_commands_thread_id);

	if (NULL != hndThread1)
		WaitForSingleObject(hndThread1, INFINITE);
	CloseHandle(hndThread1);

	return 0;
}

DWORD WINAPI receive_commands_thread(LPVOID lpParam) {
	int iResult;

	SOCKET ConnectSocket = INVALID_SOCKET;

	struct addrinfo* result = NULL,
		* ptr = NULL,
		hints;

	int iSendResult;

	int recvbuflen = DEFAULT_BUFLEN;
	char sendbuf[DEFAULT_BUFLEN];
	char recvbuf[DEFAULT_BUFLEN];

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	binn* obj;
	obj = binn_object();

	// Resolve the server address and port
	iResult = getaddrinfo("localhost", RECEIVE_COMMANDS_FROM_CONTROL_PORT, &hints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	// Attempt to connect to an address until one succeeds
	for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {

		// Create a SOCKET for connecting to server
		ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype,
			ptr->ai_protocol);
		if (ConnectSocket == INVALID_SOCKET) {
			printf("socket failed with error: %ld\n", WSAGetLastError());
			WSACleanup();
			return 1;
		}

		// Connect to server.
		iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			closesocket(ConnectSocket);
			ConnectSocket = INVALID_SOCKET;
			continue;
		}
		break;
	}

	freeaddrinfo(result);

	if (ConnectSocket == INVALID_SOCKET) {
		printf("Unable to connect to server!\n");
		WSACleanup();
		return 1;
	}

		iResult = recv(ConnectSocket, (char *)binn_ptr(obj), DEFAULT_BUFLEN, 0);
		if (iResult > 0) {
			printf("Bytes received: %d\n", iResult);
			command.ack = binn_object_int32(obj, (char*)"ack");
			command.type = binn_object_int32(obj, (char*)"type");
			command.command = binn_object_int32(obj, (char*)"command");
			printf("Experiment server received command: %d\n", command.ack);
		}
		else if (iResult == 0)
			printf("Connection closed\n");
		else
			printf("recv failed with error: %d\n", WSAGetLastError());

		iSendResult = send(ConnectSocket, (const char*)binn_ptr(obj), binn_size(obj), 0);
		printf("Bytes sent: %d\n", iSendResult);

		if (iSendResult == SOCKET_ERROR) {
			printf("send failed with error: %d\n", WSAGetLastError());
			closesocket(ConnectSocket);
			WSACleanup();
			return 1;
		}

	
	iResult = shutdown(ConnectSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed with error: %d\n", WSAGetLastError());
		closesocket(ConnectSocket);
		WSACleanup();
		return 1;
	}

	closesocket(ConnectSocket);
	WSACleanup();

	return 0;
}