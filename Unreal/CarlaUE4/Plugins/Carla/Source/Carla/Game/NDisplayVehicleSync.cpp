// Copyright (c) 2025 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "NDisplayVehicleSync.h"

#include "Carla/Game/CarlaEpisode.h"
#include "Carla/Game/CarlaGameModeBase.h"
#include "Carla/Game/CarlaStatics.h"
#include "Carla/Actor/CarlaActor.h"
#include "Carla/Vehicle/CarlaWheeledVehicle.h"

#include "Cluster/IDisplayClusterClusterManager.h"
#include "IDisplayCluster.h"
#include "JsonObjectConverter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "WheeledVehicleMovementComponent.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "DisplayClusterRootActor.h"

namespace NDisplayVehicleSyncEvent
{
  static const FString Category = TEXT("carla.ndisplay.vehicle_sync");
  static const FString TypeSpawn = TEXT("spawn");
  static const FString TypeTransform = TEXT("transform");
  static const FString TypeDestroy = TEXT("destroy");
  static const FString PayloadKey = TEXT("payload");
  static const int32 TransformEventId = 0x564E4454; // 'VNDT'
}

void UNDisplayVehicleClusterEventListener::OnClusterEventJson_Implementation(const FDisplayClusterClusterEventJson& Event)
{
  ANDisplayVehicleSync* OwnerPtr = Owner.Get();
  if (!OwnerPtr || Event.Category != NDisplayVehicleSyncEvent::Category)
  {
    return;
  }

  const FString* Payload = Event.Parameters.Find(NDisplayVehicleSyncEvent::PayloadKey);
  if (!Payload)
  {
    return;
  }

  if (Event.Type == NDisplayVehicleSyncEvent::TypeSpawn)
  {
    FNDisplayVehicleSpawnData SpawnData;
    if (FJsonObjectConverter::JsonObjectStringToUStruct(*Payload, &SpawnData, 0, 0))
    {
      OwnerPtr->ReceiveVehicleSpawnEvent(SpawnData);
    }
    return;
  }

  if (Event.Type == NDisplayVehicleSyncEvent::TypeDestroy)
  {
    FNDisplayVehicleDestructionData DestructionData;
    if (FJsonObjectConverter::JsonObjectStringToUStruct(*Payload, &DestructionData, 0, 0))
    {
      OwnerPtr->ReceiveVehicleDestructionEvent(DestructionData);
    }
    return;
  }
}

void UNDisplayVehicleClusterEventListener::OnClusterEventBinary_Implementation(const FDisplayClusterClusterEventBinary& Event)
{
  ANDisplayVehicleSync* OwnerPtr = Owner.Get();
  if (!OwnerPtr || Event.EventId != NDisplayVehicleSyncEvent::TransformEventId)
  {
    return;
  }

  if (Event.EventData.Num() == 0)
  {
    return;
  }

  FMemoryReader Reader(Event.EventData, true);
  Reader.ArIsSaveGame = false;

  int32 Count = 0;
  Reader << Count;
  if (Count <= 0)
  {
    return;
  }

  for (int32 Index = 0; Index < Count; ++Index)
  {
    FNDisplayVehicleTransformData TransformData;
    Reader << TransformData.ActorId;
    Reader << TransformData.ActorTransform;
    Reader << TransformData.LinearVelocity;
    Reader << TransformData.AngularVelocity;
    OwnerPtr->ReceiveVehicleTransformEvent(TransformData);
  }
}

ANDisplayVehicleSync::ANDisplayVehicleSync()
{
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.bStartWithTickEnabled = true;
  PrimaryActorTick.TickGroup = TG_PostUpdateWork;
  PrimaryActorTick.TickInterval = 0.0f; // Tick every frame

  SetActorHiddenInGame(true);
  SetActorEnableCollision(false);
}

