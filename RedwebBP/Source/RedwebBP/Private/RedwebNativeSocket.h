#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Runnable.h"

class FRunnableThread;

/**
 * Windows-native WebSocket transport. Network I/O stays off Unreal's game
 * thread; callbacks are forwarded by the owning component on the game thread.
 */
class FRedwebNativeSocket final : public FRunnable, public TSharedFromThis<FRedwebNativeSocket, ESPMode::ThreadSafe>
{
public:
    struct FCallbacks
    {
        TFunction<void()> OnConnected;
        TFunction<void(const FString&)> OnError;
        TFunction<void(const FString&)> OnMessage;
        TFunction<void(int32, const FString&, bool)> OnClosed;
    };

    FRedwebNativeSocket(const FString& InUrl, FCallbacks&& InCallbacks);
    virtual ~FRedwebNativeSocket() override;

    bool Start();
    void Shutdown(int32 StatusCode = 1000, const FString& Reason = TEXT("Client disconnect"));
    bool Send(const FString& Message);
    bool IsConnected() const;

    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FRedwebNativeTransportIntegrationTest;

    enum class EAutomationFailurePoint
    {
        None,
        SessionCreation,
        ConnectionCreation,
        RequestCreation
    };

    EAutomationFailurePoint AutomationFailurePoint = EAutomationFailurePoint::None;
    static bool bFailNextThreadCreationForAutomation;
#endif

    bool ConnectSocket(FString& OutError);
    void ReceiveLoop();
    void CloseHandles();
    void ReportError(const FString& Error);
    void ReportClosed(int32 StatusCode, const FString& Reason, bool bWasClean);

    FString Url;
    FCallbacks Callbacks;
    FRunnableThread* Thread;
    FCriticalSection HandleLock;
    FCriticalSection SendLock;
    void* SessionHandle;
    void* ConnectionHandle;
    void* WebSocketHandle;
    FThreadSafeBool bStopRequested;
    FThreadSafeBool bConnected;
    FThreadSafeBool bReportedClosed;
};
