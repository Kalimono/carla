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
#include "Carla/Game/CarlaStatics.h"
#include "Carla/Game/CarlaEpisode.h"
#include <string.h>  // For strtok_r, strchr, strcasecmp

// Global flags to prevent multiple instances across PIE
static bool GUdpReceiverActive = false;
static FCriticalSection GUdpReceiverLock;

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
  // Stop the thread
  Stop();
  
  // Wait for thread to complete
  if (Thread)
  {
    Thread->WaitForCompletion();
    delete Thread;
    Thread = nullptr;
  }
  
  // Clean up socket if still exists
  FScopeLock Lock(&SocketLock);
  if (Socket)
  {
    Socket->Close();
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (SocketSubsystem)
    {
      SocketSubsystem->DestroySocket(Socket);
    }
    Socket = nullptr;
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
  
  // Set socket options
  Socket->SetReuseAddr(true);  // Allow reusing the address
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
  // Use static array to avoid dynamic allocation issues in thread
  uint8 ReceivedData[1024];
  
  while (bShouldRun)
  {
    // Check if we should stop before each operation
    if (!bShouldRun)
    {
      break;
    }
    
    int32 BytesRead = 0;
    bool bHasData = false;
    
    // Check for data with a short timeout to allow quick exit on Stop()
    {
      if (!bShouldRun) break;  // Double-check before locking
      FScopeLock Lock(&SocketLock);
      if (Socket)
      {
        // Use very short timeout (10ms) so we can check bShouldRun frequently
        bHasData = Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(10));
      }
      else
      {
        break;
      }
    }
    
    // If we have data, receive it
    if (bHasData && bShouldRun)
    {
      if (!bShouldRun) break;  // Double-check before locking
      FScopeLock Lock(&SocketLock);
      if (Socket && bShouldRun)
      {
        bool bRecvSuccess = Socket->Recv(ReceivedData, 1024, BytesRead);
        
        if (bRecvSuccess && BytesRead > 0)
        {
          // Manual C-string parsing (FString is not thread-safe)
          char TempBuffer[1025];
          FMemory::Memcpy(TempBuffer, ReceivedData, BytesRead);
          TempBuffer[BytesRead] = '\0';
          
          float NewLeft = LeftOffset;
          float NewRight = RightOffset;
          
          // Parse format: "left:0.5,right:0.7"
          char* Context = nullptr;
          char* Token = strtok_r(TempBuffer, ",", &Context);
          
          while (Token != nullptr)
          {
            char* Colon = strchr(Token, ':');
            if (Colon != nullptr)
            {
              *Colon = '\0';  // Split at colon
              char* Key = Token;
              char* ValueStr = Colon + 1;
              
              // Trim whitespace
              while (*Key == ' ') Key++;
              while (*ValueStr == ' ') ValueStr++;
              
              float Value = atof(ValueStr);
              Value = FMath::Clamp(Value, 0.0f, 1.0f);
              
              // Case-insensitive comparison
              if (strcasecmp(Key, "left") == 0)
              {
                NewLeft = Value;
              }
              else if (strcasecmp(Key, "right") == 0)
              {
                NewRight = Value;
              }
            }
            
            Token = strtok_r(nullptr, ",", &Context);
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
  
  // Close the socket immediately to unblock Socket->Wait() in the thread
  {
    FScopeLock Lock(&SocketLock);
    if (Socket)
    {
      Socket->Close();
    }
  }
  
  // Thread checks bShouldRun every 10ms, so 50ms should be plenty
  FPlatformProcess::Sleep(0.05f);
}

void FUdpMirrorReceiver::Exit()
{
  // Ensure thread has stopped
  bShouldRun = false;
  
  FScopeLock Lock(&SocketLock);
  if (Socket)
  {
    // Socket may already be closed by Stop(), just ensure it's destroyed
    // Don't call GetConnectionState() as socket might be in invalid state
    Socket->Close();
    
    // Check if socket subsystem is still available before destroying
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (SocketSubsystem)
    {
      SocketSubsystem->DestroySocket(Socket);
    }
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
  MirrorUpdateTimer = 0.0f;

  // Create the forward-facing camera component (CENTER SCREEN)
  ForwardCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ForwardCamera"));
  ForwardCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  ForwardCamera->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));

  // Create the left-facing scene capture component (LEFT SCREEN - 90° left)
  LeftSceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("LeftSceneCapture"));
  LeftSceneCapture->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  LeftSceneCapture->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
  LeftSceneCapture->CaptureSource = SCS_FinalColorLDR;
  LeftSceneCapture->bCaptureEveryFrame = false;
  LeftSceneCapture->bCaptureOnMovement = false;
  LeftSceneCapture->ShowFlags.SetMotionBlur(false);
  LeftSceneCapture->ShowFlags.SetLensFlares(false);
  LeftSceneCapture->ShowFlags.SetBloom(false);

  // Create the right-facing scene capture component (RIGHT SCREEN - 90° right)
  RightSceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("RightSceneCapture"));
  RightSceneCapture->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  RightSceneCapture->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
  RightSceneCapture->CaptureSource = SCS_FinalColorLDR;
  RightSceneCapture->bCaptureEveryFrame = false;
  RightSceneCapture->bCaptureOnMovement = false;
  RightSceneCapture->ShowFlags.SetMotionBlur(false);
  RightSceneCapture->ShowFlags.SetLensFlares(false);
  RightSceneCapture->ShowFlags.SetBloom(false);

  // Create the left rear-view scene capture component (REAR VIEW for left mirror - 180° rear)
  LeftRearSceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("LeftRearSceneCapture"));
  LeftRearSceneCapture->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  LeftRearSceneCapture->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
  LeftRearSceneCapture->CaptureSource = SCS_FinalColorLDR;
  LeftRearSceneCapture->bCaptureEveryFrame = false;
  LeftRearSceneCapture->bCaptureOnMovement = false;
  LeftRearSceneCapture->ShowFlags.SetMotionBlur(false);
  LeftRearSceneCapture->ShowFlags.SetLensFlares(false);
  LeftRearSceneCapture->ShowFlags.SetBloom(false);

  // Create the right rear-view scene capture component (REAR VIEW for right mirror - 180° rear)
  RightRearSceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("RightRearSceneCapture"));
  RightRearSceneCapture->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  RightRearSceneCapture->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
  RightRearSceneCapture->CaptureSource = SCS_FinalColorLDR;
  RightRearSceneCapture->bCaptureEveryFrame = false;
  RightRearSceneCapture->bCaptureOnMovement = false;
  RightRearSceneCapture->ShowFlags.SetMotionBlur(false);
  RightRearSceneCapture->ShowFlags.SetLensFlares(false);
  RightRearSceneCapture->ShowFlags.SetBloom(false);

  LeftRenderTarget = nullptr;
  RightRenderTarget = nullptr;
  LeftRearRenderTarget = nullptr;
  RightRearRenderTarget = nullptr;

  // Initialize UDP receiver
  UdpUpdateTimer = 0.0f;

  // Initialize hero vehicle tracking
  HeroVehicle = nullptr;
  // Camera offset: x=1.6m forward, z=1.7m up (driver's eye position)
  CameraOffset = FVector(160.0f, 0.0f, 170.0f);  // Convert meters to cm
  HeroSearchLogTimer = 0.0f;
}

