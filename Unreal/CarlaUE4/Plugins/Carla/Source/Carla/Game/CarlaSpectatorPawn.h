// Copyright (c) 2025 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/AudioComponent.h"
#include "CarlaSpectatorPawn.generated.h"

/**
 * Custom Spectator Pawn for CARLA with hero vehicle tracking and engine sound.
 * 
 * This spectator pawn supports:
 * - Automatic tracking of the "hero" vehicle
 * - Engine sound playback based on vehicle RPM
 * - Rear-view mirror rendering
 * - Standard spectator controls
 * 
 * For multi-screen rendering, use nDisplay plugin instead of custom camera setup.
 */
UCLASS()
class CARLA_API ACarlaSpectatorPawn : public ASpectatorPawn
{
  GENERATED_BODY()

public:
  
  /**
   * Constructor - sets up default values and creates the camera components.
   */
  ACarlaSpectatorPawn(const FObjectInitializer& ObjectInitializer);

protected:
  
  /**
   * Called when the game starts or when spawned.
   * This is where we set up the scene capture and render target for the backward view.
   */
  virtual void BeginPlay() override;

  /**
   * Called when the actor is being destroyed or PIE ends.
   * Resets initialization state for next PIE session.
   */
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  /**
   * Called every frame.
   * Updates the backward camera to stay synchronized with the spectator.
   */
  virtual void Tick(float DeltaTime) override;

  /**
   * Sets up input bindings for the spectator pawn.
   */
  virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

public:

  /**
   * Toggles rearview mirrors on/off.
   */
  UFUNCTION(BlueprintCallable, Category = "Camera")
  void ToggleRearviewMirrors();
  
  /**
   * Main camera component.
   * This is used by nDisplay for multi-screen rendering.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  UCameraComponent* ForwardCamera;

  /**
   * Rear-view scene capture component for left mirror (180° rear view).
   * Captures a wide rear view for cropping in the left rear-view mirror.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  USceneCaptureComponent2D* LeftRearSceneCapture;

  /**
   * Rear-view scene capture component for right mirror (180° rear view).
   * Captures a wide rear view for cropping in the right rear-view mirror.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  USceneCaptureComponent2D* RightRearSceneCapture;

  /**
   * Render target for left rear-view mirror (wider for cropping).
   * The LeftRearSceneCapture renders to this texture.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  UTextureRenderTarget2D* LeftRearRenderTarget;

  /**
   * Render target for right rear-view mirror (wider for cropping).
   * The RightRearSceneCapture renders to this texture.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  UTextureRenderTarget2D* RightRearRenderTarget;

  /**
   * Engine sound audio component.
   * Plays engine sound based on RPM parameter from the hero vehicle.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
  UAudioComponent* EngineCue;

  /**
   * Horizontal crop offset for left rear-view mirror (0.0 = left edge, 1.0 = right edge).
   * Controls which portion of the rear view is displayed in the mirror.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float LeftMirrorCropOffset = 0.5f;

  /**
   * Horizontal crop offset for right rear-view mirror (0.0 = left edge, 1.0 = right edge).
   * Controls which portion of the rear view is displayed in the mirror.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float RightMirrorCropOffset = 0.5f;

private:

  /**
   * Tracks whether initialization has been completed.
   * Reset in EndPlay to allow re-initialization in subsequent PIE sessions.
   */
  bool bInitialized;

  /**
   * Time accumulator for updating rear mirrors (every 0.033 seconds = 30Hz).
   * Reduces GPU load by updating mirrors less frequently than main views.
   */
  float MirrorUpdateTimer;

  /**
   * Enable/disable rearview mirrors for performance testing.
   * Toggle this in the editor to measure performance impact of mirrors.
   */
  UPROPERTY(Category = "CARLA Spectator", EditAnywhere)
  bool bEnableRearviewMirrors = false;

  /**
   * Slate brushes for rendering the mirror textures.
   */
  TSharedPtr<FSlateBrush> LeftRearBrush;
  TSharedPtr<FSlateBrush> RightRearBrush;

  /**
   * Slate widgets for displaying the mirrors in the viewport.
   */
  TSharedPtr<SWidget> LeftMirrorWidget;
  TSharedPtr<SWidget> RightMirrorWidget;

  /**
   * Creates and adds the rearview mirror widgets to the viewport.
   */
  void CreateRearviewMirrorWidgets();

  /**
   * Finds and tracks the hero vehicle in the world.
   */
  void UpdateHeroVehicleTracking(float DeltaTime);

  /**
   * Cached reference to the hero vehicle being tracked.
   */
  AActor* HeroVehicle;

  /**
   * Camera offset from vehicle origin (driver's eye position).
   */
  FVector CameraOffset;

  /**
   * Timer for hero vehicle search logging.
   */
  float HeroSearchLogTimer;
};
