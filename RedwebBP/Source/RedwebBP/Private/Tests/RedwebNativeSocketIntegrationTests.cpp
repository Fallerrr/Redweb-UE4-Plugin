#if WITH_DEV_AUTOMATION_TESTS

#include "RedwebNativeSocket.h"
#include "RedwebSocketComponent.h"
#include "RedwebAutomationEventReceiver.h"

#include "HAL/Event.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeLock.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <winhttp.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRedwebNativeTransportIntegrationTest,
    "RedwebBP.Integration.NativeTransportEcho",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRedwebNativeTransportIntegrationTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
    URedwebSocketComponent* UrlCases = NewObject<URedwebSocketComponent>();
    UrlCases->ServerUrl = TEXT("ws://example.test///");
    UrlCases->RoutePath = TEXT("/socket");
    TestEqual(TEXT("A slash-terminated server URL does not create a duplicate route slash"),
        UrlCases->BuildFullUrl(), FString(TEXT("ws://example.test/socket?redwebVersion=1")));
    FRedwebKeyValue EmptyOnlyQuery;
    UrlCases->QueryParams.Add(EmptyOnlyQuery);
    TestEqual(TEXT("Empty query keys do not append a question mark"),
        UrlCases->BuildFullUrl(), FString(TEXT("ws://example.test/socket?redwebVersion=1")));

    const FString Url = FPlatformMisc::GetEnvironmentVariable(TEXT("REDWEBBP_TEST_URL")).IsEmpty()
        ? TEXT("ws://127.0.0.1:18182/socket")
        : FPlatformMisc::GetEnvironmentVariable(TEXT("REDWEBBP_TEST_URL"));
    URedwebSocketComponent* Component = NewObject<URedwebSocketComponent>();
    Component->ServerUrl = Url.LeftChop(FString(TEXT("/socket")).Len());
    Component->ServerUrl += TEXT("///");
    Component->RoutePath = TEXT("socket");
    FRedwebKeyValue Query;
    Query.Key = TEXT("fixture");
    Query.Value = TEXT("ue baseline & query");
    Component->QueryParams.Add(Query);
    FRedwebKeyValue EmptyQuery;
    Component->QueryParams.Add(EmptyQuery);
    FRedwebKeyValue UnsupportedVersionOverride;
    UnsupportedVersionOverride.Key = TEXT("redwebVersion");
    UnsupportedVersionOverride.Value = TEXT("2");
    Component->QueryParams.Add(UnsupportedVersionOverride);

    const FString BuiltUrl = Component->BuildFullUrl();
    TestTrue(TEXT("The URL builder trims the base and adds the route slash"), BuiltUrl.Contains(TEXT("/socket?")));
    TestTrue(TEXT("The URL builder negotiates the current Redweb protocol"), BuiltUrl.Contains(TEXT("redwebVersion=1")));
    TestFalse(TEXT("The URL builder ignores an unsupported user version override"), BuiltUrl.Contains(TEXT("redwebVersion=2")));
    TestTrue(TEXT("The URL builder includes the non-empty query key"), BuiltUrl.Contains(TEXT("fixture=")));
    TestFalse(TEXT("The URL builder skips query entries with empty keys"), BuiltUrl.Contains(TEXT("&=")));
    TestTrue(TEXT("Custom query parameters follow the protocol negotiation query"),
        BuiltUrl.Contains(TEXT("?redwebVersion=1&fixture=")));

    const TPair<FRedwebNativeSocket::EAutomationFailurePoint, FString> CreationFailures[] =
    {
        TPair<FRedwebNativeSocket::EAutomationFailurePoint, FString>(
            FRedwebNativeSocket::EAutomationFailurePoint::SessionCreation, TEXT("Could not open the Windows WebSocket session")),
        TPair<FRedwebNativeSocket::EAutomationFailurePoint, FString>(
            FRedwebNativeSocket::EAutomationFailurePoint::ConnectionCreation, TEXT("Could not reach")),
        TPair<FRedwebNativeSocket::EAutomationFailurePoint, FString>(
            FRedwebNativeSocket::EAutomationFailurePoint::RequestCreation, TEXT("Could not create the WebSocket request"))
    };
    for (const TPair<FRedwebNativeSocket::EAutomationFailurePoint, FString>& Failure : CreationFailures)
    {
        FRedwebNativeSocket::FCallbacks FailureCallbacks;
        FRedwebNativeSocket FailureSocket(BuiltUrl, MoveTemp(FailureCallbacks));
        FailureSocket.AutomationFailurePoint = Failure.Key;
        FString FailureError;
        TestFalse(TEXT("A failed WinHTTP resource-creation stage aborts connection setup"),
            FailureSocket.ConnectSocket(FailureError));
        TestTrue(TEXT("The WinHTTP failure identifies its creation stage"), FailureError.Contains(Failure.Value));
    }

    FEvent* SendFailureEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FString SendFailureError;
    FRedwebNativeSocket::FCallbacks SendFailureCallbacks;
    SendFailureCallbacks.OnError = [&SendFailureError, SendFailureEvent](const FString& Error)
    {
        SendFailureError = Error;
        SendFailureEvent->Trigger();
    };
    FRedwebNativeSocket SendFailureSocket(BuiltUrl, MoveTemp(SendFailureCallbacks));
    HINTERNET NonWebSocketHandle = WinHttpOpen(TEXT("RedwebBP coverage"), WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    TestNotNull(TEXT("The WinHTTP send-failure test opens a real session handle"), NonWebSocketHandle);
    if (NonWebSocketHandle)
    {
        SendFailureSocket.WebSocketHandle = NonWebSocketHandle;
        SendFailureSocket.bConnected = true;
        TestFalse(TEXT("WinHTTP rejects sending a WebSocket frame through a session handle"),
            SendFailureSocket.Send(TEXT("{}")));
        TestTrue(TEXT("The invalid WebSocket handle produces a send diagnostic"), SendFailureEvent->Wait(1000));
        TestTrue(TEXT("The send diagnostic identifies the failed WinHTTP operation"),
            SendFailureError.Contains(TEXT("WebSocket send failed")));
        SendFailureSocket.CloseHandles();
    }
    FPlatformProcess::ReturnSynchEventToPool(SendFailureEvent);

    URedwebSocketComponent* ThreadStartFailureComponent = NewObject<URedwebSocketComponent>();
    URedwebAutomationEventReceiver* ThreadStartFailureReceiver = NewObject<URedwebAutomationEventReceiver>();
    ThreadStartFailureComponent->ServerUrl = BuiltUrl;
    ThreadStartFailureComponent->RoutePath.Empty();
    ThreadStartFailureComponent->bAutoReconnect = false;
    ThreadStartFailureComponent->OnError.AddDynamic(ThreadStartFailureReceiver, &URedwebAutomationEventReceiver::ReceiveError);
    FRedwebNativeSocket::bFailNextThreadCreationForAutomation = true;
    AddExpectedError(TEXT("Could not start the native WebSocket worker"), EAutomationExpectedErrorFlags::Contains, 1);
    ThreadStartFailureComponent->StartSocket();
    TestFalse(TEXT("The component releases a socket whose worker could not be created"), ThreadStartFailureComponent->Socket.IsValid());
    TestEqual(TEXT("The component broadcasts the worker-creation failure"), ThreadStartFailureReceiver->Errors.Num(), 1);

    FRedwebNativeSocket::FCallbacks StateGuardCallbacks;
    FRedwebNativeSocket StateGuardSocket(BuiltUrl, MoveTemp(StateGuardCallbacks));
    TestFalse(TEXT("An unstarted native socket rejects sends"), StateGuardSocket.Send(TEXT("{}")));
    StateGuardSocket.bConnected = true;
    TestFalse(TEXT("A connected flag without an open WinHTTP handle rejects sends safely"), StateGuardSocket.Send(TEXT("{}")));
    StateGuardSocket.bConnected = false;

    int32 CloseCallbackCount = 0;
    FRedwebNativeSocket::FCallbacks CloseIdempotencyCallbacks;
    CloseIdempotencyCallbacks.OnClosed = [&CloseCallbackCount](int32, const FString&, bool) { ++CloseCallbackCount; };
    FRedwebNativeSocket CloseIdempotencySocket(BuiltUrl, MoveTemp(CloseIdempotencyCallbacks));
    CloseIdempotencySocket.ReportClosed(1000, TEXT("first"), true);
    CloseIdempotencySocket.ReportClosed(1001, TEXT("second"), false);
    TestEqual(TEXT("A native connection reports only its first close notification"), CloseCallbackCount, 1);

    int32 UnexpectedReceiveEndStatus = 0;
    FString UnexpectedReceiveEndReason;
    FRedwebNativeSocket::FCallbacks UnexpectedReceiveEndCallbacks;
    UnexpectedReceiveEndCallbacks.OnClosed = [&UnexpectedReceiveEndStatus, &UnexpectedReceiveEndReason](
        const int32 StatusCode, const FString& Reason, bool)
    {
        UnexpectedReceiveEndStatus = StatusCode;
        UnexpectedReceiveEndReason = Reason;
    };
    FRedwebNativeSocket UnexpectedReceiveEndSocket(BuiltUrl, MoveTemp(UnexpectedReceiveEndCallbacks));
    UnexpectedReceiveEndSocket.ReceiveLoop();
    TestEqual(TEXT("An unexpected empty receive loop reports an abnormal close"), UnexpectedReceiveEndStatus, 1006);
    TestEqual(TEXT("An unexpected empty receive loop explains why it ended"), UnexpectedReceiveEndReason,
        FString(TEXT("WebSocket receive loop ended.")));

    UrlCases->StartConnectionTimeout();

    FEvent* ConnectedEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* MessageEvent = FPlatformProcess::GetSynchEventFromPool(false);
    FEvent* TransportErrorEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* ClosedEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FCriticalSection ResultLock;
    TArray<FString> ReceivedMessages;
    FString TransportError;
    int32 ClosedStatusCode = 0;
    FString ClosedReason;
    bool bClosedCleanly = false;

    FRedwebNativeSocket::FCallbacks Callbacks;
    Callbacks.OnConnected = [ConnectedEvent]() { ConnectedEvent->Trigger(); };
    Callbacks.OnMessage = [&ResultLock, &ReceivedMessages, MessageEvent](const FString& Message)
    {
        FScopeLock Lock(&ResultLock);
        ReceivedMessages.Add(Message);
        MessageEvent->Trigger();
    };
    Callbacks.OnError = [&ResultLock, &TransportError, TransportErrorEvent](const FString& Error)
    {
        FScopeLock Lock(&ResultLock);
        TransportError = Error;
        TransportErrorEvent->Trigger();
    };
    Callbacks.OnClosed = [&ResultLock, &ClosedStatusCode, &ClosedReason, &bClosedCleanly, ClosedEvent](
        const int32 StatusCode, const FString& Reason, const bool bWasClean)
    {
        FScopeLock Lock(&ResultLock);
        ClosedStatusCode = StatusCode;
        ClosedReason = Reason;
        bClosedCleanly = bWasClean;
        ClosedEvent->Trigger();
    };

    TSharedPtr<FRedwebNativeSocket, ESPMode::ThreadSafe> Socket = MakeShared<FRedwebNativeSocket, ESPMode::ThreadSafe>(
        BuiltUrl, MoveTemp(Callbacks));
    Component->Socket = Socket;
    TestFalse(TEXT("Sending while the socket is not open fails"), Component->SendRaw(TEXT("{}")));
    TestFalse(TEXT("IsConnected is false before the handshake"), Component->IsConnected());

    const bool bStarted = Socket->Start();
    TestTrue(TEXT("The WinHTTP transport worker starts"), bStarted);
    if (!bStarted)
    {
        Component->Socket.Reset();
        Socket.Reset();
        FPlatformProcess::ReturnSynchEventToPool(ConnectedEvent);
        FPlatformProcess::ReturnSynchEventToPool(MessageEvent);
        FPlatformProcess::ReturnSynchEventToPool(TransportErrorEvent);
        FPlatformProcess::ReturnSynchEventToPool(ClosedEvent);
        return false;
    }
    TestFalse(TEXT("A running native transport worker cannot be started twice"), Socket->Start());

    const bool bConnected = ConnectedEvent->Wait(10000);
    TestTrue(TEXT("The native transport connects to the real Redweb fixture"), bConnected);
    if (bConnected)
    {
        TestTrue(TEXT("IsConnected is true after the handshake"), Component->IsConnected());

        const auto CheckNextEcho = [this, Component, MessageEvent, &ResultLock, &ReceivedMessages](
            const FString& Label, const FString& ExpectedField, const FString& ExpectedValue,
            const FString& ExpectedRequestId = FString())
        {
            const bool bReceived = MessageEvent->Wait(5000);
            TestTrue(*Label, bReceived);
            if (!bReceived) { return; }
            FScopeLock Lock(&ResultLock);
            const FString Message = ReceivedMessages.Last();
            TSharedPtr<FJsonObject> Object;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
            TestTrue(TEXT("The fixture response is a JSON object"), FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid());
            if (Object.IsValid())
            {
                TestEqual(TEXT("The fixture uses Redweb protocol version 1"), Object->GetStringField(TEXT("v")), FString(TEXT("1")));
                TestEqual(TEXT("The fixture identifies its echo response"), Object->GetStringField(TEXT("type")), FString(TEXT("echo")));
                const TSharedPtr<FJsonObject>* Payload = nullptr;
                TestTrue(TEXT("The Redweb envelope carries a JSON object payload"), Object->TryGetObjectField(TEXT("payload"), Payload) && Payload && Payload->IsValid());
                if (Payload && Payload->IsValid())
                {
                    TestEqual(TEXT("The expected application payload field is echoed"), (*Payload)->GetStringField(ExpectedField), ExpectedValue);
                }
                if (!ExpectedRequestId.IsEmpty())
                {
                    TestEqual(TEXT("The server preserves protocol request IDs"), Object->GetStringField(TEXT("requestId")), ExpectedRequestId);
                }
            }
        };

        TestTrue(TEXT("SendJson builds and sends an object with a type field"),
            Component->SendJson(TEXT("{\"text\":\"ue-baseline\"}"), TEXT("echo")));
        CheckNextEcho(TEXT("The Redweb server echoes SendJson"), TEXT("text"), TEXT("ue-baseline"));

        TestTrue(TEXT("SendJson wraps invalid JSON under the legacy data field"),
            Component->SendJson(TEXT("plain text"), TEXT("echo")));
        CheckNextEcho(TEXT("The Redweb server echoes the SendJson fallback"), TEXT("data"), TEXT("plain text"));

        TArray<FRedwebKeyValue> Fields;
        FRedwebKeyValue TypedText;
        TypedText.Key = TEXT("text");
        TypedText.Value = TEXT("typed-fields");
        Fields.Add(TypedText);
        TestTrue(TEXT("SendTypedFields serializes and sends fields"), Component->SendTypedFields(TEXT("echo"), Fields));
        CheckNextEcho(TEXT("The Redweb server echoes SendTypedFields"), TEXT("text"), TEXT("typed-fields"));

        TestTrue(TEXT("SendJson with an empty type preserves the raw JSON frame"),
            Component->SendJson(TEXT("{\"type\":\"echo\",\"text\":\"raw-json\"}"), FString()));
        CheckNextEcho(TEXT("The Redweb server echoes the raw SendJson frame"), TEXT("text"), TEXT("raw-json"));

        TestTrue(TEXT("SendRaw sends a connected client's frame"),
            Component->SendRaw(TEXT("{\"type\":\"echo\",\"text\":\"raw-send\"}")));
        CheckNextEcho(TEXT("The Redweb server echoes SendRaw"), TEXT("text"), TEXT("raw-send"));

        TestTrue(TEXT("SendRaw preserves an already versioned Redweb envelope"),
            Component->SendRaw(TEXT("{\"v\":\"1\",\"type\":\"echo\",\"requestId\":\"raw-v1\",\"payload\":{\"text\":\"raw-v1\"}}")));
        CheckNextEcho(TEXT("The server replies to a raw protocol v1 envelope"), TEXT("text"), TEXT("raw-v1"), TEXT("raw-v1"));

        FString LargeText;
        LargeText.Reserve(128 * 1024);
        for (int32 Index = 0; Index < 128 * 1024; ++Index)
        {
            LargeText.AppendChar(TEXT('x'));
        }
        const FString LargeJson = FString::Printf(TEXT("{\"text\":\"%s\"}"), *LargeText);
        TestTrue(TEXT("SendJson sends a payload larger than the native receive buffer"),
            Component->SendJson(LargeJson, TEXT("echo")));
        CheckNextEcho(TEXT("The native receive loop reassembles the full large text message"), TEXT("text"), LargeText);

        TestTrue(TEXT("SendRaw can request an empty text frame from the real server"),
            Component->SendRaw(TEXT("{\"type\":\"echo\",\"fixtureCommand\":\"empty\"}")));
        const bool bEmptyFrameReceived = MessageEvent->Wait(5000);
        TestTrue(TEXT("The native receive loop delivers an empty text frame"), bEmptyFrameReceived);
        if (bEmptyFrameReceived)
        {
            FScopeLock Lock(&ResultLock);
            TestTrue(TEXT("The delivered empty frame remains empty"), ReceivedMessages.Last().IsEmpty());
        }

        TestTrue(TEXT("SendRaw can request a binary response from the real server"),
            Component->SendRaw(TEXT("{\"type\":\"echo\",\"fixtureCommand\":\"binary\"}")));
        const bool bBinaryRejected = TransportErrorEvent->Wait(5000);
        TestTrue(TEXT("The native transport reports unsupported binary frames"), bBinaryRejected);
        {
            FScopeLock Lock(&ResultLock);
            TestTrue(TEXT("The binary-frame diagnostic identifies the unsupported frame"),
                TransportError.Contains(TEXT("unsupported binary WebSocket frame")));
            TransportError.Empty();
        }

        TestTrue(TEXT("SendRaw can ask the real server to close with a reason"),
            Component->SendRaw(TEXT("{\"type\":\"echo\",\"fixtureCommand\":\"close\"}")));
        const bool bRemoteClosed = ClosedEvent->Wait(5000);
        TestTrue(TEXT("The native transport reports the remote close frame"), bRemoteClosed);
        if (bRemoteClosed)
        {
            FScopeLock Lock(&ResultLock);
            TestEqual(TEXT("The remote close status code is preserved"), ClosedStatusCode, 4001);
            TestEqual(TEXT("The remote close reason is preserved"), ClosedReason, FString(TEXT("fixture-close")));
            TestTrue(TEXT("The normal remote close is reported clean"), bClosedCleanly);
        }
    }

    const FString CurrentUrl = FPlatformMisc::GetEnvironmentVariable(TEXT("REDWEBBP_CURRENT_TEST_URL")).IsEmpty()
        ? TEXT("ws://127.0.0.1:18182/current")
        : FPlatformMisc::GetEnvironmentVariable(TEXT("REDWEBBP_CURRENT_TEST_URL"));
    FEvent* CurrentErrorEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* CurrentConnectedEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FRedwebNativeSocket::FCallbacks CurrentCallbacks;
    CurrentCallbacks.OnError = [CurrentErrorEvent](const FString&) { CurrentErrorEvent->Trigger(); };
    CurrentCallbacks.OnConnected = [CurrentConnectedEvent]() { CurrentConnectedEvent->Trigger(); };
    FRedwebNativeSocket CurrentProtocolSocket(CurrentUrl, MoveTemp(CurrentCallbacks));
    const bool bCurrentProtocolStarted = CurrentProtocolSocket.Start();
    TestTrue(TEXT("A missing-version negotiation test starts its native transport worker"), bCurrentProtocolStarted);
    if (bCurrentProtocolStarted)
    {
        const bool bCurrentProtocolRejected = CurrentErrorEvent->Wait(5000);
        TestTrue(TEXT("Redweb rejects the handshake when the version query is missing"), bCurrentProtocolRejected);
        TestFalse(TEXT("An unnegotiated transport cannot connect to a versioned route"),
            CurrentConnectedEvent->Wait(100));
        CurrentProtocolSocket.Shutdown();
    }
    FPlatformProcess::ReturnSynchEventToPool(CurrentErrorEvent);
    FPlatformProcess::ReturnSynchEventToPool(CurrentConnectedEvent);

    {
        FScopeLock Lock(&ResultLock);
        if (!TransportError.IsEmpty())
        {
            AddError(FString::Printf(TEXT("Native transport reported: %s"), *TransportError));
        }
    }

    Socket->Shutdown();
    Component->Socket.Reset();
    Socket.Reset();
    FPlatformProcess::ReturnSynchEventToPool(ConnectedEvent);
    FPlatformProcess::ReturnSynchEventToPool(MessageEvent);
    FPlatformProcess::ReturnSynchEventToPool(TransportErrorEvent);
    FPlatformProcess::ReturnSynchEventToPool(ClosedEvent);

    FEvent* InvalidUrlErrorEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* InvalidUrlClosedEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FCriticalSection InvalidUrlResultLock;
    FString InvalidUrlError;
    int32 InvalidUrlCloseCode = 0;
    FRedwebNativeSocket::FCallbacks InvalidUrlCallbacks;
    InvalidUrlCallbacks.OnError = [&InvalidUrlResultLock, &InvalidUrlError, InvalidUrlErrorEvent](const FString& Error)
    {
        FScopeLock Lock(&InvalidUrlResultLock);
        InvalidUrlError = Error;
        InvalidUrlErrorEvent->Trigger();
    };
    InvalidUrlCallbacks.OnClosed = [&InvalidUrlResultLock, &InvalidUrlCloseCode, InvalidUrlClosedEvent](
        const int32 StatusCode, const FString&, const bool)
    {
        FScopeLock Lock(&InvalidUrlResultLock);
        InvalidUrlCloseCode = StatusCode;
        InvalidUrlClosedEvent->Trigger();
    };
    FRedwebNativeSocket InvalidUrlSocket(TEXT("not a websocket URL"), MoveTemp(InvalidUrlCallbacks));
    const bool bInvalidUrlStarted = InvalidUrlSocket.Start();
    TestTrue(TEXT("The invalid-URL transport worker starts"), bInvalidUrlStarted);
    if (bInvalidUrlStarted)
    {
        const bool bInvalidUrlReportedError = InvalidUrlErrorEvent->Wait(5000);
        const bool bInvalidUrlReportedClose = InvalidUrlClosedEvent->Wait(5000);
        TestTrue(TEXT("Invalid URLs report a connection error"), bInvalidUrlReportedError);
        TestTrue(TEXT("Invalid URLs report an abnormal close"), bInvalidUrlReportedClose);
        if (bInvalidUrlReportedError && bInvalidUrlReportedClose)
        {
            FScopeLock Lock(&InvalidUrlResultLock);
            TestTrue(TEXT("The URL diagnostic identifies the malformed address"), InvalidUrlError.Contains(TEXT("Invalid WebSocket URL")));
            TestEqual(TEXT("Invalid URLs use the abnormal-closure status"), InvalidUrlCloseCode, 1006);
        }
        InvalidUrlSocket.Shutdown();
    }
    FPlatformProcess::ReturnSynchEventToPool(InvalidUrlErrorEvent);
    FPlatformProcess::ReturnSynchEventToPool(InvalidUrlClosedEvent);

    FEvent* UnsupportedSchemeErrorEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FString FtpUrl = BuiltUrl;
    FtpUrl.ReplaceInline(TEXT("ws://"), TEXT("ftp://"), ESearchCase::IgnoreCase);
    FRedwebNativeSocket::FCallbacks UnsupportedSchemeCallbacks;
    UnsupportedSchemeCallbacks.OnError = [UnsupportedSchemeErrorEvent](const FString&) { UnsupportedSchemeErrorEvent->Trigger(); };
    FRedwebNativeSocket UnsupportedSchemeSocket(FtpUrl, MoveTemp(UnsupportedSchemeCallbacks));
    const bool bUnsupportedSchemeStarted = UnsupportedSchemeSocket.Start();
    TestTrue(TEXT("The unsupported-scheme worker starts before URL validation"), bUnsupportedSchemeStarted);
    if (bUnsupportedSchemeStarted)
    {
        TestTrue(TEXT("WinHTTP rejects a validly parsed non-HTTP URL scheme"), UnsupportedSchemeErrorEvent->Wait(5000));
        UnsupportedSchemeSocket.Shutdown();
    }
    FPlatformProcess::ReturnSynchEventToPool(UnsupportedSchemeErrorEvent);

    FEvent* SecureHandshakeErrorEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FString WssUrl = BuiltUrl;
    WssUrl.ReplaceInline(TEXT("ws://"), TEXT("wss://"), ESearchCase::IgnoreCase);
    FRedwebNativeSocket::FCallbacks SecureHandshakeCallbacks;
    SecureHandshakeCallbacks.OnError = [SecureHandshakeErrorEvent](const FString&) { SecureHandshakeErrorEvent->Trigger(); };
    FRedwebNativeSocket SecureHandshakeSocket(WssUrl, MoveTemp(SecureHandshakeCallbacks));
    const bool bSecureHandshakeStarted = SecureHandshakeSocket.Start();
    TestTrue(TEXT("The WSS conversion scenario starts a real native worker"), bSecureHandshakeStarted);
    if (bSecureHandshakeStarted)
    {
        TestTrue(TEXT("WinHTTP reports the TLS failure from the non-TLS Redweb fixture"), SecureHandshakeErrorEvent->Wait(10000));
        SecureHandshakeSocket.Shutdown();
    }
    FPlatformProcess::ReturnSynchEventToPool(SecureHandshakeErrorEvent);

    FRedwebNativeSocket::FCallbacks CancelledHandshakeCallbacks;
    FRedwebNativeSocket CancelledHandshakeSocket(BuiltUrl, MoveTemp(CancelledHandshakeCallbacks));
    CancelledHandshakeSocket.bStopRequested = true;
    FString CancelledHandshakeError;
    TestFalse(TEXT("A successful real handshake is discarded when shutdown wins the handle-install race"),
        CancelledHandshakeSocket.ConnectSocket(CancelledHandshakeError));
    TestEqual(TEXT("The discarded handshake reports cancellation"), CancelledHandshakeError,
        FString(TEXT("Connection was cancelled.")));

    FEvent* ClientShutdownConnectedEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FRedwebNativeSocket::FCallbacks ClientShutdownCallbacks;
    ClientShutdownCallbacks.OnConnected = [ClientShutdownConnectedEvent]() { ClientShutdownConnectedEvent->Trigger(); };
    FRedwebNativeSocket ClientShutdownSocket(BuiltUrl, MoveTemp(ClientShutdownCallbacks));
    const bool bClientShutdownStarted = ClientShutdownSocket.Start();
    TestTrue(TEXT("The client-shutdown transport worker starts"), bClientShutdownStarted);
    if (bClientShutdownStarted)
    {
        TestTrue(TEXT("The client-shutdown scenario reaches a real WebSocket connection"), ClientShutdownConnectedEvent->Wait(10000));
        ClientShutdownSocket.Shutdown(1000, TEXT("client-shutdown"));
    }
    FPlatformProcess::ReturnSynchEventToPool(ClientShutdownConnectedEvent);

    FEvent* AbruptConnectedEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* AbruptErrorEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* AbruptClosedEvent = FPlatformProcess::GetSynchEventFromPool(true);
    FCriticalSection AbruptResultLock;
    FString AbruptError;
    int32 AbruptCloseCode = 0;
    bool bAbruptCloseWasClean = true;
    FRedwebNativeSocket::FCallbacks AbruptCallbacks;
    AbruptCallbacks.OnConnected = [AbruptConnectedEvent]() { AbruptConnectedEvent->Trigger(); };
    AbruptCallbacks.OnError = [&AbruptResultLock, &AbruptError, AbruptErrorEvent](const FString& Error)
    {
        FScopeLock Lock(&AbruptResultLock);
        AbruptError = Error;
        AbruptErrorEvent->Trigger();
    };
    AbruptCallbacks.OnClosed = [&AbruptResultLock, &AbruptCloseCode, &bAbruptCloseWasClean, AbruptClosedEvent](
        const int32 StatusCode, const FString&, const bool bWasClean)
    {
        FScopeLock Lock(&AbruptResultLock);
        AbruptCloseCode = StatusCode;
        bAbruptCloseWasClean = bWasClean;
        AbruptClosedEvent->Trigger();
    };
    TSharedPtr<FRedwebNativeSocket, ESPMode::ThreadSafe> AbruptSocket = MakeShared<FRedwebNativeSocket, ESPMode::ThreadSafe>(
        BuiltUrl, MoveTemp(AbruptCallbacks));
    const bool bAbruptSocketStarted = AbruptSocket->Start();
    TestTrue(TEXT("The abrupt-close transport worker starts"), bAbruptSocketStarted);
    if (bAbruptSocketStarted)
    {
        const bool bAbruptSocketConnected = AbruptConnectedEvent->Wait(10000);
        TestTrue(TEXT("The abrupt-close transport connects to the real Redweb fixture"), bAbruptSocketConnected);
        if (bAbruptSocketConnected)
        {
            TestTrue(TEXT("The native transport sends before the peer aborts"),
                AbruptSocket->Send(TEXT("{\"v\":\"1\",\"type\":\"echo\",\"payload\":{\"fixtureCommand\":\"abort\"}}")));
            const bool bAbruptErrorReported = AbruptErrorEvent->Wait(5000);
            const bool bAbruptCloseReported = AbruptClosedEvent->Wait(5000);
            TestTrue(TEXT("The native receive failure is reported"), bAbruptErrorReported);
            TestTrue(TEXT("The native transport reports an abnormal close"), bAbruptCloseReported);
            if (bAbruptErrorReported && bAbruptCloseReported)
            {
                FScopeLock Lock(&AbruptResultLock);
                TestTrue(TEXT("The receive diagnostic identifies the transport failure"), AbruptError.Contains(TEXT("WebSocket receive failed")));
                TestEqual(TEXT("An abrupt peer disconnect maps to status 1006"), AbruptCloseCode, 1006);
                TestFalse(TEXT("An abrupt peer disconnect is not clean"), bAbruptCloseWasClean);
            }
        }
        AbruptSocket->Shutdown();
    }
    AbruptSocket.Reset();
    FPlatformProcess::ReturnSynchEventToPool(AbruptConnectedEvent);
    FPlatformProcess::ReturnSynchEventToPool(AbruptErrorEvent);
    FPlatformProcess::ReturnSynchEventToPool(AbruptClosedEvent);
    return true;
#else
    AddError(TEXT("The native transport integration test is Windows-only."));
    return false;
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
