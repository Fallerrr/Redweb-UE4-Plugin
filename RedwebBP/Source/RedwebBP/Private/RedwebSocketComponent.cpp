#include "RedwebSocketComponent.h"
#include "RedwebNativeSocket.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

#if REDWEBBP_NATIVE_COVERAGE
DEFINE_LOG_CATEGORY_STATIC(LogRedwebBP, VeryVerbose, VeryVerbose);
#else
DEFINE_LOG_CATEGORY_STATIC(LogRedwebBP, Log, VeryVerbose);
#endif

URedwebSocketComponent::URedwebSocketComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , ServerUrl(TEXT("ws://127.0.0.1:3000"))
    , RoutePath(TEXT("/socket"))
    , bAutoConnect(true)
    , bAutoReconnect(true)
    , ReconnectDelaySeconds(2.0f)
    , HeartbeatIntervalSeconds(0.0f)
    , HeartbeatMessage(TEXT(""))
    , bQueueIncomingMessages(false)
    , MaxMessagesPerFrame(120)
    , bLogReceivedMessages(false)
    , bLogReceivedPayloads(false)
    , bIntentionalDisconnect(false)
    , ConnectionGeneration(0)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void URedwebSocketComponent::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoConnect)
    {
        Connect();
    }

    SetComponentTickEnabled(bQueueIncomingMessages);
}

void URedwebSocketComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bIntentionalDisconnect = true;
    StopHeartbeat();
    StopSocket();
    {
        FScopeLock Lock(&PendingMessagesLock);
        PendingMessages.Empty();
    }
    Super::EndPlay(EndPlayReason);
}

void URedwebSocketComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    FlushPendingMessages();
}

void URedwebSocketComponent::Connect()
{
    bIntentionalDisconnect = false;

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(ReconnectTimerHandle);
    }

    StartSocket();
}

void URedwebSocketComponent::Disconnect()
{
    bIntentionalDisconnect = true;
    StopHeartbeat();
    StopSocket();
    OnDisconnected.Broadcast();
}

bool URedwebSocketComponent::SendRaw(const FString& Message)
{
    if (!Socket.IsValid() || !Socket->IsConnected())
    {
        UE_LOG(LogRedwebBP, Warning, TEXT("Redweb send failed because socket is not connected."));
        return false;
    }

    return Socket->Send(Message);
}

bool URedwebSocketComponent::SendJson(const FString& JsonPayload, const FString& Type)
{
    if (Type.IsEmpty())
    {
        return SendRaw(JsonPayload);
    }

    TSharedPtr<FJsonObject> Obj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonPayload);
    if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
    {
        Obj = MakeShareable(new FJsonObject());
        Obj->SetStringField(TEXT("data"), JsonPayload);
    }

    Obj->SetStringField(TEXT("type"), Type);

    FString Out;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    return SendRaw(Out);
}

bool URedwebSocketComponent::SendTypedFields(const FString& Type, const TArray<FRedwebKeyValue>& Fields)
{
    return SendRaw(BuildJsonFromFields(Type, Fields));
}

bool URedwebSocketComponent::IsConnected() const
{
    return Socket.IsValid() && Socket->IsConnected();
}

void URedwebSocketComponent::SendHeartbeat()
{
    if (!HeartbeatMessage.IsEmpty())
    {
        SendRaw(HeartbeatMessage);
    }
}

FString URedwebSocketComponent::BuildFullUrl() const
{
    FString Base = ServerUrl;
    while (Base.EndsWith(TEXT("/")))
    {
        Base = Base.LeftChop(1);
    }

    FString Path = RoutePath;
    if (!Path.IsEmpty() && !Path.StartsWith(TEXT("/")))
    {
        Path = TEXT("/") + Path;
    }

    FString Url = Base + Path;

    if (QueryParams.Num() > 0)
    {
        TArray<FString> Pairs;
        for (const FRedwebKeyValue& QueryParam : QueryParams)
        {
            if (!QueryParam.Key.IsEmpty())
            {
                Pairs.Add(
                    FGenericPlatformHttp::UrlEncode(QueryParam.Key) +
                    TEXT("=") +
                    FGenericPlatformHttp::UrlEncode(QueryParam.Value)
                );
            }
        }

        if (Pairs.Num() > 0)
        {
            Url += TEXT("?") + FString::Join(Pairs, TEXT("&"));
        }
    }

    return Url;
}