/**
 * BeginPlay Implementation
 */
void ACarlaSpectatorPawn::BeginPlay()
{
  Super::BeginPlay();
  
  // Check if another instance has already been initialized globally
  // Use the same lock and flag as StartUdpReceiver to ensure atomicity
  {
    FScopeLock Lock(&GUdpReceiverLock);
    if (GUdpReceiverActive)
    {
      UE_LOG(LogTemp, Warning, TEXT("CarlaSpectatorPawn: Another instance already initialized, this instance will be inactive"));
      // DO NOT set bInitialized = true here! Leave it false so Tick won't try to use null pointers
      return;
    }
    // Set flag here to reserve initialization for this instance
    GUdpReceiverActive = true;
  }
  
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: BeginPlay called, will initialize on first tick"));
  
  // Attach components to root
  USceneComponent* Root = GetRootComponent();
  if (Root)
  {
    ForwardCamera->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
    LeftSceneCapture->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
    RightSceneCapture->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
    
    if (bEnableRearviewMirrors)
    {
      LeftRearSceneCapture->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
      RightRearSceneCapture->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
    }
  }
  
  // Start asynchronous UDP receiver for dynamic mirror offset control
  StartUdpReceiver();
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: UDP receiver ENABLED on port %d"), UdpPort);
}

/**
 * EndPlay Implementation
 */
void ACarlaSpectatorPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: EndPlay called"));
  
  // Stop UDP receiver FIRST (will reset GUdpReceiverActive)
  // This must complete before we call Super::EndPlay()
  StopUdpReceiver();
  
  // Reset initialization state for next PIE session
  bInitialized = false;
  HeroVehicle = nullptr;
  
  Super::EndPlay(EndPlayReason);
  
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
  
  // Update spectator position to follow hero vehicle (only after world is ready)
  if (GetWorld() && GetWorld()->HasBegunPlay())
  {
    UpdateHeroVehicleTracking(DeltaTime);
  }
  
  // Only update mirror offsets if initialization is complete
  if (bInitialized)
  {
    // Update mirror offsets from UDP thread at 50Hz (every 0.02 seconds)
    UdpUpdateTimer += DeltaTime;
    if (UdpUpdateTimer >= 0.02f)
    {
      UdpUpdateTimer = 0.0f;
      UpdateMirrorOffsetsFromUdp();
    }

    // Update rear mirrors at 30Hz instead of every frame for better performance
    if (bEnableRearviewMirrors)
    {
      MirrorUpdateTimer += DeltaTime;
      if (MirrorUpdateTimer >= 0.033f)  // ~30 FPS
      {
        MirrorUpdateTimer = 0.0f;
        if (LeftRearSceneCapture) LeftRearSceneCapture->CaptureScene();
        if (RightRearSceneCapture) RightRearSceneCapture->CaptureScene();
      }
    }
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

    // Use fixed resolution for render targets (1280x720 per screen)
    // Lower resolution improves performance on laptops with limited GPU memory
    const int32 TargetWidth = 1280;  // 720p instead of 1080p
    const int32 TargetHeight = 720;

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
    // Smaller resolution for mirrors improves performance
    const int32 RearTargetWidth = 800;   // Smaller for rear mirrors
    const int32 RearTargetHeight = 600;  // 600p for mirrors

    // Create LEFT REAR render target (only if mirrors enabled)
    if (bEnableRearviewMirrors)
    {
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
    }

    // Create RIGHT REAR render target (only if mirrors enabled)
    if (bEnableRearviewMirrors)
    {
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
    }

    bool bMirrorRenderTargetsReady = !bEnableRearviewMirrors || (LeftRearRenderTarget && RightRearRenderTarget);
    if (LeftRenderTarget && RightRenderTarget && bMirrorRenderTargetsReady)
    {
      // Enable scene captures now that render targets are ready
      LeftSceneCapture->bCaptureEveryFrame = true;
      LeftSceneCapture->bCaptureOnMovement = true;
      RightSceneCapture->bCaptureEveryFrame = true;
      RightSceneCapture->bCaptureOnMovement = true;
      
      if (bEnableRearviewMirrors)
      {
        LeftRearSceneCapture->bCaptureEveryFrame = true;
        LeftRearSceneCapture->bCaptureOnMovement = true;
        RightRearSceneCapture->bCaptureEveryFrame = true;
        RightRearSceneCapture->bCaptureOnMovement = true;
      }
      
      CreateTripleScreenWidget();
      bInitialized = true;
      UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Initialization complete with scene captures enabled"));
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

  // Create LEFT REAR brush with UV coordinates for cropping (only if mirrors enabled)
  if (bEnableRearviewMirrors)
  {
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
  }

  // Create RIGHT REAR brush with UV coordinates for cropping (only if mirrors enabled)
  if (bEnableRearviewMirrors)
  {
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
        .HAlign(HAlign_Fill)
        .VAlign(VAlign_Fill)
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
        .Visibility(bEnableRearviewMirrors ? EVisibility::Visible : EVisibility::Hidden)
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
        .HAlign(HAlign_Fill)
        .VAlign(VAlign_Fill)
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
        .Visibility(bEnableRearviewMirrors ? EVisibility::Visible : EVisibility::Hidden)
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
  // Note: GUdpReceiverActive flag is already set in BeginPlay() before calling this
  // Prevent double initialization at the instance level
  if (UdpReceiver.IsValid() || UdpReceiverThread != nullptr)
  {
    UE_LOG(LogTemp, Warning, TEXT("CarlaSpectatorPawn: UDP receiver already started, skipping"));
    return;
  }
  
  // Create UDP receiver
  UdpReceiver = MakeUnique<FUdpMirrorReceiver>(UdpPort);
  
  // Create and start the receiver thread (FRunnableThread::Create will call Init() automatically)
  UdpReceiverThread = FRunnableThread::Create(UdpReceiver.Get(), TEXT("UdpMirrorReceiverThread"), 0, TPri_Normal);
  
  if (UdpReceiverThread)
  {
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: UDP receiver thread started on port %d"), UdpPort);
  }
  else
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaSpectatorPawn: Failed to create UDP receiver thread"));
    UdpReceiver.Reset();
    // Reset global flag on failure
    FScopeLock Lock(&GUdpReceiverLock);
    GUdpReceiverActive = false;
  }
}

/**
 * StopUdpReceiver Implementation
 */
void ACarlaSpectatorPawn::StopUdpReceiver()
{
  // Stop the thread first if it exists (outside lock to avoid blocking)
  if (UdpReceiver.IsValid())
  {
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Stopping UDP receiver..."));
    
    // Signal the thread to stop
    UdpReceiver->Stop();
    
    // Wait for thread to finish before destroying anything
    if (UdpReceiverThread)
    {
      UdpReceiverThread->WaitForCompletion();
      delete UdpReceiverThread;
      UdpReceiverThread = nullptr;
    }
    
    // Additional safety delay to ensure Exit() has completed
    FPlatformProcess::Sleep(0.05f);  // 50ms
    
    // Now safely destroy the receiver object and reset global flag
    {
      FScopeLock Lock(&GUdpReceiverLock);
      UdpReceiver.Reset();
      GUdpReceiverActive = false;
    }
    
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: UDP receiver stopped"));
  }
  else
  {
    // Just reset the flag if receiver was never created
    FScopeLock Lock(&GUdpReceiverLock);
    GUdpReceiverActive = false;
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

/**
 * UpdateHeroVehicleTracking Implementation
 */
void ACarlaSpectatorPawn::UpdateHeroVehicleTracking(float DeltaTime)
{
  UWorld* World = GetWorld();
  if (!World) return;

  // Try to find hero vehicle if we don't have it cached
  if (HeroVehicle == nullptr)// || !IsValid(HeroVehicle))
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
          FActorAttribute RoleNameAttr = ActorInfo->Description.GetAttribute(TEXT("role_name"));
          FString RoleName = RoleNameAttr.Value;
          
          if (RoleName.Equals(TEXT("hero"), ESearchCase::IgnoreCase))
          {
            HeroVehicle = CarlaActor->GetActor();
            if (HeroVehicle)
            {
              UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Found hero vehicle: %s (role_name='%s')"), 
                *HeroVehicle->GetName(), *RoleName);
              
              // Hide the hero vehicle entirely to prevent jitter caused by
              // Python API update lag vs C++ frame rate
              HeroVehicle->SetActorHiddenInGame(true);
              UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Hidden hero vehicle from game"));
              
              bFoundHero = true;
              break;
            }
          }
        }
      }
    }
    
    // Log if no hero found (only once per second to avoid spam)
    if (!bFoundHero)
    {
      HeroSearchLogTimer += DeltaTime;
      if (HeroSearchLogTimer >= 1.0f)
      {
        UE_LOG(LogTemp, Warning, TEXT("CarlaSpectatorPawn: No hero vehicle found in actor registry. Make sure vehicle has role_name='hero' attribute"));
        HeroSearchLogTimer = 0.0f;
      }
    }
    else
    {
      HeroSearchLogTimer = 0.0f;
    }
  }

  // If we have a hero vehicle, follow it
  if (HeroVehicle)
  {
    // Check if the vehicle is still valid (not destroyed)
    if (IsValid(HeroVehicle))
    {
      FTransform VehicleTransform = HeroVehicle->GetActorTransform();
      
      // Apply camera offset in vehicle's local space
      FVector WorldOffset = VehicleTransform.TransformVector(CameraOffset);
      FVector NewLocation = VehicleTransform.GetLocation() + WorldOffset;
      FRotator NewRotation = VehicleTransform.GetRotation().Rotator();
      
      // Update spectator transform
      SetActorLocation(NewLocation);
      SetActorRotation(NewRotation);
    }
    else
    {
      // Vehicle was destroyed, clear the reference
      HeroVehicle = nullptr;
      UE_LOG(LogTemp, Warning, TEXT("CarlaSpectatorPawn: Hero vehicle was destroyed, searching for new one"));
    }
  }
}

