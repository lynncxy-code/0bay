#include "WebInteraction/OntoTwinWebInteractionComponent.h"

#include "TwinInstance.h"
#include "TwinSceneManager.h"
#include "SceneInteraction/TwinInteractionManagerComponent.h"
#include "WebInteraction/OntoTwinWebBridge.h"

#include "Blueprint/UserWidget.h"
#include "Dom/JsonValue.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "InputCoreTypes.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Field)
{
    if (!Parent.IsValid()) return nullptr;
    const TSharedPtr<FJsonObject>* Value = nullptr;
    return Parent->TryGetObjectField(Field, Value) && Value ? *Value : nullptr;
}

FString StringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
    FString Value;
    return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : FString();
}

bool BoolField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool Default = true)
{
    bool Value = Default;
    return Object.IsValid() && Object->TryGetBoolField(Field, Value) ? Value : Default;
}

FString JsonScalar(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid()) return FString();
    if (Value->Type == EJson::String) return Value->AsString();
    if (Value->Type == EJson::Number) return FString::SanitizeFloat(Value->AsNumber());
    if (Value->Type == EJson::Boolean) return Value->AsBool() ? TEXT("true") : TEXT("false");
    return FString();
}

TSharedPtr<FJsonObject> NewContext(const FString& ProjectId)
{
    TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
    Context->SetStringField(TEXT("project_id"), ProjectId);
    return Context;
}

bool ScopeMatches(const TSharedPtr<FJsonObject>& Scope, const TSharedPtr<FJsonObject>& Candidate)
{
    static const TCHAR* Keys[] = {
        TEXT("zone_id"), TEXT("object_type_rid"), TEXT("instance_id"), TEXT("business_view_id")
    };
    for (const TCHAR* Key : Keys)
    {
        const FString Left = StringField(Scope, Key);
        const FString Right = StringField(Candidate, Key);
        if (Left != Right) return false;
    }
    return true;
}

FString ScopeTypeForContext(const TSharedPtr<FJsonObject>& Context)
{
    if (!StringField(Context, TEXT("business_view_id")).IsEmpty()) return TEXT("business_view");
    if (!StringField(Context, TEXT("instance_id")).IsEmpty()) return TEXT("instance");
    if (!StringField(Context, TEXT("zone_id")).IsEmpty()) return TEXT("zone");
    return TEXT("project");
}

TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Source)
{
    if (!Source.IsValid()) return nullptr;
    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    if (!FJsonSerializer::Serialize(Source.ToSharedRef(), Writer)) return nullptr;
    TSharedPtr<FJsonObject> Result;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    return FJsonSerializer::Deserialize(Reader, Result) ? Result : nullptr;
}

TArray<FString> JsonStringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
    TArray<FString> Result;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (Object.IsValid() && Object->TryGetArrayField(Field, Values) && Values)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            FString Text;
            if (Value.IsValid() && Value->TryGetString(Text)) Result.Add(Text);
        }
    }
    return Result;
}

void SetJsonStringArray(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* Field,
    const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> JsonValues;
    for (const FString& Value : Values) JsonValues.Add(MakeShared<FJsonValueString>(Value));
    Object->SetArrayField(Field, JsonValues);
}
}

UOntoTwinWebInteractionComponent::UOntoTwinWebInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UOntoTwinWebInteractionComponent::BeginPlay()
{
    Super::BeginPlay();
    SceneManager = Cast<ATwinSceneManager>(GetOwner());
    PlayerController = UGameplayStatics::GetPlayerController(this, 0);
    bShuttingDown = false;
    FString PolicyOverride;
    if (FParse::Value(FCommandLine::Get(), TEXT("OntoTwinWebUrlPolicy="), PolicyOverride))
    {
        UrlPolicy = PolicyOverride.TrimStartAndEnd().ToLower();
    }
    else
    {
        UrlPolicy = FPlatformMisc::GetEnvironmentVariable(TEXT("WEB_URL_POLICY")).TrimStartAndEnd().ToLower();
    }
    if (UrlPolicy != TEXT("allowlist") && UrlPolicy != TEXT("open"))
    {
#if UE_BUILD_SHIPPING
        UrlPolicy = TEXT("allowlist");
#else
        UrlPolicy = TEXT("open");
#endif
    }
    if (!SceneManager || !bEnableWebInteraction)
    {
        SetComponentTickEnabled(false);
        return;
    }
    PollRuntimeProjection();
}

void UOntoTwinWebInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bShuttingDown = true;
    RestoreSceneVisibility();
    RestoreInput();
    if (HostWidget)
    {
        HostWidget->RemoveFromParent();
        HostWidget = nullptr;
    }
    BridgeObject = nullptr;
    Super::EndPlay(EndPlayReason);
}

void UOntoTwinWebInteractionComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!SceneManager || bShuttingDown) return;
    if (!PlayerController) PlayerController = UGameplayStatics::GetPlayerController(this, 0);
    PollAccumulator += DeltaTime;
    if (PollAccumulator >= RuntimePollInterval)
    {
        PollAccumulator = 0.0f;
        PollRuntimeProjection();
    }
    VisibilityAccumulator += DeltaTime;
    if (VisibilityAccumulator >= 0.2f)
    {
        VisibilityAccumulator = 0.0f;
        TickVisibilityScope();
    }
    if (HostWidget && HostWidget->GetVisibility() == ESlateVisibility::Visible
        && PlayerController && PlayerController->WasInputKeyJustPressed(EKeys::Escape))
    {
        Back();
    }
}

void UOntoTwinWebInteractionComponent::AddProjectHeaders(
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request) const
{
    if (!SceneManager) return;
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Id"), SceneManager->UEProjectId);
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Name"), SceneManager->UEProjectName);
#if WITH_EDITOR
    Request->SetHeader(TEXT("X-OntoTwin-UE-Context"), TEXT("editor"));
#else
    Request->SetHeader(TEXT("X-OntoTwin-UE-Context"), TEXT("packaged"));
#endif
}

void UOntoTwinWebInteractionComponent::PollRuntimeProjection()
{
    if (bRuntimeRequestInFlight || bShuttingDown || !SceneManager) return;
    bRuntimeRequestInFlight = true;
    FString BaseUrl = SceneManager->BackendBaseUrl;
    BaseUrl.RemoveFromEnd(TEXT("/"));
    FString Url = BaseUrl + TEXT("/api/v2/web-interactions/runtime");
    if (AppliedRevision >= 0)
    {
        Url += FString::Printf(TEXT("?known_revision=%d"), AppliedRevision);
    }
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    AddProjectHeaders(Request);
    const TWeakObjectPtr<UOntoTwinWebInteractionComponent> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
        {
            UOntoTwinWebInteractionComponent* Self = WeakThis.Get();
            if (!Self || Self->bShuttingDown) return;
            Self->bRuntimeRequestInFlight = false;
            if (!bSuccess || !Response.IsValid() || Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
            {
                if (Self->HostWidget && Self->HostWidget->GetVisibility() == ESlateVisibility::Visible)
                {
                    Self->HostWidget->SetHostStatus(TEXT("Web 配置服务不可用，保留当前页面"), true);
                }
                return;
            }
            TSharedPtr<FJsonObject> Payload;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
            if (FJsonSerializer::Deserialize(Reader, Payload) && Payload.IsValid())
            {
                Self->HandleRuntimeProjection(Payload);
            }
        });
    if (!Request->ProcessRequest()) bRuntimeRequestInFlight = false;
}

void UOntoTwinWebInteractionComponent::HandleRuntimeProjection(const TSharedPtr<FJsonObject>& Payload)
{
    if (StringField(Payload, TEXT("status")) == TEXT("unchanged")) return;
    double RevisionNumber = 0.0;
    if (!Payload->TryGetNumberField(TEXT("revision"), RevisionNumber)) return;
    RebuildRuntimeIndexes(Payload);
    AppliedRevision = static_cast<int32>(RevisionNumber);
    if (bHasCurrentFrame)
    {
        const bool bStillResolved = ResolveAndOpen(
            CurrentFrame.Trigger, CurrentFrame.Context, nullptr, false);
        if (!bStillResolved && HostWidget)
        {
            HostWidget->SetHostStatus(TEXT("当前页面已被新配置禁用"), true);
        }
    }
}

