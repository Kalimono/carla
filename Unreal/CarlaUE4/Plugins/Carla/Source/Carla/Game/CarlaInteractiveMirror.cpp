// Copyright (c) 2025 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla.h"
#include "Carla/Game/CarlaInteractiveMirror.h"
#include "Carla/Game/CarlaStatics.h"
#include "Carla/Game/CarlaEpisode.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Networking/Public/Networking.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"

// =====================================================
// Custom Slate Widget for UV-Transformed Mirror
// =====================================================

/**
 * Custom slate widget that applies UV transforms to the mirror image.
 * This allows for horizontal panning and other effects without modifying the render target.
 */
class SMirrorImage : public SCompoundWidget
{
public:
  SLATE_BEGIN_ARGS(SMirrorImage) {}
    SLATE_ARGUMENT(const FSlateBrush*, Brush)
    SLATE_ARGUMENT(float, HorizontalPan)
    SLATE_ARGUMENT(bool, bIsLeftSide)  // True for left mirror, false for right
    SLATE_ARGUMENT(EMirrorMode, Mode)  // Pan or ZoomOut mode
    SLATE_ARGUMENT(float, BaseZoomLevel)  // Base zoom level (0.0-1.0), default 0.6
  SLATE_END_ARGS()

  void Construct(const FArguments& InArgs)
  {
    Brush = InArgs._Brush;
    HorizontalPan = InArgs._HorizontalPan;
    bIsLeftSide = InArgs._bIsLeftSide;
    Mode = InArgs._Mode;
    BaseZoomLevel = InArgs._BaseZoomLevel;

    ChildSlot
    [
      SNew(SImage)
      .Image(Brush)
    ];
  }

  void SetHorizontalPan(float NewPan)
  {
    HorizontalPan = FMath::Clamp(NewPan, 0.0f, 1.0f);
    Invalidate(EInvalidateWidgetReason::Paint);
  }

  void SetMode(EMirrorMode NewMode)
  {
    Mode = NewMode;
    Invalidate(EInvalidateWidgetReason::Paint);
  }

  virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, 
    const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, 
    int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
  {
    if (Brush && Brush->GetResourceObject())
    {
      const FVector2D ImageSize = Brush->ImageSize; // Render target size (e.g., 1024x768)
      const FVector2D OverlaySize = AllottedGeometry.GetLocalSize(); // Mirror overlay size (e.g., 300x400)
      const float OverlayAspect = OverlaySize.X / OverlaySize.Y; // e.g., 300/400 = 0.75
      const float ImageAspect = ImageSize.X / ImageSize.Y; // e.g., 1024/768 = 1.33
      
      if (Mode == EMirrorMode::Pan)
      {
        // PAN MODE: Slide a fixed-size slice horizontally
        // Calculate slice width to match the mirror's aspect ratio
        // Formula: SliceWidthUV = OverlayAspect / ImageAspect
        // This ensures captured content has same aspect ratio as the mirror
        float SliceWidthUV = (ImageAspect > 0) ? (OverlayAspect / ImageAspect) : 0.3f;
        SliceWidthUV = FMath::Clamp(SliceWidthUV, 0.1f, 1.0f);
        
        float StartU;
        if (bIsLeftSide)
        {
          StartU = HorizontalPan * (1.0f - SliceWidthUV);
        }
        else
        {
          StartU = (1.0f - HorizontalPan) * (1.0f - SliceWidthUV);
        }
        StartU = FMath::Clamp(StartU, 0.0f, 1.0f - SliceWidthUV);
        
        FSlateBrush ModifiedBrush = *Brush;
        ModifiedBrush.SetUVRegion(FBox2D(
          FVector2D(StartU + SliceWidthUV, 0.0f),
          FVector2D(StartU, 1.0f)
        ));
        
        FSlateDrawElement::MakeBox(
          OutDrawElements,
          LayerId,
          AllottedGeometry.ToPaintGeometry(),
          &ModifiedBrush,
          ESlateDrawEffect::None,
          FLinearColor::White
        );
      }
      else if (Mode == EMirrorMode::ZoomOut)
      {
        // ZOOM OUT MODE: Show more of the capture as pan decreases
        // Phase 1 (pan 1.0->0.25): Zoom out while staying edge-aligned until max width
        // Phase 2 (pan 0.25->0.0): Pan away from edge while zoomed out
        // At pan=1.0 (default): Zoomed in, positioned at edge
        
        // Maximum slice width that maintains mirror aspect ratio
        float MaxSliceWidthUV = (ImageAspect > 0) ? (OverlayAspect / ImageAspect) : 0.5f;
        MaxSliceWidthUV = FMath::Clamp(MaxSliceWidthUV, 0.1f, 1.0f);
        
        // Base slice size (at zoom in, pan=1.0)
        float BaseSliceWidthUV = MaxSliceWidthUV * BaseZoomLevel;
        
        // Invert zoom phase: pan 1.0->0.25 zooms out, pan 0.25->0.0 stays at max and pans
        float InvertedPan = 1.0f - HorizontalPan;  // 0.0 at pan=1.0, 1.0 at pan=0.0
        float ZoomPhase = FMath::Min(InvertedPan / 0.75f, 1.0f);
        float CurrentSliceWidthUV = FMath::Lerp(BaseSliceWidthUV, MaxSliceWidthUV, ZoomPhase);
        
        // Calculate corresponding height based on aspect ratio
        float CurrentSliceHeightUV = (CurrentSliceWidthUV * ImageSize.X) / (ImageSize.Y * OverlayAspect);
        
        // If height exceeds available capture, clamp and recalculate width
        if (CurrentSliceHeightUV > 1.0f)
        {
          CurrentSliceHeightUV = 1.0f;
          CurrentSliceWidthUV = (CurrentSliceHeightUV * ImageSize.Y * OverlayAspect) / ImageSize.X;
        }
        
        // Calculate pan factor: 0.0 from pan 1.0-0.25, then scales from 0 to 1 during pan 0.25-0.0
        float CenterPanFactor = 0.0f;
        if (InvertedPan > 0.75f)
        {
          CenterPanFactor = (InvertedPan - 0.75f) / 0.25f;  // Maps 0.75-1.0 (inverted) to 0.0-1.0
        }
        
        // Position the slice: edge-aligned during zoom phase, then pan toward center
        float StartU;
        if (bIsLeftSide)
        {
          // Left mirror: Start at right edge (visually, after mirroring), move to center during pan phase
          float EdgeAlignedU = 1.0f - CurrentSliceWidthUV;
          float CenteredU = (1.0f - CurrentSliceWidthUV) * 0.5f;
          StartU = FMath::Lerp(EdgeAlignedU, CenteredU, CenterPanFactor);
        }
        else
        {
          // Right mirror: Start at left edge (visually, after mirroring), move to center during pan phase
          float EdgeAlignedU = 0.0f;
          float CenteredU = (1.0f - CurrentSliceWidthUV) * 0.5f;
          StartU = FMath::Lerp(EdgeAlignedU, CenteredU, CenterPanFactor);
        }
        
        float StartV = (1.0f - CurrentSliceHeightUV) * 0.5f;
        
        // Draw black background for letterboxing if needed
        if (CurrentSliceHeightUV < 1.0f)
        {
          FSlateDrawElement::MakeBox(
            OutDrawElements,
            LayerId,
            AllottedGeometry.ToPaintGeometry(),
            FCoreStyle::Get().GetBrush("WhiteBrush"),
            ESlateDrawEffect::None,
            FLinearColor::Black
          );
          LayerId++;
        }
        
        FSlateBrush ModifiedBrush = *Brush;
        ModifiedBrush.SetUVRegion(FBox2D(
          FVector2D(StartU + CurrentSliceWidthUV, StartV),
          FVector2D(StartU, StartV + CurrentSliceHeightUV)
        ));
        
        FSlateDrawElement::MakeBox(
          OutDrawElements,
          LayerId,
          AllottedGeometry.ToPaintGeometry(),
          &ModifiedBrush,
          ESlateDrawEffect::None,
          FLinearColor::White
        );
      }
      else if (Mode == EMirrorMode::ZoomOutProper)
      {
        // ZOOM OUT PROPER MODE: Show more of the capture while staying edge-aligned
        // At pan=1.0 (default): Show zoomed-in edge-aligned view
        // At pan=0.0: Show maximum without center panning, still edge-aligned
        
        // Calculate zoom factor (inverted: 0.0 at pan=1.0 = zoomed in, 1.0 at pan=0.0 = zoomed out)
        float ZoomFactor = 1.0f - HorizontalPan;
        
        // Maximum slice width that maintains mirror aspect ratio
        float MaxSliceWidthUV = (ImageAspect > 0) ? (OverlayAspect / ImageAspect) : 0.5f;
        MaxSliceWidthUV = FMath::Clamp(MaxSliceWidthUV, 0.1f, 1.0f);
        
        // Base slice size (at zoom=0)
        float BaseSliceWidthUV = MaxSliceWidthUV * BaseZoomLevel;
        
        // Interpolate from zoomed-in to maximum width
        float CurrentSliceWidthUV = FMath::Lerp(BaseSliceWidthUV, MaxSliceWidthUV, ZoomFactor);
        
        // Calculate corresponding height based on aspect ratio
        float CurrentSliceHeightUV = (CurrentSliceWidthUV * ImageSize.X) / (ImageSize.Y * OverlayAspect);
        
        // If height exceeds available capture, clamp and recalculate width
        if (CurrentSliceHeightUV > 1.0f)
        {
          CurrentSliceHeightUV = 1.0f;
          CurrentSliceWidthUV = (CurrentSliceHeightUV * ImageSize.Y * OverlayAspect) / ImageSize.X;
        }
        
        // Position: Always edge-aligned (never moves toward center)
        float StartU;
        if (bIsLeftSide)
        {
          // Left mirror: Always aligned to right edge (visually, after mirroring)
          StartU = 1.0f - CurrentSliceWidthUV;
        }
        else
        {
          // Right mirror: Always aligned to left edge (visually, after mirroring)
          StartU = 0.0f;
        }
        
        float StartV = (1.0f - CurrentSliceHeightUV) * 0.5f;
        
        // Draw black background for letterboxing if needed
        if (CurrentSliceHeightUV < 1.0f)
        {
          FSlateDrawElement::MakeBox(
            OutDrawElements,
            LayerId,
            AllottedGeometry.ToPaintGeometry(),
            FCoreStyle::Get().GetBrush("WhiteBrush"),
            ESlateDrawEffect::None,
            FLinearColor::Black
          );
          LayerId++;
        }
        
        FSlateBrush ModifiedBrush = *Brush;
        ModifiedBrush.SetUVRegion(FBox2D(
          FVector2D(StartU + CurrentSliceWidthUV, StartV),
          FVector2D(StartU, StartV + CurrentSliceHeightUV)
        ));
        
        FSlateDrawElement::MakeBox(
          OutDrawElements,
          LayerId,
          AllottedGeometry.ToPaintGeometry(),
          &ModifiedBrush,
          ESlateDrawEffect::None,
          FLinearColor::White
        );
      }
      else // EMirrorMode::ZoomOutBorder
      {
        // ZOOM OUT BORDER MODE: Show more width with letterboxing when height maxes out
        // At pan=1.0 (default): Show zoomed-in edge-aligned view
        // As pan decreases: Zoom out until height = 100% (around pan=0.25), then add letterboxing and continue width
        
        // Calculate zoom factor (inverted: 0.0 at pan=1.0 = zoomed in, 1.0 at pan=0.0 = fully zoomed out)
        float ZoomFactor = 1.0f - HorizontalPan;
        
        // Maximum slice width that maintains mirror aspect ratio
        float MaxSliceWidthUV = (ImageAspect > 0) ? (OverlayAspect / ImageAspect) : 0.5f;
        MaxSliceWidthUV = FMath::Clamp(MaxSliceWidthUV, 0.1f, 1.0f);
        
        // Base slice size (at zoom=0)
        float BaseSliceWidthUV = MaxSliceWidthUV * BaseZoomLevel;
        
        // Calculate what width would give us at this zoom level
        // For ZoomOutBorder, we continue past MaxSliceWidthUV to show more width with letterboxing
        float DesiredSliceWidthUV = FMath::Lerp(BaseSliceWidthUV, 1.0f, ZoomFactor);
        
        // Calculate corresponding height based on aspect ratio
        float CorrespondingHeightUV = (DesiredSliceWidthUV * ImageSize.X) / (ImageSize.Y * OverlayAspect);
        
        float CurrentSliceWidthUV;
        float CurrentSliceHeightUV;
        float RenderHeightFraction = 1.0f; // How much of overlay height to use for image (rest is letterbox)
        
        if (CorrespondingHeightUV <= 1.0f)
        {
          // Normal case: Height fits within capture
          CurrentSliceWidthUV = DesiredSliceWidthUV;
          CurrentSliceHeightUV = CorrespondingHeightUV;
        }
        else
        {
          // Height would exceed capture - clamp height and add letterboxing
          CurrentSliceHeightUV = 1.0f; // Use full capture height
          
          // Keep increasing width beyond what aspect ratio would normally allow
          CurrentSliceWidthUV = DesiredSliceWidthUV;
          
          // Calculate how much of the overlay height the image should occupy
          // to maintain aspect ratio with the current width
          RenderHeightFraction = (CurrentSliceHeightUV * ImageSize.Y * OverlayAspect) / (CurrentSliceWidthUV * ImageSize.X);
          RenderHeightFraction = FMath::Clamp(RenderHeightFraction, 0.1f, 1.0f);
        }
        
        // Position: Always edge-aligned (never moves toward center)
        float StartU;
        if (bIsLeftSide)
        {
          // Left mirror: Always aligned to right edge (visually, after mirroring)
          StartU = 1.0f - CurrentSliceWidthUV;
        }
        else
        {
          // Right mirror: Always aligned to left edge (visually, after mirroring)
          StartU = 0.0f;
        }
        
        float StartV = (1.0f - CurrentSliceHeightUV) * 0.5f;
        
        // Draw black background for letterboxing
        if (RenderHeightFraction < 1.0f)
        {
          FSlateDrawElement::MakeBox(
            OutDrawElements,
            LayerId,
            AllottedGeometry.ToPaintGeometry(),
            FCoreStyle::Get().GetBrush("WhiteBrush"),
            ESlateDrawEffect::None,
            FLinearColor::Black
          );
          LayerId++;
        }
        
        // Create modified geometry for the image portion (excluding letterbox)
        FPaintGeometry ImageGeometry = AllottedGeometry.ToPaintGeometry(
          FVector2D(0.0f, OverlaySize.Y * (1.0f - RenderHeightFraction) * 0.5f), // Offset Y by letterbox
          FVector2D(OverlaySize.X, OverlaySize.Y * RenderHeightFraction)          // Scale height
        );
        
        FSlateBrush ModifiedBrush = *Brush;
        ModifiedBrush.SetUVRegion(FBox2D(
          FVector2D(StartU + CurrentSliceWidthUV, StartV),
          FVector2D(StartU, StartV + CurrentSliceHeightUV)
        ));
        
        FSlateDrawElement::MakeBox(
          OutDrawElements,
          LayerId,
          ImageGeometry,
          &ModifiedBrush,
          ESlateDrawEffect::None,
          FLinearColor::White
        );
      }
      
      return LayerId + 1;
    }
    
    return LayerId;
  }

private:
  const FSlateBrush* Brush;
  float HorizontalPan;
  bool bIsLeftSide;
  EMirrorMode Mode;
  float BaseZoomLevel;  // Base zoom level from config
};

