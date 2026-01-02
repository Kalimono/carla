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
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "HAL/RunnableThread.h"

// =====================================================
// FUdpMirrorReceiver - Async UDP Receiver Implementation
// =====================================================

FUdpMirrorReceiver::FUdpMirrorReceiver(int32 Port)
  : Socket(nullptr)
  , Thread(nullptr)
  , bShouldRun(false)
  , ListenPort(Port)
  , LeftOffset(0.5f)
  , RightOffset(0.5f)
{
}

FUdpMirrorReceiver::~FUdpMirrorReceiver()
{
  Stop();
  if (Thread)
  {
    Thread->WaitForCompletion();
    delete Thread;
    Thread = nullptr;
  }
}

bool FUdpMirrorReceiver::Init()
{
  ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
  if (!SocketSubsystem)
  {
    UE_LOG(LogTemp, Error, TEXT("FUdpMirrorReceiver: Failed to get socket subsystem"));
    return false;
  }
  
  // Create UDP socket
  Socket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("UdpMirrorReceiver"), false);
  if (!Socket)
  {
    UE_LOG(LogTemp, Error, TEXT("FUdpMirrorReceiver: Failed to create UDP socket"));
    return false;
  }
  
  // Set socket to non-blocking
  Socket->SetNonBlocking(false);  // Blocking for thread
  Socket->SetRecvErr(false);
  
  // Bind to port
  TSharedRef<FInternetAddr> LocalAddr = SocketSubsystem->CreateInternetAddr();
  LocalAddr->SetAnyAddress();
  LocalAddr->SetPort(ListenPort);
  
  if (!Socket->Bind(*LocalAddr))
  {
    UE_LOG(LogTemp, Error, TEXT("FUdpMirrorReceiver: Failed to bind UDP socket to port %d"), ListenPort);
    SocketSubsystem->DestroySocket(Socket);
    Socket = nullptr;
    return false;
  }
  
  // Set receive buffer size
  int32 NewSize = 0;
  Socket->SetReceiveBufferSize(2048, NewSize);
  
  bShouldRun = true;
  UE_LOG(LogTemp, Log, TEXT("FUdpMirrorReceiver: UDP socket bound to port %d"), ListenPort);
  return true;
}

uint32 FUdpMirrorReceiver::Run()
{
  TArray<uint8> ReceivedData;
  ReceivedData.SetNumUninitialized(1024);
  
  while (bShouldRun)
  {
    int32 BytesRead = 0;
    
    // Blocking receive with timeout
    if (Socket && Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(100)))
    {
      if (Socket->Recv(ReceivedData.GetData(), ReceivedData.Num(), BytesRead))
      {
        if (BytesRead > 0)
        {
          // Convert to string and parse
          FString ReceivedString = FString(BytesRead, (const char*)ReceivedData.GetData());
          
          // Expected format: "left:0.5,right:0.3"
          TArray<FString> Parts;
          ReceivedString.ParseIntoArray(Parts, TEXT(","));
          
          float NewLeft = LeftOffset;
          float NewRight = RightOffset;
          
          for (const FString& Part : Parts)
          {
            TArray<FString> KeyValue;
            Part.ParseIntoArray(KeyValue, TEXT(":"));
            
            if (KeyValue.Num() == 2)
            {
              FString Key = KeyValue[0].TrimStartAndEnd();
              float Value = FCString::Atof(*KeyValue[1].TrimStartAndEnd());
              Value = FMath::Clamp(Value, 0.0f, 1.0f);
              
              if (Key.Equals(TEXT("left"), ESearchCase::IgnoreCase))
              {
                NewLeft = Value;
              }
              else if (Key.Equals(TEXT("right"), ESearchCase::IgnoreCase))
              {
                NewRight = Value;
              }
            }
          }
          
          // Thread-safe update
          {
            FScopeLock Lock(&DataLock);
            LeftOffset = NewLeft;
            RightOffset = NewRight;
          }
        }
      }
    }
  }
  
  return 0;
}

void FUdpMirrorReceiver::Stop()
{
  bShouldRun = false;
}

void FUdpMirrorReceiver::Exit()
{
  if (Socket)
  {
    Socket->Close();
    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
    Socket = nullptr;
  }
  UE_LOG(LogTemp, Log, TEXT("FUdpMirrorReceiver: Thread exiting"));
}

void FUdpMirrorReceiver::GetMirrorOffsets(float& OutLeft, float& OutRight)
{
  FScopeLock Lock(&DataLock);
  OutLeft = LeftOffset;
  OutRight = RightOffset;
}