void ANDisplayVehicleSync::BeginPlay()
{
  Super::BeginPlay();

  // Get the current episode
  if (!EnsureEpisode())
  {
    UE_LOG(LogTemp, Warning, TEXT("NDisplayVehicleSync: CarlaEpisode not ready yet, will retry on use"));
  }

  // Get current node ID from nDisplay (fallback to command line)
  CurrentNodeId = GetDcNodeId();
  if (IDisplayCluster::IsAvailable())
  {
    if (IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr())
    {
      const FString ClusterNodeId = ClusterMgr->GetNodeId();
      if (!ClusterNodeId.IsEmpty())
      {
        CurrentNodeId = ClusterNodeId;
      }
    }
  }

  // Determine if this is the master node
  if (!MasterNodeName.IsEmpty())
  {
    bIsMasterNode = (CurrentNodeId == MasterNodeName);
  }
  else if (IDisplayCluster::IsAvailable())
  {
    if (IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr())
    {
      bIsMasterNode = ClusterMgr->IsMaster();
    }
    else
    {
      bIsMasterNode = CurrentNodeId.IsEmpty() || CurrentNodeId == TEXT("node_0") || CurrentNodeId == TEXT("Primary");
    }
  }
  else
  {
    // Auto-detect: assume first node or primary is master
    bIsMasterNode = CurrentNodeId.IsEmpty() || CurrentNodeId == TEXT("node_0") || CurrentNodeId == TEXT("Primary");
  }

  UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Initialized on node '%s' (Master=%s)"),
    *CurrentNodeId, bIsMasterNode ? TEXT("Yes") : TEXT("No"));

  // Initialize cluster event receiver
  InitializeClusterEventReceiver();

  // Set physics simulation based on node type
  if (!bIsMasterNode && !bPhysicsSimulationEnabled)
  {
    SetPhysicsSimulationEnabled(false);
    UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Physics simulation disabled on slave node"));
  }
  else if (bIsMasterNode)
  {
    SetPhysicsSimulationEnabled(true);
    UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Physics simulation enabled on master node"));
  }
}

void ANDisplayVehicleSync::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  if (ClusterEventListener && IDisplayCluster::IsAvailable())
  {
    if (IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr())
    {
      TScriptInterface<IDisplayClusterClusterEventListener> ListenerInterface;
      ListenerInterface.SetObject(ClusterEventListener);
      ListenerInterface.SetInterface(Cast<IDisplayClusterClusterEventListener>(ClusterEventListener));
      ClusterMgr->RemoveClusterEventListener(ListenerInterface);
    }
  }

  ClusterEventListener = nullptr;

  // Clean up tracked vehicles
  TrackedVehicles.Empty();
  VehicleReplicaMapping.Empty();

  Super::EndPlay(EndPlayReason);
}

void ANDisplayVehicleSync::Tick(float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);

  if (!CurrentEpisode || !bIsMasterNode)
  {
    return;
  }

  // Update sync timer
  SyncUpdateTimer += DeltaSeconds;
  float SyncInterval = SyncUpdateRate > 0.0f ? 1.0f / SyncUpdateRate : 0.016f; // Default 60Hz

  if (SyncUpdateTimer >= SyncInterval)
  {
    SyncUpdateTimer = 0.0f;
    BroadcastVehicleTransformEvents();
  }

  // Update debug log timer
  DebugLogTimer += DeltaSeconds;
  if (bDebugLoggingEnabled && DebugLogPeriod > 0.0f && DebugLogTimer >= DebugLogPeriod)
  {
    DebugLogTimer = 0.0f;
    UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Tracking %d vehicles"), TrackedVehicles.Num());
  }
}

// ===========================================================================
// -- Vehicle Spawning Support -----------------------------------------------
// ===========================================================================

void ANDisplayVehicleSync::OnVehicleSpawned(AActor* SpawnedVehicle, FActorDescription ActorDescription)
{
  if (!bIsMasterNode || !SpawnedVehicle)
  {
    return;
  }

  if (!EnsureEpisode())
  {
    return;
  }

  // Find the Carla actor wrapper to get the ID
  FCarlaActor* CarlaActor = CurrentEpisode->FindCarlaActor(SpawnedVehicle);
  if (!CarlaActor)
  {
    return;
  }

  int32 ActorId = CarlaActor->GetActorId();

  // Create spawn data
  FNDisplayVehicleSpawnData SpawnData;
  SpawnData.ActorId = ActorId;
  SpawnData.ActorUId = ActorDescription.UId;
  SpawnData.SpawnTransform = SpawnedVehicle->GetActorTransform();
  SpawnData.ActorTypeName = ActorDescription.Id;

  // Convert attributes map
  for (const auto& Attr : ActorDescription.Variations)
  {
    SpawnData.Attributes.Add(Attr.Key, Attr.Value.Value);
  }

  // Track the vehicle on master
  TrackedVehicles.Add(ActorId, TWeakObjectPtr<AActor>(SpawnedVehicle));

  if (bDebugLoggingEnabled)
  {
    UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Vehicle spawned on master - ID=%d, UId=%d, Type=%s"),
      ActorId, SpawnData.ActorUId, *SpawnData.ActorTypeName);
  }

  // Broadcast to slave nodes
  BroadcastVehicleSpawnEvent(SpawnData);
}

