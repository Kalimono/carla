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
#include "Carla/Vehicle/CarlaWheeledVehicle.h"
#include "WheeledVehicleMovementComponent.h"
#include "Sound/SoundCue.h"

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
  
  MirrorUpdateTimer = 0.0f;

  // Create the forward-facing camera component
  ForwardCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ForwardCamera"));
  ForwardCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  ForwardCamera->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
  ForwardCamera->FieldOfView = 90.0f;
  ForwardCamera->bUsePawnControlRotation = false;
  ForwardCamera->bAutoActivate = true;
  ForwardCamera->bConstrainAspectRatio = false;
  ForwardCamera->AspectRatio = 16.0f / 9.0f;

  // Create the engine sound audio component
  EngineCue = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineCue"));
  EngineCue->SetupAttachment(RootComponent);
  EngineCue->bAutoActivate = false;
  EngineCue->bStopWhenOwnerDestroyed = false;
  EngineCue->bIsUISound = true;
  EngineCue->bAllowSpatialization = false;
  EngineCue->bOverrideAttenuation = true;
  EngineCue->bIgnoreForFlushing = true;
  EngineCue->SetVolumeMultiplier(2.0f);
  
  // Load the EngineCue sound asset at /Game/Carla/Sounds/EngineCue
  static ConstructorHelpers::FObjectFinder<USoundCue> EngineSoundCue(
    TEXT("/Game/Carla/Sounds/EngineCue"));
  if (EngineSoundCue.Succeeded())
  {
    EngineCue->SetSound(EngineSoundCue.Object);
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Engine sound cue loaded successfully"));
  }
  else
  {
    UE_LOG(LogTemp, Warning, TEXT("CarlaSpectatorPawn: Failed to load EngineCue sound asset at /Game/Carla/Sounds/EngineCue"));
  }

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

  LeftRearRenderTarget = nullptr;
  RightRearRenderTarget = nullptr;

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
  
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: BeginPlay called, will initialize on first tick"));
  
  // Check if we're running with nDisplay
  // Use the -dc_cluster command line flag which is always present in nDisplay launches
  bool bIsDisplayCluster = FParse::Param(FCommandLine::Get(), TEXT("dc_cluster"));
  if (bIsDisplayCluster)
  {
    UE_LOG(LogTemp, Warning, TEXT("CarlaSpectatorPawn: nDisplay cluster mode detected - disabling ForwardCamera to avoid viewport conflicts"));
  }
  
  // Attach components to root
  USceneComponent* Root = GetRootComponent();
  if (Root)
  {
    ForwardCamera->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
    
    // Deactivate ForwardCamera when using nDisplay (nDisplay manages its own cameras)
    if (bIsDisplayCluster && ForwardCamera)
    {
      ForwardCamera->Deactivate();
      ForwardCamera->SetActive(false);
      ForwardCamera->bAutoActivate = false;
    }
    
    // Always attach rear scene captures so they follow the spectator (visibility controlled separately)
    LeftRearSceneCapture->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
    RightRearSceneCapture->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
  }
}

/**
 * SetupPlayerInputComponent Implementation
 */
void ACarlaSpectatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
  Super::SetupPlayerInputComponent(PlayerInputComponent);
  
  // Safely bind toggle rearview mirrors action
  if (PlayerInputComponent)
  {
    PlayerInputComponent->BindAction("ToggleRearviewMirrors", IE_Pressed, this, &ACarlaSpectatorPawn::ToggleRearviewMirrors);
  }
}

/**
 * ToggleRearviewMirrors Implementation
 */
