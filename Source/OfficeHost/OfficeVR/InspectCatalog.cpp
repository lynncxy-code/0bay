#include "OfficeVR/InspectCatalog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace OfficeVRCatalogPrivate
{
	FString DefaultMaintenanceCycle() { return TEXT("12个月（示例）"); }
	FString DefaultNextMaintenance() { return TEXT("2027-03-15（示例）"); }
	FString DefaultSystem() { return TEXT("办公家具"); }

	FInspectableItemData MakeItem(
		int32 Id,
		const FString& Name,
		const FString& Room,
		const FString& Dimensions,
		const FString& Material,
		const FString& Description,
		const TArray<FString>& ExtraKeys = {})
	{
		FInspectableItemData Item;
		Item.CatalogId = Id;
		Item.DisplayName = Name;
		Item.Category = UInspectCatalog::InferCategory(Name);
		Item.Room = Room.IsEmpty() ? TEXT("办公室") : Room;
		Item.Dimensions = Dimensions.IsEmpty() ? TEXT("待现场复核") : Dimensions;
		Item.Material = Material.IsEmpty() ? TEXT("面层按场景近似，未对色卡") : Material;
		Item.AssetId = FString::Printf(TEXT("OFC-%03d"), Id);
		Item.SystemName = DefaultSystem();
		Item.MaintenanceCycle = DefaultMaintenanceCycle();
		Item.NextMaintenance = DefaultNextMaintenance();
		Item.Description = Description;
		Item.MatchKeys = ExtraKeys;
		if (Id > 0)
		{
			Item.MatchKeys.Add(FString::Printf(TEXT("PDF_%02d"), Id));
			Item.MatchKeys.Add(Name);
		}
		return Item;
	}

	void AppendBuiltin(TArray<FInspectableItemData>& Out)
	{
		Out.Add(MakeItem(
			101,
			TEXT("会议室电视"),
			TEXT("接待区 / 会议室"),
			TEXT("约 122 × 14 × 81 cm"),
			TEXT("塑料壳体 + 玻璃屏（CC0 场景资产）"),
			TEXT("普通无支架电视，用于会议投屏示意。一期仅支持查看属性。"),
			{TEXT("SM_TV_Plain"), TEXT("TV_Plain_NoStand"), TEXT("电视"), TEXT("tv-stand"), TEXT("TV")}));
		Out.Add(MakeItem(
			102,
			TEXT("双人电话亭"),
			TEXT("打印茶水区"),
			TEXT("约 1600 mm 宽封闭舱"),
			TEXT("玻璃 + 织物 + 金属型材"),
			TEXT("隔音通话舱。一期仅支持查看属性，不可开关门。"),
			{TEXT("PhoneBooth"), TEXT("电话亭"), TEXT("PB26")}));
		Out.Add(MakeItem(
			103,
			TEXT("绿萝盆栽"),
			TEXT("公共区域"),
			TEXT("盆栽尺度"),
			TEXT("植物 + 陶盆"),
			TEXT("场景绿化点缀。"),
			{TEXT("Pothos"), TEXT("绿萝")}));
	}
}

FString UInspectCatalog::InferCategory(const FString& DisplayName)
{
	if (DisplayName.Contains(TEXT("电视")) || DisplayName.Contains(TEXT("TV")) || DisplayName.Contains(TEXT("冰箱")))
	{
		return TEXT("电器");
	}
	if (DisplayName.Contains(TEXT("椅")) || DisplayName.Contains(TEXT("沙发")) || DisplayName.Contains(TEXT("凳")))
	{
		return TEXT("座椅");
	}
	if (DisplayName.Contains(TEXT("桌")) || DisplayName.Contains(TEXT("几")) || DisplayName.Contains(TEXT("吧台")))
	{
		return TEXT("桌台");
	}
	if (DisplayName.Contains(TEXT("柜")) || DisplayName.Contains(TEXT("文件")))
	{
		return TEXT("储物");
	}
	if (DisplayName.Contains(TEXT("电话亭")))
	{
		return TEXT("隔间");
	}
	if (DisplayName.Contains(TEXT("植物")) || DisplayName.Contains(TEXT("绿萝")))
	{
		return TEXT("绿化");
	}
	return TEXT("家具");
}

FString UInspectCatalog::FormatDimensionsMm(const TArray<double>& Mm)
{
	if (Mm.Num() < 3)
	{
		return TEXT("待现场复核");
	}
	return FString::Printf(TEXT("%.0f × %.0f × %.0f mm"), Mm[0], Mm[1], Mm[2]);
}

