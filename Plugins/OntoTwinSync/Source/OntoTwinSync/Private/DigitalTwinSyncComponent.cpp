// ============================================================================
// DigitalTwinSyncComponent.cpp
//
// 数字孪生同步组件 — 实现文件
//
// 实现逻辑概览：
//   BeginPlay  → 初始化实体 ID、创建动态材质
//   Tick       → 计时 → 发起 HTTP GET → 收到 JSON → 解析 → 驱动 Actor
//   ApplyState → 分发到 Spatial / Visual / Behavior 三个子处理函数
// ============================================================================

#include "DigitalTwinSyncComponent.h"
#include "GameFramework/Actor.h"
#include "Components/MeshComponent.h"
#include "Components/TextBlock.h"          // UMG 文字控件
#include "Components/PanelWidget.h"        // UMG 面板控件
#include "Components/EditableTextBox.h"    // UMG 可编辑文本框
#include "Blueprint/UserWidget.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/Engine.h"

// ── 构造函数 ─────────────────────────────────────────────────────────────────

UDigitalTwinSyncComponent::UDigitalTwinSyncComponent()
{
    // 开启 Tick，用于轮询计时和平滑插值
    PrimaryComponentTick.bCanEverTick = true;

    // 默认 Tick 间隔不限制（通过内部计时器控制轮询频率）
    PrimaryComponentTick.TickInterval = 0.0f;
}

// ── BeginPlay ────────────────────────────────────────────────────────────────

void UDigitalTwinSyncComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        UE_LOG(LogTemp, Error, TEXT("[数字孪生] 组件未附加到任何 Actor！"));
        return;
    }

    // ── 实体 ID：优先使用手动配置，否则从 Actor Tags[0] 读取 ──────────────
    if (InstanceId.IsEmpty() && Owner->Tags.Num() > 0)
    {
        InstanceId = Owner->Tags[0].ToString();
        UE_LOG(LogTemp, Log, TEXT("[数字孪生] 自动读取 Actor Tag 作为实体ID: %s"), *InstanceId);
    }

    // ── 初始化目标值为当前 Actor 的变换 ──────────────────────────────────
    TargetLocation = Owner->GetActorLocation();
    TargetRotation = Owner->GetActorRotation();
    TargetScale    = Owner->GetActorScale3D();

    // ── 创建动态材质实例（用于运行时切换颜色） ───────────────────────────
    // 查找第一个 MeshComponent 的第一个材质槽
    UMeshComponent* MeshComp = Owner->FindComponentByClass<UMeshComponent>();
    if (MeshComp && MeshComp->GetNumMaterials() > 0)
    {
        UMaterialInterface* BaseMat = MeshComp->GetMaterial(0);
        if (BaseMat)
        {
            DynMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
            MeshComp->SetMaterial(0, DynMaterial);
            UE_LOG(LogTemp, Log, TEXT("[数字孪生] 动态材质实例创建成功"));
        }
    }

    // ── 智能搜寻并分配组件 (仅在未手动分配时触发) ────────────────────────
    
    TArray<UNiagaraComponent*> NiagaraComps;
    Owner->GetComponents<UNiagaraComponent>(NiagaraComps);

    // 如果未手动分配，尝试通过名字匹配搜索分配
    for (int32 i = 0; i < NiagaraComps.Num(); i++)
    {
        UNiagaraComponent* NC = NiagaraComps[i];
        FString N = NC->GetFName().ToString();
        
        if (!SparkFxComponent && N.Equals(TEXT("FX_Spark")))
        {
            SparkFxComponent = NC;
        }
        else if (!SmokeFxComponent && N.Equals(TEXT("FX_Smoke")))
        {
            SmokeFxComponent = NC;
        }
    }

    // fallback：如果有多的组件，则兜底分配给空位
    if (!SparkFxComponent && NiagaraComps.Num() >= 1)
    {
        SparkFxComponent = NiagaraComps[0];
    }
    if (!SmokeFxComponent && NiagaraComps.Num() >= 2)
    {
        SmokeFxComponent = NiagaraComps[1];
    }

    // 智能搜寻 WidgetComponent
    if (!LabelWidgetComponent)
    {
        TArray<UWidgetComponent*> WidgetComps;
        Owner->GetComponents<UWidgetComponent>(WidgetComps);
        if (WidgetComps.Num() > 0)
        {
            LabelWidgetComponent = WidgetComps[0];
        }
    }

    UE_LOG(LogTemp, Log,
           TEXT("[数字孪生] 组件初始化完毕 | 实体ID=%s | 轮询间隔=%.2fs | API=%s"),
           *InstanceId, PollIntervalSeconds, *ApiUrl);

    // 将 UE 世界真实的初始坐标刻印到数字孪生后端
    PushStateToBackend();
}

