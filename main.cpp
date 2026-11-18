#include <iostream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

constexpr int PORT = 8080;
constexpr size_t MAX_REQUEST_SIZE = 1024 * 1024;

// 1. HTTP Request Structure

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;

    std::unordered_map<std::string, std::string> headers;

    std::string body;
};

// 2. Utility Functions

std::string trim(const std::string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");

    if (start == std::string::npos) {
        return "";
    }

    size_t end = value.find_last_not_of(" \t\r\n");

    return value.substr(start, end - start + 1);
}

std::string toLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );

    return value;
}

// 3. Send All Bytes

bool sendAll(SOCKET client, const std::string& data) {
    size_t totalSent = 0;

    while (totalSent < data.size()) {
        int sent = send(
            client,
            data.data() + totalSent,
            static_cast<int>(data.size() - totalSent),
            0
        );

        if (sent == SOCKET_ERROR || sent == 0) {
            return false;
        }

        totalSent += sent;
    }

    return true;
}

// 4. HTTP Response Generator

std::string createResponse(
    int statusCode,
    const std::string& statusText,
    const std::string& body
) {
    std::ostringstream response;

    response << "HTTP/1.1 "
             << statusCode << " "
             << statusText << "\r\n";

    response << "Content-Type: text/plain; charset=utf-8\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;

    return response.str();
}

// 5. Receive Complete HTTP Request

bool receiveRequest(SOCKET client, HttpRequest& request) {

    std::string rawRequest;
    char buffer[4096];

    size_t headerEnd = std::string::npos;

    // Read until complete headers arrive.
    while (headerEnd == std::string::npos) {

        int received = recv(
            client,
            buffer,
            sizeof(buffer),
            0
        );

        if (received <= 0) {
            return false;
        }

        rawRequest.append(buffer, received);

        if (rawRequest.size() > MAX_REQUEST_SIZE) {
            return false;
        }

        headerEnd = rawRequest.find("\r\n\r\n");
    }

    // Separate headers and body.
    std::string headerSection =
        rawRequest.substr(0, headerEnd);

    std::string body =
        rawRequest.substr(headerEnd + 4);

    // Parse request line.
    std::istringstream headerStream(headerSection);

    std::string requestLine;

    if (!std::getline(headerStream, requestLine)) {
        return false;
    }

    requestLine = trim(requestLine);

    std::istringstream requestLineStream(requestLine);

    if (!(requestLineStream
        >> request.method
        >> request.path
        >> request.version)) {
        return false;
    }

    // Reject unsupported HTTP versions.
    if (request.version != "HTTP/1.1") {
        return false;
    }

    // Parse headers.
    std::string line;

    while (std::getline(headerStream, line)) {

        line = trim(line);

        if (line.empty()) {
            continue;
        }

        size_t colon = line.find(':');

        if (colon == std::string::npos) {
            return false;
        }

        std::string key =
            toLower(trim(line.substr(0, colon)));

        std::string value =
            trim(line.substr(colon + 1));

        if (key.empty()) {
            return false;
        }

        // Duplicate Content-Length is rejected.
        if (key == "content-length" &&
            request.headers.count(key)) {
            return false;
        }

        request.headers[key] = value;
    }

    // HTTP/1.1 requires Host.
    if (request.headers.find("host") == request.headers.end()) {
        return false;
    }

    // This starter does not implement chunked transfer encoding.
    if (request.headers.count("transfer-encoding")) {
        return false;
    }

    // Read Content-Length.
    size_t contentLength = 0;

    if (request.headers.count("content-length")) {
        const std::string& length =
            request.headers["content-length"];

        if (length.empty() ||
            !std::all_of(
                length.begin(),
                length.end(),
                [](unsigned char c) {
                    return std::isdigit(c);
                }
            )) {
            return false;
        }

        try {
            contentLength = std::stoull(length);
        }
        catch (...) {
            return false;
        }
    }

    if (contentLength > MAX_REQUEST_SIZE - headerEnd - 4) {
        return false;
    }

    // Read remaining body bytes.
    while (body.size() < contentLength) {

        int received = recv(
            client,
            buffer,
            sizeof(buffer),
            0
        );

        if (received <= 0) {
            return false;
        }

        body.append(buffer, received);
    }

    request.body = body.substr(0, contentLength);

    return true;
}

// 6. HTTP Router

std::string handleRequest(const HttpRequest& request) {

    if (request.method == "GET" && request.path == "/") {

        return createResponse(
            200,
            "OK",
            "Welcome to my C++ HTTP Server!"
        );
    }

    if (request.method == "GET" && request.path == "/hello") {

        return createResponse(
            200,
            "OK",
            "Hello Darshan! Server is working."
        );
    }

    if (request.method == "GET" && request.path == "/health") {

        return createResponse(
            200,
            "OK",
            "Server Status: UP"
        );
    }

    if (request.method == "POST" && request.path == "/users") {

        return createResponse(
            201,
            "Created",
            "Received user data:\n" + request.body
        );
    }

    if (request.method != "GET" &&
        request.method != "POST") {

        return createResponse(
            405,
            "Method Not Allowed",
            "HTTP method not supported."
        );
    }

    return createResponse(
        404,
        "Not Found",
        "Route not found."
    );
}

// 7. Handle Client

void handleClient(SOCKET client) {

    HttpRequest request;

    if (!receiveRequest(client, request)) {

        std::string response = createResponse(
            400,
            "Bad Request",
            "Invalid or incomplete HTTP request."
        );

        sendAll(client, response);
        return;
    }

    std::cout << "\nRequest received\n";
    std::cout << "Method: " << request.method << "\n";
    std::cout << "Path: " << request.path << "\n";
    std::cout << "Version: " << request.version << "\n";

    std::string response = handleRequest(request);

    sendAll(client, response);
}

// 8. Main Server

int main() {

    WSADATA wsaData;

    // Initialize Winsock.
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Winsock initialization failed\n";
        return 1;
    }

    // Create TCP socket.
    SOCKET serverSocket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );

    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }

    // Configure server address.
    sockaddr_in serverAddress{};

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(PORT);

    // Bind socket.
    if (bind(
        serverSocket,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress)
    ) == SOCKET_ERROR) {

        std::cerr << "Bind failed: " << WSAGetLastError() << "\n";

        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // Start listening.
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {

        std::cerr << "Listen failed\n";

        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "HTTP Server running at:\n";
    std::cout << "http://localhost:" << PORT << "\n";

    // Accept clients.
    while (true) {

        SOCKET clientSocket = accept(
            serverSocket,
            nullptr,
            nullptr
        );

        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed\n";
            continue;
        }

        handleClient(clientSocket);

        // Close client connection.
        closesocket(clientSocket);
    }

    closesocket(serverSocket);
    WSACleanup();

    return 0;
}