void URedwebSocketComponent::StartSocket()
{
    StopSocket();

    const int32 NewConnectionGeneration = ++ConnectionGeneration;

    const FString FullUrl = BuildFullUrl();
    if (!FullUrl.StartsWith(TEXT("ws://"), ESearchCase::IgnoreCase) &&
        !FullUrl.StartsWith(TEXT("wss://"), ESearchCase::IgnoreCase))
    {
        const FString Error = FString::Printf(TEXT("WebSocket URL must begin with ws:// or wss://: %s"), *FullUrl);
        UE_LOG(LogRedwebBP, Error, TEXT("%s"), *Error);
        OnError.Broadcast(Error);
        return;
    }

    UE_LOG(LogRedwebBP, Log, TEXT("Redweb connecting: %s"), *FullUrl);

    const TWeakObjectPtr<URedwebSocketComponent> WeakThis(this);
    FRedwebNativeSocket::FCallbacks Callbacks;
    Callbacks.OnConnected = [WeakThis, NewConnectionGeneration]()
    {
        AsyncTask(ENamedThreads::GameThread, [WeakThis, NewConnectionGeneration]()
        {
            if (WeakThis.IsValid() && WeakThis->ConnectionGeneration == NewConnectionGeneration)
            {
                WeakThis->HandleSocketConnected();
            }
        });
    };
    Callbacks.OnError = [WeakThis, NewConnectionGeneration](const FString& Error)
    {
        const FString ErrorCopy = Error;
        AsyncTask(ENamedThreads::GameThread, [WeakThis, NewConnectionGeneration, ErrorCopy]()
        {
            if (WeakThis.IsValid() && WeakThis->ConnectionGeneration == NewConnectionGeneration)
            {
                WeakThis->HandleSocketConnectionError(ErrorCopy);
            }
        });
    };
    Callbacks.OnMessage = [WeakThis, NewConnectionGeneration](const FString& Message)
    {
        const FString MessageCopy = Message;
        AsyncTask(ENamedThreads::GameThread, [WeakThis, NewConnectionGeneration, MessageCopy]()
        {
            if (WeakThis.IsValid() && WeakThis->ConnectionGeneration == NewConnectionGeneration)
            {
                WeakThis->HandleSocketMessage(MessageCopy);
            }
        });
    };
    Callbacks.OnClosed = [WeakThis, NewConnectionGeneration](const int32 StatusCode, const FString& Reason, const bool bWasClean)
    {
        const FString ReasonCopy = Reason;
        AsyncTask(ENamedThreads::GameThread, [WeakThis, NewConnectionGeneration, StatusCode, ReasonCopy, bWasClean]()
        {
            if (WeakThis.IsValid() && WeakThis->ConnectionGeneration == NewConnectionGeneration)
            {
                WeakThis->HandleSocketClosed(StatusCode, ReasonCopy, bWasClean);
            }
        });
    };

    Socket = MakeShared<FRedwebNativeSocket, ESPMode::ThreadSafe>(FullUrl, MoveTemp(Callbacks));
    if (!Socket->Start())
    {
        const FString Error = FString::Printf(TEXT("Could not start the native WebSocket worker for %s"), *FullUrl);
        UE_LOG(LogRedwebBP, Error, TEXT("%s"), *Error);
        Socket.Reset();
        OnError.Broadcast(Error);
        ScheduleReconnect();
        return;
    }

    StartConnectionTimeout();
}

void URedwebSocketComponent::StopSocket()
{
    StopConnectionTimeout();
    ++ConnectionGeneration;

    if (!Socket.IsValid())
    {
        return;
    }

    Socket->Shutdown();
    Socket.Reset();

    FScopeLock Lock(&PendingMessagesLock);
    PendingMessages.Empty();
}