void UOntoTwinWebInteractionComponent::RebuildRuntimeIndexes(const TSharedPtr<FJsonObject>& Payload)
{
    ActiveProjectId = StringField(Payload, TEXT("project_id"));
    PublishedConfig = ObjectField(Payload, TEXT("config"));
    PagesById.Reset();
    InstancesById.Reset();
    BusinessViewMembers.Reset();
    ZoneParents.Reset();
    ZoneDisplayNames.Reset();
    BusinessViewDisplayNames.Reset();
    AllowedHosts.Reset();
    if (!PublishedConfig.IsValid()) PublishedConfig = MakeShared<FJsonObject>();

    const TArray<TSharedPtr<FJsonValue>>* Pages = nullptr;
    if (PublishedConfig->TryGetArrayField(TEXT("pages"), Pages) && Pages)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Pages)
        {
            const TSharedPtr<FJsonObject> Page = Value.IsValid() ? Value->AsObject() : nullptr;
            const FString PageId = StringField(Page, TEXT("page_id"));
            if (!PageId.IsEmpty()) PagesById.Add(PageId, Page);
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* InstanceIndex = nullptr;
    if (Payload->TryGetArrayField(TEXT("instance_index"), InstanceIndex) && InstanceIndex)
    {
        for (const TSharedPtr<FJsonValue>& Value : *InstanceIndex)
        {
            const TSharedPtr<FJsonObject> Instance = Value.IsValid() ? Value->AsObject() : nullptr;
            const FString InstanceId = StringField(Instance, TEXT("instance_id"));
            if (!InstanceId.IsEmpty()) InstancesById.Add(InstanceId, Instance);
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* Zones = nullptr;
    if (Payload->TryGetArrayField(TEXT("zones"), Zones) && Zones)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Zones)
        {
            const TSharedPtr<FJsonObject> Zone = Value.IsValid() ? Value->AsObject() : nullptr;
            const FString ZoneId = StringField(Zone, TEXT("zone_id"));
            if (!ZoneId.IsEmpty())
            {
                ZoneParents.Add(ZoneId, StringField(Zone, TEXT("parent_zone_id")));
                const FString Name = StringField(Zone, TEXT("name"));
                ZoneDisplayNames.Add(ZoneId, Name.IsEmpty() ? ZoneId : Name);
            }
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* BusinessViews = nullptr;
    if (PublishedConfig->TryGetArrayField(TEXT("business_views"), BusinessViews) && BusinessViews)
    {
        for (const TSharedPtr<FJsonValue>& Value : *BusinessViews)
        {
            const TSharedPtr<FJsonObject> BusinessView = Value.IsValid() ? Value->AsObject() : nullptr;
            if (!BusinessView.IsValid() || !BoolField(BusinessView, TEXT("enabled"), true)) continue;
            const FString BusinessViewId = StringField(BusinessView, TEXT("business_view_id"));
            if (BusinessViewId.IsEmpty()) continue;
            const FString Name = StringField(BusinessView, TEXT("name"));
            BusinessViewDisplayNames.Add(
                BusinessViewId,
                Name.IsEmpty() ? BusinessViewId : Name);
        }
    }
    const TSharedPtr<FJsonObject> Memberships = ObjectField(Payload, TEXT("business_view_memberships"));
    if (Memberships.IsValid())
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Memberships->Values)
        {
            TSet<FString> Members;
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            if (Pair.Value.IsValid() && Pair.Value->TryGetArray(Values) && Values)
            {
                for (const TSharedPtr<FJsonValue>& Value : *Values)
                {
                    FString InstanceId;
                    if (Value.IsValid() && Value->TryGetString(InstanceId)) Members.Add(InstanceId);
                }
            }
            BusinessViewMembers.Add(Pair.Key, MoveTemp(Members));
        }
    }
    const TSharedPtr<FJsonObject> Policy = ObjectField(PublishedConfig, TEXT("web_policy"));
    const TArray<TSharedPtr<FJsonValue>>* Hosts = nullptr;
    if (Policy.IsValid() && Policy->TryGetArrayField(TEXT("allowed_hosts"), Hosts) && Hosts)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Hosts)
        {
            FString Host;
            if (Value.IsValid() && Value->TryGetString(Host)) AllowedHosts.Add(Host.TrimStartAndEnd().ToLower());
        }
    }
}

bool UOntoTwinWebInteractionComponent::OpenProjectHome()
{
    if (!SceneManager) return false;
    return ResolveAndOpen(TEXT("project_home_activated"), NewContext(ActiveProjectId));
}

bool UOntoTwinWebInteractionComponent::OpenZone(const FString& ZoneId)
{
    if (!SceneManager || !IsZoneKnown(ZoneId)) return false;
    TSharedPtr<FJsonObject> Context = NewContext(ActiveProjectId);
    Context->SetStringField(TEXT("zone_id"), ZoneId);
    return ResolveAndOpen(TEXT("zone_activated"), Context);
}

bool UOntoTwinWebInteractionComponent::OpenBusinessView(
    const FString& BusinessViewId,
    const FString& ZoneId)
{
    if (!SceneManager || !IsBusinessViewKnown(BusinessViewId)
        || (!ZoneId.IsEmpty() && !IsZoneKnown(ZoneId))) return false;
    TSharedPtr<FJsonObject> Context = NewContext(ActiveProjectId);
    Context->SetStringField(TEXT("business_view_id"), BusinessViewId);
    if (!ZoneId.IsEmpty()) Context->SetStringField(TEXT("zone_id"), ZoneId);
    return ResolveAndOpen(TEXT("business_view_activated"), Context);
}

bool UOntoTwinWebInteractionComponent::OpenInstanceDetail(const FString& InstanceId)
{
    if (!SceneManager || !IsInstanceKnown(InstanceId)) return false;
    TSharedPtr<FJsonObject> Context = NewContext(ActiveProjectId);
    Context->SetStringField(TEXT("instance_id"), InstanceId);
    const TSharedPtr<FJsonObject>* Instance = InstancesById.Find(InstanceId);
    if (Instance && Instance->IsValid())
    {
        const FString ZoneId = StringField(*Instance, TEXT("zone_id"));
        const FString TypeRid = StringField(*Instance, TEXT("object_type_rid"));
        if (!ZoneId.IsEmpty()) Context->SetStringField(TEXT("zone_id"), ZoneId);
        if (!TypeRid.IsEmpty()) Context->SetStringField(TEXT("object_type_rid"), TypeRid);
    }
    const bool bOpened = ResolveAndOpen(TEXT("open_detail"), Context);
    if (bOpened) SelectAndFocusInstance(InstanceId);
    return bOpened;
}

void UOntoTwinWebInteractionComponent::GetAvailableZones(
    TArray<FString>& OutZoneIds,
    TArray<FString>& OutDisplayNames) const
{
    OutZoneIds.Reset();
    OutDisplayNames.Reset();
    ZoneParents.GetKeys(OutZoneIds);
    OutZoneIds.Sort(
        [this](const FString& Left, const FString& Right)
        {
            const FString LeftName = ZoneDisplayNames.FindRef(Left);
            const FString RightName = ZoneDisplayNames.FindRef(Right);
            const int32 NameOrder = LeftName.Compare(RightName, ESearchCase::IgnoreCase);
            return NameOrder == 0
                ? Left.Compare(Right, ESearchCase::IgnoreCase) < 0
                : NameOrder < 0;
        });
    for (const FString& ZoneId : OutZoneIds)
    {
        const FString Name = ZoneDisplayNames.FindRef(ZoneId);
        OutDisplayNames.Add(Name.IsEmpty() ? ZoneId : Name);
    }
}

void UOntoTwinWebInteractionComponent::GetAvailableZoneTree(
    TArray<FString>& OutZoneIds,
    TArray<FString>& OutDisplayNames,
    TArray<FString>& OutParentZoneIds) const
{
    GetAvailableZones(OutZoneIds, OutDisplayNames);
    OutParentZoneIds.Reset();
    OutParentZoneIds.Reserve(OutZoneIds.Num());
    for (const FString& ZoneId : OutZoneIds)
    {
        OutParentZoneIds.Add(ZoneParents.FindRef(ZoneId));
    }
}