void ANDisplayVehicleSync::OnVehicleDestroyed(AActor* DestroyedVehicle)
{
  if (!bIsMasterNode || !DestroyedVehicle)
  {
    return;
  }

  // Find the Carla actor wrapper to get the ID
  FCarlaActor* CarlaActor = CurrentEpisode->FindCarlaActor(DestroyedVehicle);
  if (!CarlaActor)
  {
    return;
  }

  int32 ActorId = CarlaActor->GetActorId();

  // Remove from tracked vehicles
  TrackedVehicles.Remove(ActorId);

  if (bDebugLoggingEnabled)
  {
    UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Vehicle destroyed on master - ID=%d"),
      ActorId);
  }

  // Broadcast destruction to slave nodes
  FNDisplayVehicleDestructionData DestructionData;
  DestructionData.ActorId = ActorId;
  BroadcastVehicleDestructionEvent(DestructionData);
}

// ===========================================================================
// -- nDisplay Event Handling ------------------------------------------------
// ===========================================================================

void ANDisplayVehicleSync::ReceiveVehicleSpawnEvent(FNDisplayVehicleSpawnData SpawnData)
{
  if (bIsMasterNode)
  {
    return; // Master doesn't receive its own events
  }

  if (!EnsureEpisode())
  {
    UE_LOG(LogTemp, Warning, TEXT("NDisplayVehicleSync: Episode not available for spawn event"));
    return;
  }

  // Create a replica vehicle on this slave node
  AActor* ReplicaActor = CreateVehicleReplica(SpawnData);
  if (ReplicaActor)
  {
    VehicleReplicaMapping.Add(SpawnData.ActorId, SpawnData.ActorId);

    if (bDebugLoggingEnabled)
    {
      UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Vehicle replica created on slave - ID=%d, UId=%d, Type=%s"),
        SpawnData.ActorId, SpawnData.ActorUId, *SpawnData.ActorTypeName);
    }
  }
  else
  {
    UE_LOG(LogTemp, Warning, TEXT("NDisplayVehicleSync: Failed to create vehicle replica - ID=%d, UId=%d, Type=%s"),
      SpawnData.ActorId, SpawnData.ActorUId, *SpawnData.ActorTypeName);
  }
}

void ANDisplayVehicleSync::ReceiveVehicleTransformEvent(FNDisplayVehicleTransformData TransformData)
{
  if (bIsMasterNode)
  {
    return; // Master doesn't receive transform events
  }

  UpdateReplicaTransform(TransformData.ActorId, TransformData.ActorTransform);
}

void ANDisplayVehicleSync::ReceiveVehicleDestructionEvent(FNDisplayVehicleDestructionData DestructionData)
{
  if (bIsMasterNode)
  {
    return; // Master doesn't receive destruction events
  }

  AActor* ReplicaActor = FindTrackedVehicle(DestructionData.ActorId);
  if (ReplicaActor && !ReplicaActor->IsActorBeingDestroyed())
  {
    ReplicaActor->Destroy();

    if (bDebugLoggingEnabled)
    {
      UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Vehicle replica destroyed - ID=%d"),
        DestructionData.ActorId);
    }
  }

  TrackedVehicles.Remove(DestructionData.ActorId);
  VehicleReplicaMapping.Remove(DestructionData.ActorId);
}

// ===========================================================================
// -- Configuration ----------------------------------------------------------
// ===========================================================================

