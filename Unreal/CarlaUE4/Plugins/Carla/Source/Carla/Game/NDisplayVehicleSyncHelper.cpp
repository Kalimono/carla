// Copyright (c) 2025 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "NDisplayVehicleSyncHelper.h"

#include "Carla/Game/NDisplayVehicleSync.h"
#include "DisplayClusterRootActor.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "IDisplayCluster.h"
#include "Engine/World.h"
#include "EngineUtils.h"

TWeakObjectPtr<ANDisplayVehicleSync> FNDisplayVehicleSyncHelper::CachedVehicleSync;

ANDisplayVehicleSync* FNDisplayVehicleSyncHelper::GetOrCreateVehicleSync(UWorld* World)
{
  if (!World)
  {
    return nullptr;
  }

  // Check cached actor
  if (CachedVehicleSync.IsValid())
  {
    return CachedVehicleSync.Get();
  }

  // Try to find existing actor in world
  for (TActorIterator<ANDisplayVehicleSync> It(World); It; ++It)
  {
    if (It->IsValidLowLevel() && !It->IsActorBeingDestroyed())
    {
      CachedVehicleSync = *It;
      return *It;
    }
  }

  // Create new actor if not found
  ANDisplayVehicleSync* NewVehicleSync = World->SpawnActor<ANDisplayVehicleSync>();
  if (NewVehicleSync)
  {
    CachedVehicleSync = NewVehicleSync;
    UE_LOG(LogTemp, Log, TEXT("FNDisplayVehicleSyncHelper: Created new nDisplay Vehicle Synchronizer"));
    return NewVehicleSync;
  }

  return nullptr;
}

void FNDisplayVehicleSyncHelper::NotifyVehicleSpawned(UWorld* World, AActor* SpawnedVehicle, FActorDescription ActorDesc)
{
  if (!World)
  {
    UE_LOG(LogTemp, Error, TEXT("FNDisplayVehicleSyncHelper: NotifyVehicleSpawned called with null World"));
    return;
  }

  if (!SpawnedVehicle)
  {
    UE_LOG(LogTemp, Error, TEXT("FNDisplayVehicleSyncHelper: NotifyVehicleSpawned called with null SpawnedVehicle"));
    return;
  }

  if (IDisplayCluster::IsAvailable())
  {
    if (IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr())
    {
      if (!ClusterMgr->IsMaster())
      {
        UE_LOG(LogTemp, Warning, TEXT("FNDisplayVehicleSyncHelper: Skipping spawn notify on slave node '%s' for actor '%s'"),
          *ClusterMgr->GetNodeId(), *SpawnedVehicle->GetName());
        return;
      }
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("FNDisplayVehicleSyncHelper: DisplayCluster available but ClusterMgr is null; allowing spawn notify"));
    }
  }
  else
  {
    UE_LOG(LogTemp, Verbose, TEXT("FNDisplayVehicleSyncHelper: DisplayCluster not available; allowing spawn notify"));
  }

  ANDisplayVehicleSync* VehicleSync = GetOrCreateVehicleSync(World);
  if (VehicleSync)
  {
    UE_LOG(LogTemp, Log, TEXT("FNDisplayVehicleSyncHelper: Notifying spawn for actor '%s' on master"),
      *SpawnedVehicle->GetName());
    VehicleSync->OnVehicleSpawned(SpawnedVehicle, ActorDesc);
  }
  else
  {
    UE_LOG(LogTemp, Error, TEXT("FNDisplayVehicleSyncHelper: Failed to get or create VehicleSync for spawn notify"));
  }
}

void FNDisplayVehicleSyncHelper::NotifyVehicleDestroyed(UWorld* World, AActor* DestroyedVehicle)
{
  if (!World)
  {
    UE_LOG(LogTemp, Error, TEXT("FNDisplayVehicleSyncHelper: NotifyVehicleDestroyed called with null World"));
    return;
  }

  if (!DestroyedVehicle)
  {
    UE_LOG(LogTemp, Error, TEXT("FNDisplayVehicleSyncHelper: NotifyVehicleDestroyed called with null DestroyedVehicle"));
    return;
  }

  if (IDisplayCluster::IsAvailable())
  {
    if (IDisplayClusterClusterManager* ClusterMgr = IDisplayCluster::Get().GetClusterMgr())
    {
      if (!ClusterMgr->IsMaster())
      {
        UE_LOG(LogTemp, Warning, TEXT("FNDisplayVehicleSyncHelper: Skipping destroy notify on slave node '%s' for actor '%s'"),
          *ClusterMgr->GetNodeId(), *DestroyedVehicle->GetName());
        return;
      }
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("FNDisplayVehicleSyncHelper: DisplayCluster available but ClusterMgr is null; allowing destroy notify"));
    }
  }
  else
  {
    UE_LOG(LogTemp, Verbose, TEXT("FNDisplayVehicleSyncHelper: DisplayCluster not available; allowing destroy notify"));
  }

  ANDisplayVehicleSync* VehicleSync = GetOrCreateVehicleSync(World);
  if (VehicleSync)
  {
    UE_LOG(LogTemp, Log, TEXT("FNDisplayVehicleSyncHelper: Notifying destroy for actor '%s' on master"),
      *DestroyedVehicle->GetName());
    VehicleSync->OnVehicleDestroyed(DestroyedVehicle);
  }
  else
  {
    UE_LOG(LogTemp, Error, TEXT("FNDisplayVehicleSyncHelper: Failed to get or create VehicleSync for destroy notify"));
  }
}

bool FNDisplayVehicleSyncHelper::IsNDisplayAvailable(UWorld* World)
{
  if (!World)
  {
    return false;
  }

  // Check if DisplayClusterRootActor exists in world
  for (TActorIterator<ADisplayClusterRootActor> It(World); It; ++It)
  {
    if (It->IsValidLowLevel())
    {
      return true;
    }
  }

  return false;
}
