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
 * Custom Spectator Pawn with dual camera support.
 * 
 * This class extends the standard Unreal Engine SpectatorPawn to support
 * two simultaneous camera views: one forward-facing and one backward-facing.
 * The views are displayed in a split-screen configuration (horizontal split).
 * 
 * IMPLEMENTATION NOTE:
 * We use a UCameraComponent for the main forward view (standard Unreal rendering).
 * For the backward view, we use a USceneCaptureComponent2D that renders to a texture,
 * which is then displayed on screen using a UMG widget. This approach is simpler and
 * more reliable than trying to create multiple player controllers for split-screen.
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
   * Called every frame.
   * Updates the backward camera to stay synchronized with the spectator.
   */
  virtual void Tick(float DeltaTime) override;

public:
  
  /**
   * Forward-facing camera component (main view).
   * This is the standard camera that the player controller uses.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  UCameraComponent* ForwardCamera;

  /**
   * Backward-facing scene capture component.
   * This captures the scene from the backward direction and renders it to a texture.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  USceneCaptureComponent2D* BackwardSceneCapture;

  /**
   * Render target for the backward view.
   * The BackwardSceneCapture renders to this texture, which is then displayed on screen.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
  UTextureRenderTarget2D* BackwardRenderTarget;

  /**
   * The UMG widget class to use for displaying the split-screen view.
   * Set this in Blueprint or C++ to reference your custom widget.
   */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
  TSubclassOf<UUserWidget> SplitScreenWidgetClass;

  /**
   * Instance of the split-screen widget currently displayed.
   */
  UPROPERTY()
  UUserWidget* SplitScreenWidgetInstance;

private:
  
  /**
   * Creates and displays the split-screen UI widget.
   * This widget shows both the main camera view and the backward render target.
   */
  void CreateSplitScreenWidget();
};