void ANDisplayVehicleSync::SetMasterNodeName(const FString& InMasterNodeName)
{
  MasterNodeName = InMasterNodeName;
  bIsMasterNode = (CurrentNodeId == MasterNodeName);

  if (bDebugLoggingEnabled)
  {
    UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Master node set to '%s' (this node is %s)"),
      *MasterNodeName, bIsMasterNode ? TEXT("MASTER") : TEXT("SLAVE"));
  }
}

void ANDisplayVehicleSync::SetPhysicsSimulationEnabled(bool bEnabled)
{
  bPhysicsSimulationEnabled = bEnabled;

  if (!EnsureEpisode())
  {
    return;
  }

  // Update physics for all tracked vehicles
  const FActorRegistry& Registry = CurrentEpisode->GetActorRegistry();
  for (auto It = Registry.begin(); It != Registry.end(); ++It)
  {
    FCarlaActor* CarlaActor = It.Value().Get();
    if (!CarlaActor) continue;

    AActor* Actor = CarlaActor->GetActor();
    if (!Actor) continue;

    // Check if it's a vehicle
    if (CarlaActor->GetActorType() != FCarlaActor::ActorType::Vehicle)
    {
      continue;
    }

    if (bEnabled)
    {
      EnablePhysicsOnActor(Actor);
    }
    else
    {
      DisablePhysicsOnActor(Actor);
    }
  }

  if (bDebugLoggingEnabled)
  {
    UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Physics simulation %s"),
      bEnabled ? TEXT("ENABLED") : TEXT("DISABLED"));
  }
}

void ANDisplayVehicleSync::SetSyncUpdateRate(float UpdatesPerSecond)
{
  SyncUpdateRate = FMath::Max(1.0f, UpdatesPerSecond);

  if (bDebugLoggingEnabled)
  {
    UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Sync update rate set to %.1f Hz"),
      SyncUpdateRate);
  }
}

FString ANDisplayVehicleSync::GetCurrentNodeId() const
{
  return CurrentNodeId;
}

bool ANDisplayVehicleSync::IsMasterNode() const
{
  return bIsMasterNode;
}

void ANDisplayVehicleSync::SetDebugLoggingEnabled(bool bEnabled)
{
  bDebugLoggingEnabled = bEnabled;
}

// ===========================================================================
// -- Internal Methods -------------------------------------------------------
// ===========================================================================

void ANDisplayVehicleSync::InitializeClusterEventReceiver()
{
  if (bClusterEventReceiverInitialized)
  {
    return;
  }

  if (!IDisplayCluster::IsAvailable())
  {
    UE_LOG(LogTemp, Warning, TEXT("NDisplayVehicleSync: DisplayCluster module not available"));
    return;
  }

  IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr();
  if (!ClusterMgr)
  {
    UE_LOG(LogTemp, Warning, TEXT("NDisplayVehicleSync: Cluster manager not available"));
    return;
  }

  if (!ClusterEventListener)
  {
    ClusterEventListener = NewObject<UNDisplayVehicleClusterEventListener>(this);
    ClusterEventListener->Init(this);
  }

  TScriptInterface<IDisplayClusterClusterEventListener> ListenerInterface;
  ListenerInterface.SetObject(ClusterEventListener);
  ListenerInterface.SetInterface(Cast<IDisplayClusterClusterEventListener>(ClusterEventListener));
  ClusterMgr->AddClusterEventListener(ListenerInterface);

  bClusterEventReceiverInitialized = true;

  UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Cluster event receiver initialized"));
}

ADisplayClusterRootActor* ANDisplayVehicleSync::GetDisplayClusterRootActor() const
{
  if (DisplayClusterRoot && DisplayClusterRoot->IsValidLowLevel())
  {
    return DisplayClusterRoot;
  }

  UWorld* World = GetWorld();
  if (!World)
  {
    return nullptr;
  }

  for (TActorIterator<ADisplayClusterRootActor> It(World); It; ++It)
  {
    if (It->IsValidLowLevel())
    {
      return *It;
    }
  }

  return nullptr;
}