void URedwebSocketComponent::ScheduleReconnect()
{
    if (bIntentionalDisconnect || !bAutoReconnect || !GetWorld())
    {
        return;
    }

    GetWorld()->GetTimerManager().ClearTimer(ReconnectTimerHandle);
    GetWorld()->GetTimerManager().SetTimer(
        ReconnectTimerHandle,
        this,
        &URedwebSocketComponent::Connect,
        ReconnectDelaySeconds > 0.1f ? ReconnectDelaySeconds : 0.1f,
        false
    );
}

void URedwebSocketComponent::StartHeartbeat()
{
    if (!GetWorld() || HeartbeatIntervalSeconds <= 0.f || HeartbeatMessage.IsEmpty())
    {
        return;
    }

    GetWorld()->GetTimerManager().ClearTimer(HeartbeatTimerHandle);
    GetWorld()->GetTimerManager().SetTimer(
        HeartbeatTimerHandle,
        this,
        &URedwebSocketComponent::SendHeartbeat,
        HeartbeatIntervalSeconds,
        true
    );
}

void URedwebSocketComponent::StopHeartbeat()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(HeartbeatTimerHandle);
    }
}

void URedwebSocketComponent::StartConnectionTimeout()
{
    if (!GetWorld())
    {
        return;
    }

    GetWorld()->GetTimerManager().ClearTimer(ConnectionTimeoutTimerHandle);
    GetWorld()->GetTimerManager().SetTimer(
        ConnectionTimeoutTimerHandle,
        this,
        &URedwebSocketComponent::HandleConnectionTimeout,
        15.0f,
        false
    );
}

void URedwebSocketComponent::StopConnectionTimeout()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(ConnectionTimeoutTimerHandle);
    }
}

void URedwebSocketComponent::HandleConnectionTimeout()
{
    if (Socket.IsValid() && !Socket->IsConnected())
    {
        const FString Error = FString::Printf(
            TEXT("WebSocket connection timed out after 15 seconds. Attempted URL: %s"),
            *BuildFullUrl()
        );

        UE_LOG(LogRedwebBP, Error, TEXT("%s"), *Error);
        OnError.Broadcast(Error);
        StopSocket();
        ScheduleReconnect();
    }
}

void URedwebSocketComponent::HandleSocketConnected()
{
    StopConnectionTimeout();
    UE_LOG(LogRedwebBP, Log, TEXT("Redweb connected: %s"), *BuildFullUrl());
    OnConnected.Broadcast();
    StartHeartbeat();
}

void URedwebSocketComponent::HandleSocketConnectionError(const FString& Error)
{
    StopConnectionTimeout();
    StopHeartbeat();

    const FString DetailedError = FString::Printf(
        TEXT("WebSocket connection error for %s: %s"),
        *BuildFullUrl(),
        Error.IsEmpty() ? TEXT("Unknown connection error") : *Error
    );

    UE_LOG(LogRedwebBP, Error, TEXT("%s"), *DetailedError);
    OnError.Broadcast(DetailedError);
}

void URedwebSocketComponent::HandleSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    StopConnectionTimeout();
    StopHeartbeat();

    UE_LOG(
        LogRedwebBP,
        Warning,
        TEXT("Redweb closed: code=%d clean=%s reason=%s"),
        StatusCode,
        bWasClean ? TEXT("true") : TEXT("false"),
        Reason.IsEmpty() ? TEXT("<empty>") : *Reason
    );

    OnDisconnected.Broadcast();
    Socket.Reset();

    if (!bIntentionalDisconnect)
    {
        ScheduleReconnect();
    }
}

void URedwebSocketComponent::HandleSocketMessage(const FString& Message)
{
    if (bQueueIncomingMessages)
    {
        FScopeLock Lock(&PendingMessagesLock);
        PendingMessages.Add(Message);
        SetComponentTickEnabled(true);
        return;
    }

    DispatchRawAndTypedMessage(Message);
}

