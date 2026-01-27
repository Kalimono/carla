// Simple follower component implementation (scaffold).
#include "HeroFollowerComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Carla/Game/CarlaStatics.h"
#include "Carla/Game/CarlaEpisode.h"
#include "Carla/Vehicle/CarlaWheeledVehicle.h"
#include "WheeledVehicleMovementComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Slate/SlateTextures.h"
#include "EngineUtils.h"

UHeroFollowerComponent::UHeroFollowerComponent()
{
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = true;
  HeroVehicle = nullptr;
  RootDisplayActor = nullptr;
  bHasAutoAttached = false;
  HeroSearchLogTimer = 0.0f;
  LeftRearCapture = nullptr;
  RightRearCapture = nullptr;
  LeftRearRenderTarget = nullptr;
  RightRearRenderTarget = nullptr;
  MirrorTimer = 0.0f;
}

void UHeroFollowerComponent::BeginPlay()
{
  Super::BeginPlay();

  // Create captures only when needed
  if (bEnableRearviewMirrors)
  {
    CreateMirrorCaptures();
    if (ShouldCreateWidgetForThisNode())
    {
      CreateMirrorWidgets();
    }
  }
  UE_LOG(LogTemp, Log, TEXT("HeroFollower: Initialized Follower Component"));

  // Try find local nDisplay root actor (if not already cached)
  TryFindRootDisplayActor();

  // If we found a root that isn't our owner, attempt to auto-attach the component there
  if (RootDisplayActor && !bHasAutoAttached)
  {
    TryAutoAttachToRoot();
  }
}

void UHeroFollowerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  LeftRearRenderTarget = nullptr;
  RightRearRenderTarget = nullptr;
  LeftRearBrush.Reset();
  RightRearBrush.Reset();
  Super::EndPlay(EndPlayReason);
}

void UHeroFollowerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  if (!RootDisplayActor)
  {
    TryFindRootDisplayActor();
  }

  UE_LOG(LogTemp, Verbose, TEXT("HeroFollower: Tick - updating hero vehicle tracking"));  

  // Mirror the behavior in CarlaSpectatorPawn: search for hero until found,
  // then update transforms each tick. No audio handling here.
  UpdateHeroVehicleTracking(DeltaTime);

  if (bEnableRearviewMirrors && (LeftRearCapture || RightRearCapture))
  {
    MirrorTimer += DeltaTime;
    float Interval = 1.0f / FMath::Max(1.0f, MirrorUpdateHz);
    if (MirrorTimer >= Interval)
    {
      MirrorTimer = 0.0f;
      if (LeftRearCapture) LeftRearCapture->CaptureScene();
      if (RightRearCapture) RightRearCapture->CaptureScene();
    }
  }
}

void UHeroFollowerComponent::TryFindHero()
{
  UWorld* World = GetWorld();
  if (!World) return;

  UCarlaEpisode* Episode = UCarlaStatics::GetCurrentEpisode(World);
  if (!Episode) return;

  const FActorRegistry& Registry = Episode->GetActorRegistry();
  for (auto It = Registry.begin(); It != Registry.end(); ++It)
  {
    FCarlaActor* CarlaActor = It.Value().Get();
    if (CarlaActor && CarlaActor->GetActorType() == FCarlaActor::ActorType::Vehicle)
    {
      const FActorInfo* ActorInfo = CarlaActor->GetActorInfo();
      if (ActorInfo)
      {
        const FActorAttribute* RoleNameAttr = ActorInfo->Description.Variations.Find(TEXT("role_name"));
        FString RoleName = RoleNameAttr ? RoleNameAttr->Value : TEXT("");
        if (RoleName.Equals(TEXT("hero"), ESearchCase::IgnoreCase) || RoleName.Equals(TEXT("ego"), ESearchCase::IgnoreCase))
        {
          HeroVehicle = CarlaActor->GetActor();
          if (HeroVehicle)
          {
            HeroVehicle->SetActorHiddenInGame(true);
            UE_LOG(LogTemp, Log, TEXT("HeroFollower: Found hero vehicle %s"), *HeroVehicle->GetName());
            break;
          }
        }
      }
    }
  }
}