void ACarlaSpectatorPawn::ToggleRearviewMirrors()
{
  bEnableRearviewMirrors = !bEnableRearviewMirrors;
  
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Rearview mirrors %s"), 
    bEnableRearviewMirrors ? TEXT("ENABLED") : TEXT("DISABLED"));
  
  // Update scene capture visibility, activity, and capture flags
  if (LeftRearSceneCapture)
  {
    LeftRearSceneCapture->SetVisibility(bEnableRearviewMirrors);
    LeftRearSceneCapture->SetActive(bEnableRearviewMirrors);
    LeftRearSceneCapture->bCaptureEveryFrame = bEnableRearviewMirrors;
    LeftRearSceneCapture->bCaptureOnMovement = bEnableRearviewMirrors;
  }
  if (RightRearSceneCapture)
  {
    RightRearSceneCapture->SetVisibility(bEnableRearviewMirrors);
    RightRearSceneCapture->SetActive(bEnableRearviewMirrors);
    RightRearSceneCapture->bCaptureEveryFrame = bEnableRearviewMirrors;
    RightRearSceneCapture->bCaptureOnMovement = bEnableRearviewMirrors;
  }
  
  // Update Slate widget visibility
  EVisibility NewVisibility = bEnableRearviewMirrors ? EVisibility::Visible : EVisibility::Hidden;
  
  if (LeftMirrorWidget.IsValid())
  {
    LeftMirrorWidget->SetVisibility(NewVisibility);
  }
  if (RightMirrorWidget.IsValid())
  {
    RightMirrorWidget->SetVisibility(NewVisibility);
  }
}

/**
 * EndPlay Implementation
 */
void ACarlaSpectatorPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: EndPlay called"));
  
  // Reset initialization state for next PIE session
  bInitialized = false;
  HeroVehicle = nullptr;
  
  Super::EndPlay(EndPlayReason);
  
  LeftRearRenderTarget = nullptr;
  RightRearRenderTarget = nullptr;
  
  // Clear brush references
  LeftRearBrush.Reset();
  RightRearBrush.Reset();
  
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: EndPlay - reset state for next session"));
}

/**
 * Tick Implementation
 */
void ACarlaSpectatorPawn::Tick(float DeltaTime)
{
  UE_LOG(LogTemp, Warning, TEXT("SPECTATOR TICK %f"), GetWorld()->GetTimeSeconds());

  Super::Tick(DeltaTime);

  UE_LOG(LogTemp, Verbose, TEXT("CarlaSpectatorPawn: Tick - updating hero vehicle tracking"));
  
  // Update spectator position to follow hero vehicle (only after world is ready)
  if (GetWorld() && GetWorld()->HasBegunPlay())
  {
    UpdateHeroVehicleTracking(DeltaTime);
  }
  
  // Only update mirror rendering if initialization is complete
  if (bInitialized)
  {
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
  
  if (!bInitialized && LeftRearRenderTarget == nullptr)
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

    // Create rear-view render targets for mirrors
    const int32 RearTargetWidth = 800;
    const int32 RearTargetHeight = 600;

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

    if (LeftRearRenderTarget && RightRearRenderTarget)
    {
      // Enable rear captures (will only capture when bEnableRearviewMirrors is true)
      LeftRearSceneCapture->bCaptureEveryFrame = bEnableRearviewMirrors;
      LeftRearSceneCapture->bCaptureOnMovement = bEnableRearviewMirrors;
      RightRearSceneCapture->bCaptureEveryFrame = bEnableRearviewMirrors;
      RightRearSceneCapture->bCaptureOnMovement = bEnableRearviewMirrors;
      
      CreateRearviewMirrorWidgets();
      bInitialized = true;
      UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Initialization complete with rearview mirrors"));
    }
  }
}

/**
 * CreateRearviewMirrorWidgets Implementation
 */