void UOntoTwinWebInteractionComponent::GetAvailableBusinessViews(
    TArray<FString>& OutBusinessViewIds,
    TArray<FString>& OutDisplayNames) const
{
    OutBusinessViewIds.Reset();
    OutDisplayNames.Reset();
    BusinessViewDisplayNames.GetKeys(OutBusinessViewIds);
    OutBusinessViewIds.Sort(
        [this](const FString& Left, const FString& Right)
        {
            const FString LeftName = BusinessViewDisplayNames.FindRef(Left);
            const FString RightName = BusinessViewDisplayNames.FindRef(Right);
            const int32 NameOrder = LeftName.Compare(RightName, ESearchCase::IgnoreCase);
            return NameOrder == 0
                ? Left.Compare(Right, ESearchCase::IgnoreCase) < 0
                : NameOrder < 0;
        });
    for (const FString& BusinessViewId : OutBusinessViewIds)
    {
        const FString Name = BusinessViewDisplayNames.FindRef(BusinessViewId);
        OutDisplayNames.Add(Name.IsEmpty() ? BusinessViewId : Name);
    }
}

void UOntoTwinWebInteractionComponent::GetAvailableBusinessViewSummaries(
    TArray<FString>& OutBusinessViewIds,
    TArray<FString>& OutDisplayNames,
    TArray<int32>& OutMemberCounts) const
{
    GetAvailableBusinessViews(OutBusinessViewIds, OutDisplayNames);
    OutMemberCounts.Reset();
    OutMemberCounts.Reserve(OutBusinessViewIds.Num());
    for (const FString& BusinessViewId : OutBusinessViewIds)
    {
        const TSet<FString>* Members = BusinessViewMembers.Find(BusinessViewId);
        OutMemberCounts.Add(Members ? Members->Num() : 0);
    }
}

bool UOntoTwinWebInteractionComponent::GetRuntimeBusinessEditSnapshot(
    TSharedPtr<FJsonObject>& OutConfig,
    int32& OutRevision) const
{
    OutConfig = CloneJsonObject(PublishedConfig);
    OutRevision = AppliedRevision;
    return OutConfig.IsValid() && OutRevision >= 0;
}

TSet<FString> UOntoTwinWebInteractionComponent::BusinessMembersForConfig(
    const TSharedPtr<FJsonObject>& BusinessView) const
{
    TSet<FString> Members;
    const TArray<TSharedPtr<FJsonValue>>* Groups = nullptr;
    if (!BusinessView.IsValid()
        || !BusinessView->TryGetArrayField(TEXT("rule_groups"), Groups) || !Groups)
    {
        return Members;
    }

    for (const TSharedPtr<FJsonValue>& GroupValue : *Groups)
    {
        const TSharedPtr<FJsonObject> Group = GroupValue.IsValid() ? GroupValue->AsObject() : nullptr;
        if (!Group.IsValid()) continue;
        const TArray<FString> ZoneIds = JsonStringArray(Group, TEXT("zone_ids"));
        const TArray<FString> TypeIds = JsonStringArray(Group, TEXT("object_type_rids"));
        const TArray<FString> ExplicitIds = JsonStringArray(Group, TEXT("instance_ids"));
        if (ZoneIds.Num() == 0 && TypeIds.Num() == 0 && ExplicitIds.Num() == 0) continue;

        TSet<FString> AllowedZones;
        for (const FString& ZoneId : ZoneIds) AllowedZones.Append(DescendantZones(ZoneId));
        for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : InstancesById)
        {
            if (ZoneIds.Num() > 0 && !AllowedZones.Contains(StringField(Pair.Value, TEXT("zone_id")))) continue;
            if (TypeIds.Num() > 0 && !TypeIds.Contains(StringField(Pair.Value, TEXT("object_type_rid")))) continue;
            if (ExplicitIds.Num() > 0 && !ExplicitIds.Contains(Pair.Key)) continue;
            Members.Add(Pair.Key);
        }
    }
    for (const FString& Excluded : JsonStringArray(BusinessView, TEXT("exclude_instance_ids")))
    {
        Members.Remove(Excluded);
    }
    return Members;
}

void UOntoTwinWebInteractionComponent::GetRuntimeBusinessMembershipStates(
    const TSharedPtr<FJsonObject>& Config,
    const TArray<FString>& InstanceIds,
    TArray<FString>& OutBusinessIds,
    TArray<FString>& OutNames,
    TArray<EOntoTwinBusinessMembershipState>& OutStates) const
{
    OutBusinessIds.Reset();
    OutNames.Reset();
    OutStates.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Views = nullptr;
    if (!Config.IsValid() || !Config->TryGetArrayField(TEXT("business_views"), Views) || !Views) return;
    for (const TSharedPtr<FJsonValue>& Value : *Views)
    {
        const TSharedPtr<FJsonObject> View = Value.IsValid() ? Value->AsObject() : nullptr;
        if (!View.IsValid() || !BoolField(View, TEXT("enabled"), true)) continue;
        const FString BusinessId = StringField(View, TEXT("business_view_id"));
        if (BusinessId.IsEmpty()) continue;
        const TSet<FString> Members = BusinessMembersForConfig(View);
        int32 Included = 0;
        for (const FString& InstanceId : InstanceIds) if (Members.Contains(InstanceId)) ++Included;
        OutBusinessIds.Add(BusinessId);
        const FString Name = StringField(View, TEXT("name"));
        OutNames.Add(Name.IsEmpty() ? BusinessId : Name);
        OutStates.Add(InstanceIds.Num() == 0 || Included == 0
            ? EOntoTwinBusinessMembershipState::None
            : (Included == InstanceIds.Num()
                ? EOntoTwinBusinessMembershipState::All
                : EOntoTwinBusinessMembershipState::Mixed));
    }
}

bool UOntoTwinWebInteractionComponent::SetRuntimeBusinessMembership(
    const TSharedPtr<FJsonObject>& Config,
    const FString& BusinessId,
    const TArray<FString>& InstanceIds,
    bool bAdd,
    FString& OutError) const
{
    OutError.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Views = nullptr;
    if (!Config.IsValid() || !Config->TryGetArrayField(TEXT("business_views"), Views) || !Views)
    {
        OutError = TEXT("业务配置尚未就绪");
        return false;
    }
    TSharedPtr<FJsonObject> TargetView;
    for (const TSharedPtr<FJsonValue>& Value : *Views)
    {
        const TSharedPtr<FJsonObject> View = Value.IsValid() ? Value->AsObject() : nullptr;
        if (View.IsValid() && StringField(View, TEXT("business_view_id")) == BusinessId)
        {
            TargetView = View;
            break;
        }
    }
    if (!TargetView.IsValid())
    {
        OutError = TEXT("业务已不存在，请加载最新配置");
        return false;
    }

    TArray<FString> Excluded = JsonStringArray(TargetView, TEXT("exclude_instance_ids"));
    TArray<TSharedPtr<FJsonValue>> Groups;
    const TArray<TSharedPtr<FJsonValue>>* ExistingGroups = nullptr;
    if (TargetView->TryGetArrayField(TEXT("rule_groups"), ExistingGroups) && ExistingGroups)
    {
        Groups = *ExistingGroups;
    }
    if (bAdd)
    {
        for (const FString& InstanceId : InstanceIds) Excluded.Remove(InstanceId);
        TSharedPtr<FJsonObject> ExactGroup;
        for (const TSharedPtr<FJsonValue>& Value : Groups)
        {
            const TSharedPtr<FJsonObject> Group = Value.IsValid() ? Value->AsObject() : nullptr;
            if (Group.IsValid()
                && JsonStringArray(Group, TEXT("zone_ids")).Num() == 0
                && JsonStringArray(Group, TEXT("object_type_rids")).Num() == 0)
            {
                ExactGroup = Group;
                break;
            }
        }
        if (!ExactGroup.IsValid())
        {
            ExactGroup = MakeShared<FJsonObject>();
            SetJsonStringArray(ExactGroup, TEXT("zone_ids"), {});
            SetJsonStringArray(ExactGroup, TEXT("object_type_rids"), {});
            SetJsonStringArray(ExactGroup, TEXT("instance_ids"), {});
            Groups.Add(MakeShared<FJsonValueObject>(ExactGroup));
        }
        TArray<FString> ExactIds = JsonStringArray(ExactGroup, TEXT("instance_ids"));
        for (const FString& InstanceId : InstanceIds) ExactIds.AddUnique(InstanceId);
        SetJsonStringArray(ExactGroup, TEXT("instance_ids"), ExactIds);
    }
    else
    {
        for (const TSharedPtr<FJsonValue>& Value : Groups)
        {
            const TSharedPtr<FJsonObject> Group = Value.IsValid() ? Value->AsObject() : nullptr;
            if (!Group.IsValid()) continue;
            TArray<FString> ExactIds = JsonStringArray(Group, TEXT("instance_ids"));
            for (const FString& InstanceId : InstanceIds) ExactIds.Remove(InstanceId);
            SetJsonStringArray(Group, TEXT("instance_ids"), ExactIds);
        }
        Groups.RemoveAll([](const TSharedPtr<FJsonValue>& Value)
        {
            const TSharedPtr<FJsonObject> Group = Value.IsValid() ? Value->AsObject() : nullptr;
            return !Group.IsValid()
                || (JsonStringArray(Group, TEXT("zone_ids")).Num() == 0
                    && JsonStringArray(Group, TEXT("object_type_rids")).Num() == 0
                    && JsonStringArray(Group, TEXT("instance_ids")).Num() == 0);
        });
    }
    TargetView->SetArrayField(TEXT("rule_groups"), Groups);
    SetJsonStringArray(TargetView, TEXT("exclude_instance_ids"), Excluded);
    if (!bAdd)
    {
        const TSet<FString> StillMatched = BusinessMembersForConfig(TargetView);
        for (const FString& InstanceId : InstanceIds)
        {
            if (StillMatched.Contains(InstanceId)) Excluded.AddUnique(InstanceId);
        }
        SetJsonStringArray(TargetView, TEXT("exclude_instance_ids"), Excluded);
    }
    return true;
}

