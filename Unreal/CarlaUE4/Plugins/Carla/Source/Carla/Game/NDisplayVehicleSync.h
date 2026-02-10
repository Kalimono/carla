// Copyright (c) 2025 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Carla/Game/CarlaEpisode.h"
#include "Carla/Actor/ActorBlueprintFunctionLibrary.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterEventListener.h"

#include "NDisplayVehicleSync.generated.h"

// Forward declarations
class ADisplayClusterRootActor;
class UNDisplayVehicleClusterEventListener;

UCLASS()
class CARLA_API UNDisplayVehicleClusterEventListener : public UObject, public IDisplayClusterClusterEventListener
{
  GENERATED_BODY()

public:
  void Init(class ANDisplayVehicleSync* InOwner) { Owner = InOwner; }

  UFUNCTION(BlueprintNativeEvent)
  void OnClusterEventJson(const FDisplayClusterClusterEventJson& Event);

  UFUNCTION(BlueprintNativeEvent)
  void OnClusterEventBinary(const FDisplayClusterClusterEventBinary& Event);

private:
  TWeakObjectPtr<class ANDisplayVehicleSync> Owner;
};

/// Structure to hold vehicle spawn data for nDisplay synchronization
USTRUCT(BlueprintType)
struct FNDisplayVehicleSpawnData
{
  GENERATED_BODY()

  UPROPERTY()
  int32 ActorId;

  UPROPERTY()
  uint32 ActorUId;

  UPROPERTY()
  FTransform SpawnTransform;

  UPROPERTY()
  FString ActorTypeName;

  UPROPERTY()
  TMap<FString, FString> Attributes;
};

/// Structure to hold vehicle transform update data for nDisplay synchronization
USTRUCT(BlueprintType)
struct FNDisplayVehicleTransformData
{
  GENERATED_BODY()

  UPROPERTY()
  int32 ActorId;

  UPROPERTY()
  FTransform ActorTransform;

  UPROPERTY()
  FVector LinearVelocity;

  UPROPERTY()
  FVector AngularVelocity;
};

/// Structure to hold vehicle destruction data for nDisplay synchronization
USTRUCT(BlueprintType)
struct FNDisplayVehicleDestructionData
{
  GENERATED_BODY()

  UPROPERTY()
  int32 ActorId;
};

/// Main class for handling vehicle spawning and physics synchronization across nDisplay cluster nodes
/// 
/// This class:
/// 1. Sets up cluster event receivers on all nodes
/// 2. Hooks into vehicle spawning via Python API to replicate on slave nodes
/// 3. Disables physics on slave nodes (replication-only)
/// 4. Maintains a transform sync loop for all vehicles
/// 5. Handles vehicle destruction across all nodes
UCLASS(ClassGroup=(Custom), DisplayName="nDisplay Vehicle Synchronizer")
class CARLA_API ANDisplayVehicleSync : public AActor
{
  GENERATED_BODY()

public:
  ANDisplayVehicleSync();

  virtual void BeginPlay() override;
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  virtual void Tick(float DeltaSeconds) override;

  // ===========================================================================
  // -- Vehicle Spawning Support -----------------------------------------------
  // ===========================================================================

  /// Called when a vehicle is spawned on the master node
  /// This distributes the spawn command to all slave nodes
  void OnVehicleSpawned(AActor* SpawnedVehicle, FActorDescription ActorDescription);

  /// Called when a vehicle is destroyed on the master node
  /// This distributes the destruction command to all slave nodes
  void OnVehicleDestroyed(AActor* DestroyedVehicle);

  // ===========================================================================
  // -- nDisplay Event Handling ------------------------------------------------
  // ===========================================================================

  /// Receive vehicle spawn event from master node
  /// Called on slave nodes to create the vehicle locally
  void ReceiveVehicleSpawnEvent(FNDisplayVehicleSpawnData SpawnData);

  /// Receive vehicle transform update from master node
  /// Called on slave nodes to update vehicle positions/rotations
  void ReceiveVehicleTransformEvent(FNDisplayVehicleTransformData TransformData);

  /// Receive vehicle destruction event from master node
  /// Called on slave nodes to destroy the vehicle locally
  void ReceiveVehicleDestructionEvent(FNDisplayVehicleDestructionData DestructionData);

  // ===========================================================================
  // -- Configuration ----------------------------------------------------------
  // ===========================================================================

  /// Set the master node name (if empty, this node will be master)
  UFUNCTION(BlueprintCallable, Category="nDisplay Vehicle Sync")
  void SetMasterNodeName(const FString& InMasterNodeName);

  /// Enable/disable physics simulation on this node
  UFUNCTION(BlueprintCallable, Category="nDisplay Vehicle Sync")
  void SetPhysicsSimulationEnabled(bool bEnabled);