// =====================================================
// Console Variable Registration (works in nDisplay)
// =====================================================

static TAutoConsoleVariable<int32> CVarMirrorMode(
  TEXT("Mirror.Mode"),
  0,
  TEXT("Interactive mirror mode: 0=Pan, 1=ZoomOut, 2=ZoomOutProper, 3=ZoomOutBorder\nChange with: Mirror.Mode 1"),
  ECVF_Default
);

static TAutoConsoleVariable<float> CVarMirrorPan(
  TEXT("Mirror.Pan"),
  0.0f,
  TEXT("Interactive mirror pan/zoom value (0.0 to 1.0)\nChange with: Mirror.Pan 0.5"),
  ECVF_Default
);

// Keep command-style registration as well for compatibility
static FAutoConsoleCommand MirrorModeConsoleCommand(
  TEXT("Mirror.SetMode"),
  TEXT("Set the interactive mirror mode (0=Pan, 1=ZoomOut, 2=ZoomOutProper, 3=ZoomOutBorder)"),
  FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
  {
    if (Args.Num() < 1)
    {
      UE_LOG(LogTemp, Warning, TEXT("Usage: Mirror.SetMode <mode> (0=Pan, 1=ZoomOut, 2=ZoomOutProper, 3=ZoomOutBorder)"));
      return;
    }

    int32 ModeValue = FCString::Atoi(*Args[0]);
    CVarMirrorMode->Set(ModeValue, ECVF_SetByConsole);
    UE_LOG(LogTemp, Log, TEXT("Mirror mode set to %d via command"), ModeValue);
  })
);

static FAutoConsoleCommand MirrorPanConsoleCommand(
  TEXT("Mirror.SetPan"),
  TEXT("Set the interactive mirror pan/zoom value (0.0 to 1.0)"),
  FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
  {
    if (Args.Num() < 1)
    {
      UE_LOG(LogTemp, Warning, TEXT("Usage: Mirror.SetPan <value> (0.0 to 1.0)"));
      return;
    }

    float PanValue = FCString::Atof(*Args[0]);
    CVarMirrorPan->Set(PanValue, ECVF_SetByConsole);
    UE_LOG(LogTemp, Log, TEXT("Mirror pan set to %.3f via command"), PanValue);
  })
);

// =====================================================
// ACarlaInteractiveMirror Implementation
// =====================================================

/**
 * Constructor Implementation
 */