bool UOntoTwinWebInteractionComponent::CreateRuntimeBusiness(
    const TSharedPtr<FJsonObject>& Config,
    const FString& Name,
    const TArray<FString>& InstanceIds,
    FString& OutBusinessId,
    FString& OutError) const
{
    OutBusinessId.Reset();
    OutError.Reset();
    const FString CleanName = Name.TrimStartAndEnd();
    if (CleanName.IsEmpty())
    {
        OutError = TEXT("请输入业务名称");
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
    if (!Config.IsValid() || !Config->TryGetArrayField(TEXT("business_views"), Existing) || !Existing)
    {
        OutError = TEXT("业务配置尚未就绪");
        return false;
    }
    TArray<TSharedPtr<FJsonValue>> Views = *Existing;
    TSet<FString> UsedIds;
    for (const TSharedPtr<FJsonValue>& Value : Views)
    {
        const TSharedPtr<FJsonObject> View = Value.IsValid() ? Value->AsObject() : nullptr;
        if (View.IsValid()) UsedIds.Add(StringField(View, TEXT("business_view_id")));
    }
    FString BusinessId = FString::Printf(TEXT("bv.f8.%lld"), FDateTime::UtcNow().ToUnixTimestamp());
    int32 Suffix = 1;
    const FString BaseId = BusinessId;
    while (UsedIds.Contains(BusinessId)) BusinessId = FString::Printf(TEXT("%s.%d"), *BaseId, Suffix++);

    const TSharedPtr<FJsonObject> Group = MakeShared<FJsonObject>();
    SetJsonStringArray(Group, TEXT("zone_ids"), {});
    SetJsonStringArray(Group, TEXT("object_type_rids"), {});
    SetJsonStringArray(Group, TEXT("instance_ids"), InstanceIds);
    const TSharedPtr<FJsonObject> View = MakeShared<FJsonObject>();
    View->SetStringField(TEXT("business_view_id"), BusinessId);
    View->SetStringField(TEXT("name"), CleanName);
    View->SetStringField(TEXT("description"), TEXT("由 F10 业务编辑创建"));
    View->SetBoolField(TEXT("enabled"), true);
    View->SetStringField(TEXT("scene_behavior"), TEXT("isolate_focus"));
    View->SetArrayField(TEXT("rule_groups"), {MakeShared<FJsonValueObject>(Group)});
    View->SetArrayField(TEXT("exclude_instance_ids"), TArray<TSharedPtr<FJsonValue>>());
    Views.Add(MakeShared<FJsonValueObject>(View));
    Config->SetArrayField(TEXT("business_views"), Views);
    OutBusinessId = BusinessId;
    return true;
}

void UOntoTwinWebInteractionComponent::ApplyRuntimeBusinessEdit(
    const TSharedPtr<FJsonObject>& Config,
    int32 ExpectedRevision,
    TFunction<void(bool, int32, const FString&)> Completion)
{
    if (!Config.IsValid())
    {
        Completion(false, ExpectedRevision, TEXT("业务配置无效"));
        return;
    }
    const TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetNumberField(TEXT("expected_revision"), ExpectedRevision);
    Body->SetBoolField(TEXT("confirm_warnings"), true);
    const TSharedPtr<FJsonObject> AppliedConfig = CloneJsonObject(Config);
    Body->SetObjectField(TEXT("config"), AppliedConfig);
    FString BodyText;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyText);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(SceneManager
        ? FString::Printf(TEXT("%s/api/v2/web-interactions/apply"), *SceneManager->BackendBaseUrl)
        : FString());
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    AddProjectHeaders(Request);
    Request->SetContentAsString(BodyText);
    TWeakObjectPtr<UOntoTwinWebInteractionComponent> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, ExpectedRevision, AppliedConfig, Completion = MoveTemp(Completion)](
            FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk) mutable
        {
            UOntoTwinWebInteractionComponent* Self = WeakThis.Get();
            const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : -1;
            TSharedPtr<FJsonObject> Root;
            if (Resp.IsValid())
            {
                const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
                FJsonSerializer::Deserialize(Reader, Root);
            }
            FString Status;
            if (Root.IsValid()) Root->TryGetStringField(TEXT("status"), Status);
            if (Self && bOk && Code == 200 && Status == TEXT("ok"))
            {
                double RevisionNumber = ExpectedRevision + 1;
                Root->TryGetNumberField(TEXT("revision"), RevisionNumber);
                const int32 SavedRevision = static_cast<int32>(RevisionNumber);
                Self->PublishedConfig = CloneJsonObject(AppliedConfig);
                Self->AppliedRevision = -1;
                Self->PollAccumulator = 1000.0f;
                Completion(true, SavedRevision, FString());
                return;
            }
            FString Error = Code == 409
                ? TEXT("业务配置已在其他入口更新；本地修改仍保留")
                : TEXT("业务修改保存失败；本地修改仍保留");
            if (Root.IsValid())
            {
                FString Message;
                if (Root->TryGetStringField(TEXT("message"), Message) && !Message.IsEmpty()) Error = Message;
            }
            Completion(false, ExpectedRevision, Error);
        });
    Request->ProcessRequest();
}

