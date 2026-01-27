// Copyright (c) 2026 Computer Vision Center (CVC).
// Simple follower component scaffold for nDisplay root actor.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeroFollowerComponent.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * UHeroFollowerComponent
 * - Moves its owner actor to follow the CARLA "hero" vehicle
 * - Optionally creates two SceneCaptureComponents and render targets for rear/side mirrors
 * - Optionally creates a simple Slate/viewport widget on local nodes to show the mirrors
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CARLA_API UHeroFollowerComponent : public UActorComponent
{
  GENERATED_BODY()

public:
  UHeroFollowerComponent();

  virtual void BeginPlay() override;
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

  // Camera offset in vehicle local space (cm)
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower")
  FVector CameraOffset = FVector(160.0f, 0.0f, 170.0f);

  // Mirror settings
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower")
  bool bEnableRearviewMirrors = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower")
  int32 MirrorWidth = 800;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower")
  int32 MirrorHeight = 600;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower", meta=(ClampMin="1.0"))
  float MirrorUpdateHz = 30.0f;

  // If true, create a viewport widget on this local node (only created when a local PlayerController exists)
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower")
  bool bCreateWidgetOnLocalNode = true;

protected:
  // Cached hero actor
  AActor* HeroVehicle;
  // Timer for hero search logging (to avoid spam)
  float HeroSearchLogTimer;
  // Cached nDisplay root actor (if present on this node)
  AActor* RootDisplayActor;
  // Whether we've already auto-attached ourselves to the root actor
  bool bHasAutoAttached;

  // SceneCapture components and render targets
  USceneCaptureComponent2D* LeftRearCapture;
  USceneCaptureComponent2D* RightRearCapture;
  UTextureRenderTarget2D* LeftRearRenderTarget;
  UTextureRenderTarget2D* RightRearRenderTarget;

  // Slate brushes/widgets for overlay
  TSharedPtr<FSlateBrush> LeftRearBrush;
  TSharedPtr<FSlateBrush> RightRearBrush;
  TSharedPtr<SWidget> LeftMirrorWidget;
  TSharedPtr<SWidget> RightMirrorWidget;

  float MirrorTimer;

  // Internal helpers
  void TryFindHero();
  void UpdateHeroVehicleTracking(float DeltaTime);
  void TryFindRootDisplayActor();
  void TryAutoAttachToRoot();
  void UpdateOwnerTransform();
  void CreateMirrorCaptures();
  void CreateMirrorWidgets();
  bool ShouldCreateWidgetForThisNode() const;
};