void UHeroFollowerComponent::TryFindRootDisplayActor()
{
  // If we've already found and cached the root actor, don't search again
  if (RootDisplayActor) return;

  UWorld* World = GetWorld();
  if (!World) return;

  // Look for any actor whose class name contains "DisplayCluster" or "RootCluster"
  for (TActorIterator<AActor> It(World); It; ++It)
  {
    AActor* Actor = *It;
    if (!Actor) continue;
    FString ClassName = Actor->GetClass()->GetName();
    if (ClassName.Contains(TEXT("DisplayCluster")) || ClassName.Contains(TEXT("RootCluster")) || ClassName.Contains(TEXT("ClusterRoot")))
    {
      RootDisplayActor = Actor;
      UE_LOG(LogTemp, Log, TEXT("HeroFollower: Found nDisplay root actor: %s (class=%s)"), *Actor->GetName(), *ClassName);
      break;
    }
  }
}

void UHeroFollowerComponent::TryAutoAttachToRoot()
{
  if (bHasAutoAttached) return;
  if (!RootDisplayActor) return;

  AActor* Owner = GetOwner();
  if (!Owner)
  {
    bHasAutoAttached = true;
    return;
  }

  // If we're already attached to the root, nothing to do
  if (Owner == RootDisplayActor)
  {
    bHasAutoAttached = true;
    return;
  }

  // Create a new follower component on the RootDisplayActor and copy settings
  UHeroFollowerComponent* NewComp = NewObject<UHeroFollowerComponent>(RootDisplayActor);
  if (NewComp)
  {
    NewComp->CameraOffset = CameraOffset;
    NewComp->bEnableRearviewMirrors = bEnableRearviewMirrors;
    NewComp->MirrorWidth = MirrorWidth;
    NewComp->MirrorHeight = MirrorHeight;
    NewComp->MirrorUpdateHz = MirrorUpdateHz;
    NewComp->bCreateWidgetOnLocalNode = bCreateWidgetOnLocalNode;

    // Set the discovered root on the new component and mark it as already auto-attached
    NewComp->RootDisplayActor = RootDisplayActor;
    NewComp->bHasAutoAttached = true;

    NewComp->RegisterComponent();

    // Initialize mirrors/widgets on the new component immediately if requested
    if (NewComp->bEnableRearviewMirrors)
    {
      NewComp->CreateMirrorCaptures();
      if (NewComp->ShouldCreateWidgetForThisNode())
      {
        NewComp->CreateMirrorWidgets();
      }
    }

    UE_LOG(LogTemp, Log, TEXT("HeroFollower: Auto-attached follower component to root actor %s and registered new component."), *RootDisplayActor->GetName());

    // Destroy this component to avoid duplicate work
    DestroyComponent();
  }

  bHasAutoAttached = true;
}

void UHeroFollowerComponent::UpdateOwnerTransform()
{
  if (!HeroVehicle) return;
  FTransform VehicleT = HeroVehicle->GetActorTransform();
  FVector WorldOffset = VehicleT.TransformVector(CameraOffset);
  FVector NewLocation = VehicleT.GetLocation() + WorldOffset;
  FRotator NewRot = VehicleT.GetRotation().Rotator();

  AActor* Owner = GetOwner();
  if (Owner)
  {
    Owner->SetActorLocationAndRotation(NewLocation, NewRot);
  }

  // Also move the nDisplay root actor if present and it's not the same as our owner
  if (RootDisplayActor && RootDisplayActor != Owner)
  {
    RootDisplayActor->SetActorLocationAndRotation(NewLocation, NewRot);
  }
}