void ACarlaSpectatorPawn::CreateRearviewMirrorWidgets()
{
  UWorld* World = GetWorld();
  if (!World) return;

  APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
  if (!PC) return;

  UGameViewportClient* ViewportClient = World->GetGameViewport();
  if (!ViewportClient) return;

  // Create LEFT REAR brush
  LeftRearBrush = MakeShared<FSlateBrush>();
  if (LeftRearRenderTarget)
  {
    LeftRearBrush->SetResourceObject(LeftRearRenderTarget);
    LeftRearBrush->ImageSize = FVector2D(LeftRearRenderTarget->SizeX, LeftRearRenderTarget->SizeY);
    LeftRearBrush->DrawAs = ESlateBrushDrawType::Image;
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created LEFT REAR brush"));
  }

  // Create RIGHT REAR brush
  RightRearBrush = MakeShared<FSlateBrush>();
  if (RightRearRenderTarget)
  {
    RightRearBrush->SetResourceObject(RightRearRenderTarget);
    RightRearBrush->ImageSize = FVector2D(RightRearRenderTarget->SizeX, RightRearRenderTarget->SizeY);
    RightRearBrush->DrawAs = ESlateBrushDrawType::Image;
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created RIGHT REAR brush"));
  }

  // Create Slate canvas for rearview mirrors only
  TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas)
    
    // Left rear-view mirror (200x600 at top-left)
    + SConstraintCanvas::Slot()
    .Anchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f))
    .Offset(FMargin(20.0f, 20.0f, 200.0f, 600.0f))
    .Alignment(FVector2D(0.0f, 0.0f))
    .AutoSize(false)
    [
      SAssignNew(LeftMirrorWidget, SBox)
      .WidthOverride(200.0f)
      .HeightOverride(600.0f)
      .Visibility(bEnableRearviewMirrors ? EVisibility::Visible : EVisibility::Hidden)
      [
        SNew(SImage)
        .Image(LeftRearBrush.Get())
      ]
    ]
    
    // Right rear-view mirror (200x600 at top-right)
    + SConstraintCanvas::Slot()
    .Anchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f))
    .Offset(FMargin(-220.0f, 20.0f, 200.0f, 600.0f))
    .Alignment(FVector2D(1.0f, 0.0f))
    .AutoSize(false)
    [
      SAssignNew(RightMirrorWidget, SBox)
      .WidthOverride(200.0f)
      .HeightOverride(600.0f)
      .Visibility(bEnableRearviewMirrors ? EVisibility::Visible : EVisibility::Hidden)
      [
        SNew(SImage)
        .Image(RightRearBrush.Get())
      ]
    ];

  ViewportClient->AddViewportWidgetContent(Canvas, 0);
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Rearview mirror widgets added to viewport"));
}

/**
 * UpdateHeroVehicleTracking Implementation
 */
void ACarlaSpectatorPawn::UpdateHeroVehicleTracking(float DeltaTime)
{
  UWorld* World = GetWorld();
  if (!World) return;

  // Try to find hero vehicle if we don't have it cached
  if (HeroVehicle == nullptr || !IsValid(HeroVehicle))
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
              UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Found hero vehicle: %s (role_name='%s')"), 
                *HeroVehicle->GetName(), *RoleName);
              
              // Hide the hero vehicle entirely to prevent jitter caused by
              // Python API update lag vs C++ frame rate
              // HeroVehicle->SetActorHiddenInGame(true);
              // UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Hidden hero vehicle from game"));
              
              // Activate engine sound when hero vehicle is found
              if (EngineCue && !EngineCue->IsActive())
              {
                EngineCue->Activate(true);
                UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Engine sound activated"));
              }
              
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
  if (HeroVehicle != nullptr)
  {
    UE_LOG(LogTemp, Verbose, TEXT("CarlaSpectatorPawn: We have a hero"));
    // Check if the vehicle is still valid (not destroyed)
    if (IsValid(HeroVehicle))
    { 
      UE_LOG(LogTemp, Verbose, TEXT("CarlaSpectatorPawn: Hero vehicle is valid"));
      FTransform VehicleTransform = HeroVehicle->GetActorTransform();
      
      // Apply camera offset in vehicle's local space
      FVector WorldOffset = VehicleTransform.TransformVector(CameraOffset);
      FVector NewLocation = VehicleTransform.GetLocation() + WorldOffset;
      FRotator NewRotation = VehicleTransform.GetRotation().Rotator();

      // Update engine sound based on hero vehicle's RPM
      if (EngineCue && EngineCue->IsActive())
      {
        ACarlaWheeledVehicle* CarlaVehicle = Cast<ACarlaWheeledVehicle>(HeroVehicle);
        if (CarlaVehicle)
        {
          UWheeledVehicleMovementComponent* MovementComponent = CarlaVehicle->GetVehicleMovement();
          if (MovementComponent)
          {
            float RPM = MovementComponent->GetEngineRotationSpeed();
            EngineCue->SetFloatParameter(FName("RPM"), RPM);
            
            // Debug log (only log occasionally to avoid spam)
            static float LogTimer = 0.0f;
            LogTimer += DeltaTime;
            if (LogTimer >= 1.0f)
            {
              UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Engine RPM = %.1f, Sound playing = %s"), 
                RPM, EngineCue->IsPlaying() ? TEXT("Yes") : TEXT("No"));
              LogTimer = 0.0f;
            }
          }
        }
      }
      
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

