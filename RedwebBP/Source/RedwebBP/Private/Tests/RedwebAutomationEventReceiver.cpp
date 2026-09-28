#include "RedwebAutomationEventReceiver.h"

void URedwebAutomationEventReceiver::ReceiveConnected()
{
    ++ConnectedCount;
}

void URedwebAutomationEventReceiver::ReceiveDisconnected()
{
    ++DisconnectedCount;
}

void URedwebAutomationEventReceiver::ReceiveError(const FString& Error)
{
    Errors.Add(Error);
}

void URedwebAutomationEventReceiver::ReceiveRawMessage(const FString& Message)
{
    RawMessages.Add(Message);
}

void URedwebAutomationEventReceiver::ReceiveTypedMessage(const FString& Type, const FString& PayloadJson)
{
    MessageTypes.Add(Type);
    TypedPayloads.Add(PayloadJson);
}
