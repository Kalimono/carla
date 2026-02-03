// Copyright (c) 2025 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include "Carla/Game/NDisplayVehicleSync.h"

/// Helper macros and functions for integrating nDisplay vehicle synchronization
/// into the vehicle spawning pipeline

// Macro to hook nDisplay vehicle spawn notification
// Place this in your vehicle spawning code (e.g., in CarlaEpisode::SpawnActorWithInfo)
#define NDISPLAY_ON_VEHICLE_SPAWNED(World, VehicleActor, ActorDesc) \
  do { \
    FNDisplayVehicleSyncHelper::NotifyVehicleSpawned(World, VehicleActor, ActorDesc); \
  } while(0)

// Macro to hook nDisplay vehicle destruction notification
// Place this in your vehicle destruction code (e.g., in CarlaEpisode::DestroyActor)
#define NDISPLAY_ON_VEHICLE_DESTROYED(World, VehicleActor) \
  do { \
    FNDisplayVehicleSyncHelper::NotifyVehicleDestroyed(World, VehicleActor); \
  } while(0)

/// Static helper class for integrating nDisplay synchronization
class CARLA_API FNDisplayVehicleSyncHelper
{
public:
  /// Get or create the nDisplay vehicle synchronizer actor in the world
  static ANDisplayVehicleSync* GetOrCreateVehicleSync(UWorld* World);

  /// Notify about a vehicle being spawned (call from vehicle spawn code)
  static void NotifyVehicleSpawned(UWorld* World, AActor* SpawnedVehicle, FActorDescription ActorDesc);

  /// Notify about a vehicle being destroyed (call from vehicle destruction code)
  static void NotifyVehicleDestroyed(UWorld* World, AActor* DestroyedVehicle);

  /// Check if nDisplay is available in the current world
  static bool IsNDisplayAvailable(UWorld* World);

private:
  /// Cached nDisplay vehicle sync actor (weak reference)
  static TWeakObjectPtr<ANDisplayVehicleSync> CachedVehicleSync;
};