ACarlaInteractiveMirror::ACarlaInteractiveMirror()
{
  PrimaryActorTick.bCanEverTick = true;
  bInitialized = false;
  UpdateTimer = 0.0f;
  UDPSocket = nullptr;
  bShouldBeActive = false;
  HeroVehicle = nullptr;
  HeroSearchLogTimer = 0.0f;

  // Create the scene capture component
  MirrorSceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("MirrorSceneCapture"));
  MirrorSceneCapture->SetupAttachment(RootComponent);
  MirrorSceneCapture->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  MirrorSceneCapture->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f)); // Default: rear view
  MirrorSceneCapture->CaptureSource = SCS_FinalColorLDR;
  MirrorSceneCapture->bCaptureEveryFrame = false;  // Use manual capture for rate control
  MirrorSceneCapture->bCaptureOnMovement = false;
  MirrorSceneCapture->bAlwaysPersistRenderingState = true;  // Keep render state between captures
  MirrorSceneCapture->ShowFlags.SetMotionBlur(false);
  MirrorSceneCapture->ShowFlags.SetLensFlares(false);
  MirrorSceneCapture->ShowFlags.SetBloom(false);
  // MirrorSceneCapture->ShowFlags.SetEyeAdaptation(false);
  // MirrorSceneCapture->ShowFlags.SetTemporalAA(false);
  
  // Fix brightness: disable auto exposure and use fixed exposure
  // MirrorSceneCapture->PostProcessSettings.bOverride_AutoExposureMethod = true;
  // MirrorSceneCapture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
  // MirrorSceneCapture->PostProcessSettings.bOverride_AutoExposureBias = true;
  // MirrorSceneCapture->PostProcessSettings.AutoExposureBias = 0.0f;
  // MirrorSceneCapture->PostProcessSettings.bOverride_CameraExposureOffset = true;
  // MirrorSceneCapture->PostProcessSettings.CameraExposureOffset = 0.0f;

  MirrorRenderTarget = nullptr;

  // Set up default horizontal pan transform
  TransformDelegate.BindUObject(this, &ACarlaInteractiveMirror::DefaultPanTransform);
}

/**
 * BeginPlay Implementation
 */
void ACarlaInteractiveMirror::BeginPlay()
{
  Super::BeginPlay();

  // Determine node name and whether we should be active
  NodeName = GetCurrentNodeName();
  bShouldBeActive = ShouldActivateOnCurrentNode();

  if (bShouldBeActive)
  {
    UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Active on node '%s'"), *NodeName);
    
    // Load configuration from JSON
    LoadConfigFromJSON();
    
    // Apply mode from config
    MirrorMode = CurrentConfig.Mode;
    
    // Initialize mirror system
    InitializeMirror();
    InitializeUDPSocket();
  }
  else
  {
    UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Inactive on node '%s' (only active on node_1 and node_3)"), 
      *NodeName);
  }
}

/**
 * EndPlay Implementation
 */
void ACarlaInteractiveMirror::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  Super::EndPlay(EndPlayReason);

  // Clean up UDP socket
  if (UDPSocket)
  {
    UDPSocket->Close();
    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(UDPSocket);
    UDPSocket = nullptr;
  }

  // Clean up resources
  MirrorRenderTarget = nullptr;
  MirrorBrush.Reset();
  MirrorWidget.Reset();
  bInitialized = false;
  HeroVehicle = nullptr;

  UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Cleanup complete"));
}

/**
 * Tick Implementation
 */
void ACarlaInteractiveMirror::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);

  if (!bShouldBeActive || !bInitialized || !bEnableMirror)
  {
    return;
  }

  // Check console variables for changes (works in nDisplay)
  static int32 LastCVarMode = -1;
  static float LastCVarPan = -1.0f;
  
  int32 CurrentCVarMode = CVarMirrorMode.GetValueOnGameThread();
  float CurrentCVarPan = CVarMirrorPan.GetValueOnGameThread();
  
  if (CurrentCVarMode != LastCVarMode && CurrentCVarMode >= 0 && CurrentCVarMode <= 3)
  {
    SetMirrorMode(CurrentCVarMode);
    LastCVarMode = CurrentCVarMode;
  }
  
  if (FMath::Abs(CurrentCVarPan - LastCVarPan) > 0.001f)
  {
    SetHorizontalPan(CurrentCVarPan);
    LastCVarPan = CurrentCVarPan;
  }

  // Update hero vehicle tracking (attach/detach scene capture)
  UpdateHeroVehicleTracking(DeltaTime);

  // Process UDP packets for transform updates
  ProcessUDPPackets();

  // Update mirror at specified rate
  UpdateTimer += DeltaTime;
  const float UpdateInterval = (UpdateRate > 0.0f) ? (1.0f / UpdateRate) : 0.0f;
  
  if (UpdateTimer >= UpdateInterval)
  {
    UpdateTimer = 0.0f;

    // Apply transform delegate if bound
    if (TransformDelegate.IsBound())
    {
      // The transform delegate can modify how we interpret the render target
      // For now, we just use it to validate the transform value
      FVector2D TestUV = TransformDelegate.Execute(HorizontalPan, FVector2D(0.5f, 0.5f), DeltaTime);
    }

    // Update the mirror widget with current pan value and mode
    if (MirrorImageWidget.IsValid())
    {
      MirrorImageWidget->SetHorizontalPan(HorizontalPan);
      MirrorImageWidget->SetMode(MirrorMode);
    }

    // Trigger scene capture
    if (MirrorSceneCapture)
    {
      MirrorSceneCapture->CaptureScene();
    }
  }
}

/**
 * InitializeMirror Implementation
 */
void ACarlaInteractiveMirror::InitializeMirror()
{
  UWorld* World = GetWorld();
  if (!World)
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaInteractiveMirror: World is null"));
    return;
  }

  // Don't apply config here - wait until attached to hero vehicle
  // Just store FOV for now
  if (MirrorSceneCapture)
  {
    MirrorSceneCapture->FOVAngle = CurrentConfig.FOV;
  }

  // Create render target
  MirrorRenderTarget = NewObject<UTextureRenderTarget2D>();
  if (!MirrorRenderTarget)
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaInteractiveMirror: Failed to create render target"));
    return;
  }

  MirrorRenderTarget->InitAutoFormat(MirrorWidth, MirrorHeight);
  MirrorRenderTarget->RenderTargetFormat = RTF_RGBA8;
  MirrorRenderTarget->UpdateResource();
  MirrorSceneCapture->TextureTarget = MirrorRenderTarget;

  UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Created render target %dx%d"), 
    MirrorWidth, MirrorHeight);

  // Create slate widget
  CreateMirrorWidget();

  bInitialized = true;
  UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Initialization complete"));
}

