#if WITH_DEV_AUTOMATION_TESTS

#include "RedwebNativeSocket.h"
#include "RedwebSocketComponent.h"

#include "HAL/Event.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeLock.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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
        UrlCases->BuildFullUrl(), FString(TEXT("ws://example.test/socket")));
    FRedwebKeyValue EmptyOnlyQuery;
    UrlCases->QueryParams.Add(EmptyOnlyQuery);
    TestEqual(TEXT("Empty query keys do not append a question mark"),
        UrlCases->BuildFullUrl(), FString(TEXT("ws://example.test/socket")));

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

    const FString BuiltUrl = Component->BuildFullUrl();
    TestTrue(TEXT("The URL builder trims the base and adds the route slash"), BuiltUrl.Contains(TEXT("/socket?")));
    TestTrue(TEXT("The URL builder includes the non-empty query key"), BuiltUrl.Contains(TEXT("fixture=")));
    TestFalse(TEXT("The URL builder skips query entries with empty keys"), BuiltUrl.Contains(TEXT("&=")));

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

    const bool bConnected = ConnectedEvent->Wait(10000);
    TestTrue(TEXT("The native transport connects to the real Redweb fixture"), bConnected);
    if (bConnected)
    {
        TestTrue(TEXT("IsConnected is true after the handshake"), Component->IsConnected());

        const auto CheckNextEcho = [this, Component, MessageEvent, &ResultLock, &ReceivedMessages](
            const FString& Label, const FString& ExpectedField, const FString& ExpectedValue)
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
                TestEqual(TEXT("The fixture identifies its echo response"), Object->GetStringField(TEXT("type")), FString(TEXT("echo")));
                TestEqual(TEXT("The expected legacy payload field is echoed"), Object->GetStringField(ExpectedField), ExpectedValue);
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
    TestTrue(TEXT("The legacy transport test starts against the current protocol route"), bCurrentProtocolStarted);
    if (bCurrentProtocolStarted)
    {
        const bool bCurrentProtocolRejected = CurrentErrorEvent->Wait(5000);
        TestTrue(TEXT("Current Redweb rejects the legacy handshake without version negotiation"), bCurrentProtocolRejected);
        TestFalse(TEXT("The legacy transport does not accidentally connect to the versioned route"),
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
                AbruptSocket->Send(TEXT("{\"type\":\"echo\",\"fixtureCommand\":\"abort\"}")));
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
