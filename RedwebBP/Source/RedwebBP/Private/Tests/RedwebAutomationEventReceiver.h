#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"

#include "RedwebAutomationEventReceiver.generated.h"

UCLASS()
class URedwebAutomationEventReceiver : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void ReceiveConnected();

    UFUNCTION()
    void ReceiveDisconnected();

    UFUNCTION()
    void ReceiveError(const FString& Error);

    UFUNCTION()
    void ReceiveRawMessage(const FString& Message);

    UFUNCTION()
    void ReceiveTypedMessage(const FString& Type, const FString& PayloadJson);

    int32 ConnectedCount = 0;
    int32 DisconnectedCount = 0;
    TArray<FString> Errors;
    TArray<FString> RawMessages;
    TArray<FString> MessageTypes;
    TArray<FString> TypedPayloads;
};