TSharedPtr<FJsonObject> UOntoTwinWebInteractionComponent::ResolveBinding(
    const FString& Trigger,
    const TSharedPtr<FJsonObject>& Context,
    TArray<FString>& OutChain,
    bool& bBlocked) const
{
    bBlocked = false;
    if (!PublishedConfig.IsValid()) return nullptr;
    TArray<TSharedPtr<FJsonObject>> Candidates;
    TArray<FString> Labels;
    auto AddCandidate = [&Candidates, &Labels](const FString& Label, const TMap<FString, FString>& Values)
    {
        TSharedPtr<FJsonObject> Scope = MakeShared<FJsonObject>();
        bool bMissing = false;
        for (const TPair<FString, FString>& Pair : Values)
        {
            if (Pair.Value.IsEmpty()) bMissing = true;
            else Scope->SetStringField(Pair.Key, Pair.Value);
        }
        if (!bMissing)
        {
            Labels.Add(Label);
            Candidates.Add(Scope);
        }
    };
    if (Trigger == TEXT("open_detail"))
    {
        AddCandidate(TEXT("Instance"), {{TEXT("instance_id"), StringField(Context, TEXT("instance_id"))}});
        AddCandidate(TEXT("Zone+Type"), {{TEXT("zone_id"), StringField(Context, TEXT("zone_id"))}, {TEXT("object_type_rid"), StringField(Context, TEXT("object_type_rid"))}});
        AddCandidate(TEXT("Type"), {{TEXT("object_type_rid"), StringField(Context, TEXT("object_type_rid"))}});
        AddCandidate(TEXT("Zone"), {{TEXT("zone_id"), StringField(Context, TEXT("zone_id"))}});
    }
    else if (Trigger == TEXT("business_view_activated"))
    {
        AddCandidate(TEXT("BusinessView"), {{TEXT("business_view_id"), StringField(Context, TEXT("business_view_id"))}});
    }
    else if (Trigger == TEXT("zone_activated"))
    {
        AddCandidate(TEXT("Zone"), {{TEXT("zone_id"), StringField(Context, TEXT("zone_id"))}});
    }
    AddCandidate(TEXT("Project"), {});

    const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
    if (!PublishedConfig->TryGetArrayField(TEXT("bindings"), Bindings) || !Bindings) return nullptr;
    for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
    {
        bool bFoundDisabled = false;
        for (const TSharedPtr<FJsonValue>& Value : *Bindings)
        {
            const TSharedPtr<FJsonObject> Binding = Value.IsValid() ? Value->AsObject() : nullptr;
            if (!Binding.IsValid() || StringField(Binding, TEXT("trigger")) != Trigger) continue;
            const TSharedPtr<FJsonObject> Scope = ObjectField(Binding, TEXT("scope"));
            if (!ScopeMatches(Scope.IsValid() ? Scope : MakeShared<FJsonObject>(), Candidates[CandidateIndex])) continue;
            if (!BoolField(Binding, TEXT("enabled"), true))
            {
                bFoundDisabled = true;
                continue;
            }
            const FString Effect = StringField(Binding, TEXT("effect"));
            OutChain.Add(Labels[CandidateIndex] + TEXT(":") + StringField(Binding, TEXT("binding_id")));
            bBlocked = Effect == TEXT("block");
            return Binding;
        }
        OutChain.Add(Labels[CandidateIndex] + (bFoundDisabled ? TEXT(":disabled") : TEXT(":none")));
    }
    return nullptr;
}

bool UOntoTwinWebInteractionComponent::ResolveAndOpen(
    const FString& Trigger,
    const TSharedPtr<FJsonObject>& Context,
    const TSharedPtr<FJsonObject>& ExtraParams,
    bool bPushHistory)
{
    TArray<FString> Chain;
    bool bBlocked = false;
    const TSharedPtr<FJsonObject> Binding = ResolveBinding(Trigger, Context, Chain, bBlocked);
    if (!Binding.IsValid() || bBlocked)
    {
        SendRuntimeEvent(TEXT("resolve"), bBlocked ? TEXT("blocked") : TEXT("no_binding"));
        if (Trigger != TEXT("zone_activated") && Trigger != TEXT("business_view_activated"))
        {
            return false;
        }

        // Space and business activation is useful even without a page. Keep a
        // navigation frame so Back restores both the previous page and scene.
        FOntoTwinWebNavigationFrame Next;
        Next.Trigger = Trigger;
        Next.Context = Context;
        const TSharedPtr<FJsonObject> SceneOnlyPage = MakeShared<FJsonObject>();
        const TSharedPtr<FJsonObject> Effects = MakeShared<FJsonObject>();
        Effects->SetStringField(
            Trigger == TEXT("zone_activated") ? TEXT("zone") : TEXT("business_view"),
            TEXT("web_and_scene"));
        SceneOnlyPage->SetObjectField(TEXT("scope_effects"), Effects);
        ApplySceneScope(SceneOnlyPage, Context, Next);
        if (bPushHistory && bHasCurrentFrame) NavigationHistory.Add(CurrentFrame);
        CurrentFrame = Next;
        bHasCurrentFrame = true;
        ApplyVisibilityFrame(CurrentFrame);
        if (SceneManager && CurrentFrame.FocusInstanceIds.Num() > 0)
        {
            SceneManager->FocusManagedInstances(CurrentFrame.FocusInstanceIds);
        }
        if (HostWidget) HostWidget->SetVisibility(ESlateVisibility::Collapsed);
        RestoreInput();
        return true;
    }
    const FString PageId = StringField(Binding, TEXT("page_id"));
    const TSharedPtr<FJsonObject>* Page = PagesById.Find(PageId);
    if (!Page || !Page->IsValid() || !BoolField(*Page, TEXT("enabled"), true)) return false;
    return OpenPage(*Page, Trigger, Context, ExtraParams, bPushHistory);
}

bool UOntoTwinWebInteractionComponent::OpenPage(
    const TSharedPtr<FJsonObject>& Page,
    const FString& Trigger,
    const TSharedPtr<FJsonObject>& Context,
    const TSharedPtr<FJsonObject>& ExtraParams,
    bool bPushHistory)
{
    if (bRuntimeEditorSuppressed) return false;
    FString Error;
    const FString FinalUrl = BuildFinalUrl(Page, Context, ExtraParams, Error);
    if (FinalUrl.IsEmpty())
    {
        SendRuntimeEvent(TEXT("navigation"), TEXT("rejected"), Error);
        return false;
    }
    FOntoTwinWebNavigationFrame Next;
    Next.PageId = StringField(Page, TEXT("page_id"));
    Next.FinalUrl = FinalUrl;
    Next.Trigger = Trigger;
    Next.Context = Context;
    ApplySceneScope(Page, Context, Next);
    if (bPushHistory && bHasCurrentFrame
        && (CurrentFrame.PageId != Next.PageId || CurrentFrame.FinalUrl != Next.FinalUrl))
    {
        NavigationHistory.Add(CurrentFrame);
    }
    const bool bSamePage = bHasCurrentFrame
        && CurrentFrame.PageId == Next.PageId && CurrentFrame.FinalUrl == Next.FinalUrl;
    CurrentFrame = Next;
    bHasCurrentFrame = true;
    ApplyVisibilityFrame(CurrentFrame);
    const FString ScopeType = ScopeTypeForContext(Context);
    if (CurrentFrame.bSceneScopeActive
        && (ScopeType == TEXT("zone") || ScopeType == TEXT("business_view")))
    {
        SceneManager->FocusManagedInstances(CurrentFrame.FocusInstanceIds);
    }
    EnsureHostWidget();
    if (!HostWidget) return false;
    HostWidget->SetVisibility(ESlateVisibility::Visible);
    CaptureWebInput();
    if (!bSamePage) HostWidget->Navigate(FinalUrl);
    else HostWidget->SetHostStatus(TEXT("页面已重新显示"));
    SendRuntimeEvent(TEXT("navigation"), bSamePage ? TEXT("shown_without_reload") : TEXT("loading"));
    return true;
}

