#if WITH_DEV_AUTOMATION_TESTS

#include "RedwebSocketComponent.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRedwebLegacyCodecAutomationTest,
    "RedwebBP.Unit.LegacyWireCodec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRedwebLegacyCodecAutomationTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Invalid raw data remains untouched for server-side diagnostics"),
        URedwebSocketComponent::UpgradeLegacyMessage(TEXT("not-json")), FString(TEXT("not-json")));
    TestEqual(TEXT("JSON without a message type remains untouched"),
        URedwebSocketComponent::UpgradeLegacyMessage(TEXT("{\"value\":1}")), FString(TEXT("{\"value\":1}")));
    const FString CurrentEnvelope = TEXT("{\"v\":\"1\",\"type\":\"move\",\"payload\":{\"cell\":3}}");
    TestEqual(TEXT("A current protocol envelope is not rewritten"),
        URedwebSocketComponent::UpgradeLegacyMessage(CurrentEnvelope), CurrentEnvelope);
    const FString ProtocolErrorEnvelope = TEXT("{\"v\":\"1\",\"type\":\"error\",\"error\":{\"code\":\"DENIED\",\"message\":\"denied\"}}");
    TestEqual(TEXT("A current protocol error envelope is not rewritten"),
        URedwebSocketComponent::UpgradeLegacyMessage(ProtocolErrorEnvelope), ProtocolErrorEnvelope);

    FString Type;
    FString Payload;

    TestFalse(TEXT("Malformed JSON is not a typed message"),
        URedwebSocketComponent::ExtractTypedPayload(TEXT("{"), Type, Payload));
    TestFalse(TEXT("JSON arrays are not typed messages"),
        URedwebSocketComponent::ExtractTypedPayload(TEXT("[]"), Type, Payload));
    TestFalse(TEXT("Objects without a type are not typed messages"),
        URedwebSocketComponent::ExtractTypedPayload(TEXT("{\"value\":1}"), Type, Payload));

    TestTrue(TEXT("A legacy typed message is recognized"),
        URedwebSocketComponent::ExtractTypedPayload(TEXT("{\"type\":\"move\",\"cell\":3}"), Type, Payload));
    TestEqual(TEXT("The message type is extracted"), Type, FString(TEXT("move")));

    TSharedPtr<FJsonObject> PayloadObject;
    const TSharedRef<TJsonReader<>> PayloadReader = TJsonReaderFactory<>::Create(Payload);
    TestTrue(TEXT("The extracted payload is valid JSON"),
        FJsonSerializer::Deserialize(PayloadReader, PayloadObject) && PayloadObject.IsValid());
    if (PayloadObject.IsValid())
    {
        TestFalse(TEXT("The type is not repeated inside the extracted payload"), PayloadObject->HasField(TEXT("type")));
        TestEqual(TEXT("Other fields remain in the extracted payload"), PayloadObject->GetIntegerField(TEXT("cell")), 3);
    }

    TArray<FRedwebKeyValue> NoFields;
    const FString EmptyMessage = URedwebSocketComponent::BuildJsonFromFields(FString(), NoFields);
    TSharedPtr<FJsonObject> EmptyObject;
    const TSharedRef<TJsonReader<>> EmptyReader = TJsonReaderFactory<>::Create(EmptyMessage);
    TestTrue(TEXT("An empty legacy message serializes as a JSON object"),
        FJsonSerializer::Deserialize(EmptyReader, EmptyObject) && EmptyObject.IsValid());
    if (EmptyObject.IsValid())
    {
        TestEqual(TEXT("An empty type is omitted"), EmptyObject->Values.Num(), 0);
    }

    TArray<FRedwebKeyValue> Fields;
    FRedwebKeyValue TextField;
    TextField.Key = TEXT("text");
    TextField.Value = TEXT("hello \"redweb\"");
    Fields.Add(TextField);
    FRedwebKeyValue EmptyKey;
    EmptyKey.Value = TEXT("ignored");
    Fields.Add(EmptyKey);

    const FString TypedMessage = URedwebSocketComponent::BuildJsonFromFields(TEXT("echo"), Fields);
    TSharedPtr<FJsonObject> TypedObject;
    const TSharedRef<TJsonReader<>> TypedReader = TJsonReaderFactory<>::Create(TypedMessage);
    TestTrue(TEXT("A legacy typed message serializes as a JSON object"),
        FJsonSerializer::Deserialize(TypedReader, TypedObject) && TypedObject.IsValid());
    if (TypedObject.IsValid())
    {
        TestEqual(TEXT("The legacy type field is serialized"), TypedObject->GetStringField(TEXT("type")), FString(TEXT("echo")));
        TestEqual(TEXT("String values survive JSON escaping"), TypedObject->GetStringField(TEXT("text")), TextField.Value);
        TestFalse(TEXT("Fields with an empty key are omitted"), TypedObject->HasField(TEXT("")));
    }

    TestTrue(TEXT("A protocol v1 envelope is recognized"), URedwebSocketComponent::ExtractTypedPayload(
        TEXT("{\"v\":\"1\",\"type\":\"move\",\"payload\":{\"cell\":3}}"), Type, Payload));
    TestEqual(TEXT("The envelope type is exposed to typed listeners"), Type, FString(TEXT("move")));
    TSharedPtr<FJsonObject> VersionedPayloadObject;
    const TSharedRef<TJsonReader<>> VersionedPayloadReader = TJsonReaderFactory<>::Create(Payload);
    TestTrue(TEXT("Typed listeners receive the envelope payload without protocol fields"),
        FJsonSerializer::Deserialize(VersionedPayloadReader, VersionedPayloadObject) && VersionedPayloadObject.IsValid());
    if (VersionedPayloadObject.IsValid())
    {
        TestEqual(TEXT("The protocol payload keeps its application fields"), VersionedPayloadObject->GetIntegerField(TEXT("cell")), 3);
        TestFalse(TEXT("The protocol envelope version is not leaked into the payload"), VersionedPayloadObject->HasField(TEXT("v")));
    }

    TestTrue(TEXT("Protocol envelopes can carry scalar payloads"), URedwebSocketComponent::ExtractTypedPayload(
        TEXT("{\"v\":\"1\",\"type\":\"status\",\"payload\":\"ready\"}"), Type, Payload));
    TestEqual(TEXT("Scalar payload JSON is preserved as a root value"), Payload, FString(TEXT("\"ready\"")));

    TestTrue(TEXT("Protocol errors are decoded as typed Redweb errors"), URedwebSocketComponent::ExtractTypedPayload(
        TEXT("{\"v\":\"1\",\"type\":\"error\",\"error\":{\"code\":\"UNKNOWN_HANDLER\",\"message\":\"Unknown handler\"}}"),
        Type, Payload));
    TestEqual(TEXT("The protocol error type is preserved"), Type, FString(TEXT("error")));
    TSharedPtr<FJsonObject> ErrorPayloadObject;
    const TSharedRef<TJsonReader<>> ErrorPayloadReader = TJsonReaderFactory<>::Create(Payload);
    TestTrue(TEXT("The protocol error details are converted to JSON"),
        FJsonSerializer::Deserialize(ErrorPayloadReader, ErrorPayloadObject) && ErrorPayloadObject.IsValid());
    if (ErrorPayloadObject.IsValid())
    {
        TestEqual(TEXT("The protocol error code survives decoding"), ErrorPayloadObject->GetStringField(TEXT("code")), FString(TEXT("UNKNOWN_HANDLER")));
    }

    TestFalse(TEXT("Unknown protocol versions are rejected by the typed decoder"), URedwebSocketComponent::ExtractTypedPayload(
        TEXT("{\"v\":\"2\",\"type\":\"move\",\"payload\":{}}"), Type, Payload));
    TestFalse(TEXT("Malformed protocol errors without error details are rejected"), URedwebSocketComponent::ExtractTypedPayload(
        TEXT("{\"v\":\"1\",\"type\":\"error\"}"), Type, Payload));
    TestFalse(TEXT("Protocol messages without payloads are rejected"), URedwebSocketComponent::ExtractTypedPayload(
        TEXT("{\"v\":\"1\",\"type\":\"move\"}"), Type, Payload));

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