/**
 * CreateMirrorWidget Implementation
 */
void ACarlaInteractiveMirror::CreateMirrorWidget()
{
  UWorld* World = GetWorld();
  if (!World) return;

  APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
  if (!PC) return;

  UGameViewportClient* ViewportClient = World->GetGameViewport();
  if (!ViewportClient) return;

  // Create slate brush
  MirrorBrush = MakeShared<FSlateBrush>();
  if (MirrorRenderTarget)
  {
    MirrorBrush->SetResourceObject(MirrorRenderTarget);
    MirrorBrush->ImageSize = FVector2D(MirrorRenderTarget->SizeX, MirrorRenderTarget->SizeY);
    MirrorBrush->DrawAs = ESlateBrushDrawType::Image;
    UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Created slate brush"));
  }

  // Determine anchor position based on configuration
  bool bIsLeftSide = CurrentConfig.AnchorSide.Equals(TEXT("left"), ESearchCase::IgnoreCase);
  FAnchors Anchors = bIsLeftSide ? 
    FAnchors(0.0f, 0.0f, 0.0f, 0.0f) :  // Left anchor
    FAnchors(1.0f, 0.0f, 1.0f, 0.0f);   // Right anchor

  FVector2D Alignment = bIsLeftSide ? 
    FVector2D(0.0f, 0.0f) :  // Align to left
    FVector2D(1.0f, 0.0f);   // Align to right

  // Fixed positioning logic:
  // For left side: offset is positive from left edge
  // For right side: offset is negative (distance from right edge)
  float OffsetX = bIsLeftSide ? 
    CurrentConfig.OverlayOffsetX : 
    -CurrentConfig.OverlayOffsetX;

  // Get border width from config (use class property as override)
  float BorderThickness = (bEnableBorder && CurrentConfig.bEnableBorder) ? CurrentConfig.BorderWidth : 0.0f;

  // Create slate canvas with mirror overlay
  TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas)
    + SConstraintCanvas::Slot()
    .Anchors(Anchors)
    .Offset(FMargin(OffsetX, CurrentConfig.OverlayOffsetY, CurrentConfig.OverlayWidth, CurrentConfig.OverlayHeight))
    .Alignment(Alignment)
    .AutoSize(false)
    [
      SAssignNew(MirrorWidget, SBox)
      .WidthOverride(CurrentConfig.OverlayWidth)
      .HeightOverride(CurrentConfig.OverlayHeight)
      .Visibility(bEnableMirror ? EVisibility::Visible : EVisibility::Hidden)
      [
        // Outer border (if enabled)
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("Border"))
        .BorderBackgroundColor(FLinearColor::Black)
        .Padding(FMargin(BorderThickness))
        [
          // Inner mirror image with UV-based slice rendering
          SAssignNew(MirrorImageWidget, SMirrorImage)
          .Brush(MirrorBrush.Get())
          .HorizontalPan(HorizontalPan)
          .bIsLeftSide(bIsLeftSide)
          .Mode(MirrorMode)
          .BaseZoomLevel(CurrentConfig.BaseZoomLevel)
        ]
      ]
    ];

  ViewportClient->AddViewportWidgetContent(Canvas, 0);
  UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Mirror widget added to viewport at %s side"), 
    bIsLeftSide ? TEXT("left") : TEXT("right"));
  
  // Initialize the widget with current pan value and force a redraw
  if (MirrorImageWidget.IsValid())
  {
    MirrorImageWidget->SetHorizontalPan(HorizontalPan);
    MirrorImageWidget->Invalidate(EInvalidateWidgetReason::Paint);
    UE_LOG(LogTemp, Warning, TEXT("CarlaInteractiveMirror: Initial HorizontalPan set to %.3f"), HorizontalPan);
  }
}

/**
 * InitializeUDPSocket Implementation
 */
void ACarlaInteractiveMirror::InitializeUDPSocket()
{
  ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
  if (!SocketSubsystem)
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaInteractiveMirror: Failed to get socket subsystem"));
    return;
  }

  // Create UDP socket
  UDPSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("CarlaInteractiveMirrorSocket"), false);
  if (!UDPSocket)
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaInteractiveMirror: Failed to create UDP socket"));
    return;
  }

  // Bind to port
  TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
  Addr->SetAnyAddress();
  Addr->SetPort(UDPPort);

  if (!UDPSocket->Bind(*Addr))
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaInteractiveMirror: Failed to bind UDP socket to port %d"), UDPPort);
    SocketSubsystem->DestroySocket(UDPSocket);
    UDPSocket = nullptr;
    return;
  }

  // Set socket to non-blocking
  UDPSocket->SetNonBlocking(true);

  UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: UDP socket initialized on port %d"), UDPPort);
}

/**
 * ProcessUDPPackets Implementation
 */
void ACarlaInteractiveMirror::ProcessUDPPackets()
{
  if (!UDPSocket)
  {
    return;
  }

  TArray<uint8> RecvData;
  RecvData.SetNumUninitialized(1024);
  int32 BytesRead = 0;

  // Read all available packets (non-blocking)
  while (UDPSocket->Recv(RecvData.GetData(), RecvData.Num(), BytesRead))
  {
    if (BytesRead > 0)
    {
      FString Command = FString(BytesRead, (const char*)RecvData.GetData());
      UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Received UDP packet (%d bytes): %s"), BytesRead, *Command);
      ParseUDPCommand(Command);
    }
  }
}

/**
 * ParseUDPCommand Implementation
 */
