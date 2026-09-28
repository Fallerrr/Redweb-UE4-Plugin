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

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