FString UOntoTwinWebInteractionComponent::BuildFinalUrl(
    const TSharedPtr<FJsonObject>& Page,
    const TSharedPtr<FJsonObject>& Context,
    const TSharedPtr<FJsonObject>& ExtraParams,
    FString& OutError) const
{
    FString Url = StringField(Page, TEXT("base_url")).TrimStartAndEnd();
    if (!ValidateNavigation(Url, OutError)) return FString();
    FString Fragment;
    int32 HashIndex = INDEX_NONE;
    if (Url.FindChar(TEXT('#'), HashIndex))
    {
        Fragment = Url.Mid(HashIndex);
        Url.LeftInline(HashIndex);
    }
    TArray<FString> Params;
    const TSharedPtr<FJsonObject> Mapping = ObjectField(Page, TEXT("param_mapping"));
    if (Mapping.IsValid())
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Mapping->Values)
        {
            const FString Destination = JsonScalar(Pair.Value);
            const FString SourceValue = StringField(Context, *Pair.Key);
            if (!Destination.IsEmpty() && !SourceValue.IsEmpty())
            {
                Params.Add(FGenericPlatformHttp::UrlEncode(Destination) + TEXT("=")
                    + FGenericPlatformHttp::UrlEncode(SourceValue));
            }
        }
    }
    TSet<FString> Declared;
    const TArray<TSharedPtr<FJsonValue>>* DeclaredValues = nullptr;
    if (Page->TryGetArrayField(TEXT("declared_extra_params"), DeclaredValues) && DeclaredValues)
    {
        for (const TSharedPtr<FJsonValue>& Value : *DeclaredValues)
        {
            FString Key;
            if (Value.IsValid() && Value->TryGetString(Key)) Declared.Add(Key);
        }
    }
    if (ExtraParams.IsValid())
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ExtraParams->Values)
        {
            if (!Declared.Contains(Pair.Key))
            {
                OutError = TEXT("extra_param_not_declared");
                return FString();
            }
            const FString Value = JsonScalar(Pair.Value);
            if (!Value.IsEmpty()) Params.Add(FGenericPlatformHttp::UrlEncode(Pair.Key) + TEXT("=") + FGenericPlatformHttp::UrlEncode(Value));
        }
    }
    if (Params.Num() > 0)
    {
        Url += Url.Contains(TEXT("?")) ? TEXT("&") : TEXT("?");
        Url += FString::Join(Params, TEXT("&"));
    }
    return Url + Fragment;
}

bool UOntoTwinWebInteractionComponent::ValidateNavigation(const FString& Url, FString& OutReason) const
{
    FString Clean = Url.TrimStartAndEnd();
    const FString Lower = Clean.ToLower();
    if (Lower == TEXT("about:blank")) return true;
    if (!Lower.StartsWith(TEXT("http://")) && !Lower.StartsWith(TEXT("https://")))
    {
        OutReason = TEXT("dangerous_url_scheme");
        return false;
    }
    const int32 SchemeEnd = Clean.Find(TEXT("://"));
    const int32 AuthorityStart = SchemeEnd + 3;
    int32 AuthorityEnd = Clean.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, AuthorityStart);
    if (AuthorityEnd == INDEX_NONE) AuthorityEnd = Clean.Len();
    FString Authority = Clean.Mid(AuthorityStart, AuthorityEnd - AuthorityStart);
    const int32 QueryInAuthority = Authority.Find(TEXT("?"));
    if (QueryInAuthority != INDEX_NONE) Authority.LeftInline(QueryInAuthority);
    if (Authority.Contains(TEXT("@")))
    {
        OutReason = TEXT("embedded_credentials");
        return false;
    }
    FString Host = Authority;
    int32 ColonIndex = INDEX_NONE;
    if (Host.FindChar(TEXT(':'), ColonIndex)) Host.LeftInline(ColonIndex);
    Host = Host.ToLower();
    if (Host.IsEmpty())
    {
        OutReason = TEXT("url_host_required");
        return false;
    }
    if (UrlPolicy == TEXT("allowlist") && !AllowedHosts.Contains(Host) && !AllowedHosts.Contains(Authority.ToLower()))
    {
        OutReason = TEXT("host_not_allowed");
        return false;
    }
    return true;
}

bool UOntoTwinWebInteractionComponent::HandlePopupNavigation(const FString& Url)
{
    FString Reason;
    if (ValidateNavigation(Url, Reason) && HostWidget)
    {
        HostWidget->Navigate(Url);
        SendRuntimeEvent(TEXT("popup"), TEXT("reused_single_browser"));
    }
    else if (HostWidget)
    {
        HostWidget->SetHostStatus(TEXT("已阻止不安全的新窗口地址"), true);
        SendRuntimeEvent(TEXT("popup"), TEXT("rejected"), Reason);
    }
    return true; // Always suppress creation of a second browser window.
}

void UOntoTwinWebInteractionComponent::EnsureHostWidget()
{
    if (HostWidget || !PlayerController) return;
    BridgeObject = NewObject<UOntoTwinWebBridge>(this);
    BridgeObject->Initialize(this);
    HostWidget = CreateWidget<UOntoTwinWebHostWidget>(
        PlayerController, UOntoTwinWebHostWidget::StaticClass());
    if (!HostWidget) return;
    HostWidget->Configure(this, BridgeObject, GlassQuality);
    HostWidget->AddToViewport(1500);
}

void UOntoTwinWebInteractionComponent::CaptureWebInput()
{
    if (!PlayerController || !HostWidget || bInputCaptured) return;
    bPreviousMouseCursor = PlayerController->bShowMouseCursor;
    PlayerController->bShowMouseCursor = true;
    FInputModeGameAndUI Mode;
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    Mode.SetHideCursorDuringCapture(false);
    PlayerController->SetInputMode(Mode);
    bInputCaptured = true;
}

void UOntoTwinWebInteractionComponent::RestoreInput()
{
    if (!PlayerController || !bInputCaptured) return;
    FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
    PlayerController->bShowMouseCursor = bPreviousMouseCursor;
    if (bPreviousMouseCursor)
    {
        FInputModeGameAndUI Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Mode.SetHideCursorDuringCapture(false);
        PlayerController->SetInputMode(Mode);
    }
    else
    {
        PlayerController->SetInputMode(FInputModeGameOnly());
    }
    bInputCaptured = false;
}

void UOntoTwinWebInteractionComponent::Back()
{
    if (NavigationHistory.Num() == 0)
    {
        RestoreSceneVisibility();
        bHasCurrentFrame = false;
        if (HostWidget) HostWidget->SetVisibility(ESlateVisibility::Collapsed);
        RestoreInput();
        return;
    }
    const FOntoTwinWebNavigationFrame Previous = NavigationHistory.Pop();
    CurrentFrame = Previous;
    bHasCurrentFrame = true;
    RestoreSceneVisibility();
    ApplyVisibilityFrame(CurrentFrame);
    if (CurrentFrame.FinalUrl.IsEmpty())
    {
        if (HostWidget) HostWidget->SetVisibility(ESlateVisibility::Collapsed);
        RestoreInput();
        if (SceneManager && CurrentFrame.FocusInstanceIds.Num() > 0)
        {
            SceneManager->FocusManagedInstances(CurrentFrame.FocusInstanceIds);
        }
        return;
    }
    EnsureHostWidget();
    if (HostWidget)
    {
        HostWidget->SetVisibility(ESlateVisibility::Visible);
        if (HostWidget->GetCurrentUrl() != CurrentFrame.FinalUrl) HostWidget->Navigate(CurrentFrame.FinalUrl);
    }
    CaptureWebInput();
}

void UOntoTwinWebInteractionComponent::Close()
{
    if (HostWidget) HostWidget->SetVisibility(ESlateVisibility::Collapsed);
    RestoreInput();
    SendToPage(TEXT("visibility_changed"), FString(), nullptr);
}

bool UOntoTwinWebInteractionComponent::HandleHostShortcut(const FKey& Key)
{
    if (!SceneManager || !SceneManager->InteractionManager) return false;
    if (Key == EKeys::Tab)
    {
        Close();
        if (!SceneManager->InteractionManager->IsHudInteractionOpen())
        {
            SceneManager->InteractionManager->ToggleHudInteraction();
        }
        return true;
    }
    if (Key == EKeys::F7)
    {
        Close();
        SceneManager->InteractionManager->ToggleRoaming();
        return true;
    }
    if (Key == EKeys::Escape)
    {
        Back();
        return true;
    }
    return false;
}

void UOntoTwinWebInteractionComponent::ResetHome()
{
    NavigationHistory.Reset();
    bHasCurrentFrame = false;
    RestoreSceneVisibility();
    if (HostWidget) HostWidget->SetVisibility(ESlateVisibility::Collapsed);
    RestoreInput();
    SendToPage(TEXT("visibility_changed"), FString(), nullptr);
}