void ANDisplayVehicleSync::BroadcastVehicleSpawnEvent(const FNDisplayVehicleSpawnData& SpawnData)
{
  // This would use DisplayCluster's event broadcasting system
  // For now, we prepare the data structure
  if (bDebugLoggingEnabled)
  {
    UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Broadcasting spawn event for vehicle ID=%d to all nodes"),
      SpawnData.ActorId);
  }

  if (!IDisplayCluster::IsAvailable())
  {
    return;
  }

  IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr();
  if (!ClusterMgr)
  {
    return;
  }

  FString Payload;
  if (!FJsonObjectConverter::UStructToJsonObjectString(SpawnData, Payload))
  {
    return;
  }

  FDisplayClusterClusterEventJson Event;
  Event.Category = NDisplayVehicleSyncEvent::Category;
  Event.Type = NDisplayVehicleSyncEvent::TypeSpawn;
  Event.Name = FString::Printf(TEXT("CarlaVehicleSpawn_%d"), SpawnData.ActorId);
  Event.bIsSystemEvent = false;
  Event.bShouldDiscardOnRepeat = false;
  Event.Parameters.Add(NDisplayVehicleSyncEvent::PayloadKey, MoveTemp(Payload));

  ClusterMgr->EmitClusterEventJson(Event, false);
}

void ANDisplayVehicleSync::BroadcastVehicleTransformEvents()
{
  if (!bIsMasterNode || TrackedVehicles.Num() == 0)
  {
    return;
  }

  if (!EnsureEpisode())
  {
    return;
  }

  // Collect all vehicle transforms
  TArray<FNDisplayVehicleTransformData> TransformUpdates;
  TransformUpdates.Reserve(TrackedVehicles.Num());

  for (auto& Pair : TrackedVehicles)
  {
    int32 ActorId = Pair.Key;
    AActor* Vehicle = Pair.Value.Get();

    if (!Vehicle || Vehicle->IsActorBeingDestroyed())
    {
      continue;
    }

    FNDisplayVehicleTransformData TransformData;
    TransformData.ActorId = ActorId;
    TransformData.ActorTransform = Vehicle->GetActorTransform();

    // Get velocity if it's a character or pawn
    if (ACharacter* Character = Cast<ACharacter>(Vehicle))
    {
      if (Character->GetCharacterMovement())
      {
        TransformData.LinearVelocity = Character->GetCharacterMovement()->Velocity;
        TransformData.AngularVelocity = FVector::ZeroVector;
      }
    }
    else if (ACarlaWheeledVehicle* WheeledVehicle = Cast<ACarlaWheeledVehicle>(Vehicle))
    {
      TransformData.LinearVelocity = WheeledVehicle->GetVelocity();
      TransformData.AngularVelocity = FVector::ZeroVector;
    }
    else if (APawn* Pawn = Cast<APawn>(Vehicle))
    {
      TransformData.LinearVelocity = Pawn->GetActorForwardVector() * 100.0f; // Placeholder
      TransformData.AngularVelocity = FVector::ZeroVector;
    }

    TransformUpdates.Add(TransformData);
  }

  if (TransformUpdates.Num() > 0)
  {
    if (!IDisplayCluster::IsAvailable())
    {
      return;
    }

    IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr();
    if (!ClusterMgr)
    {
      return;
    }

    TArray<uint8> Data;
    Data.Reserve(TransformUpdates.Num() * (sizeof(int32) + sizeof(FTransform) + sizeof(FVector) * 2) + sizeof(int32));

    FMemoryWriter Writer(Data, true);
    Writer.ArIsSaveGame = false;

    int32 Count = TransformUpdates.Num();
    Writer << Count;
    for (const FNDisplayVehicleTransformData& TransformData : TransformUpdates)
    {
      Writer << const_cast<int32&>(TransformData.ActorId);
      Writer << const_cast<FTransform&>(TransformData.ActorTransform);
      Writer << const_cast<FVector&>(TransformData.LinearVelocity);
      Writer << const_cast<FVector&>(TransformData.AngularVelocity);
    }

    FDisplayClusterClusterEventBinary Event;
    Event.EventId = NDisplayVehicleSyncEvent::TransformEventId;
    Event.bIsSystemEvent = false;
    Event.bShouldDiscardOnRepeat = true;
    Event.EventData = MoveTemp(Data);

    ClusterMgr->EmitClusterEventBinary(Event, false);

    if (bDebugLoggingEnabled && DebugLogTimer == 0.0f)
    {
      UE_LOG(LogTemp, Verbose, TEXT("NDisplayVehicleSync: Broadcasting %d vehicle transforms"),
        TransformUpdates.Num());
    }
  }
}

