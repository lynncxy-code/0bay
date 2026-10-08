#include "Core/StateFrameParser.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FStateFrameParser::Parse(const FString& Json, FStateFrame& Out)
{
    Out = FStateFrame();
    // 先验证协议 envelope。JsonObjectConverter 对缺失字段会静默使用默认值，
    // 若把 {} 当成空帧，TargetSyncManager 会误回收当前所有临时人物。
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
    if (!Root->TryGetArrayField(TEXT("targets"), Targets) || Targets == nullptr)
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectStringToUStruct<FStateFrame>(Json, &Out, 0, 0);
}