void UOntoTwinWebInteractionComponent::SetRuntimeEditorSuppressed(bool bSuppressed)
{
    if (bRuntimeEditorSuppressed == bSuppressed) return;
    bRuntimeEditorSuppressed = bSuppressed;
    if (bSuppressed)
    {
        bRestoreAfterRuntimeEditor = HostWidget
            && HostWidget->GetVisibility() == ESlateVisibility::Visible;
        if (HostWidget) HostWidget->SetVisibility(ESlateVisibility::Collapsed);
        RestoreInput();
        return;
    }

    if (bRestoreAfterRuntimeEditor && bHasCurrentFrame)
    {
        EnsureHostWidget();
        if (HostWidget) HostWidget->SetVisibility(ESlateVisibility::Visible);
        ApplyVisibilityFrame(CurrentFrame);
        CaptureWebInput();
        SendContextToPage();
    }
    bRestoreAfterRuntimeEditor = false;
}

bool UOntoTwinWebInteractionComponent::IsPointerOverInteractiveWeb() const
{
    return IsWebOpen() && HostWidget
        && HostWidget->GetVisibility() == ESlateVisibility::Visible
        && HostWidget->IsPointerOverInteractiveRegion();
}

void UOntoTwinWebInteractionComponent::Retry()
{
    if (HostWidget && bHasCurrentFrame) HostWidget->Navigate(CurrentFrame.FinalUrl);
}

void UOntoTwinWebInteractionComponent::ApplySceneScope(
    const TSharedPtr<FJsonObject>& Page,
    const TSharedPtr<FJsonObject>& Context,
    FOntoTwinWebNavigationFrame& Frame)
{
    const FString ScopeType = ScopeTypeForContext(Context);
    const TSharedPtr<FJsonObject> Effects = ObjectField(Page, TEXT("scope_effects"));
    FString SceneBehavior = StringField(Effects, *ScopeType) == TEXT("web_and_scene")
        ? TEXT("isolate_focus")
        : TEXT("web_only");
    if (ScopeType == TEXT("business_view") && PublishedConfig.IsValid())
    {
        const FString BusinessViewId = StringField(Context, TEXT("business_view_id"));
        const TArray<TSharedPtr<FJsonValue>>* Views = nullptr;
        if (PublishedConfig->TryGetArrayField(TEXT("business_views"), Views) && Views)
        {
            for (const TSharedPtr<FJsonValue>& Value : *Views)
            {
                const TSharedPtr<FJsonObject> View = Value.IsValid() ? Value->AsObject() : nullptr;
                if (View.IsValid() && StringField(View, TEXT("business_view_id")) == BusinessViewId)
                {
                    const FString Configured = StringField(View, TEXT("scene_behavior"));
                    if (!Configured.IsEmpty()) SceneBehavior = Configured;
                    break;
                }
            }
        }
    }
    if (SceneBehavior == TEXT("web_only")) return;
    Frame.bSceneScopeActive = true;
    TSet<FString> Target;
    if (ScopeType == TEXT("instance"))
    {
        const FString InstanceId = StringField(Context, TEXT("instance_id"));
        if (IsInstanceKnown(InstanceId)) Target.Add(InstanceId);
    }
    else if (ScopeType == TEXT("zone"))
    {
        const TSet<FString> Zones = DescendantZones(StringField(Context, TEXT("zone_id")));
        for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : InstancesById)
        {
            if (Zones.Contains(StringField(Pair.Value, TEXT("zone_id")))) Target.Add(Pair.Key);
        }
    }
    else if (ScopeType == TEXT("business_view"))
    {
        const FString BusinessViewId = StringField(Context, TEXT("business_view_id"));
        if (const TSet<FString>* Members = BusinessViewMembers.Find(BusinessViewId)) Target = *Members;
        const FString ZoneId = StringField(Context, TEXT("zone_id"));
        if (!ZoneId.IsEmpty())
        {
            const TSet<FString> Zones = DescendantZones(ZoneId);
            for (auto It = Target.CreateIterator(); It; ++It)
            {
                const TSharedPtr<FJsonObject>* Instance = InstancesById.Find(*It);
                if (!Instance || !Zones.Contains(StringField(*Instance, TEXT("zone_id")))) It.RemoveCurrent();
            }
        }
    }
    Frame.FocusInstanceIds = Target;
    if (SceneBehavior == TEXT("highlight"))
    {
        for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : InstancesById)
        {
            Target.Add(Pair.Key);
        }
    }
    else for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : InstancesById)
    {
        if (StringField(Pair.Value, TEXT("zone_id")).IsEmpty()) Target.Add(Pair.Key);
    }
    Frame.VisibleInstanceIds = MoveTemp(Target);
}

void UOntoTwinWebInteractionComponent::ApplyVisibilityFrame(const FOntoTwinWebNavigationFrame& Frame)
{
    if (!Frame.bSceneScopeActive) return;
    TArray<ATwinInstance*> Instances;
    SceneManager->GetManagedInstances(Instances);
    for (ATwinInstance* Instance : Instances)
    {
        if (!Instance || !IsValid(Instance)) continue;
        const TWeakObjectPtr<ATwinInstance> Key(Instance);
        if (!OriginalHiddenStates.Contains(Key)) OriginalHiddenStates.Add(Key, Instance->IsHidden());
        const bool bOriginallyHidden = OriginalHiddenStates.FindRef(Key);
        Instance->SetActorHiddenInGame(
            !Frame.VisibleInstanceIds.Contains(Instance->GetInstanceId()) || bOriginallyHidden);
    }
}

void UOntoTwinWebInteractionComponent::TickVisibilityScope()
{
    if (bHasCurrentFrame && CurrentFrame.bSceneScopeActive) ApplyVisibilityFrame(CurrentFrame);
}

void UOntoTwinWebInteractionComponent::RestoreSceneVisibility()
{
    for (const TPair<TWeakObjectPtr<ATwinInstance>, bool>& Pair : OriginalHiddenStates)
    {
        if (Pair.Key.IsValid()) Pair.Key->SetActorHiddenInGame(Pair.Value);
    }
    OriginalHiddenStates.Reset();
}

TSet<FString> UOntoTwinWebInteractionComponent::DescendantZones(const FString& ZoneId) const
{
    TSet<FString> Result;
    if (ZoneId.IsEmpty()) return Result;
    Result.Add(ZoneId);
    bool bChanged = true;
    while (bChanged)
    {
        bChanged = false;
        for (const TPair<FString, FString>& Pair : ZoneParents)
        {
            if (Result.Contains(Pair.Value) && !Result.Contains(Pair.Key))
            {
                Result.Add(Pair.Key);
                bChanged = true;
            }
        }
    }
    return Result;
}

bool UOntoTwinWebInteractionComponent::IsInstanceKnown(const FString& InstanceId) const
{
    return !InstanceId.IsEmpty() && InstancesById.Contains(InstanceId);
}

bool UOntoTwinWebInteractionComponent::IsZoneKnown(const FString& ZoneId) const
{
    return !ZoneId.IsEmpty() && ZoneParents.Contains(ZoneId);
}

bool UOntoTwinWebInteractionComponent::IsBusinessViewKnown(const FString& BusinessViewId) const
{
    return !BusinessViewId.IsEmpty() && BusinessViewMembers.Contains(BusinessViewId);
}

void UOntoTwinWebInteractionComponent::SelectAndFocusInstance(const FString& InstanceId)
{
    if (!SceneManager) return;
    if (ATwinInstance* Instance = SceneManager->FindManagedInstance(InstanceId))
    {
        SceneManager->SelectOverlayFromSceneInteraction(Instance);
        SceneManager->FocusManagedInstance(Instance);
    }
}

