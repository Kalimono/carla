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
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "CarlaSpectatorPawn.generated.h"

// Forward declarations
class FSocket;
class FRunnableThread;

/**
 * Asynchronous UDP receiver that runs on a separate thread.
 */
class FUdpMirrorReceiver : public FRunnable
{
public:
  FUdpMirrorReceiver(int32 Port);
  virtual ~FUdpMirrorReceiver();

  // FRunnable interface
  virtual bool Init() override;
  virtual uint32 Run() override;
  virtual void Stop() override;
  virtual void Exit() override;

  // Thread-safe getters for mirror offsets
  void GetMirrorOffsets(float& OutLeft, float& OutRight);

private:
  FSocket* Socket;
  FRunnableThread* Thread;
  FThreadSafeBool bShouldRun;
  int32 ListenPort;

  // Thread-safe storage for mirror offsets
  FCriticalSection DataLock;
  float LeftOffset;
  float RightOffset;
};

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

  /**
   * Slate brushes for rear-view mirrors.
   * Must persist as member variables for Slate widget lifetime.
   */
  TSharedPtr<FSlateBrush> LeftRearBrush;
  TSharedPtr<FSlateBrush> RightRearBrush;

  /**
   * Asynchronous UDP receiver for mirror crop offset updates.
   */
  TUniquePtr<FUdpMirrorReceiver> UdpReceiver;

  /**
   * UDP receiver thread.
   */
  FRunnableThread* UdpReceiverThread;

  /**
   * Time accumulator for checking UDP updates (every 0.02 seconds = 50Hz).
   */
  float UdpUpdateTimer;

  /**
   * UDP port to listen on for mirror offset updates.
   */
  int32 UdpPort = 8888;

  /**
   * Starts the asynchronous UDP receiver.
   */
  void StartUdpReceiver();

  /**
   * Stops the asynchronous UDP receiver.
   */
  void StopUdpReceiver();

  /**
   * Updates mirror offsets from UDP data.
   */
  void UpdateMirrorOffsetsFromUdp();
};