void ACarlaInteractiveMirror::ParseUDPCommand(const FString& Command)
{
  // Expected format: "command:value"
  // Examples: "pan:0.5", "mode:0", "mode:1"
  
  FString CommandName, ValueString;
  if (Command.Split(TEXT(":"), &CommandName, &ValueString))
  {
    CommandName = CommandName.TrimStartAndEnd();
    ValueString = ValueString.TrimStartAndEnd();
    
    if (CommandName.Equals(TEXT("pan"), ESearchCase::IgnoreCase))
    {
      float Value = FCString::Atof(*ValueString);
      SetHorizontalPan(Value);
    }
    else if (CommandName.Equals(TEXT("mode"), ESearchCase::IgnoreCase))
    {
      int32 Value = FCString::Atoi(*ValueString);
      SetMirrorMode(Value);
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("CarlaInteractiveMirror: Unknown command '%s'"), *CommandName);
    }
  }
  else
  {
    UE_LOG(LogTemp, Warning, TEXT("CarlaInteractiveMirror: Invalid command format '%s'"), *Command);
  }
}

/**
 * DefaultPanTransform Implementation
 */
FVector2D ACarlaInteractiveMirror::DefaultPanTransform(float PanValue, FVector2D UVCoord, float DeltaTime)
{
  // Default horizontal pan behavior
  // PanValue: 0.0 = show left edge, 1.0 = show right edge
  // This function can be replaced via SetTransformDelegate for custom behavior
  
  const float ViewportWidth = 0.5f; // Show 50% of the render target width
  const float UOffset = FMath::Clamp(PanValue - ViewportWidth * 0.5f, 0.0f, 1.0f - ViewportWidth);
  
  // Apply offset to U coordinate
  FVector2D TransformedUV = UVCoord;
  TransformedUV.X = UOffset + (UVCoord.X * ViewportWidth);
  
  return TransformedUV;
}

/**
 * SetHorizontalPan Implementation
 */
void ACarlaInteractiveMirror::SetHorizontalPan(float NewPan)
{
  HorizontalPan = FMath::Clamp(NewPan, 0.0f, 1.0f);
  
  // Invalidate widget to trigger repaint
  if (MirrorImageWidget.IsValid())
  {
    MirrorImageWidget->Invalidate(EInvalidateWidgetReason::Paint);
  }
}

/**
 * SetMirrorMode Implementation
 */
void ACarlaInteractiveMirror::SetMirrorMode(int32 Mode)
{
  if (Mode == 0)
  {
    MirrorMode = EMirrorMode::Pan;
    UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Switched to Pan mode"));
  }
  else if (Mode == 1)
  {
    MirrorMode = EMirrorMode::ZoomOut;
    UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Switched to ZoomOut mode"));
  }
  else if (Mode == 2)
  {
    MirrorMode = EMirrorMode::ZoomOutProper;
    UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Switched to ZoomOutProper mode"));
  }
  else if (Mode == 3)
  {
    MirrorMode = EMirrorMode::ZoomOutBorder;
    UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Switched to ZoomOutBorder mode"));
  }
  else
  {
    UE_LOG(LogTemp, Warning, TEXT("CarlaInteractiveMirror: Invalid mode %d (use 0=Pan, 1=ZoomOut, 2=ZoomOutProper, 3=ZoomOutBorder)"), Mode);
    return;
  }
  
  // Invalidate widget to trigger repaint with new mode
  if (MirrorImageWidget.IsValid())
  {
    MirrorImageWidget->Invalidate(EInvalidateWidgetReason::Paint);
  }
}

/**
 * SetTransformDelegate Implementation
 */
void ACarlaInteractiveMirror::SetTransformDelegate(FMirrorTransformDelegate InDelegate)
{
  TransformDelegate = InDelegate;
  UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Custom transform delegate set"));
}

/**
 * GetCurrentNodeName Implementation
 */
FString ACarlaInteractiveMirror::GetCurrentNodeName() const
{
  // Check for nDisplay node name in command line
  FString NodeNameValue;
  if (FParse::Value(FCommandLine::Get(), TEXT("dc_node="), NodeNameValue))
  {
    return NodeNameValue;
  }
  
  // Not running in nDisplay mode
  return TEXT("");
}

/**
 * ShouldActivateOnCurrentNode Implementation
 */
bool ACarlaInteractiveMirror::ShouldActivateOnCurrentNode() const
{
  // Only activate on node_1 (left view) and node_3 (right view)
  return NodeName.Equals(TEXT("node_1")) || NodeName.Equals(TEXT("node_3"));
}

/**
 * LoadConfigFromJSON Implementation
 */