void ANDisplayVehicleSync::BroadcastVehicleDestructionEvent(const FNDisplayVehicleDestructionData& DestructionData)
{
  // This would use DisplayCluster's event broadcasting system
  if (bDebugLoggingEnabled)
  {
    UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Broadcasting destruction event for vehicle ID=%d to all nodes"),
      DestructionData.ActorId);
  }

  if (!IDisplayCluster::IsAvailable())
  {
    return;
  }

  IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr();
  if (!ClusterMgr)
  {
    return;
  }

  FString Payload;
  if (!FJsonObjectConverter::UStructToJsonObjectString(DestructionData, Payload))
  {
    return;
  }

  FDisplayClusterClusterEventJson Event;
  Event.Category = NDisplayVehicleSyncEvent::Category;
  Event.Type = NDisplayVehicleSyncEvent::TypeDestroy;
  Event.Name = FString::Printf(TEXT("CarlaVehicleDestroy_%d"), DestructionData.ActorId);
  Event.bIsSystemEvent = false;
  Event.bShouldDiscardOnRepeat = false;
  Event.Parameters.Add(NDisplayVehicleSyncEvent::PayloadKey, MoveTemp(Payload));

  ClusterMgr->EmitClusterEventJson(Event, false);
}

AActor* ANDisplayVehicleSync::CreateVehicleReplica(const FNDisplayVehicleSpawnData& SpawnData)
{
  if (!EnsureEpisode())
  {
    return nullptr;
  }

  // Create actor description from spawn data
  FActorDescription ActorDesc;
  ActorDesc.Id = SpawnData.ActorTypeName;
  ActorDesc.UId = SpawnData.ActorUId;

  for (const auto& Attr : SpawnData.Attributes)
  {
    FActorAttribute ActorAttr;
    ActorAttr.Id = Attr.Key;
    ActorAttr.Value = Attr.Value;
    ActorDesc.Variations.Add(Attr.Key, ActorAttr);
  }

  // Spawn the vehicle using the episode's spawn actor function (preserve ActorId)
  const TPair<EActorSpawnResultStatus, FCarlaActor*> Result =
      CurrentEpisode->SpawnActorWithInfo(SpawnData.SpawnTransform, ActorDesc, SpawnData.ActorId);
  AActor* ReplicaActor = Result.Value ? Result.Value->GetActor() : nullptr;

  if (ReplicaActor)
  {
    // Track the replica
    TrackedVehicles.Add(SpawnData.ActorId, TWeakObjectPtr<AActor>(ReplicaActor));

    // Disable physics on slave nodes
    if (!bIsMasterNode && !bPhysicsSimulationEnabled)
    {
      DisablePhysicsOnActor(ReplicaActor);
    }
  }

  return ReplicaActor;
}