// ── EndPlay ──────────────────────────────────────────────────────────────────

void UDigitalTwinSyncComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
    UE_LOG(LogTemp, Log, TEXT("[数字孪生] 组件已销毁，停止轮询"));
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void UDigitalTwinSyncComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    AActor* Owner = GetOwner();
    if (!Owner) return;

    // ── 1. 平滑插值：每帧将 Actor 朝目标位置/旋转平滑移动 ────────────────
    {
        // 位置插值（VInterpTo：匀速逼近，手感自然）
        FVector CurrentLoc = Owner->GetActorLocation();
        FVector NewLoc = FMath::VInterpTo(CurrentLoc, TargetLocation,
                                          DeltaTime, LocationInterpSpeed);
        Owner->SetActorLocation(NewLoc);

        // 旋转插值（RInterpTo：平滑旋转，避免万向锁抖动）
        FRotator CurrentRot = Owner->GetActorRotation();
        FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRotation,
                                            DeltaTime, RotationInterpSpeed);
        Owner->SetActorRotation(NewRot);

        // 缩放直接设置（通常不需要插值）
        Owner->SetActorScale3D(TargetScale);
    }

    // ── 2. 轮询计时器：到达间隔后发起 HTTP 请求 ──────────────────────────
    TimeSinceLastPoll += DeltaTime;
    if (TimeSinceLastPoll >= PollIntervalSeconds && !bRequestInFlight)
    {
        TimeSinceLastPoll = 0.0f;
        SendHttpRequest();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// HTTP 通信
// ═══════════════════════════════════════════════════════════════════════════

void UDigitalTwinSyncComponent::SendHttpRequest()
{
    // 创建 HTTP 请求
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
        FHttpModule::Get().CreateRequest();

    // ── 拼接 InstanceId ─────────────────────────────────────────────────
    FString FinalUrl = ApiUrl;
    if (!InstanceId.IsEmpty())
    {
        // 如果 ApiUrl 尚未包含 id 查询参数，则自动追加
        if (!ApiUrl.Contains(TEXT("?id=")) && !ApiUrl.Contains(TEXT("&id=")))
        {
            FString Separator = ApiUrl.Contains(TEXT("?")) ? TEXT("&") : TEXT("?");
            FinalUrl = FString::Printf(TEXT("%s%sid=%s"), *ApiUrl, *Separator, *InstanceId);
        }
    }

    Request->SetURL(FinalUrl);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

    // 绑定回调（UE5 保证回调在 Game Thread 执行）
    Request->OnProcessRequestComplete().BindUObject(
        this, &UDigitalTwinSyncComponent::OnHttpResponseReceived);

    // 设置请求锁，防止重复请求
    bRequestInFlight = true;

    // 发送请求
    Request->ProcessRequest();
}

void UDigitalTwinSyncComponent::OnHttpResponseReceived(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful)
{
    // 释放请求锁
    bRequestInFlight = false;

    // ── 错误处理 ─────────────────────────────────────────────────────────
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[数字孪生] HTTP 请求失败，将在下次轮询重试"));
        return;
    }

    int32 StatusCode = Response->GetResponseCode();
    if (StatusCode != 200)
    {
        UE_LOG(LogTemp, Warning,
               TEXT("[数字孪生] 后端返回非 200 状态码: %d"), StatusCode);
        return;
    }

    // ── 解析 JSON ────────────────────────────────────────────────────────
    FString ResponseBody = Response->GetContentAsString();

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[数字孪生] JSON 解析失败: %s"),
               *ResponseBody.Left(200));
        return;
    }

    // ── 实体 ID 匹配检查 ─────────────────────────────────────────────────
    // 检查 JSON 中的 asset_id 是否与本组件绑定的实体 ID 一致
    FString JsonAssetId;
    if (JsonObject->TryGetStringField(TEXT("asset_id"), JsonAssetId))
    {
        if (!InstanceId.IsEmpty() && JsonAssetId != InstanceId)
        {
            // ID 不匹配，跳过本次更新
            UE_LOG(LogTemp, Verbose,
                   TEXT("[数字孪生] 实体ID不匹配: JSON=%s, 本组件=%s，跳过"),
                   *JsonAssetId, *InstanceId);
            return;
        }
    }

    // ── 时间戳更新：仅记录最新时间戳，始终执行状态应用 ────────────────────
    // 注意：这里不再有“未变则跳过”逻辑！原先的时间戳对比导致后端没有新操作时
    // UE 会永远跳过坐标应用导致模型不动
    double NewTimestamp = 0.0;
    if (JsonObject->TryGetNumberField(TEXT("timestamp"), NewTimestamp))
    {
        LastTimestamp = NewTimestamp;
    }

    // ── 应用状态到 Actor ─────────────────────────────────────────────────
    ApplyStateFromJson(JsonObject);
}

