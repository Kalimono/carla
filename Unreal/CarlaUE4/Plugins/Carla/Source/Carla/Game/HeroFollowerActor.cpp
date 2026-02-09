#include "HeroFollowerActor.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "Carla/Game/CarlaStatics.h"
#include "Carla/Game/CarlaEpisode.h"

#include "DisplayClusterRootActor.h"

AHeroFollowerActor::AHeroFollowerActor()
{
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.bStartWithTickEnabled = true;
  PrimaryActorTick.bTickEvenWhenPaused = true;
  PrimaryActorTick.bAllowTickOnDedicatedServer = true;

  // Run late to reduce chances of nDisplay overwriting after us.
  PrimaryActorTick.TickGroup = TG_PostUpdateWork;

  SetActorHiddenInGame(true);
  SetActorEnableCollision(false);
}

void AHeroFollowerActor::BeginPlay()
{
  Super::BeginPlay();

  SetActorTickEnabled(true);

  TryFindRootDisplayActorAndTarget();

  UE_LOG(LogTemp, Log, TEXT("HeroFollower: Initialized (target component name: %s)"),
    *NDisplayTargetComponentName.ToString());

  NodeId = this->GetDcNodeId();

  UE_LOG(LogTemp, Log, TEXT("HeroFollower: Running on nDisplay node '%s'"), *NodeId);

  // Check if this is the master node that will UPDATE the transform
  if (!MasterNodeName.IsEmpty() && NodeId != MasterNodeName)
  {
    bIsActiveOnThisNode = false;
    // DO NOT disable tick - we need to keep checking for the hero and updating the target
    // SetActorTickEnabled(false);  // REMOVED - slaves need to tick to sync transforms
    UE_LOG(LogTemp, Log, TEXT("HeroFollower: SLAVE on node '%s' (master is '%s') - will sync transforms from master"),
      *NodeId, *MasterNodeName);
    return;
  }

  bIsActiveOnThisNode = true;
  UE_LOG(LogTemp, Log, TEXT("HeroFollower: MASTER on node '%s' - will update transforms"), *NodeId);
}

void AHeroFollowerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  Super::EndPlay(EndPlayReason);
}

void AHeroFollowerActor::Tick(float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);

  // Skip transform UPDATE logic if not master node, but continue to ensure components are found
  // Slaves still need to find the DisplayCluster components to receive synced transforms
  const bool bShouldUpdateTransform = bIsActiveOnThisNode;

  // cadence debug
  DebugTimer += DeltaSeconds;
  const bool bDoDebug = (DebugLogPeriod > 0.0f && DebugTimer >= DebugLogPeriod);
  if (bDoDebug) DebugTimer = 0.0f;

  // validate cached pointers
  if (!IsValid(HeroVehicle)) HeroVehicle = nullptr;
  if (!IsValid(RootDisplayActor)) RootDisplayActor = nullptr;
  if (!IsValid(NDisplayTargetComponent)) NDisplayTargetComponent = nullptr;

  // re-find if needed
  if (!RootDisplayActor || !NDisplayTargetComponent)
  {
    TryFindRootDisplayActorAndTarget();
  }

  // Both master and slaves should search for hero vehicle
  // But only master updates the transform
  if (!HeroVehicle)
  {
    TryFindHero();
  }

  // Debug logging removed

  // Only master should update the transform - slaves will sync automatically via nDisplay
  if (HeroVehicle && NDisplayTargetComponent)
  {
    UpdateNDisplayTargetTransform();

    // Debug logging removed
  }
  else if (!bShouldUpdateTransform && NDisplayTargetComponent && bDoDebug)
  {
    // Debug logging removed
  }
}

FString AHeroFollowerActor::GetDcNodeId() const
{
    FString NodeId;
    if (FParse::Value(FCommandLine::Get(), TEXT("dc_node="), NodeId))
    {
        return NodeId;
    }
    return TEXT("unknown");
}