// =====================================================
// ACarlaSpectatorPawn Implementation
// =====================================================

/**
 * Constructor Implementation
 */
ACarlaSpectatorPawn::ACarlaSpectatorPawn(const FObjectInitializer& ObjectInitializer)
  : Super(ObjectInitializer)
{
  PrimaryActorTick.bCanEverTick = true;
  bInitialized = false;
  
  TripleScreenWidgetInstance = nullptr;
  UdpReceiverThread = nullptr;
  UdpUpdateTimer = 0.0f;

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

  // Initialize UDP receiver
  UdpUpdateTimer = 0.0f;
}

/**
 * BeginPlay Implementation
 */
void ACarlaSpectatorPawn::BeginPlay()
{
  Super::BeginPlay();
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: BeginPlay called, will initialize on first tick"));
  
  // Start asynchronous UDP receiver
  StartUdpReceiver();
}

/**
 * EndPlay Implementation
 */
void ACarlaSpectatorPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  Super::EndPlay(EndPlayReason);
  
  // Stop UDP receiver
  StopUdpReceiver();
  
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
  
  // Update mirror offsets from UDP thread at 50Hz (every 0.02 seconds)
  UdpUpdateTimer += DeltaTime;
  if (UdpUpdateTimer >= 0.02f)
  {
    UdpUpdateTimer = 0.0f;
    UpdateMirrorOffsetsFromUdp();
  }
  
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
    LeftRearRenderTarget = NewObject<UTextureRenderTarget2D>();
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
    RightRearRenderTarget = NewObject<UTextureRenderTarget2D>();
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

/**
 * StartUdpReceiver Implementation
 */
void ACarlaSpectatorPawn::StartUdpReceiver()
{
  // Create and initialize UDP receiver
  UdpReceiver = MakeUnique<FUdpMirrorReceiver>(UdpPort);
  
  if (UdpReceiver->Init())
  {
    // Create and start the receiver thread
    UdpReceiverThread = FRunnableThread::Create(UdpReceiver.Get(), TEXT("UdpMirrorReceiverThread"), 0, TPri_Normal);
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: UDP receiver thread started on port %d"), UdpPort);
  }
  else
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaSpectatorPawn: Failed to initialize UDP receiver"));
    UdpReceiver.Reset();
  }
}

/**
 * StopUdpReceiver Implementation
 */
void ACarlaSpectatorPawn::StopUdpReceiver()
{
  if (UdpReceiver.IsValid())
  {
    // Signal the thread to stop
    UdpReceiver->Stop();
    
    // Wait for thread to finish if it exists
    if (UdpReceiverThread)
    {
      UdpReceiverThread->WaitForCompletion();
      delete UdpReceiverThread;
      UdpReceiverThread = nullptr;
    }
    
    // Cleanup receiver
    UdpReceiver.Reset();
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: UDP receiver stopped"));
  }
}

/**
 * UpdateMirrorOffsetsFromUdp Implementation
 */
void ACarlaSpectatorPawn::UpdateMirrorOffsetsFromUdp()
{
  if (UdpReceiver.IsValid())
  {
    // Get thread-safe mirror offsets
    float NewLeftOffset, NewRightOffset;
    UdpReceiver->GetMirrorOffsets(NewLeftOffset, NewRightOffset);
    
    // Update member variables
    LeftMirrorCropOffset = NewLeftOffset;
    RightMirrorCropOffset = NewRightOffset;
    
    // Update brush UV regions if they exist
    if (LeftRearBrush.IsValid() && LeftRearRenderTarget)
    {
      float MirrorWidthRatio = 200.0f / LeftRearRenderTarget->SizeX;
      LeftRearBrush->SetUVRegion(FBox2D(
        FVector2D(LeftMirrorCropOffset - MirrorWidthRatio * 0.5f, 0.0f),
        FVector2D(LeftMirrorCropOffset + MirrorWidthRatio * 0.5f, 1.0f)
      ));
    }
    
    if (RightRearBrush.IsValid() && RightRearRenderTarget)
    {
      float MirrorWidthRatio = 200.0f / RightRearRenderTarget->SizeX;
      RightRearBrush->SetUVRegion(FBox2D(
        FVector2D(RightMirrorCropOffset - MirrorWidthRatio * 0.5f, 0.0f),
        FVector2D(RightMirrorCropOffset + MirrorWidthRatio * 0.5f, 1.0f)
      ));
    }
  }
}