void UDigitalTwinSyncComponent::PushStateToBackend()
{
    if (InstanceId.IsEmpty()) return;

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();

    FString UpdateUrl = ApiUrl.Replace(TEXT("/api/state"), TEXT("/api/update"));
    if (!UpdateUrl.Contains(TEXT("?id=")) && !UpdateUrl.Contains(TEXT("&id=")))
    {
        FString Separator = UpdateUrl.Contains(TEXT("?")) ? TEXT("&") : TEXT("?");
        UpdateUrl = FString::Printf(TEXT("%s%sid=%s"), *UpdateUrl, *Separator, *InstanceId);
    }

    Request->SetURL(UpdateUrl);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

    TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject());
    JsonObj->SetStringField(TEXT("id"), InstanceId);
    JsonObj->SetNumberField(TEXT("translation_x"), TargetLocation.X);
    JsonObj->SetNumberField(TEXT("translation_y"), TargetLocation.Y);
    JsonObj->SetNumberField(TEXT("translation_z"), TargetLocation.Z);
    JsonObj->SetNumberField(TEXT("rotation_x"), TargetRotation.Roll);
    JsonObj->SetNumberField(TEXT("rotation_y"), TargetRotation.Pitch);
    JsonObj->SetNumberField(TEXT("rotation_z"), TargetRotation.Yaw);
    JsonObj->SetNumberField(TEXT("scale_x"), TargetScale.X);
    JsonObj->SetNumberField(TEXT("scale_y"), TargetScale.Y);
    JsonObj->SetNumberField(TEXT("scale_z"), TargetScale.Z);

    FString ContentString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ContentString);
    FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

    Request->SetContentAsString(ContentString);
    Request->OnProcessRequestComplete().BindUObject(this, &UDigitalTwinSyncComponent::OnStatePushed);
    Request->ProcessRequest();
}

void UDigitalTwinSyncComponent::OnStatePushed(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[数字孪生] 初始坐标推送失败: Web状态可能不同步"));
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("[数字孪生] 初始坐标签名已成功植入中心服务器 (Code: %d)"), Response->GetResponseCode());
}

// ═══════════════════════════════════════════════════════════════════════════
// 状态分发器
// ═══════════════════════════════════════════════════════════════════════════

void UDigitalTwinSyncComponent::ApplyStateFromJson(
    TSharedPtr<FJsonObject> JsonObject)
{
    // 按三大本体接口分别处理
    ApplySpatial(JsonObject);   // I3DSpatial：空间变换
    ApplyVisual(JsonObject);    // I3DVisual：视觉表达
    ApplyBehavior(JsonObject);  // I3DBehavior：动态行为
}

// ═══════════════════════════════════════════════════════════════════════════
// I3DSpatial — 空间变换接口
// ═══════════════════════════════════════════════════════════════════════════

void UDigitalTwinSyncComponent::ApplySpatial(
    const TSharedPtr<FJsonObject>& Json)
{
    // ── 位置：映射到目标值，Tick 中插值平滑 ──────────────────────────────
    // 注意：UE5 坐标系 X=前，Y=右，Z=上
    // 后端坐标单位为厘米（cm），与 UE 默认单位一致
    double tx = 0, ty = 0, tz = 0;
    Json->TryGetNumberField(TEXT("translation_x"), tx);
    Json->TryGetNumberField(TEXT("translation_y"), ty);
    Json->TryGetNumberField(TEXT("translation_z"), tz);
    TargetLocation = FVector(tx, ty, tz);

    // ── 旋转：映射到目标旋转值 ──────────────────────────────────────────
    double rx = 0, ry = 0, rz = 0;
    Json->TryGetNumberField(TEXT("rotation_x"), rx);
    Json->TryGetNumberField(TEXT("rotation_y"), ry);
    Json->TryGetNumberField(TEXT("rotation_z"), rz);
    // UE 旋转分量：Pitch(Y轴), Yaw(Z轴), Roll(X轴)
    TargetRotation = FRotator(ry, rz, rx);

    // ── 缩放：直接设定，下一帧生效 ──────────────────────────────────────
    double sx = 1, sy = 1, sz = 1;
    Json->TryGetNumberField(TEXT("scale_x"), sx);
    Json->TryGetNumberField(TEXT("scale_y"), sy);
    Json->TryGetNumberField(TEXT("scale_z"), sz);
    TargetScale = FVector(sx, sy, sz);
}

