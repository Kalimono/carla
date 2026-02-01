#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeroFollowerActor.generated.h"

class ADisplayClusterRootActor;
class USceneComponent;

UCLASS(ClassGroup=(Custom))
class CARLA_API AHeroFollowerActor : public AActor
{
  GENERATED_BODY()

public:
  AHeroFollowerActor();

  virtual void BeginPlay() override;
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  virtual void Tick(float DeltaSeconds) override;

  // Offset in HERO VEHICLE local space (cm)
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower")
  FVector CameraOffset = FVector(160.0f, 0.0f, 170.0f);

  // nDisplay scene node component name to drive (must match your config parent)
  // Your config uses parent="socket_cam"
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower|nDisplay")
  FName NDisplayTargetComponentName = TEXT("cave_origin");

  // Debug logs cadence (seconds). Set 0 to disable.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower|Debug")
  float DebugLogPeriod = 1.0f;

  // Name of the nDisplay node that should run this actor (e.g., "node_0" for master).
  // Leave empty to run on all nodes.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower|nDisplay")
  FString MasterNodeName = TEXT("node_2");

protected:
  UPROPERTY()
  AActor* HeroVehicle = nullptr;

  bool bIsActiveOnThisNode = false;

  UPROPERTY()
  ADisplayClusterRootActor* RootDisplayActor = nullptr;

  UPROPERTY()
  USceneComponent* NDisplayTargetComponent = nullptr;

  FString NodeId = TEXT("unknown");

  float HeroSearchLogTimer = 0.0f;
  float DebugTimer = 0.0f;

  void TryFindHero();
  void TryFindRootDisplayActorAndTarget();
  void UpdateNDisplayTargetTransform();
  FString GetDcNodeId() const;
};