bool ACarlaInteractiveMirror::LoadConfigFromJSON()
{
  // Try multiple possible paths for the config file
  TArray<FString> PathsToTry = {
    FPaths::ProjectDir() / ConfigFilePath,                    // Development: c:/carla/Unreal/CarlaUE4/Config/...
    FPaths::LaunchDir() / ConfigFilePath,                      // Packaged: Build/.../CarlaUE4/Config/...
    FPaths::ProjectDir() / TEXT("../../../") / ConfigFilePath, // Alternative: root/Config/...
    FPaths::LaunchDir() / TEXT("../../../") / ConfigFilePath   // Alternative packaged path
  };
  
  FString FullPath;
  FString JsonString;
  bool bFileLoaded = false;
  
  // Try each path until we find the file
  for (const FString& PathToTry : PathsToTry)
  {
    FString NormalizedPath = FPaths::ConvertRelativePathToFull(PathToTry);
    if (FFileHelper::LoadFileToString(JsonString, *NormalizedPath))
    {
      FullPath = NormalizedPath;
      bFileLoaded = true;
      break;
    }
  }
  
  if (!bFileLoaded)
  {
    UE_LOG(LogTemp, Warning, TEXT("CarlaInteractiveMirror: Could not load config file (tried %d paths), using defaults"), PathsToTry.Num());
    
    // Set default configuration based on node
    if (NodeName.Equals(TEXT("node_1")))
    {
      CurrentConfig.AnchorSide = TEXT("right");  // Left screen: mirror on right edge
      CurrentConfig.OverlayOffsetX = 20.0f;
      CurrentConfig.RelativeLocation = FVector(160.0f, -80.0f, 170.0f);
      CurrentConfig.RelativeRotation = FRotator(0.0f, -90.0f, 0.0f);  // Look left
    }
    else if (NodeName.Equals(TEXT("node_3")))
    {
      CurrentConfig.AnchorSide = TEXT("left");  // Right screen: mirror on left edge
      CurrentConfig.OverlayOffsetX = 20.0f;
      CurrentConfig.RelativeLocation = FVector(160.0f, 80.0f, 170.0f);
      CurrentConfig.RelativeRotation = FRotator(0.0f, 90.0f, 0.0f);  // Look right
    }
    
    return false;
  }
  
  // Parse JSON
  TSharedPtr<FJsonObject> JsonObject;
  TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
  
  if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaInteractiveMirror: Failed to parse JSON config file"));
    return false;
  }
  
  // Get configuration for current node
  TSharedPtr<FJsonObject> NodeConfig = JsonObject->GetObjectField(NodeName);
  if (!NodeConfig.IsValid())
  {
    UE_LOG(LogTemp, Warning, TEXT("CarlaInteractiveMirror: No configuration found for node '%s'"), *NodeName);
    return false;
  }
  
  // Load scene capture settings
  if (NodeConfig->HasTypedField<EJson::Object>(TEXT("scene_capture")))
  {
    TSharedPtr<FJsonObject> SceneCaptureObj = NodeConfig->GetObjectField(TEXT("scene_capture"));
    
    if (SceneCaptureObj->HasTypedField<EJson::Array>(TEXT("relative_location")))
    {
      TArray<TSharedPtr<FJsonValue>> LocArray = SceneCaptureObj->GetArrayField(TEXT("relative_location"));
      if (LocArray.Num() == 3)
      {
        CurrentConfig.RelativeLocation = FVector(
          LocArray[0]->AsNumber(),
          LocArray[1]->AsNumber(),
          LocArray[2]->AsNumber()
        );
      }
    }
    
    if (SceneCaptureObj->HasTypedField<EJson::Array>(TEXT("relative_rotation")))
    {
      TArray<TSharedPtr<FJsonValue>> RotArray = SceneCaptureObj->GetArrayField(TEXT("relative_rotation"));
      if (RotArray.Num() == 3)
      {
        CurrentConfig.RelativeRotation = FRotator(
          RotArray[0]->AsNumber(),
          RotArray[1]->AsNumber(),
          RotArray[2]->AsNumber()
        );
      }
    }
    
    if (SceneCaptureObj->HasField(TEXT("fov")))
    {
      CurrentConfig.FOV = SceneCaptureObj->GetNumberField(TEXT("fov"));
    }
  }
  
  // Load overlay settings
  if (NodeConfig->HasTypedField<EJson::Object>(TEXT("overlay")))
  {
    TSharedPtr<FJsonObject> OverlayObj = NodeConfig->GetObjectField(TEXT("overlay"));
    
    if (OverlayObj->HasField(TEXT("width")))
    {
      CurrentConfig.OverlayWidth = OverlayObj->GetNumberField(TEXT("width"));
    }
    
    if (OverlayObj->HasField(TEXT("height")))
    {
      CurrentConfig.OverlayHeight = OverlayObj->GetNumberField(TEXT("height"));
    }
    
    if (OverlayObj->HasField(TEXT("offset_x")))
    {
      CurrentConfig.OverlayOffsetX = OverlayObj->GetNumberField(TEXT("offset_x"));
    }
    
    if (OverlayObj->HasField(TEXT("offset_y")))
    {
      CurrentConfig.OverlayOffsetY = OverlayObj->GetNumberField(TEXT("offset_y"));
    }
    
    if (OverlayObj->HasField(TEXT("anchor_side")))
    {
      CurrentConfig.AnchorSide = OverlayObj->GetStringField(TEXT("anchor_side"));
    }

    if (OverlayObj->HasField(TEXT("enable_border")))
    {
      CurrentConfig.bEnableBorder = OverlayObj->GetBoolField(TEXT("enable_border"));
    }

    if (OverlayObj->HasField(TEXT("border_width")))
    {
      CurrentConfig.BorderWidth = OverlayObj->GetNumberField(TEXT("border_width"));
    }

    if (OverlayObj->HasField(TEXT("mode")))
    {
      FString ModeString = OverlayObj->GetStringField(TEXT("mode"));
      if (ModeString.Equals(TEXT("zoomout"), ESearchCase::IgnoreCase))
      {
        CurrentConfig.Mode = EMirrorMode::ZoomOut;
      }
      else if (ModeString.Equals(TEXT("zoomout_proper"), ESearchCase::IgnoreCase))
      {
        CurrentConfig.Mode = EMirrorMode::ZoomOutProper;
      }
      else if (ModeString.Equals(TEXT("zoomout_border"), ESearchCase::IgnoreCase))
      {
        CurrentConfig.Mode = EMirrorMode::ZoomOutBorder;
      }
      else
      {
        CurrentConfig.Mode = EMirrorMode::Pan;
      }
    }

    if (OverlayObj->HasField(TEXT("base_zoom_level")))
    {
      CurrentConfig.BaseZoomLevel = FMath::Clamp((float)OverlayObj->GetNumberField(TEXT("base_zoom_level")), 0.1f, 0.95f);
    }
  }
  
  FString ModeName = TEXT("Pan");
  if (CurrentConfig.Mode == EMirrorMode::ZoomOut)
  {
    ModeName = TEXT("ZoomOut");
  }
  else if (CurrentConfig.Mode == EMirrorMode::ZoomOutProper)
  {
    ModeName = TEXT("ZoomOutProper");
  }
  else if (CurrentConfig.Mode == EMirrorMode::ZoomOutBorder)
  {
    ModeName = TEXT("ZoomOutBorder");
  }
  
  UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Loaded configuration for node '%s' - Border: %s (%.0fpx), Mode: %s"), 
    *NodeName, 
    CurrentConfig.bEnableBorder ? TEXT("enabled") : TEXT("disabled"), 
    CurrentConfig.BorderWidth,
    *ModeName);
  return true;
}

/**
 * ApplyNodeConfig Implementation
 */
