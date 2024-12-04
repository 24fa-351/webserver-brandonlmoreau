#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

#define DEFAULT_PORT 80

static size_t sentBytesTotal = 0;
CRITICAL_SECTION statsLock;
static int reqCounter = 0;
static size_t recvBytesTotal = 0;

const char* mimeTypeLookup(const char* filename);
void calcHandler(SOCKET clientSock, const char* path);
void statsHandler(SOCKET clientSock);
void staticHandler(SOCKET clientSock, const char* path);
DWORD WINAPI processClient(LPVOID arg);

const char* mimeTypeLookup(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot) return "application/octet-stream";
    if (_stricmp(dot, ".html") == 0) return "text/html";
    if (_stricmp(dot, ".htm") == 0) return "text/html";
    if (_stricmp(dot, ".jpg") == 0) return "image/jpeg";
    if (_stricmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (_stricmp(dot, ".png") == 0) return "image/png";
    if (_stricmp(dot, ".gif") == 0) return "image/gif";
    if (_stricmp(dot, ".css") == 0) return "text/css";
    if (_stricmp(dot, ".js") == 0) return "application/javascript";
    if (_stricmp(dot, ".txt") == 0) return "text/plain";
    return "application/octet-stream";
}

void calcHandler(SOCKET clientSock, const char* path) {
    char pathCopy[256];
    strncpy_s(pathCopy, sizeof(pathCopy), path, _TRUNCATE);
    char* query = strchr(pathCopy, '?');
    if (!query) {
        char* response = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(clientSock, response, (int)strlen(response), 0);
        return;
    }
    *query = '\0';
    query++;
    int aFound = 0, bFound = 0;
    int a = 0, b = 0;
    char* nextToken = NULL;
    char* token = strtok_s(query, "&", &nextToken);
    while (token) {
        if (strncmp(token, "a=", 2) == 0) {
            a = atoi(token + 2); aFound = 1;
        }
        else if (strncmp(token, "b=", 2) == 0) {
            b = atoi(token + 2); bFound = 1;
        }
        token = strtok_s(NULL, "&", &nextToken);
    }
    if (!aFound || !bFound) {
        char* response = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(clientSock, response, (int)strlen(response), 0);
        return;
    }
    int sum = a + b;
    char body[256];
    sprintf_s(body, sizeof(body), "<html><body><h1>Calculation Result</h1><p>%d + %d = %d</p></body></html>", a, b, sum);
    char header[256];
    sprintf_s(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n", strlen(body));
    send(clientSock, header, (int)strlen(header), 0);
    send(clientSock, body, (int)strlen(body), 0);
    EnterCriticalSection(&statsLock);
    sentBytesTotal += strlen(header) + strlen(body);
    LeaveCriticalSection(&statsLock);
}

void statsHandler(SOCKET clientSock) {
    EnterCriticalSection(&statsLock);
    int reqCount = reqCounter;
    size_t recvBytes = recvBytesTotal;
    size_t sentBytes = sentBytesTotal;
    LeaveCriticalSection(&statsLock);
    char body[512];
    sprintf_s(body, sizeof(body),
        "<html><body><h1>Server Stats</h1>"
        "<p>Requests: %d</p>"
        "<p>Received Bytes: %zu</p>"
        "<p>Sent Bytes: %zu</p>"
        "</body></html>",
        reqCount, recvBytes, sentBytes);
    char header[256];
    sprintf_s(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n",
        strlen(body));
    send(clientSock, header, (int)strlen(header), 0);
    send(clientSock, body, (int)strlen(body), 0);
    EnterCriticalSection(&statsLock);
    sentBytesTotal += strlen(header) + strlen(body);
    LeaveCriticalSection(&statsLock);
}

void staticHandler(SOCKET clientSock, const char* path) {
    const char* filePath = path + 7; // skip "/static"
    char fullPath[512] = ".\\static";
    strcat_s(fullPath, sizeof(fullPath), filePath);
    int fd = _open(fullPath, _O_RDONLY | _O_BINARY);
    if (fd < 0) {
        char* response = "HTTP/1.1 404 Not Found\r\n\r\n";
        send(clientSock, response, (int)strlen(response), 0);
        return;
    }
    struct _stat st;
    _fstat(fd, &st);
    size_t fileSize = st.st_size;
    const char* mimeType = mimeTypeLookup(fullPath);
    char header[256];
    sprintf_s(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n",
        mimeType, fileSize);
    send(clientSock, header, (int)strlen(header), 0);
    char fileBuffer[1024];
    int bytesRead;
    size_t totalSent = 0;
    while ((bytesRead = _read(fd, fileBuffer, sizeof(fileBuffer))) > 0) {
        send(clientSock, fileBuffer, bytesRead, 0);
        totalSent += bytesRead;
    }
    EnterCriticalSection(&statsLock);
    sentBytesTotal += strlen(header) + totalSent;
    LeaveCriticalSection(&statsLock);
    _close(fd);
}

DWORD WINAPI processClient(LPVOID arg) {
    SOCKET clientSock = *((SOCKET*)arg);
    free(arg);
    char buffer[4096];
    int received = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
    if (received == SOCKET_ERROR || received == 0) {
        printf("recv error %d\n", WSAGetLastError());
        closesocket(clientSock);
        return 0;
    }
    buffer[received] = '\0';
    EnterCriticalSection(&statsLock);
    reqCounter++;
    recvBytesTotal += received;
    LeaveCriticalSection(&statsLock);
    char method[16], path[256], version[16];
    sscanf_s(buffer, "%15s %255s %15s",
        method, (unsigned)_countof(method),
        path, (unsigned)_countof(path),
        version, (unsigned)_countof(version));
    if (strcmp(method, "GET") != 0) {
        char* response = "HTTP/1.1 405 Method Not Allowed\r\n\r\n";
        send(clientSock, response, (int)strlen(response), 0);
        closesocket(clientSock);
        return 0;
    }
    if (strcmp(version, "HTTP/1.1") != 0) {
        char* response = "HTTP/1.1 505 HTTP Version Not Supported\r\n\r\n";
        send(clientSock, response, (int)strlen(response), 0);
        closesocket(clientSock);
        return 0;
    }
    char pathOnly[256];
    strncpy_s(pathOnly, sizeof(pathOnly), path, _TRUNCATE);
    char* queryStart = strchr(pathOnly, '?');
    if (queryStart) {
        *queryStart = '\0';
    }
    if (strncmp(pathOnly, "/static/", 8) == 0) {
        staticHandler(clientSock, path);
    }
    else if (strcmp(pathOnly, "/stats") == 0) {
        statsHandler(clientSock);
    }
    else if (strcmp(pathOnly, "/calc") == 0) {
        calcHandler(clientSock, path);
    }
    else {
        char* response = "HTTP/1.1 404 Not Found\r\n\r\n";
        send(clientSock, response, (int)strlen(response), 0);
    }
    closesocket(clientSock);
    return 0;
}

int main(int argc, char* argv[]) {
    int port = DEFAULT_PORT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
    }
    WSADATA wsaData;
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaerr != 0) {
        printf("wsa startup error %d\n", wsaerr);
        return 1;
    }
    InitializeCriticalSection(&statsLock);
    DWORD ftyp = GetFileAttributesA(".\\static");
    if (ftyp == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryA(".\\static", NULL)) {
            printf("couldn't make 'static' folder, error %lu\n", GetLastError());
        }
        else {
            printf("'static' folder made.\n");
        }
    }
    else if (!(ftyp & FILE_ATTRIBUTE_DIRECTORY)) {
        printf("'static' exists but isn't a folder.\n");
        return 1;
    }
    SOCKET serverSock;
    struct sockaddr_in addr;
    int opt = 1;
    if ((serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET) {
        printf("socket creation error %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
        printf("setsockopt error %d\n", WSAGetLastError());
        closesocket(serverSock);
        WSACleanup();
        return 1;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(serverSock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("bind error %d\n", WSAGetLastError());
        closesocket(serverSock);
        WSACleanup();
        return 1;
    }
    if (listen(serverSock, SOMAXCONN) == SOCKET_ERROR) {
        printf("listen error %d\n", WSAGetLastError());
        closesocket(serverSock);
        WSACleanup();
        return 1;
    }
    printf("listening on port %d\n", port);
    while (1) {
        SOCKET clientSock;
        struct sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSock == INVALID_SOCKET) {
            printf("accept error %d\n", WSAGetLastError());
            continue;
        }
        DWORD threadId;
        SOCKET* pClient = malloc(sizeof(SOCKET));
        *pClient = clientSock;
        HANDLE threadHandle = CreateThread(NULL, 0, processClient, pClient, 0, &threadId);
        if (threadHandle == NULL) {
            printf("thread creation error %lu\n", GetLastError());
            free(pClient);
            closesocket(clientSock);
        }
        else {
            CloseHandle(threadHandle);
        }
    }
    DeleteCriticalSection(&statsLock);
    closesocket(serverSock);
    WSACleanup();
    return 0;
}
