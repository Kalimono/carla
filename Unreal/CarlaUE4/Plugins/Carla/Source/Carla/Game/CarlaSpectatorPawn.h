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
#include "Components/SceneCaptureComponent2D.h"
#include "Blueprint/UserWidget.h"
#include "CarlaSpectatorPawn.generated.h"

/**
 * Custom Spectator Pawn with triple-screen camera support.
 * 
 * This class extends the standard Unreal Engine SpectatorPawn to support
 * three simultaneous camera views for a triple-monitor setup (5760x1080):
 * - Left screen: 90° left view (1920x1080)
 * - Center screen: Forward view (1920x1080)
 * - Right screen: 90° right view (1920x1080)
 * 
 * IMPLEMENTATION:
 * We use a UCameraComponent for the center forward view (standard rendering).
 * For the left and right views, we use USceneCaptureComponent2D components that 
 * render to textures, which are then displayed on screen using a UMG widget.
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

public:
  
  /**
   * Forward-facing camera component (center screen - main view).
   * This is the standard camera that the player controller uses.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  UCameraComponent* ForwardCamera;

  /**
   * Left-facing scene capture component (left screen - 90° left view).
   * This captures the scene 90° to the left and renders it to a texture.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  USceneCaptureComponent2D* LeftSceneCapture;

  /**
   * Right-facing scene capture component (right screen - 90° right view).
   * This captures the scene 90° to the right and renders it to a texture.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  USceneCaptureComponent2D* RightSceneCapture;

  /**
   * Render target for the left view (1920x1080).
   * The LeftSceneCapture renders to this texture.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  UTextureRenderTarget2D* LeftRenderTarget;

  /**
   * Render target for the right view (1920x1080).
   * The RightSceneCapture renders to this texture.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  UTextureRenderTarget2D* RightRenderTarget;

  /**
   * Instance of the triple-screen widget (created programmatically).
   */
  UPROPERTY()
  UUserWidget* TripleScreenWidgetInstance;

private:
  
  /**
   * Creates and displays the triple-screen UI widget.
   * This widget shows all three camera views side-by-side.
   */
  void CreateTripleScreenWidget();

  /**
   * Tracks whether initialization has been completed.
   * Reset in EndPlay to allow re-initialization in subsequent PIE sessions.
   */
  bool bInitialized;

  /**
   * Slate brushes for left and right camera images.
   * Must persist as member variables for Slate widget lifetime.
   */
  TSharedPtr<FSlateBrush> LeftBrush;
  TSharedPtr<FSlateBrush> RightBrush;
};