void ACarlaInteractiveMirror::ApplyNodeConfig(const FMirrorConfig& Config)
{
  // Apply scene capture transform and settings
  if (MirrorSceneCapture)
  {
    MirrorSceneCapture->SetRelativeLocation(Config.RelativeLocation);
    MirrorSceneCapture->SetRelativeRotation(Config.RelativeRotation);
    MirrorSceneCapture->FOVAngle = Config.FOV;
    
    UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Applied scene capture - Loc(%.1f,%.1f,%.1f) Rot(%.1f,%.1f,%.1f) FOV(%.1f)"),
      Config.RelativeLocation.X, Config.RelativeLocation.Y, Config.RelativeLocation.Z,
      Config.RelativeRotation.Pitch, Config.RelativeRotation.Yaw, Config.RelativeRotation.Roll,
      Config.FOV);
  }
  
  UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Applied overlay - Size(%.0fx%.0f) Offset(%.0f,%.0f) Anchor(%s)"),
    Config.OverlayWidth, Config.OverlayHeight,
    Config.OverlayOffsetX, Config.OverlayOffsetY,
    *Config.AnchorSide);
}

/**
 * UpdateHeroVehicleTracking Implementation
 */
void ACarlaInteractiveMirror::UpdateHeroVehicleTracking(float DeltaTime)
{
  UWorld* World = GetWorld();
  if (!World) return;

  // Try to find hero vehicle if we don't have it cached
  if (HeroVehicle == nullptr)
  {
    // Get the CARLA episode to access the actor registry
    UCarlaEpisode* Episode = UCarlaStatics::GetCurrentEpisode(World);
    if (!Episode)
    {
      return; // Episode not ready yet
    }
    
    // Search for vehicle with role_name="hero" in the actor registry
    bool bFoundHero = false;
    const FActorRegistry& Registry = Episode->GetActorRegistry();
    
    for (auto It = Registry.begin(); It != Registry.end(); ++It)
    {
      FCarlaActor* CarlaActor = It.Value().Get();
      if (CarlaActor && CarlaActor->GetActorType() == FCarlaActor::ActorType::Vehicle)
      {
        // Get the actor description to check role_name
        const FActorInfo* ActorInfo = CarlaActor->GetActorInfo();
        if (ActorInfo)
        {
          const FActorAttribute* RoleNameAttr = ActorInfo->Description.Variations.Find(TEXT("role_name"));
          FString RoleName = RoleNameAttr ? RoleNameAttr->Value : TEXT("");
          
          if (RoleName.Equals(TEXT("hero"), ESearchCase::IgnoreCase))
          {
            HeroVehicle = CarlaActor->GetActor();
            if (HeroVehicle)
            {
              UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Found hero vehicle: %s (role_name='%s')"), 
                *HeroVehicle->GetName(), *RoleName);
              
              // Attach scene capture to hero vehicle's root component
              if (MirrorSceneCapture && HeroVehicle->GetRootComponent())
              {
                UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Before attach - World Location: %s"), 
                  *MirrorSceneCapture->GetComponentLocation().ToString());
                
                // Detach from actor first (it's attached to CarlaInteractiveMirror root by default)
                MirrorSceneCapture->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
                
                // Attach to hero vehicle root component with KeepRelativeTransform
                MirrorSceneCapture->AttachToComponent(
                  HeroVehicle->GetRootComponent(), 
                  FAttachmentTransformRules::KeepRelativeTransform
                );
                
                // NOW set the configured transform (relative to hero vehicle)
                MirrorSceneCapture->SetRelativeLocation(CurrentConfig.RelativeLocation);
                MirrorSceneCapture->SetRelativeRotation(CurrentConfig.RelativeRotation);
                
                // Reset pan to 1.0 when attaching to hero vehicle
                // This sets the default position (zoomed in at far edge)
                HorizontalPan = 1.0f;
                if (MirrorImageWidget.IsValid())
                {
                  MirrorImageWidget->SetHorizontalPan(HorizontalPan);
                  MirrorImageWidget->SetMode(MirrorMode);  // Ensure mode is also updated
                  
                  FString ModeName = TEXT("Pan");
                  if (MirrorMode == EMirrorMode::ZoomOut) ModeName = TEXT("ZoomOut");
                  else if (MirrorMode == EMirrorMode::ZoomOutProper) ModeName = TEXT("ZoomOutProper");
                  else if (MirrorMode == EMirrorMode::ZoomOutBorder) ModeName = TEXT("ZoomOutBorder");
                  
                  UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Reset HorizontalPan to 1.0 on hero attach (Mode: %s)"), *ModeName);
                }
                
                UE_LOG(LogTemp, Warning, TEXT("CarlaInteractiveMirror: Attached to hero - Relative Loc: (%.1f, %.1f, %.1f) World Loc: %s"),
                  CurrentConfig.RelativeLocation.X, CurrentConfig.RelativeLocation.Y, CurrentConfig.RelativeLocation.Z,
                  *MirrorSceneCapture->GetComponentLocation().ToString());
              }
              
              bFoundHero = true;
              break;
            }
          }
        }
      }
    }
    
    // Log if no hero found (only once per 5 seconds to avoid spam)
    if (!bFoundHero)
    {
      HeroSearchLogTimer += DeltaTime;
      if (HeroSearchLogTimer >= 5.0f)
      {
        UE_LOG(LogTemp, Verbose, TEXT("CarlaInteractiveMirror: No hero vehicle found in actor registry. Make sure vehicle has role_name='hero' attribute"));
        HeroSearchLogTimer = 0.0f;
      }
    }
    else
    {
      HeroSearchLogTimer = 0.0f;
    }
  }
  else
  {
    // Check if the vehicle is still valid (not destroyed)
    if (!IsValid(HeroVehicle))
    {
      // Vehicle was destroyed, detach and clear the reference
      if (MirrorSceneCapture)
      {
        MirrorSceneCapture->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Detached scene capture from destroyed hero vehicle"));
      }
      HeroVehicle = nullptr;
      UE_LOG(LogTemp, Warning, TEXT("CarlaInteractiveMirror: Hero vehicle was destroyed, searching for new one"));
    }
  }
}

