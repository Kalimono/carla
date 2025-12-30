// Copyright (c) 2025 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "CarlaSpectatorPawn.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Slate/SlateTextures.h"
#include "Engine/Texture.h"

/**
 * Constructor Implementation
 */
ACarlaSpectatorPawn::ACarlaSpectatorPawn(const FObjectInitializer& ObjectInitializer)
  : Super(ObjectInitializer)
{
  PrimaryActorTick.bCanEverTick = true;
  bInitialized = false;
  
  TripleScreenWidgetInstance = nullptr;

  // Create the forward-facing camera component (CENTER SCREEN)
  ForwardCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ForwardCamera"));
  ForwardCamera->SetupAttachment(RootComponent);
  ForwardCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  ForwardCamera->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));

  // Create the left-facing scene capture component (LEFT SCREEN - 90° left)
  LeftSceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("LeftSceneCapture"));
  LeftSceneCapture->SetupAttachment(RootComponent);
  LeftSceneCapture->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  LeftSceneCapture->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
  LeftSceneCapture->CaptureSource = SCS_FinalColorLDR;
  LeftSceneCapture->bCaptureEveryFrame = true;
  LeftSceneCapture->bCaptureOnMovement = true;

  // Create the right-facing scene capture component (RIGHT SCREEN - 90° right)
  RightSceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("RightSceneCapture"));
  RightSceneCapture->SetupAttachment(RootComponent);
  RightSceneCapture->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  RightSceneCapture->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
  RightSceneCapture->CaptureSource = SCS_FinalColorLDR;
  RightSceneCapture->bCaptureEveryFrame = true;
  RightSceneCapture->bCaptureOnMovement = true;

  // Create the left rear-view scene capture component (REAR VIEW for left mirror - 180° rear)
  LeftRearSceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("LeftRearSceneCapture"));
  LeftRearSceneCapture->SetupAttachment(RootComponent);
  LeftRearSceneCapture->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  LeftRearSceneCapture->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
  LeftRearSceneCapture->CaptureSource = SCS_FinalColorLDR;
  LeftRearSceneCapture->bCaptureEveryFrame = true;
  LeftRearSceneCapture->bCaptureOnMovement = true;

  // Create the right rear-view scene capture component (REAR VIEW for right mirror - 180° rear)
  RightRearSceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("RightRearSceneCapture"));
  RightRearSceneCapture->SetupAttachment(RootComponent);
  RightRearSceneCapture->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  RightRearSceneCapture->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
  RightRearSceneCapture->CaptureSource = SCS_FinalColorLDR;
  RightRearSceneCapture->bCaptureEveryFrame = true;
  RightRearSceneCapture->bCaptureOnMovement = true;

  LeftRenderTarget = nullptr;
  RightRenderTarget = nullptr;
  LeftRearRenderTarget = nullptr;
  RightRearRenderTarget = nullptr;
}

/**
 * BeginPlay Implementation
 */
void ACarlaSpectatorPawn::BeginPlay()
{
  Super::BeginPlay();
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: BeginPlay called, will initialize on first tick"));
}

/**
 * EndPlay Implementation
 */
void ACarlaSpectatorPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  Super::EndPlay(EndPlayReason);
  
  // Reset initialization state for next PIE session
  bInitialized = false;
  LeftRenderTarget = nullptr;
  RightRenderTarget = nullptr;
  LeftRearRenderTarget = nullptr;
  RightRearRenderTarget = nullptr;
  
  // Clear brush references
  LeftBrush.Reset();
  RightBrush.Reset();
  LeftRearBrush.Reset();
  RightRearBrush.Reset();
  
  // Note: Slate widgets are automatically cleaned up by the viewport
  TripleScreenWidgetInstance = nullptr;
  
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: EndPlay - reset state for next session"));
}

/**
 * Tick Implementation
 */