void ANDisplayVehicleSync::DisablePhysicsOnActor(AActor* Actor)
{
  if (!Actor)
  {
    return;
  }

  // 1. Destroy all physics constraints first (doors, hood, trunk hinges)
  //    This detaches constrained bodies so they can be welded to root
  TArray<UPhysicsConstraintComponent*> Constraints;
  Actor->GetComponents<UPhysicsConstraintComponent>(Constraints);
  for (UPhysicsConstraintComponent* Constraint : Constraints)
  {
    Constraint->BreakConstraint();
    Constraint->DestroyComponent();
  }

  // 2. Disable vehicle movement component (WheeledVehicle specific)
  if (AWheeledVehicle* WheeledVehicle = Cast<AWheeledVehicle>(Actor))
  {
    if (UWheeledVehicleMovementComponent* VehicleMovement = WheeledVehicle->GetVehicleMovementComponent())
    {
      VehicleMovement->SetComponentTickEnabled(false);
      VehicleMovement->Deactivate();
    }
  }

  // 3. Disable physics on ALL primitive components and attach them to root
  USceneComponent* RootComp = Actor->GetRootComponent();
  TArray<UPrimitiveComponent*> PrimComponents;
  Actor->GetComponents<UPrimitiveComponent>(PrimComponents);

  for (UPrimitiveComponent* PrimComp : PrimComponents)
  {
    PrimComp->SetSimulatePhysics(false);
    PrimComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PrimComp->PutRigidBodyToSleep();

    // Re-attach any detached components back to root so they move together
    if (PrimComp != RootComp && PrimComp->GetAttachParent() == nullptr)
    {
      PrimComp->AttachToComponent(RootComp, FAttachmentTransformRules::KeepWorldTransform);
    }
  }

  // 4. Disable character movement if it's a character
  if (ACharacter* Character = Cast<ACharacter>(Actor))
  {
    if (Character->GetCharacterMovement())
    {
      Character->GetCharacterMovement()->StopMovementImmediately();
      Character->GetCharacterMovement()->DisableMovement();
    }
  }

  UE_LOG(LogTemp, Log, TEXT("NDisplayVehicleSync: Physics fully disabled for %s (removed %d constraints)"), 
    *Actor->GetName(), Constraints.Num());
}

void ANDisplayVehicleSync::EnablePhysicsOnActor(AActor* Actor)
{
  if (!Actor)
  {
    return;
  }

  // Enable root component physics
  if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
  {
    RootPrim->SetSimulatePhysics(true);
    // RootPrim->SetCollisionEnabled(ECC_QueryAndPhysics);
    RootPrim->WakeRigidBody();
  }

  // Enable physics on all primitive components
  TArray<UActorComponent*> Components;
  Actor->GetComponents(UPrimitiveComponent::StaticClass(), Components);

  for (UActorComponent* Component : Components)
  {
    if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
    {
      PrimComp->SetSimulatePhysics(true);
    //   PrimComp->SetCollisionEnabled(ECC_QueryAndPhysics);
      PrimComp->WakeRigidBody();
    }
  }

  if (bDebugLoggingEnabled)
  {
    UE_LOG(LogTemp, Verbose, TEXT("NDisplayVehicleSync: Physics enabled for %s"), *Actor->GetName());
  }
}

void ANDisplayVehicleSync::UpdateReplicaTransform(int32 ActorId, const FTransform& NewTransform)
{
  AActor* ReplicaActor = FindTrackedVehicle(ActorId);
  if (!ReplicaActor || ReplicaActor->IsActorBeingDestroyed())
  {
    return;
  }

  // Move the root component directly - all children (doors, windows, wheels)
  // are attached to it (physics disabled, constraints removed) so they follow
  if (USceneComponent* RootComp = ReplicaActor->GetRootComponent())
  {
    RootComp->SetWorldTransform(NewTransform, false, nullptr, ETeleportType::None);
  }
}

AActor* ANDisplayVehicleSync::FindTrackedVehicle(int32 ActorId)
{
  TWeakObjectPtr<AActor>* VehiclePtr = TrackedVehicles.Find(ActorId);
  if (VehiclePtr && VehiclePtr->IsValid())
  {
    return VehiclePtr->Get();
  }

  return nullptr;
}

bool ANDisplayVehicleSync::EnsureEpisode()
{
  if (CurrentEpisode)
  {
    return true;
  }

  CurrentEpisode = UCarlaStatics::GetCurrentEpisode(GetWorld());
  if (CurrentEpisode)
  {
    return true;
  }

  if (ACarlaGameModeBase* GameMode = UCarlaStatics::GetGameMode(GetWorld()))
  {
    CurrentEpisode = GameMode->GetCarlaEpisodePtr();
  }

  return CurrentEpisode != nullptr;
}

FString ANDisplayVehicleSync::GetDcNodeId() const
{
  FString NodeId;
  if (FParse::Value(FCommandLine::Get(), TEXT("dc_node="), NodeId))
  {
    return NodeId;
  }

  // Try alternative parameter names
  if (FParse::Value(FCommandLine::Get(), TEXT("-dc_node="), NodeId))
  {
    return NodeId;
  }

  return TEXT("unknown");
}
