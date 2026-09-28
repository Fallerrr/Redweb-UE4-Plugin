#include "RedwebNativeSocket.h"

#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <winhttp.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
#if PLATFORM_WINDOWS
    FString DescribeWindowsError(const DWORD ErrorCode)
    {
        LPWSTR MessageBuffer = nullptr;
        const DWORD Length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            ErrorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&MessageBuffer),
            0,
            nullptr
        );

        FString Result = Length > 0 && MessageBuffer
            ? FString(Length, MessageBuffer).TrimStartAndEnd()
            : FString::Printf(TEXT("Windows error %lu"), ErrorCode);

        if (MessageBuffer)
        {
            LocalFree(MessageBuffer);
        }

        return Result;
    }
#endif
}

FRedwebNativeSocket::FRedwebNativeSocket(const FString& InUrl, FCallbacks&& InCallbacks)
    : Url(InUrl)
    , Callbacks(MoveTemp(InCallbacks))
    , Thread(nullptr)
    , SessionHandle(nullptr)
    , ConnectionHandle(nullptr)
    , RequestHandle(nullptr)
    , WebSocketHandle(nullptr)
    , bStopRequested(false)
    , bConnected(false)
    , bReportedClosed(false)
{
}

FRedwebNativeSocket::~FRedwebNativeSocket()
{
    Shutdown();
}

bool FRedwebNativeSocket::Start()
{
    if (Thread)
    {
        return false;
    }

    Thread = FRunnableThread::Create(this, TEXT("RedwebNativeSocket"), 0, TPri_AboveNormal);
    return Thread != nullptr;
}

void FRedwebNativeSocket::Shutdown(const int32 StatusCode, const FString& Reason)
{
    bStopRequested = true;

#if PLATFORM_WINDOWS
    {
        FScopeLock Lock(&HandleLock);
        if (WebSocketHandle)
        {
            FTCHARToUTF8 Utf8Reason(*Reason);
            WinHttpWebSocketClose(static_cast<HINTERNET>(WebSocketHandle), static_cast<USHORT>(StatusCode),
                const_cast<ANSICHAR*>(Utf8Reason.Get()), static_cast<DWORD>(Utf8Reason.Length()));
        }
    }
#endif

    // Closing WinHTTP handles cancels a pending receive immediately.
    CloseHandles();

    if (Thread)
    {
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }
}

bool FRedwebNativeSocket::Send(const FString& Message)
{
#if PLATFORM_WINDOWS
    if (!bConnected || bStopRequested)
    {
        return false;
    }

    FTCHARToUTF8 Utf8(*Message);
    FScopeLock SendGuard(&SendLock);
    FScopeLock HandleGuard(&HandleLock);
    if (!WebSocketHandle || bStopRequested)
    {
        return false;
    }

    const DWORD Result = WinHttpWebSocketSend(
        static_cast<HINTERNET>(WebSocketHandle),
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<ANSICHAR*>(Utf8.Get()),
        static_cast<DWORD>(Utf8.Length())
    );

    if (Result != NO_ERROR)
    {
        ReportError(FString::Printf(TEXT("WebSocket send failed: %s"), *DescribeWindowsError(Result)));
        return false;
    }

    return true;
#else
    return false;
#endif
}

bool FRedwebNativeSocket::IsConnected() const
{
    return bConnected && !bStopRequested;
}

void FRedwebNativeSocket::Stop()
{
    bStopRequested = true;
    CloseHandles();
}

uint32 FRedwebNativeSocket::Run()
{
#if !PLATFORM_WINDOWS
    ReportError(TEXT("RedwebBP native transport currently supports Windows builds only."));
    ReportClosed(1006, TEXT("Unsupported platform"), false);
    return 0;
#else
    FString Error;
    if (!ConnectSocket(Error))
    {
        if (!bStopRequested)
        {
            ReportError(Error);
            ReportClosed(1006, Error, false);
        }
        CloseHandles();
        return 0;
    }

    bConnected = true;
    if (Callbacks.OnConnected)
    {
        Callbacks.OnConnected();
    }

    ReceiveLoop();
    CloseHandles();
    return 0;
#endif
}

void FRedwebNativeSocket::Exit()
{
    bConnected = false;
}