void ACarlaSpectatorPawn::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);
  
  if (!bInitialized && LeftRenderTarget == nullptr)
  {
    UWorld* World = GetWorld();
    if (!World) return;

    APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
    if (!PC) return;

    int32 ViewportWidth, ViewportHeight;
    PC->GetViewportSize(ViewportWidth, ViewportHeight);

    if (ViewportWidth <= 0 || ViewportHeight <= 0)
    {
      UE_LOG(LogTemp, Warning, TEXT("CarlaSpectatorPawn: Viewport not ready yet (%dx%d)"), 
        ViewportWidth, ViewportHeight);
      return;
    }

    const int32 SingleScreenWidth = ViewportWidth / 3;
    const int32 SingleScreenHeight = ViewportHeight;

    if (SingleScreenWidth <= 0 || SingleScreenHeight <= 0)
    {
      UE_LOG(LogTemp, Error, TEXT("CarlaSpectatorPawn: Invalid dimensions: %dx%d"), 
        SingleScreenWidth, SingleScreenHeight);
      return;
    }

    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Viewport %dx%d, each camera %dx%d"), 
      ViewportWidth, ViewportHeight, SingleScreenWidth, SingleScreenHeight);

    // Use fixed resolution for render targets (1920x1080 per screen)
    // This prevents aspect ratio distortion when viewport is resized
    const int32 TargetWidth = 1920;
    const int32 TargetHeight = 1080;

    // Create LEFT render target
    LeftRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("LeftRenderTarget"));
    if (LeftRenderTarget)
    {
      LeftRenderTarget->InitAutoFormat(TargetWidth, TargetHeight);
      LeftRenderTarget->RenderTargetFormat = RTF_RGBA8;
      LeftRenderTarget->UpdateResource();
      LeftSceneCapture->TextureTarget = LeftRenderTarget;
      UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created LEFT render target %dx%d"), 
        TargetWidth, TargetHeight);
    }

    // Create RIGHT render target
    RightRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("RightRenderTarget"));
    if (RightRenderTarget)
    {
      RightRenderTarget->InitAutoFormat(TargetWidth, TargetHeight);
      RightRenderTarget->RenderTargetFormat = RTF_RGBA8;
      RightRenderTarget->UpdateResource();
      RightSceneCapture->TextureTarget = RightRenderTarget;
      UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created RIGHT render target %dx%d"), 
        TargetWidth, TargetHeight);
    }

    // Create rear-view render targets (wider to allow horizontal cropping)
    // Width is 3x the mirror width (200) to provide cropping range
    const int32 RearTargetWidth = 1920;  // Wide rear view for cropping
    const int32 RearTargetHeight = 1080; // Full height

    // Create LEFT REAR render target
    LeftRearRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("LeftRearRenderTarget"));
    if (LeftRearRenderTarget)
    {
      LeftRearRenderTarget->InitAutoFormat(RearTargetWidth, RearTargetHeight);
      LeftRearRenderTarget->RenderTargetFormat = RTF_RGBA8;
      LeftRearRenderTarget->UpdateResource();
      LeftRearSceneCapture->TextureTarget = LeftRearRenderTarget;
      UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created LEFT REAR render target %dx%d"), 
        RearTargetWidth, RearTargetHeight);
    }

    // Create RIGHT REAR render target
    RightRearRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("RightRearRenderTarget"));
    if (RightRearRenderTarget)
    {
      RightRearRenderTarget->InitAutoFormat(RearTargetWidth, RearTargetHeight);
      RightRearRenderTarget->RenderTargetFormat = RTF_RGBA8;
      RightRearRenderTarget->UpdateResource();
      RightRearSceneCapture->TextureTarget = RightRearRenderTarget;
      UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created RIGHT REAR render target %dx%d"), 
        RearTargetWidth, RearTargetHeight);
    }

    if (LeftRenderTarget && RightRenderTarget && LeftRearRenderTarget && RightRearRenderTarget)
    {
      CreateTripleScreenWidget();
      bInitialized = true;
      UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Initialization complete!"));
    }
  }
}

/**
 * CreateTripleScreenWidget Implementation
 */