void UHeroFollowerComponent::CreateMirrorCaptures()
{
  AActor* Owner = GetOwner();
  if (!Owner) return;

  // Create captures as components on the owner
  LeftRearCapture = NewObject<USceneCaptureComponent2D>(Owner, TEXT("LeftRearCapture"));
  if (LeftRearCapture)
  {
    LeftRearCapture->RegisterComponent();
    LeftRearCapture->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    LeftRearCapture->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
    LeftRearCapture->CaptureSource = SCS_FinalColorLDR;
    LeftRearCapture->bCaptureEveryFrame = false;
    LeftRearCapture->bCaptureOnMovement = false;
    LeftRearCapture->ShowFlags.SetMotionBlur(false);
    LeftRearCapture->ShowFlags.SetLensFlares(false);
    LeftRearCapture->ShowFlags.SetBloom(false);

    LeftRearRenderTarget = NewObject<UTextureRenderTarget2D>(this);
    if (LeftRearRenderTarget)
    {
      LeftRearRenderTarget->InitAutoFormat(MirrorWidth, MirrorHeight);
      LeftRearRenderTarget->RenderTargetFormat = RTF_RGBA8;
      LeftRearRenderTarget->UpdateResourceImmediate();
      LeftRearCapture->TextureTarget = LeftRearRenderTarget;
    }
  }

  RightRearCapture = NewObject<USceneCaptureComponent2D>(Owner, TEXT("RightRearCapture"));
  if (RightRearCapture)
  {
    RightRearCapture->RegisterComponent();
    RightRearCapture->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    RightRearCapture->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
    RightRearCapture->CaptureSource = SCS_FinalColorLDR;
    RightRearCapture->bCaptureEveryFrame = false;
    RightRearCapture->bCaptureOnMovement = false;
    RightRearCapture->ShowFlags.SetMotionBlur(false);
    RightRearCapture->ShowFlags.SetLensFlares(false);
    RightRearCapture->ShowFlags.SetBloom(false);

    RightRearRenderTarget = NewObject<UTextureRenderTarget2D>(this);
    if (RightRearRenderTarget)
    {
      RightRearRenderTarget->InitAutoFormat(MirrorWidth, MirrorHeight);
      RightRearRenderTarget->RenderTargetFormat = RTF_RGBA8;
      RightRearRenderTarget->UpdateResourceImmediate();
      RightRearCapture->TextureTarget = RightRearRenderTarget;
    }
  }
}

bool UHeroFollowerComponent::ShouldCreateWidgetForThisNode() const
{
  if (!bCreateWidgetOnLocalNode) return false;
  UWorld* World = GetWorld();
  if (!World) return false;
  APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
  return (PC != nullptr);
}

void UHeroFollowerComponent::CreateMirrorWidgets()
{
  UWorld* World = GetWorld();
  if (!World) return;

  UGameViewportClient* ViewportClient = World->GetGameViewport();
  if (!ViewportClient) return;

  // Create brushes
  LeftRearBrush = MakeShared<FSlateBrush>();
  if (LeftRearRenderTarget)
  {
    LeftRearBrush->SetResourceObject(LeftRearRenderTarget);
    LeftRearBrush->ImageSize = FVector2D(LeftRearRenderTarget->SizeX, LeftRearRenderTarget->SizeY);
    LeftRearBrush->DrawAs = ESlateBrushDrawType::Image;
  }

  RightRearBrush = MakeShared<FSlateBrush>();
  if (RightRearRenderTarget)
  {
    RightRearBrush->SetResourceObject(RightRearRenderTarget);
    RightRearBrush->ImageSize = FVector2D(RightRearRenderTarget->SizeX, RightRearRenderTarget->SizeY);
    RightRearBrush->DrawAs = ESlateBrushDrawType::Image;
  }

  TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas)
    + SConstraintCanvas::Slot()
    .Anchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f))
    .Offset(FMargin(20.0f, 20.0f, 200.0f, 200.0f))
    .Alignment(FVector2D(0.0f, 0.0f))
    .AutoSize(false)
    [
      SAssignNew(LeftMirrorWidget, SBox)
      .WidthOverride(200.0f)
      .HeightOverride(200.0f)
      .Visibility(bEnableRearviewMirrors ? EVisibility::Visible : EVisibility::Hidden)
      [
        SNew(SImage)
        .Image(LeftRearBrush.Get())
      ]
    ]
    + SConstraintCanvas::Slot()
    .Anchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f))
    .Offset(FMargin(-220.0f, 20.0f, 200.0f, 200.0f))
    .Alignment(FVector2D(1.0f, 0.0f))
    .AutoSize(false)
    [
      SAssignNew(RightMirrorWidget, SBox)
      .WidthOverride(200.0f)
      .HeightOverride(200.0f)
      .Visibility(bEnableRearviewMirrors ? EVisibility::Visible : EVisibility::Hidden)
      [
        SNew(SImage)
        .Image(RightRearBrush.Get())
      ]
    ];

  ViewportClient->AddViewportWidgetContent(Canvas, 0);
  UE_LOG(LogTemp, Log, TEXT("HeroFollower: Mirror widgets added to viewport"));
}