void AHeroFollowerActor::TryFindHero()
{
  UWorld* World = GetWorld();
  if (!World) return;

  UCarlaEpisode* Episode = UCarlaStatics::GetCurrentEpisode(World);
  if (!Episode) return;

  const FActorRegistry& Registry = Episode->GetActorRegistry();

  bool bFoundHero = false;

  for (auto It = Registry.begin(); It != Registry.end(); ++It)
  {
    FCarlaActor* CarlaActor = It.Value().Get();
    if (!CarlaActor) continue;

    if (CarlaActor->GetActorType() != FCarlaActor::ActorType::Vehicle)
      continue;

    const FActorInfo* ActorInfo = CarlaActor->GetActorInfo();
    if (!ActorInfo) continue;

    const FActorAttribute* RoleNameAttr =
      ActorInfo->Description.Variations.Find(TEXT("role_name"));

    const FString RoleName = RoleNameAttr ? RoleNameAttr->Value : TEXT("");

    if (RoleName.Equals(TEXT("hero"), ESearchCase::IgnoreCase) ||
        RoleName.Equals(TEXT("ego"),  ESearchCase::IgnoreCase))
    {
      AActor* Found = CarlaActor->GetActor();
      if (IsValid(Found))
      {
        HeroVehicle = Found;
        bFoundHero = true;

        UE_LOG(LogTemp, Log, TEXT("HeroFollower: Found hero vehicle %s (role_name='%s')"),
          *HeroVehicle->GetName(), *RoleName);
        break;
      }
    }
  }

  if (!bFoundHero)
  {
    HeroSearchLogTimer += GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
    if (HeroSearchLogTimer >= 1.0f)
    {
      UE_LOG(LogTemp, Warning, TEXT("HeroFollower: No hero vehicle found (need role_name='hero' or 'ego')"));
      HeroSearchLogTimer = 0.0f;
    }
  }
  else
  {
    HeroSearchLogTimer = 0.0f;
  }
}

void AHeroFollowerActor::TryFindRootDisplayActorAndTarget()
{
  UWorld* World = GetWorld();
  if (!World) return;

  if (!RootDisplayActor)
  {
    for (TActorIterator<ADisplayClusterRootActor> It(World); It; ++It)
    {
      ADisplayClusterRootActor* Found = *It;
      if (IsValid(Found))
      {
        RootDisplayActor = Found;
        UE_LOG(LogTemp, Log, TEXT("HeroFollower: Found DisplayClusterRootActor %s"),
          *RootDisplayActor->GetName());
        break;
      }
    }
  }

  if (RootDisplayActor && !NDisplayTargetComponent)
  {
    // Find a component whose NAME matches the nDisplay scene_node id you want to drive.
    // In your config: camera_static parent="socket_cam"
    TArray<UActorComponent*> Comps = RootDisplayActor->GetComponentsByClass(USceneComponent::StaticClass());
    for (UActorComponent* C : Comps)
    {
      USceneComponent* SC = Cast<USceneComponent>(C);
      if (!SC) continue;

      if (SC->GetFName() == NDisplayTargetComponentName)
      {
        NDisplayTargetComponent = SC;
        UE_LOG(LogTemp, Log, TEXT("HeroFollower: Found target scene component '%s' on root actor"),
          *NDisplayTargetComponentName.ToString());
        break;
      }
    }

    if (!NDisplayTargetComponent)
    {
      UE_LOG(LogTemp, Warning, TEXT("HeroFollower: Could not find component named '%s' on nDisplay root actor."),
        *NDisplayTargetComponentName.ToString());
    }
  }
}

void AHeroFollowerActor::UpdateNDisplayTargetTransform()
{
  // Compute desired camera origin in world space (cm)
  const FTransform VehicleT = HeroVehicle->GetActorTransform();
  const FVector DesiredWorldLoc = VehicleT.TransformPosition(CameraOffset);
  const FRotator DesiredWorldRot = VehicleT.GetRotation().Rotator();

  // IMPORTANT: Move the nDisplay scene-node component (socket_cam), not the root actor.
  // This directly drives the camera's parent defined in the .cfg.
  NDisplayTargetComponent->SetWorldLocationAndRotation(
    DesiredWorldLoc,
    DesiredWorldRot,
    false,
    nullptr,
    ETeleportType::TeleportPhysics
  );
}