void ACarlaSpectatorPawn::CreateTripleScreenWidget()
{
  UWorld* World = GetWorld();
  if (!World) return;

  APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
  if (!PC) return;

  UGameViewportClient* ViewportClient = World->GetGameViewport();
  if (!ViewportClient) return;

  int32 ViewportWidth, ViewportHeight;
  PC->GetViewportSize(ViewportWidth, ViewportHeight);

  // Create LEFT brush (stored as member variable)
  LeftBrush = MakeShared<FSlateBrush>();
  if (LeftRenderTarget)
  {
    LeftBrush->SetResourceObject(LeftRenderTarget);
    LeftBrush->ImageSize = FVector2D(LeftRenderTarget->SizeX, LeftRenderTarget->SizeY);
    LeftBrush->DrawAs = ESlateBrushDrawType::Image;
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created LEFT brush"));
  }

  // Create RIGHT brush (stored as member variable)  
  RightBrush = MakeShared<FSlateBrush>();
  if (RightRenderTarget)
  {
    RightBrush->SetResourceObject(RightRenderTarget);
    RightBrush->ImageSize = FVector2D(RightRenderTarget->SizeX, RightRenderTarget->SizeY);
    RightBrush->DrawAs = ESlateBrushDrawType::Image;
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created RIGHT brush"));
  }

  // Create LEFT REAR brush with UV coordinates for cropping
  LeftRearBrush = MakeShared<FSlateBrush>();
  if (LeftRearRenderTarget)
  {
    LeftRearBrush->SetResourceObject(LeftRearRenderTarget);
    LeftRearBrush->ImageSize = FVector2D(LeftRearRenderTarget->SizeX, LeftRearRenderTarget->SizeY);
    LeftRearBrush->DrawAs = ESlateBrushDrawType::Image;
    // Set UV coordinates to crop based on offset (mirror width 200 / render target width)
    float MirrorWidthRatio = 200.0f / LeftRearRenderTarget->SizeX;
    LeftRearBrush->SetUVRegion(FBox2D(
      FVector2D(LeftMirrorCropOffset - MirrorWidthRatio * 0.5f, 0.0f),
      FVector2D(LeftMirrorCropOffset + MirrorWidthRatio * 0.5f, 1.0f)
    ));
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created LEFT REAR brush with crop offset %.2f"), LeftMirrorCropOffset);
  }

  // Create RIGHT REAR brush with UV coordinates for cropping
  RightRearBrush = MakeShared<FSlateBrush>();
  if (RightRearRenderTarget)
  {
    RightRearBrush->SetResourceObject(RightRearRenderTarget);
    RightRearBrush->ImageSize = FVector2D(RightRearRenderTarget->SizeX, RightRearRenderTarget->SizeY);
    RightRearBrush->DrawAs = ESlateBrushDrawType::Image;
    // Set UV coordinates to crop based on offset (mirror width 200 / render target width)
    float MirrorWidthRatio = 200.0f / RightRearRenderTarget->SizeX;
    RightRearBrush->SetUVRegion(FBox2D(
      FVector2D(RightMirrorCropOffset - MirrorWidthRatio * 0.5f, 0.0f),
      FVector2D(RightMirrorCropOffset + MirrorWidthRatio * 0.5f, 1.0f)
    ));
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created RIGHT REAR brush with crop offset %.2f"), RightMirrorCropOffset);
  }

  // Create Slate constraint canvas with anchor-based positioning (scales dynamically)
  TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas)
    
    // LEFT image with rear-view mirror overlay (0-33% horizontal using anchors)
    + SConstraintCanvas::Slot()
    .Anchors(FAnchors(0.0f, 0.0f, 0.33f, 1.0f))  // Left third, full height
    .Offset(FMargin(0.0f, 0.0f, 0.0f, 0.0f))     // No offset, fill anchor area
    .Alignment(FVector2D(0.0f, 0.0f))
    .AutoSize(false)
    [
      // Overlay to stack mirror on top of main view
      SNew(SOverlay)
      
      // Main left view
      + SOverlay::Slot()
      [
        SNew(SBox)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .Clipping(EWidgetClipping::ClipToBounds)
        [
          SNew(SImage)
          .Image(LeftBrush.Get())
        ]
      ]
      
      // Left rear-view mirror (200x600 at top-right of left screen)
      + SOverlay::Slot()
      .HAlign(HAlign_Right)
      .VAlign(VAlign_Top)
      .Padding(FMargin(0.0f, 20.0f, 20.0f, 0.0f))
      [
        SNew(SBox)
        .WidthOverride(200.0f)
        .HeightOverride(600.0f)
        [
          SNew(SImage)
          .Image(LeftRearBrush.Get())
        ]
      ]
    ]
    
    // RIGHT image with rear-view mirror overlay (66-100% horizontal using anchors)
    + SConstraintCanvas::Slot()
    .Anchors(FAnchors(0.66f, 0.0f, 1.0f, 1.0f))  // Right third, full height
    .Offset(FMargin(0.0f, 0.0f, 0.0f, 0.0f))     // No offset, fill anchor area
    .Alignment(FVector2D(0.0f, 0.0f))
    .AutoSize(false)
    [
      // Overlay to stack mirror on top of main view
      SNew(SOverlay)
      
      // Main right view
      + SOverlay::Slot()
      [
        SNew(SBox)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .Clipping(EWidgetClipping::ClipToBounds)
        [
          SNew(SImage)
          .Image(RightBrush.Get())
        ]
      ]
      
      // Right rear-view mirror (200x600 at top-left of right screen)
      + SOverlay::Slot()
      .HAlign(HAlign_Left)
      .VAlign(VAlign_Top)
      .Padding(FMargin(20.0f, 20.0f, 0.0f, 0.0f))
      [
        SNew(SBox)
        .WidthOverride(200.0f)
        .HeightOverride(600.0f)
        [
          SNew(SImage)
          .Image(RightRearBrush.Get())
        ]
      ]
    ];

  ViewportClient->AddViewportWidgetContent(Canvas, 0);
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Constraint canvas with anchors added to viewport"));
}