void UHeroFollowerComponent::UpdateHeroVehicleTracking(float DeltaTime)
{
  UE_LOG(LogTemp, Verbose, TEXT("HeroFollower: UpdateHeroVehicleTracking called"));
  UWorld* World = GetWorld();
  if (!World) return;

  // Try to find hero vehicle if we don't have it cached
  if (HeroVehicle == nullptr)
  {
    UE_LOG(LogTemp, Verbose, TEXT("HeroFollower: Searching for hero vehicle"));
    UCarlaEpisode* Episode = UCarlaStatics::GetCurrentEpisode(World);
    if (!Episode)
    {
      return; // Episode not ready yet
    }

    bool bFoundHero = false;
    const FActorRegistry& Registry = Episode->GetActorRegistry();

    for (auto It = Registry.begin(); It != Registry.end(); ++It)
    {
      FCarlaActor* CarlaActor = It.Value().Get();
      if (CarlaActor && CarlaActor->GetActorType() == FCarlaActor::ActorType::Vehicle)
      {
        const FActorInfo* ActorInfo = CarlaActor->GetActorInfo();
        if (ActorInfo)
        {
          const FActorAttribute* RoleNameAttr = ActorInfo->Description.Variations.Find(TEXT("role_name"));
          FString RoleName = RoleNameAttr ? RoleNameAttr->Value : TEXT("");

          if (RoleName.Equals(TEXT("hero"), ESearchCase::IgnoreCase) || RoleName.Equals(TEXT("ego"), ESearchCase::IgnoreCase))
          {
            HeroVehicle = CarlaActor->GetActor();
            if (HeroVehicle)
            {
              UE_LOG(LogTemp, Log, TEXT("HeroFollower: Found hero vehicle: %s (role_name='%s')"), *HeroVehicle->GetName(), *RoleName);

              // Hide the hero vehicle to prevent jitter from external updates
              // HeroVehicle->SetActorHiddenInGame(true);
              UE_LOG(LogTemp, Log, TEXT("HeroFollower: Hidden hero vehicle from game"));

              bFoundHero = true;
              break;
            }
          }
        }
      }
    }
    // Log if no hero found (only once per second to avoid spam)
    if (!bFoundHero)
    {
      HeroSearchLogTimer += DeltaTime;
      if (HeroSearchLogTimer >= 1.0f)
      {
        UE_LOG(LogTemp, Warning, TEXT("HeroFollower: No hero vehicle found in actor registry. Make sure vehicle has role_name='hero' attribute"));
        HeroSearchLogTimer = 0.0f;
      }
    }
    else
    {
      HeroSearchLogTimer = 0.0f;
    }
  }
  else
  {
    UE_LOG(LogTemp, Verbose, TEXT("HeroFollower: We have a hero"));
    // If we have a hero vehicle, follow it
    if (HeroVehicle)
    {
      UE_LOG(LogTemp, Verbose, TEXT("HeroFollower: We have a hero %s"), *HeroVehicle->GetName());
      if (IsValid(HeroVehicle))
      {
        UE_LOG(LogTemp, Verbose, TEXT("HeroFollower: Hero is valid %s, updating transform"), *HeroVehicle->GetName());
        FTransform VehicleTransform = HeroVehicle->GetActorTransform();

        // Apply camera offset in vehicle's local space
        FVector WorldOffset = VehicleTransform.TransformVector(CameraOffset);
        FVector NewLocation = VehicleTransform.GetLocation() + WorldOffset;
        FRotator NewRotation = VehicleTransform.GetRotation().Rotator();

        if (RootDisplayActor)
        {
          RootDisplayActor->SetActorLocationAndRotation(NewLocation, NewRotation);
        }

        // Update owner transform
        AActor* Owner = GetOwner();
        if (Owner)
        {

          Owner->SetActorLocation(NewLocation);
          Owner->SetActorRotation(NewRotation);

          if (RootDisplayActor)
          {
            RootDisplayActor->SetActorLocationAndRotation(NewLocation, NewRotation);
          }
        }

        // Also move the nDisplay root actor if present and it's not the same as our owner
        if (RootDisplayActor && RootDisplayActor != Owner)
        {
          RootDisplayActor->SetActorLocation(NewLocation);
          RootDisplayActor->SetActorRotation(NewRotation);
          RootDisplayActor->SetActorLocationAndRotation(NewLocation, NewRotation);
        }
      }
      else
      {
        // Vehicle was destroyed or invalid, clear the reference
        HeroVehicle = nullptr;
        UE_LOG(LogTemp, Warning, TEXT("HeroFollower: Hero vehicle was destroyed, searching for new one"));
      }
    }
  }
}
