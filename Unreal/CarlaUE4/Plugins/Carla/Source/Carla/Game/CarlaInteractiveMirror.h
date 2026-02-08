// Copyright (c) 2025 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/Runnable.h"
#include "Dom/JsonObject.h"
#include "CarlaInteractiveMirror.generated.h"

// Forward declarations
class SMirrorImage;

/**
 * Mirror display mode enumeration.
 */
UENUM(BlueprintType)
enum class EMirrorMode : uint8
{
  Pan           UMETA(DisplayName = "Pan Mode"),           // Slide a fixed-size slice horizontally
  ZoomOut       UMETA(DisplayName = "Zoom Out Mode"),      // Zoom out and pan toward center
  ZoomOutProper UMETA(DisplayName = "Zoom Out Proper"),    // Zoom out while staying edge-aligned
  ZoomOutBorder UMETA(DisplayName = "Zoom Out Border")     // Zoom out with letterboxing to show more width
};

/**
 * Mirror configuration structure loaded from JSON.
 */
USTRUCT(BlueprintType)
struct FMirrorConfig
{
  GENERATED_BODY()

  // Scene capture settings
  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  FVector RelativeLocation = FVector::ZeroVector;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  FRotator RelativeRotation = FRotator::ZeroRotator;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  float FOV = 90.0f;

  // Overlay display settings
  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  float OverlayWidth = 300.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  float OverlayHeight = 400.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  float OverlayOffsetX = 20.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  float OverlayOffsetY = 20.0f;

  // Anchor position: "left" or "right"
  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  FString AnchorSide = TEXT("left");

  // Border settings
  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  bool bEnableBorder = true;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  float BorderWidth = 5.0f;

  // Mirror display mode
  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  EMirrorMode Mode = EMirrorMode::Pan;
};

/**
 * Delegate for modular mirror transform behavior.
 * 
 * This delegate is called every frame to transform the mirror view.
 * It receives the current transform value (e.g., pan offset) and DeltaTime,
 * and can modify the UV coordinates used to sample the render target.
 * 
 * @param TransformValue The current transform parameter (e.g., horizontal pan from 0.0 to 1.0)
 * @param UVCoord The UV coordinate being sampled (can be modified for effects)
 * @param DeltaTime Frame delta time for smooth animations
 * @return Modified UV coordinate
 */
DECLARE_DELEGATE_RetVal_ThreeParams(FVector2D, FMirrorTransformDelegate, float /*TransformValue*/, FVector2D /*UVCoord*/, float /*DeltaTime*/);

/**
 * Interactive mirror actor for nDisplay side views (node_1 and node_3).
 * 
 * This actor creates a slate overlay that displays a scene capture as a "mirror",
 * with support for real-time transform effects (pan, zoom, etc.) controlled via UDP.
 * 
 * Features:
 * - Only activates on node_1 and node_3 (side view nodes)
 * - UDP socket for receiving transform parameters
 * - Modular transform system using delegates
 * - Default horizontal pan behavior
 * - Can be extended with zoom, rotation, or custom effects
 * 
 * Usage:
 * - Place in world or spawn via Blueprint/C++
 * - Send UDP packets to control transform (format: "pan:0.5" for 50% pan)
 * - Override transform delegate for custom behavior
 */
UCLASS(Blueprintable, BlueprintType)
class CARLA_API ACarlaInteractiveMirror : public AActor
{
  GENERATED_BODY()

public:

  /**
   * Constructor - sets up default values.
   */
  ACarlaInteractiveMirror();

protected:

  /**
   * Called when the game starts or when spawned.
   * Initializes the mirror system if running on node_1 or node_3.
   */
  virtual void BeginPlay() override;

  /**
   * Called when the actor is being destroyed.
   * Cleans up UDP socket and slate widgets.
   */
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  /**
   * Called every frame.
   * Updates transform from UDP, applies mirror transform, and triggers render.
   */
  virtual void Tick(float DeltaTime) override;

public:

  /**
   * Scene capture component for capturing the mirror view.
   * Configure rotation and position to capture the desired view.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mirror")
  USceneCaptureComponent2D* MirrorSceneCapture;

  /**
   * Render target for the mirror capture.
   * This texture is displayed in the slate overlay.
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mirror")
  UTextureRenderTarget2D* MirrorRenderTarget;

  /**
   * Enable/disable the mirror overlay.
   * When disabled, the mirror is hidden and scene capture stops.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mirror")
  bool bEnableMirror = true;

  /**
   * Enable/disable the black border around the mirror.
   * When enabled, draws a 5-pixel black border around the mirror overlay.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mirror")
  bool bEnableBorder = true;

  /**
   * Path to JSON configuration file.
   * Format: {"node_1": {...config...}, "node_3": {...config...}}
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mirror|Config")
  FString ConfigFilePath = TEXT("Config/MirrorConfig.json");

  /**
   * UDP port for receiving transform commands.
   * Send packets in format: "pan:0.5" or "zoom:1.5"
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mirror|Network")
  int32 UDPPort = 9876;

  /**
   * Mirror render target resolution width.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mirror|Rendering")
  int32 MirrorWidth = 1280;

  /**
   * Mirror render target resolution height.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mirror|Rendering")
  int32 MirrorHeight = 960;

  /**
   * Current mirror display mode.
   * Pan: Slide a fixed-size slice horizontally
   * ZoomOut: Zoom out to show more of the capture while maintaining aspect ratio
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mirror|Transform")
  EMirrorMode MirrorMode = EMirrorMode::Pan;

  /**
   * Current horizontal pan value (0.0 to 1.0).
   * Left mirror: 0.0 = far right edge, 1.0 = far left edge
   * Right mirror: 0.0 = far left edge, 1.0 = far right edge
   * This is updated via UDP and controls which slice of the render target is displayed.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mirror|Transform")
  float HorizontalPan = 0.0f;

  /**
   * Mirror update rate in Hz (0 = every frame).
   * Lower values improve performance but reduce mirror smoothness.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mirror|Performance")
  float UpdateRate = 30.0f;

  /**
   * Set the horizontal pan value (0.0 to 1.0).
   * This controls which slice of the render target is displayed.
   */
  UFUNCTION(BlueprintCallable, Category = "Mirror")
  void SetHorizontalPan(float NewPan);

  /**
   * Set the mirror display mode.
   * Use console command: SetMirrorMode 0 (Pan) or SetMirrorMode 1 (ZoomOut)
   */
  UFUNCTION(Exec, BlueprintCallable, Category = "Mirror")
  void SetMirrorMode(int32 Mode);

  /**
   * Set a custom transform delegate for modular behavior.
   * Use this to implement zoom, rotation, or other effects.
   */
  void SetTransformDelegate(FMirrorTransformDelegate InDelegate);

  /**
   * Get the current node name (e.g., "node_1", "node_3").
   * Returns empty string if not in nDisplay mode.
   */
  UFUNCTION(BlueprintCallable, Category = "Mirror")
  FString GetCurrentNodeName() const;

  /**
   * Check if this mirror should be active on the current node.
   * Returns true if running on node_1 or node_3.
   */
  UFUNCTION(BlueprintCallable, Category = "Mirror")
  bool ShouldActivateOnCurrentNode() const;

private:

  /**
   * Initialize the mirror system (render target, slate overlay, UDP socket).
   */
  void InitializeMirror();

  /**
   * Load configuration from JSON file.
   */
  bool LoadConfigFromJSON();

  /**
   * Apply configuration for the current node.
   */
  void ApplyNodeConfig(const FMirrorConfig& Config);

  /**
   * Create the slate overlay widget.
   */
  void CreateMirrorWidget();

  /**
   * Initialize UDP socket for receiving transform commands.
   */
  void InitializeUDPSocket();

  /**
   * Process UDP packets and update transform values.
   */
  void ProcessUDPPackets();

  /**
   * Default horizontal pan transform function.
   * Maps horizontal pan value to UV coordinate offset.
   */
  FVector2D DefaultPanTransform(float PanValue, FVector2D UVCoord, float DeltaTime);

  /**
   * Parse UDP command string (e.g., "pan:0.5").
   */
  void ParseUDPCommand(const FString& Command);

  /**
   * Update hero vehicle tracking - attach/detach scene capture to hero.
   */
  void UpdateHeroVehicleTracking(float DeltaTime);

  /**
   * Tracks whether initialization has been completed.
   */
  bool bInitialized;

  /**
   * Time accumulator for update rate limiting.
   */
  float UpdateTimer;

  /**
   * Slate brush for rendering the mirror texture.
   */
  TSharedPtr<FSlateBrush> MirrorBrush;

  /**
   * Slate widget for displaying the mirror in the viewport.
   */
  TSharedPtr<SWidget> MirrorWidget;

  /**
   * Reference to the mirror image widget for updating pan values.
   */
  TSharedPtr<SMirrorImage> MirrorImageWidget;

  /**
   * UDP socket for receiving transform commands.
   */
  FSocket* UDPSocket;

  /**
   * Cached node name for nDisplay.
   */
  FString NodeName;

  /**
   * Whether this mirror should be active on the current node.
   */
  bool bShouldBeActive;

  /**
   * Modular transform delegate.
   * Override this to implement custom transform behaviors.
   */
  FMirrorTransformDelegate TransformDelegate;

  /**
   * Current node configuration loaded from JSON.
   */
  FMirrorConfig CurrentConfig;

  /**
   * Cached reference to the hero vehicle being tracked.
   */
  AActor* HeroVehicle;

  /**
   * Timer for hero vehicle search logging.
   */
  float HeroSearchLogTimer;
};