bool FRedwebNativeSocket::ConnectSocket(FString& OutError)
{
#if !PLATFORM_WINDOWS
    OutError = TEXT("RedwebBP native transport currently supports Windows builds only.");
    return false;
#else
    FString HttpUrl = Url;
    if (HttpUrl.StartsWith(TEXT("ws://"), ESearchCase::IgnoreCase))
    {
        HttpUrl = TEXT("http://") + HttpUrl.Mid(5);
    }
    else if (HttpUrl.StartsWith(TEXT("wss://"), ESearchCase::IgnoreCase))
    {
        HttpUrl = TEXT("https://") + HttpUrl.Mid(6);
    }

    URL_COMPONENTS Components{};
    Components.dwStructSize = sizeof(Components);
    Components.dwSchemeLength = static_cast<DWORD>(-1);
    Components.dwHostNameLength = static_cast<DWORD>(-1);
    Components.dwUrlPathLength = static_cast<DWORD>(-1);
    Components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(*HttpUrl, 0, 0, &Components))
    {
        OutError = FString::Printf(TEXT("Invalid WebSocket URL '%s': %s"), *Url, *DescribeWindowsError(GetLastError()));
        return false;
    }

    const bool bSecure = Components.nScheme == INTERNET_SCHEME_HTTPS;
    if (Components.nScheme != INTERNET_SCHEME_HTTP && !bSecure)
    {
        OutError = TEXT("WebSocket URL must begin with ws:// or wss://.");
        return false;
    }

    const FString Host(Components.dwHostNameLength, Components.lpszHostName);
    FString Path = Components.dwUrlPathLength > 0
        ? FString(Components.dwUrlPathLength, Components.lpszUrlPath)
        : TEXT("/");
    if (Components.dwExtraInfoLength > 0)
    {
        Path += FString(Components.dwExtraInfoLength, Components.lpszExtraInfo);
    }

    HINTERNET NewSession = WinHttpOpen(TEXT("Gemhouse-RedwebBP/3.0"), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!NewSession)
    {
        OutError = FString::Printf(TEXT("Could not open the Windows WebSocket session: %s"), *DescribeWindowsError(GetLastError()));
        return false;
    }

    // Bound connection and request stages so an unreachable host cannot stall shutdown indefinitely.
    WinHttpSetTimeouts(NewSession, 5000, 5000, 10000, 10000);

    HINTERNET NewConnection = WinHttpConnect(NewSession, *Host, Components.nPort, 0);
    if (!NewConnection)
    {
        const FString Failure = DescribeWindowsError(GetLastError());
        WinHttpCloseHandle(NewSession);
        OutError = FString::Printf(TEXT("Could not reach %s: %s"), *Host, *Failure);
        return false;
    }

    HINTERNET NewRequest = WinHttpOpenRequest(NewConnection, TEXT("GET"), *Path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, bSecure ? WINHTTP_FLAG_SECURE : 0);
    if (!NewRequest)
    {
        const FString Failure = DescribeWindowsError(GetLastError());
        WinHttpCloseHandle(NewConnection);
        WinHttpCloseHandle(NewSession);
        OutError = FString::Printf(TEXT("Could not create the WebSocket request: %s"), *Failure);
        return false;
    }

    if (!WinHttpSetOption(NewRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
        !WinHttpSendRequest(NewRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(NewRequest, nullptr))
    {
        const FString Failure = DescribeWindowsError(GetLastError());
        WinHttpCloseHandle(NewRequest);
        WinHttpCloseHandle(NewConnection);
        WinHttpCloseHandle(NewSession);
        OutError = FString::Printf(TEXT("WebSocket handshake failed: %s"), *Failure);
        return false;
    }

    HINTERNET NewWebSocket = WinHttpWebSocketCompleteUpgrade(NewRequest, 0);
    WinHttpCloseHandle(NewRequest);
    if (!NewWebSocket)
    {
        const FString Failure = DescribeWindowsError(GetLastError());
        WinHttpCloseHandle(NewConnection);
        WinHttpCloseHandle(NewSession);
        OutError = FString::Printf(TEXT("The server did not accept the WebSocket connection: %s"), *Failure);
        return false;
    }

    {
        FScopeLock Lock(&HandleLock);
        if (bStopRequested)
        {
            WinHttpCloseHandle(NewWebSocket);
            WinHttpCloseHandle(NewConnection);
            WinHttpCloseHandle(NewSession);
            OutError = TEXT("Connection was cancelled.");
            return false;
        }

        SessionHandle = NewSession;
        ConnectionHandle = NewConnection;
        RequestHandle = nullptr;
        WebSocketHandle = NewWebSocket;
    }

    return true;
#endif
}

void FRedwebNativeSocket::ReceiveLoop()
{
#if PLATFORM_WINDOWS
    TArray<uint8> Buffer;
    Buffer.SetNumUninitialized(64 * 1024);
    TArray<uint8> MessageBytes;

    while (!bStopRequested)
    {
        HINTERNET Socket = nullptr;
        {
            FScopeLock Lock(&HandleLock);
            Socket = static_cast<HINTERNET>(WebSocketHandle);
        }

        if (!Socket)
        {
            break;
        }

        DWORD BytesReceived = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE BufferType = WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE;
        const DWORD Result = WinHttpWebSocketReceive(Socket, Buffer.GetData(), Buffer.Num(), &BytesReceived, &BufferType);
        if (Result != NO_ERROR)
        {
            if (!bStopRequested)
            {
                ReportError(FString::Printf(TEXT("WebSocket receive failed: %s"), *DescribeWindowsError(Result)));
                ReportClosed(1006, DescribeWindowsError(Result), false);
            }
            break;
        }

        if (BufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
        {
            USHORT CloseCode = 1000;
            DWORD ReasonLength = 0;
            uint8 ReasonBytes[256]{};
            WinHttpWebSocketQueryCloseStatus(Socket, &CloseCode, ReasonBytes, UE_ARRAY_COUNT(ReasonBytes), &ReasonLength);
            FString Reason = TEXT("Remote endpoint closed the connection.");
            if (ReasonLength > 0)
            {
                FUTF8ToTCHAR ReasonText(reinterpret_cast<const ANSICHAR*>(ReasonBytes), ReasonLength);
                Reason = FString(ReasonText.Length(), ReasonText.Get());
            }
            ReportClosed(static_cast<int32>(CloseCode), Reason, true);
            break;
        }

        if (BufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE || BufferType == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE)
        {
            ReportError(TEXT("Redweb received an unsupported binary WebSocket frame."));
            continue;
        }

        if (BytesReceived > 0)
        {
            MessageBytes.Append(Buffer.GetData(), BytesReceived);
        }

        if (BufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
        {
            MessageBytes.Add(0);
            const FString Message = UTF8_TO_TCHAR(reinterpret_cast<const char*>(MessageBytes.GetData()));
            MessageBytes.Reset();
            if (Callbacks.OnMessage)
            {
                Callbacks.OnMessage(Message);
            }
        }
    }

    bConnected = false;
    if (!bReportedClosed && !bStopRequested)
    {
        ReportClosed(1006, TEXT("WebSocket receive loop ended."), false);
    }
#endif
}

void FRedwebNativeSocket::CloseHandles()
{
#if PLATFORM_WINDOWS
    FScopeLock Lock(&HandleLock);
    if (WebSocketHandle)
    {
        WinHttpCloseHandle(static_cast<HINTERNET>(WebSocketHandle));
        WebSocketHandle = nullptr;
    }
    if (RequestHandle)
    {
        WinHttpCloseHandle(static_cast<HINTERNET>(RequestHandle));
        RequestHandle = nullptr;
    }
    if (ConnectionHandle)
    {
        WinHttpCloseHandle(static_cast<HINTERNET>(ConnectionHandle));
        ConnectionHandle = nullptr;
    }
    if (SessionHandle)
    {
        WinHttpCloseHandle(static_cast<HINTERNET>(SessionHandle));
        SessionHandle = nullptr;
    }
#endif
}

void FRedwebNativeSocket::ReportError(const FString& Error)
{
    if (Callbacks.OnError)
    {
        Callbacks.OnError(Error);
    }
}

void FRedwebNativeSocket::ReportClosed(const int32 StatusCode, const FString& Reason, const bool bWasClean)
{
    if (bReportedClosed)
    {
        return;
    }

    bReportedClosed = true;

    if (Callbacks.OnClosed)
    {
        Callbacks.OnClosed(StatusCode, Reason, bWasClean);
    }
}
