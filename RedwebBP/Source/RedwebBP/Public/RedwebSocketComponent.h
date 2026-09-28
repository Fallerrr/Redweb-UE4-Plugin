#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "RedwebSocketComponent.generated.h"

class FRedwebNativeSocket;

USTRUCT(BlueprintType)
struct FRedwebKeyValue
{
    GENERATED_USTRUCT_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb")
    FString Key;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb")
    FString Value;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRedwebConnectedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRedwebErrorEvent, const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRedwebRawMessageEvent, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRedwebTypedMessageEvent, const FString&, Type, const FString&, PayloadJson);

UCLASS(ClassGroup=Networking, meta=(BlueprintSpawnableComponent))
class REDWEBBP_API URedwebSocketComponent : public UActorComponent
{
    GENERATED_UCLASS_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Connection")
    FString ServerUrl;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Connection")
    FString RoutePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Connection")
    TArray<FRedwebKeyValue> QueryParams;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Connection")
    bool bAutoConnect;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Connection")
    bool bAutoReconnect;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Connection")
    float ReconnectDelaySeconds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Heartbeat")
    float HeartbeatIntervalSeconds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Heartbeat")
    FString HeartbeatMessage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Performance")
    bool bQueueIncomingMessages;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Performance", meta=(ClampMin="0"))
    int32 MaxMessagesPerFrame;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Debug")
    bool bLogReceivedMessages;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Redweb|Debug")
    bool bLogReceivedPayloads;

    UPROPERTY(BlueprintAssignable, Category="Redweb|Events")
    FRedwebConnectedEvent OnConnected;

    UPROPERTY(BlueprintAssignable, Category="Redweb|Events")
    FRedwebConnectedEvent OnDisconnected;

    UPROPERTY(BlueprintAssignable, Category="Redweb|Events")
    FRedwebErrorEvent OnError;

    UPROPERTY(BlueprintAssignable, Category="Redweb|Events")
    FRedwebRawMessageEvent OnRawMessage;

    UPROPERTY(BlueprintAssignable, Category="Redweb|Events")
    FRedwebTypedMessageEvent OnTypedMessage;

    UFUNCTION(BlueprintCallable, Category="Redweb|Connection")
    void Connect();

    UFUNCTION(BlueprintCallable, Category="Redweb|Connection")
    void Disconnect();

    UFUNCTION(BlueprintCallable, Category="Redweb|Send")
    bool SendRaw(const FString& Message);

    UFUNCTION(BlueprintCallable, Category="Redweb|Send")
    bool SendJson(const FString& JsonPayload, const FString& Type);

    UFUNCTION(BlueprintCallable, Category="Redweb|Send")
    bool SendTypedFields(const FString& Type, const TArray<FRedwebKeyValue>& Fields);

    UFUNCTION(BlueprintPure, Category="Redweb|Connection")
    bool IsConnected() const;

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UFUNCTION()
    void SendHeartbeat();

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FRedwebLegacyCodecAutomationTest;
    friend class FRedwebNativeTransportIntegrationTest;
#endif

    TSharedPtr<FRedwebNativeSocket, ESPMode::ThreadSafe> Socket;
    FTimerHandle ReconnectTimerHandle;
    FTimerHandle HeartbeatTimerHandle;
    FTimerHandle ConnectionTimeoutTimerHandle;
    TArray<FString> PendingMessages;
    FCriticalSection PendingMessagesLock;
    bool bIntentionalDisconnect;
    int32 ConnectionGeneration;

    FString BuildFullUrl() const;
    void StartSocket();
    void StopSocket();
    void ScheduleReconnect();
    void StartHeartbeat();
    void StopHeartbeat();
    void StartConnectionTimeout();
    void StopConnectionTimeout();
    void HandleConnectionTimeout();
    void HandleSocketConnected();
    void HandleSocketConnectionError(const FString& Error);
    void HandleSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
    void HandleSocketMessage(const FString& Message);
    void FlushPendingMessages();
    void DispatchRawAndTypedMessage(const FString& Message);

    static bool ExtractTypedPayload(const FString& InMessage, FString& OutType, FString& OutPayloadJson);
    static FString BuildJsonFromFields(const FString& Type, const TArray<FRedwebKeyValue>& Fields);
};