void URedwebSocketComponent::FlushPendingMessages()
{
    TArray<FString> MessagesToDispatch;
    {
        FScopeLock Lock(&PendingMessagesLock);
        const int32 MessageLimit = MaxMessagesPerFrame <= 0 ? PendingMessages.Num() : FMath::Min(MaxMessagesPerFrame, PendingMessages.Num());
        if (MessageLimit <= 0)
        {
            return;
        }

        MessagesToDispatch.Reserve(MessageLimit);
        for (int32 Index = 0; Index < MessageLimit; ++Index)
        {
            MessagesToDispatch.Add(MoveTemp(PendingMessages[Index]));
        }

        PendingMessages.RemoveAt(0, MessageLimit, false);
    }

    for (const FString& Message : MessagesToDispatch)
    {
        DispatchRawAndTypedMessage(Message);
    }
}

void URedwebSocketComponent::DispatchRawAndTypedMessage(const FString& Message)
{
    FString MessageTypeForLog = TEXT("<missing>");
    double ServerTimestampMs = 0.0;
    TSharedPtr<FJsonObject> MessageObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (FJsonSerializer::Deserialize(Reader, MessageObject) && MessageObject.IsValid())
    {
        MessageObject->TryGetStringField(TEXT("type"), MessageTypeForLog);
        MessageObject->TryGetNumberField(TEXT("timestamp"), ServerTimestampMs);
    }

    if (bLogReceivedMessages)
    {
        if (ServerTimestampMs > 0.0)
        {
            const FDateTime UnixEpoch(1970, 1, 1);
            const double ClientTimestampMs = (FDateTime::UtcNow() - UnixEpoch).GetTotalMilliseconds();
            if (bLogReceivedPayloads)
            {
                UE_LOG(
                    LogRedwebBP,
                    Log,
                    TEXT("Redweb received type=%s server_to_client_latency=%.0fms payload=%s"),
                    *MessageTypeForLog,
                    ClientTimestampMs - ServerTimestampMs,
                    *Message
                );
            }
            else
            {
                UE_LOG(
                    LogRedwebBP,
                    Verbose,
                    TEXT("Redweb received type=%s server_to_client_latency=%.0fms"),
                    *MessageTypeForLog,
                    ClientTimestampMs - ServerTimestampMs
                );
            }
        }
        else if (bLogReceivedPayloads)
        {
            UE_LOG(LogRedwebBP, Log, TEXT("Redweb received type=%s payload=%s"), *MessageTypeForLog, *Message);
        }
        else
        {
            UE_LOG(LogRedwebBP, Verbose, TEXT("Redweb received type=%s"), *MessageTypeForLog);
        }
    }

    if (OnRawMessage.IsBound())
    {
        OnRawMessage.Broadcast(Message);
    }

    if (OnTypedMessage.IsBound() && MessageObject.IsValid() && MessageObject->HasField(TEXT("type")))
    {
        const FString Type = MessageObject->GetStringField(TEXT("type"));
        MessageObject->RemoveField(TEXT("type"));

        FString PayloadJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadJson);
        FJsonSerializer::Serialize(MessageObject.ToSharedRef(), Writer);
        OnTypedMessage.Broadcast(Type, PayloadJson);
    }
}

bool URedwebSocketComponent::ExtractTypedPayload(const FString& InMessage, FString& OutType, FString& OutPayloadJson)
{
    OutType.Empty();
    OutPayloadJson = InMessage;

    TSharedPtr<FJsonObject> Obj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InMessage);
    if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
    {
        return false;
    }

    if (!Obj->HasField(TEXT("type")))
    {
        return false;
    }

    OutType = Obj->GetStringField(TEXT("type"));
    Obj->RemoveField(TEXT("type"));

    OutPayloadJson.Empty();
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutPayloadJson);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    return true;
}

FString URedwebSocketComponent::BuildJsonFromFields(const FString& Type, const TArray<FRedwebKeyValue>& Fields)
{
    TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
    if (!Type.IsEmpty())
    {
        Obj->SetStringField(TEXT("type"), Type);
    }

    for (const FRedwebKeyValue& Field : Fields)
    {
        if (!Field.Key.IsEmpty())
        {
            Obj->SetStringField(Field.Key, Field.Value);
        }
    }

    FString Json;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    return Json;
}