  /// Set the sync update rate (updates per second)
  UFUNCTION(BlueprintCallable, Category="nDisplay Vehicle Sync")
  void SetSyncUpdateRate(float UpdatesPerSecond);

  /// Get the current node ID from nDisplay cluster
  UFUNCTION(BlueprintCallable, Category="nDisplay Vehicle Sync")
  FString GetCurrentNodeId() const;

  /// Check if this is the master node
  UFUNCTION(BlueprintCallable, Category="nDisplay Vehicle Sync")
  bool IsMasterNode() const;

  /// Enable/disable debug logging
  UFUNCTION(BlueprintCallable, Category="nDisplay Vehicle Sync|Debug")
  void SetDebugLoggingEnabled(bool bEnabled);

protected:
  // ===========================================================================
  // -- Internal Methods -------------------------------------------------------
  // ===========================================================================

  /// Initialize cluster event receiver and register with DisplayCluster
  void InitializeClusterEventReceiver();

  /// Find or create the DisplayCluster root actor
  ADisplayClusterRootActor* GetDisplayClusterRootActor() const;

  /// Broadcast a spawn event to all cluster nodes
  void BroadcastVehicleSpawnEvent(const FNDisplayVehicleSpawnData& SpawnData);

  /// Broadcast transform updates to all cluster nodes
  void BroadcastVehicleTransformEvents();

  /// Broadcast a destruction event to all cluster nodes
  void BroadcastVehicleDestructionEvent(const FNDisplayVehicleDestructionData& DestructionData);

  /// Get or create a local replica of a vehicle on slave nodes
  AActor* CreateVehicleReplica(const FNDisplayVehicleSpawnData& SpawnData);

  /// Disable all physics components on a vehicle actor
  void DisablePhysicsOnActor(AActor* Actor);

  /// Enable/restore physics components on a vehicle actor
  void EnablePhysicsOnActor(AActor* Actor);

  /// Update transform for a replicated vehicle
  void UpdateReplicaTransform(int32 ActorId, const FTransform& NewTransform);

  /// Find tracked vehicle by actor ID
  AActor* FindTrackedVehicle(int32 ActorId);

  /// Get the node ID from nDisplay command line parameters
  FString GetDcNodeId() const;

  /// Ensure CurrentEpisode is available (late bind in nDisplay startup)
  bool EnsureEpisode();

  // ===========================================================================
  // -- Properties for configuration -------------------------------------------
  // ===========================================================================

  /// Name of the master nDisplay node (if empty, auto-detect)
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="nDisplay Vehicle Sync")
  FString MasterNodeName = TEXT("");

  /// Enable physics simulation on this node (only relevant on master; slaves always disable physics on replicas)
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="nDisplay Vehicle Sync")
  bool bPhysicsSimulationEnabled = false;

  /// Target synchronization update rate (Hz)
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="nDisplay Vehicle Sync")
  float SyncUpdateRate = 60.0f;

  /// Enable debug output
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="nDisplay Vehicle Sync|Debug")
  bool bDebugLoggingEnabled = false;

  /// Log period for debug messages (seconds, 0 to disable)
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="nDisplay Vehicle Sync|Debug")
  float DebugLogPeriod = 2.0f;

private:
  // ===========================================================================
  // -- Internal State ---------------------------------------------------------
  // ===========================================================================

  /// Reference to the current episode
  UPROPERTY()
  UCarlaEpisode* CurrentEpisode = nullptr;

  /// Reference to nDisplay root actor
  UPROPERTY()
  ADisplayClusterRootActor* DisplayClusterRoot = nullptr;

  /// Current node ID (e.g., "node_0")
  FString CurrentNodeId = TEXT("unknown");

  /// Is this node the master?
  bool bIsMasterNode = false;

  /// Mapping of actor IDs to their spawned actors
  TMap<int32, TWeakObjectPtr<AActor>> TrackedVehicles;

  /// Mapping of original actor IDs to replica actor IDs (for slave nodes)
  TMap<int32, int32> VehicleReplicaMapping;

  /// Timer for sync updates
  float SyncUpdateTimer = 0.0f;

  /// Timer for debug logging
  float DebugLogTimer = 0.0f;

  /// Whether cluster event receiver is initialized
  bool bClusterEventReceiverInitialized = false;

  /// Cluster event listener for receiving nDisplay sync events
  UPROPERTY()
  UNDisplayVehicleClusterEventListener* ClusterEventListener = nullptr;

  /// Delegate handles for monitoring actor spawning/destruction
  FDelegateHandle ActorSpawnedDelegateHandle;
  FDelegateHandle ActorDestroyedDelegateHandle;
};