FInspectableItemData UInspectCatalog::MakeHelpPlaque()
{
	FInspectableItemData Item;
	Item.CatalogId = 0;
	Item.DisplayName = TEXT("办公室 VR 操作说明");
	Item.Category = TEXT("系统");
	Item.Room = TEXT("公司大门 / 出生点");
	Item.Dimensions = TEXT("-");
	Item.Material = TEXT("-");
	Item.AssetId = TEXT("OFC-HELP");
	Item.SystemName = TEXT("VR 交互");
	Item.MaintenanceCycle = TEXT("-");
	Item.NextMaintenance = TEXT("-");
	Item.Description = TEXT("左摇杆或 WASD 行走。右摇杆左右或 Q/E 转身。右摇杆下推对准地面后松开瞬移。右手扳机查看家具。对准人按住左手 Y 说话，松开后出字。B 结束对话或关闭面板。");
	return Item;
}

TArray<FInspectableItemData> UInspectCatalog::MakeGateDemoItems()
{
	TArray<FInspectableItemData> Items;
	const TArray<FInspectableItemData>& All = GetItems();
	const int32 WantedIds[] = {38, 39, 40, 17, 18, 19, 21, 22, 101, 14};
	for (int32 Id : WantedIds)
	{
		const FInspectableItemData* Found = All.FindByPredicate([Id](const FInspectableItemData& It)
		{
			return It.CatalogId == Id;
		});
		if (Found)
		{
			Items.Add(*Found);
		}
	}
	return Items;
}

const TArray<FInspectableItemData>& UInspectCatalog::GetItems()
{
	static TArray<FInspectableItemData> Cached;
	static bool bLoaded = false;
	if (bLoaded)
	{
		return Cached;
	}
	bLoaded = true;

	const FString JsonPath = FPaths::ProjectContentDir() / TEXT("Art/家具_PDF匹配/完整清单.json");
	FString Raw;
	if (FFileHelper::LoadFileToString(Raw, *JsonPath))
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (FJsonSerializer::Deserialize(Reader, Array))
		{
			for (const TSharedPtr<FJsonValue>& Value : Array)
			{
				const TSharedPtr<FJsonObject> Obj = Value ? Value->AsObject() : nullptr;
				if (!Obj)
				{
					continue;
				}

				TArray<double> Mm;
				const TArray<TSharedPtr<FJsonValue>>* DimArray = nullptr;
				if (Obj->TryGetArrayField(TEXT("dimensions_mm"), DimArray) && DimArray)
				{
					for (const TSharedPtr<FJsonValue>& Dim : *DimArray)
					{
						Mm.Add(Dim->AsNumber());
					}
				}

				FString FileName;
				Obj->TryGetStringField(TEXT("file"), FileName);

				TArray<FString> Keys;
				if (!FileName.IsEmpty())
				{
					Keys.Add(FPaths::GetBaseFilename(FileName));
				}

				Cached.Add(OfficeVRCatalogPrivate::MakeItem(
					Obj->GetIntegerField(TEXT("id")),
					Obj->GetStringField(TEXT("name")),
					Obj->GetStringField(TEXT("room")),
					FormatDimensionsMm(Mm),
					Obj->GetStringField(TEXT("material_note")),
					Obj->GetStringField(TEXT("geometry_note")),
					Keys));
			}
		}
	}

	OfficeVRCatalogPrivate::AppendBuiltin(Cached);
	return Cached;
}

bool UInspectCatalog::FindForName(const FString& ActorOrMeshName, FInspectableItemData& OutItem)
{
	if (ActorOrMeshName.IsEmpty())
	{
		return false;
	}

	const TArray<FInspectableItemData>& Items = GetItems();
	int32 BestScore = 0;
	const FInspectableItemData* Best = nullptr;
	for (const FInspectableItemData& Item : Items)
	{
		int32 Score = 0;
		if (!Item.DisplayName.IsEmpty() && ActorOrMeshName.Contains(Item.DisplayName))
		{
			Score = FMath::Max(Score, Item.DisplayName.Len() + 10);
		}
		for (const FString& Key : Item.MatchKeys)
		{
			if (!Key.IsEmpty() && ActorOrMeshName.Contains(Key))
			{
				Score = FMath::Max(Score, Key.Len());
			}
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = &Item;
		}
	}

	if (!Best || BestScore < 2)
	{
		return false;
	}

	OutItem = *Best;
	return true;
}