void UOntoTwinWebInteractionComponent::HandleBridgeMessage(const FString& MessageJson)
{
    TSharedPtr<FJsonObject> Message;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageJson);
    if (!FJsonSerializer::Deserialize(Reader, Message) || !Message.IsValid()) return;
    const FString Type = StringField(Message, TEXT("type"));
    const FString RequestId = StringField(Message, TEXT("request_id"));
    const TSharedPtr<FJsonObject> Payload = ObjectField(Message, TEXT("payload"));
    auto Reply = [this, &RequestId](const FString& Result, const FString& Error = FString())
    {
        TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
        Body->SetStringField(TEXT("result"), Result);
        if (!Error.IsEmpty()) Body->SetStringField(TEXT("error_code"), Error);
        SendToPage(TEXT("action_result"), RequestId, Body);
    };
    if (Type == TEXT("ready"))
    {
        const FString Version = StringField(Payload, TEXT("version"));
        bBridgeReady = Version == TEXT("1.0");
        if (HostWidget) HostWidget->SetBridgeReady(bBridgeReady);
        TSharedPtr<FJsonObject> Ready = MakeShared<FJsonObject>();
        Ready->SetStringField(TEXT("version"), TEXT("1.0"));
        Ready->SetBoolField(TEXT("compatible"), bBridgeReady);
        SendToPage(TEXT("host_ready"), RequestId, Ready);
        SendContextToPage();
        return;
    }
    if (!bBridgeReady)
    {
        Reply(TEXT("rejected"), TEXT("bridge_not_ready"));
        return;
    }
    if (Type == TEXT("interactive_regions"))
    {
        TArray<FSlateRect> Regions;
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Payload.IsValid() || !Payload->TryGetArrayField(TEXT("regions"), Values) || !Values || Values->Num() > 256)
        {
            if (HostWidget) HostWidget->SetInteractiveRegions({});
            Reply(TEXT("rejected"), TEXT("invalid_interactive_regions"));
            return;
        }
        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            const TSharedPtr<FJsonObject> Region = Value.IsValid() ? Value->AsObject() : nullptr;
            double X = 0, Y = 0, Width = 0, Height = 0;
            if (!Region.IsValid() || !Region->TryGetNumberField(TEXT("x"), X)
                || !Region->TryGetNumberField(TEXT("y"), Y)
                || !Region->TryGetNumberField(TEXT("width"), Width)
                || !Region->TryGetNumberField(TEXT("height"), Height)
                || !FMath::IsFinite(X) || !FMath::IsFinite(Y)
                || !FMath::IsFinite(Width) || !FMath::IsFinite(Height)
                || X < 0 || Y < 0 || Width <= 0 || Height <= 0
                || X + Width > 16384 || Y + Height > 16384)
            {
                if (HostWidget) HostWidget->SetInteractiveRegions({});
                Reply(TEXT("rejected"), TEXT("invalid_interactive_regions"));
                return;
            }
            Regions.Emplace(X, Y, X + Width, Y + Height);
        }
        if (HostWidget) HostWidget->SetInteractiveRegions(Regions);
        Reply(TEXT("ok"));
        return;
    }
    if (Type == TEXT("select_instance"))
    {
        const FString InstanceId = StringField(Payload, TEXT("instance_id"));
        if (!IsInstanceKnown(InstanceId)) Reply(TEXT("rejected"), TEXT("instance_not_found"));
        else { SelectAndFocusInstance(InstanceId); Reply(TEXT("ok")); }
        return;
    }
    if (Type == TEXT("clear_selection"))
    {
        if (SceneManager) SceneManager->ClearOverlayFromSceneInteraction();
        Reply(TEXT("ok"));
        return;
    }
    if (Type == TEXT("focus_current_scope"))
    {
        if (!SceneManager || !bHasCurrentFrame || CurrentFrame.FocusInstanceIds.Num() == 0)
        {
            Reply(TEXT("rejected"), TEXT("scope_empty"));
        }
        else
        {
            SceneManager->FocusManagedInstances(CurrentFrame.FocusInstanceIds);
            Reply(TEXT("ok"));
        }
        return;
    }
    if (Type == TEXT("close_page"))
    {
        Reply(TEXT("ok"));
        Close();
        return;
    }
    if (Type == TEXT("request_open_scope"))
    {
        const FString ScopeType = StringField(Payload, TEXT("scope_type"));
        bool bOpened = false;
        if (ScopeType == TEXT("zone")) bOpened = OpenZone(StringField(Payload, TEXT("zone_id")));
        else if (ScopeType == TEXT("business_view")) bOpened = OpenBusinessView(
            StringField(Payload, TEXT("business_view_id")), StringField(Payload, TEXT("zone_id")));
        else if (ScopeType == TEXT("instance")) bOpened = OpenInstanceDetail(StringField(Payload, TEXT("instance_id")));
        Reply(bOpened ? TEXT("ok") : TEXT("rejected"), bOpened ? FString() : TEXT("scope_not_found"));
        return;
    }
    if (Type == TEXT("request_open_page"))
    {
        const FString PageId = StringField(Payload, TEXT("page_id"));
        const TSharedPtr<FJsonObject>* Page = PagesById.Find(PageId);
        const TSharedPtr<FJsonObject> Extra = ObjectField(Payload, TEXT("extra_params"));
        if (!Page || !Page->IsValid() || !BoolField(*Page, TEXT("enabled"), true) || !bHasCurrentFrame)
        {
            Reply(TEXT("rejected"), TEXT("page_not_found"));
        }
        else
        {
            const bool bOpened = OpenPage(*Page, CurrentFrame.Trigger, CurrentFrame.Context, Extra, true);
            Reply(bOpened ? TEXT("ok") : TEXT("rejected"), bOpened ? FString() : TEXT("navigation_rejected"));
        }
        return;
    }
    Reply(TEXT("rejected"), TEXT("unsupported_action"));
}

void UOntoTwinWebInteractionComponent::HandlePageLoadStarted()
{
    bBridgeReady = false;
}

void UOntoTwinWebInteractionComponent::HandlePageLoaded()
{
    SendRuntimeEvent(TEXT("page_load"), TEXT("completed"));
}

void UOntoTwinWebInteractionComponent::HandlePageLoadError()
{
    SendRuntimeEvent(TEXT("page_load"), TEXT("failed"), TEXT("browser_load_error"));
}

void UOntoTwinWebInteractionComponent::HandleUrlChanged(const FString& Url)
{
    FString Reason;
    if (!ValidateNavigation(Url, Reason) && HostWidget)
    {
        HostWidget->SetHostStatus(TEXT("已阻止不安全的页面跳转"), true);
        SendRuntimeEvent(TEXT("navigation"), TEXT("rejected"), Reason);
    }
}

void UOntoTwinWebInteractionComponent::SendContextToPage()
{
    if (!bHasCurrentFrame || !CurrentFrame.Context.IsValid()) return;
    TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
    static const TCHAR* Keys[] = {
        TEXT("project_id"), TEXT("business_view_id"), TEXT("zone_id"),
        TEXT("object_type_rid"), TEXT("instance_id")
    };
    for (const TCHAR* Key : Keys)
    {
        const FString Value = StringField(CurrentFrame.Context, Key);
        if (!Value.IsEmpty()) Context->SetStringField(Key, Value);
    }
    Context->SetStringField(TEXT("trigger"), CurrentFrame.Trigger);
    SendToPage(TEXT("context"), FString(), Context);
}

void UOntoTwinWebInteractionComponent::SendToPage(
    const FString& Type,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload)
{
    if (!HostWidget) return;
    TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
    Message->SetStringField(TEXT("type"), Type);
    if (!RequestId.IsEmpty()) Message->SetStringField(TEXT("request_id"), RequestId);
    Message->SetObjectField(TEXT("payload"), Payload.IsValid() ? Payload : MakeShared<FJsonObject>());
    HostWidget->ExecuteJavascript(FString::Printf(
        TEXT("window.OntoTwinBridge&&window.OntoTwinBridge.receive(%s);"), *JsonString(Message)));
}

void UOntoTwinWebInteractionComponent::SendRuntimeEvent(
    const FString& EventType,
    const FString& Result,
    const FString& ErrorCode) const
{
    if (!SceneManager || bShuttingDown) return;
    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("event_type"), EventType);
    Body->SetStringField(TEXT("result"), Result);
    if (!ErrorCode.IsEmpty()) Body->SetStringField(TEXT("error_code"), ErrorCode);
    if (bHasCurrentFrame) Body->SetStringField(TEXT("page_id"), CurrentFrame.PageId);
    const FString JsonBody = JsonString(Body);
    FString BaseUrl = SceneManager->BackendBaseUrl;
    BaseUrl.RemoveFromEnd(TEXT("/"));
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BaseUrl + TEXT("/api/v2/web-interactions/runtime-events"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    AddProjectHeaders(Request);
    Request->SetContentAsString(JsonBody);
    Request->ProcessRequest();
}

FString UOntoTwinWebInteractionComponent::JsonString(const TSharedPtr<FJsonObject>& Object)
{
    FString Result;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
    return Result;
}
