#if WITH_DEV_AUTOMATION_TESTS

#include "RedwebAutomationEventReceiver.h"
#include "RedwebNativeSocket.h"
#include "RedwebSocketComponent.h"

#include "Async/TaskGraphInterfaces.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRedwebComponentStateAutomationTest,
    "RedwebBP.Unit.ComponentState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRedwebComponentStateAutomationTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("A transient game world can be created"), World);
    if (!World)
    {
        return false;
    }

    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);

    AActor* Owner = World->SpawnActor<AActor>();
    TestNotNull(TEXT("An actor can own the component in the transient world"), Owner);
    if (!Owner)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }

    URedwebSocketComponent* Component = NewObject<URedwebSocketComponent>(Owner);
    TestNotNull(TEXT("The component can be constructed for a real actor"), Component);
    if (!Component)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }
    URedwebAutomationEventReceiver* Receiver = NewObject<URedwebAutomationEventReceiver>(World);
    Component->OnConnected.AddDynamic(Receiver, &URedwebAutomationEventReceiver::ReceiveConnected);
    Component->OnDisconnected.AddDynamic(Receiver, &URedwebAutomationEventReceiver::ReceiveDisconnected);
    Component->OnError.AddDynamic(Receiver, &URedwebAutomationEventReceiver::ReceiveError);
    Component->OnRawMessage.AddDynamic(Receiver, &URedwebAutomationEventReceiver::ReceiveRawMessage);
    Component->OnTypedMessage.AddDynamic(Receiver, &URedwebAutomationEventReceiver::ReceiveTypedMessage);

    Component->bAutoConnect = true;
    Component->ServerUrl = TEXT("http://localhost///");
    Component->RoutePath = TEXT("socket");
    Owner->AddInstanceComponent(Component);
    Component->RegisterComponent();
    FURL URL;
    World->InitializeActorsForPlay(URL);
    AddExpectedError(TEXT("WebSocket URL must begin with ws:// or wss://"), EAutomationExpectedErrorFlags::Contains, 1);
    Owner->DispatchBeginPlay();
    TestTrue(TEXT("The component has entered play through its owner"), Component->HasBegunPlay());
    TestEqual(TEXT("The public error delegate receives the invalid URL error"), Receiver->Errors.Num(), 1);
    TestFalse(TEXT("An HTTP URL is rejected as a WebSocket endpoint"), Component->IsConnected());
    TestFalse(TEXT("A rejected endpoint does not retain a socket"), Component->Socket.IsValid());

    const FString FixtureUrl = FPlatformMisc::GetEnvironmentVariable(TEXT("REDWEBBP_TEST_URL"));
    const auto PumpGameThreadUntil = [](const TFunction<bool()>& Predicate, const double TimeoutSeconds)
    {
        const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
        while (!Predicate() && FPlatformTime::Seconds() < Deadline)
        {
            FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
            FPlatformProcess::Sleep(0.01f);
        }
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        return Predicate();
    };
    TestFalse(TEXT("The real integration fixture provides a WebSocket URL"), FixtureUrl.IsEmpty());
    if (!FixtureUrl.IsEmpty())
    {
        Component->ServerUrl = FixtureUrl;
        Component->RoutePath.Empty();
        Component->QueryParams.Empty();
        Component->bAutoReconnect = false;
        Component->Connect();

        const double ConnectDeadline = FPlatformTime::Seconds() + 10.0;
        while (!Component->IsConnected() && FPlatformTime::Seconds() < ConnectDeadline)
        {
            FPlatformProcess::Sleep(0.01f);
        }
        TestTrue(TEXT("The Blueprint component connects through its public Connect path to Redweb"), Component->IsConnected());
        TestTrue(TEXT("The native connection callback is dispatched on the game thread"),
            PumpGameThreadUntil([Receiver]() { return Receiver->ConnectedCount > 0; }, 5.0));
        if (Component->IsConnected())
        {
            TestTrue(TEXT("The component sends through its connected native transport"),
                Component->SendRaw(TEXT("{\"type\":\"echo\",\"text\":\"component-connect\"}")));

            TestTrue(TEXT("The real Redweb response reaches the Blueprint raw-message event"),
                PumpGameThreadUntil([Receiver]()
                {
                    for (const FString& Message : Receiver->RawMessages)
                    {
                        if (Message.Contains(TEXT("component-connect"))) { return true; }
                    }
                    return false;
                }, 5.0));

            const int32 DisconnectedBeforeRemoteClose = Receiver->DisconnectedCount;
            TestTrue(TEXT("The component sends a remote-close request through the real server"),
                Component->SendRaw(TEXT("{\"type\":\"echo\",\"fixtureCommand\":\"close\"}")));
            TestTrue(TEXT("The remote-close callback reaches the Blueprint event"),
                PumpGameThreadUntil([Receiver, DisconnectedBeforeRemoteClose]()
                {
                    return Receiver->DisconnectedCount > DisconnectedBeforeRemoteClose;
                }, 5.0));
        }

        const int32 ErrorsBeforeRefusedConnection = Receiver->Errors.Num();
        Component->ServerUrl = TEXT("ws://127.0.0.1:1");
        Component->RoutePath = TEXT("/socket");
        AddExpectedError(TEXT("WebSocket connection error for ws://127.0.0.1:1/socket"), EAutomationExpectedErrorFlags::Contains, 1);
        Component->Connect();
        TestTrue(TEXT("A refused local connection is delivered through the asynchronous error callback"),
            PumpGameThreadUntil([Receiver, ErrorsBeforeRefusedConnection]()
            {
                return Receiver->Errors.Num() > ErrorsBeforeRefusedConnection;
            }, 5.0));
        Component->Disconnect();
        TestFalse(TEXT("Disconnect synchronously clears the component connection"), Component->IsConnected());
    }

    Component->ServerUrl = TEXT("ws://example.test/");
    Component->RoutePath = TEXT("/room");
    FRedwebKeyValue Query;
    Query.Key = TEXT("room name");
    Query.Value = TEXT("a&b");
    Component->QueryParams.Add(Query);
    TestEqual(TEXT("URL construction encodes query keys and values"), Component->BuildFullUrl(),
        FString(TEXT("ws://example.test/room?room%20name=a%26b")));
    TestFalse(TEXT("The disconnected component reports no open socket"), Component->IsConnected());
    TestFalse(TEXT("Unframed JSON fails cleanly without a connected socket"), Component->SendJson(TEXT("{}"), FString()));
    TestFalse(TEXT("Valid typed JSON fails cleanly without a connected socket"), Component->SendJson(TEXT("{}"), TEXT("event")));
    TestFalse(TEXT("Malformed typed JSON still fails cleanly without a connected socket"),
        Component->SendJson(TEXT("plain"), TEXT("event")));
    TArray<FRedwebKeyValue> NoSendFields;
    TestFalse(TEXT("Typed fields fail cleanly without a connected socket"),
        Component->SendTypedFields(TEXT("event"), NoSendFields));

    AddExpectedError(TEXT("Unknown connection error"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("connection refused"), EAutomationExpectedErrorFlags::Contains, 1);
    const int32 ErrorsBeforeDirectCallbacks = Receiver->Errors.Num();
    Component->HandleSocketConnectionError(FString());
    Component->HandleSocketConnectionError(TEXT("connection refused"));
    const int32 DisconnectionsBeforeCloseCases = Receiver->DisconnectedCount;
    Component->HandleSocketClosed(1006, FString(), false);
    Component->HandleSocketClosed(1000, TEXT("finished"), true);
    Component->bIntentionalDisconnect = true;
    Component->HandleSocketClosed(1001, TEXT("intentional"), true);
    TestFalse(TEXT("A close clears the component's socket"), Component->Socket.IsValid());
    TestEqual(TEXT("The disconnection delegate receives every close"), Receiver->DisconnectedCount, DisconnectionsBeforeCloseCases + 3);
    TestEqual(TEXT("The error delegate receives unknown and transport errors"), Receiver->Errors.Num(), ErrorsBeforeDirectCallbacks + 2);

    FRedwebNativeSocket::FCallbacks TimeoutCallbacks;
    Component->Socket = MakeShared<FRedwebNativeSocket, ESPMode::ThreadSafe>(Component->BuildFullUrl(), MoveTemp(TimeoutCallbacks));
    AddExpectedError(TEXT("WebSocket connection timed out after 15 seconds"), EAutomationExpectedErrorFlags::Contains, 1);
    Component->HandleConnectionTimeout();
    TestFalse(TEXT("Timeout cleanup discards the disconnected native transport"), Component->Socket.IsValid());

    Component->bLogReceivedMessages = true;
    Component->bLogReceivedPayloads = false;
    const FString TypedMessage = TEXT("{\"type\":\"typed-event\",\"value\":2}");
    Component->DispatchRawAndTypedMessage(TypedMessage);
    TestEqual(TEXT("Raw listeners receive the unchanged full message"), Receiver->RawMessages.Last(), TypedMessage);
    TestEqual(TEXT("Typed listeners receive the type field separately"), Receiver->MessageTypes.Last(), FString(TEXT("typed-event")));
    TSharedPtr<FJsonObject> TypedPayloadObject;
    const TSharedRef<TJsonReader<>> TypedPayloadReader = TJsonReaderFactory<>::Create(Receiver->TypedPayloads.Last());
    TestTrue(TEXT("Typed listeners receive a valid JSON payload"),
        FJsonSerializer::Deserialize(TypedPayloadReader, TypedPayloadObject) && TypedPayloadObject.IsValid());
    if (TypedPayloadObject.IsValid())
    {
        TestFalse(TEXT("Typed payload omits the routing type"), TypedPayloadObject->HasField(TEXT("type")));
        TestEqual(TEXT("Typed payload retains its application data"), TypedPayloadObject->GetNumberField(TEXT("value")), 2.0);
    }

    Component->DispatchRawAndTypedMessage(TEXT("{\"type\":\"tick\",\"timestamp\":1}"));
    Component->bLogReceivedPayloads = true;
    Component->DispatchRawAndTypedMessage(TEXT("{\"type\":\"tick\",\"timestamp\":1}"));
    Component->DispatchRawAndTypedMessage(TEXT("{\"type\":\"tick\"}"));
    Component->DispatchRawAndTypedMessage(TEXT("not-json"));
    Component->HandleSocketMessage(TEXT("{\"type\":\"immediate\"}"));

    Component->bQueueIncomingMessages = true;
    Component->MaxMessagesPerFrame = 1;
    Component->PendingMessages.Add(TEXT("{\"type\":\"first\"}"));
    Component->PendingMessages.Add(TEXT("{\"type\":\"second\"}"));
    TestEqual(TEXT("Incoming messages are queued when frame batching is enabled"), Component->PendingMessages.Num(), 2);
    Component->FlushPendingMessages();
    TestEqual(TEXT("A frame flush respects the configured message limit"), Component->PendingMessages.Num(), 1);
    Component->MaxMessagesPerFrame = 0;
    Component->FlushPendingMessages();
    TestEqual(TEXT("A zero message limit drains the remaining queue"), Component->PendingMessages.Num(), 0);
    Component->FlushPendingMessages();

    Component->HeartbeatMessage = TEXT("heartbeat");
    Component->SendHeartbeat();
    Component->HeartbeatMessage.Empty();
    Component->SendHeartbeat();
    Component->HeartbeatMessage = TEXT("heartbeat");
    Component->HeartbeatIntervalSeconds = 60.0f;
    Component->StartHeartbeat();
    TestTrue(TEXT("A configured heartbeat creates a world timer"),
        World->GetTimerManager().IsTimerActive(Component->HeartbeatTimerHandle));
    Component->StopHeartbeat();
    Component->bQueueIncomingMessages = true;
    Component->HandleSocketMessage(TEXT("{\"type\":\"queued-one\"}"));
    Component->HandleSocketMessage(TEXT("{\"type\":\"queued-two\"}"));
    TestEqual(TEXT("Queued callbacks append messages to the component queue"), Component->PendingMessages.Num(), 2);
    Component->MaxMessagesPerFrame = 1;
    Component->TickComponent(0.016f, LEVELTICK_All, &Component->PrimaryComponentTick);
    TestEqual(TEXT("The registered component flushes one queued message per tick"), Component->PendingMessages.Num(), 1);
    Component->MaxMessagesPerFrame = 0;
    Component->TickComponent(0.016f, LEVELTICK_All, &Component->PrimaryComponentTick);
    TestEqual(TEXT("A zero limit flushes all remaining messages on the next tick"), Component->PendingMessages.Num(), 0);

    Component->HandleConnectionTimeout();
    Component->bIntentionalDisconnect = false;
    Component->bAutoReconnect = true;
    Component->ReconnectDelaySeconds = 0.0f;
    Component->ScheduleReconnect();
    TestTrue(TEXT("A transient disconnect schedules the minimum-delay reconnect"),
        World->GetTimerManager().IsTimerActive(Component->ReconnectTimerHandle));
    World->GetTimerManager().ClearTimer(Component->ReconnectTimerHandle);
    Component->bIntentionalDisconnect = true;
    Component->bAutoReconnect = false;
    Component->StartConnectionTimeout();
    const int32 DisconnectionsBeforeExplicitDisconnect = Receiver->DisconnectedCount;
    Component->Disconnect();
    TestEqual(TEXT("Explicit disconnect notifies Blueprint listeners"),
        Receiver->DisconnectedCount, DisconnectionsBeforeExplicitDisconnect + 1);
    Component->EndPlay(EEndPlayReason::Destroyed);
    TestFalse(TEXT("EndPlay clears messages and leaves the connection stopped"), Component->IsConnected());
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