// ═══════════════════════════════════════════════════════════════════════════
// I3DVisual — 视觉表达接口
// ═══════════════════════════════════════════════════════════════════════════

void UDigitalTwinSyncComponent::ApplyVisual(
    const TSharedPtr<FJsonObject>& Json)
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    // ── 材质变体 (material_variant) ──────────────────────────────────────
    FString MaterialVariant;
    if (Json->TryGetStringField(TEXT("material_variant"), MaterialVariant))
    {
        // 仅在变体发生变化时更新（避免每帧重复设置）
        if (MaterialVariant != CurrentMaterialVariant && DynMaterial)
        {
            CurrentMaterialVariant = MaterialVariant;

            // 根据变体名称选择对应颜色
            FLinearColor NewColor = ColorNormal; // 默认
            if (MaterialVariant == TEXT("fault"))
            {
                NewColor = ColorFault;
            }
            else if (MaterialVariant == TEXT("alarm"))
            {
                NewColor = ColorAlarm;
            }
            else if (MaterialVariant == TEXT("offline"))
            {
                NewColor = ColorOffline;
            }

            // 设置动态材质参数 "BaseColor"
            // （需要材质中有一个名为 BaseColor 的 Vector Parameter）
            DynMaterial->SetVectorParameterValue(TEXT("BaseColor"), NewColor);

            UE_LOG(LogTemp, Log,
                   TEXT("[数字孪生] 材质变体切换: %s → 颜色(%s)"),
                   *MaterialVariant, *NewColor.ToString());
        }
    }

    // ── 可见性 (is_visible) ──────────────────────────────────────────────
    bool bIsVisible = true;
    if (Json->TryGetBoolField(TEXT("is_visible"), bIsVisible))
    {
        // SetHiddenInGame：true = 隐藏，所以取反
        Owner->SetActorHiddenInGame(!bIsVisible);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// I3DBehavior — 动态行为接口
// ═══════════════════════════════════════════════════════════════════════════

void UDigitalTwinSyncComponent::ApplyBehavior(
    const TSharedPtr<FJsonObject>& Json)
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    // ── 动画状态 (animation_state) ──────────────────────────────────────
    // 将 animation_state 字符串传递给动画蓝图（AnimInstance）的变量
    FString AnimState;
    if (Json->TryGetStringField(TEXT("animation_state"), AnimState))
    {
        if (AnimState != CurrentAnimationState)
        {
            CurrentAnimationState = AnimState;

            // 查找 SkeletalMeshComponent 上的 AnimInstance
            USkeletalMeshComponent* SkelMesh =
                Owner->FindComponentByClass<USkeletalMeshComponent>();
            if (SkelMesh)
            {
                UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
                if (AnimInst)
                {
                    // 方案 A：通过 Property 反射设置蓝图变量 "AnimationState"
                    // （需要动画蓝图中有一个名为 AnimationState 的 String 变量）
                    FProperty* Prop = AnimInst->GetClass()->FindPropertyByName(
                        TEXT("AnimationState"));
                    if (Prop)
                    {
                        FStrProperty* StrProp = CastField<FStrProperty>(Prop);
                        if (StrProp)
                        {
                            StrProp->SetPropertyValue_InContainer(AnimInst, AnimState);
                        }
                    }

                    UE_LOG(LogTemp, Log,
                           TEXT("[数字孪生] 动画状态切换: %s"), *AnimState);
                }
            }
        }
    }

    // ── 特效触发 (fx_trigger) ────────────────────────────────────────────
    FString FxTrigger;
    if (Json->TryGetStringField(TEXT("fx_trigger"), FxTrigger))
    {
        if (FxTrigger != CurrentFxTrigger)
        {
            CurrentFxTrigger = FxTrigger;

            // 先关闭所有特效
            SetFxActive(SparkFxComponent, false);
            SetFxActive(SmokeFxComponent, false);

            // 根据触发名称激活对应特效
            if (FxTrigger == TEXT("spark"))
            {
                SetFxActive(SparkFxComponent, true);
                UE_LOG(LogTemp, Log, TEXT("[数字孪生] 激活特效: 电火花"));
            }
            else if (FxTrigger == TEXT("smoke"))
            {
                SetFxActive(SmokeFxComponent, true);
                UE_LOG(LogTemp, Log, TEXT("[数字孪生] 激活特效: 烟雾"));
            }
            else
            {
                UE_LOG(LogTemp, Log, TEXT("[数字孪生] 特效已全部关闭"));
            }
        }
    }

    // ── UI 标签内容 (ui_label_content) ───────────────────────────────────
    FString LabelContent;
    if (Json->TryGetStringField(TEXT("ui_label_content"), LabelContent))
    {
        AActor* LabelOwner = GetOwner();
        if (!LabelOwner) return;

        TArray<UWidgetComponent*> AllWC;
        LabelOwner->GetComponents<UWidgetComponent>(AllWC);

        bool bUpdated = false;
        for (int32 i = 0; i < AllWC.Num() && !bUpdated; i++)
        {
            UUserWidget* W = AllWC[i]->GetWidget();
            if (!W) continue;

            // 尝试找到 LabelText 控件（支持 TextBlock 和 EditableTextBox 两种类型）
            UWidget* FoundWidget = W->GetWidgetFromName(TEXT("LabelText"));

            // 如果 GetWidgetFromName 失败，遍历根面板子控件
            if (!FoundWidget)
            {
                UWidget* Root = W->GetRootWidget();
                UPanelWidget* Panel = Cast<UPanelWidget>(Root);
                if (Panel && Panel->GetChildrenCount() > 0)
                {
                    FoundWidget = Panel->GetChildAt(0);
                }
            }

            if (FoundWidget)
            {
                // 尝试作为 EditableTextBox
                UEditableTextBox* ETB = Cast<UEditableTextBox>(FoundWidget);
                if (ETB)
                {
                    ETB->SetText(FText::FromString(LabelContent));
                    UE_LOG(LogTemp, Log, TEXT("[数字孪生] UI标签已更新(EditableTextBox): %s"), *LabelContent);
                    bUpdated = true;
                }
                else
                {
                    // 尝试作为 TextBlock
                    UTextBlock* TB = Cast<UTextBlock>(FoundWidget);
                    if (TB)
                    {
                        TB->SetText(FText::FromString(LabelContent));
                        UE_LOG(LogTemp, Log, TEXT("[数字孪生] UI标签已更新(TextBlock): %s"), *LabelContent);
                        bUpdated = true;
                    }
                }
            }
        }

        if (!bUpdated)
        {
            static bool bLoggedOnce = false;
            if (!bLoggedOnce)
            {
                for (int32 i = 0; i < AllWC.Num(); i++)
                {
                    UUserWidget* W = AllWC[i]->GetWidget();
                    if (W)
                    {
                        UWidget* Root = W->GetRootWidget();
                        UE_LOG(LogTemp, Warning, TEXT("[数字孪生] Widget[%d] Root=%s(%s)"),
                            i,
                            Root ? *Root->GetName() : TEXT("NULL"),
                            Root ? *Root->GetClass()->GetName() : TEXT("?"));
                        UPanelWidget* Panel = Cast<UPanelWidget>(Root);
                        if (Panel)
                        {
                            for (int32 c = 0; c < Panel->GetChildrenCount(); c++)
                            {
                                UWidget* Child = Panel->GetChildAt(c);
                                UE_LOG(LogTemp, Warning, TEXT("[数字孪生]   Child[%d]=%s (%s)"),
                                    c, *Child->GetName(), *Child->GetClass()->GetName());
                            }
                        }
                    }
                }
                bLoggedOnce = true;
            }
            UE_LOG(LogTemp, Warning, TEXT("[数字孪生] UI标签失败: 未找到TextBlock"));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 辅助函数
// ═══════════════════════════════════════════════════════════════════════════

void UDigitalTwinSyncComponent::SetFxActive(UNiagaraComponent* Comp, bool bActive)
{
    if (!Comp) return;

    if (bActive)
    {
        // 激活并开始播放粒子
        Comp->SetVisibility(true);
        Comp->Activate(true);
    }
    else
    {
        // 停止粒子并隐藏
        Comp->Deactivate();
        Comp->SetVisibility(false);
    }
}
