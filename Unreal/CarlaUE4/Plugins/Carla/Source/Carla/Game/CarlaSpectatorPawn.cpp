// Copyright (c) 2025 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "CarlaSpectatorPawn.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

/**
 * Constructor Implementation
 * 
 * Here we create and configure the camera components:
 * 1. ForwardCamera - a standard UCameraComponent for the main first-person view
 * 2. BackwardSceneCapture - a USceneCaptureComponent2D that captures the rear view
 * 
 * Why USceneCaptureComponent2D for the backward view?
 * - It renders the scene from its perspective to a texture (render target)
 * - This texture can be displayed anywhere on screen using a UI widget
 * - It's much simpler than trying to create multiple player controllers
 * - It's commonly used for rear-view mirrors, security cameras, etc. in games
 */
ACarlaSpectatorPawn::ACarlaSpectatorPawn(const FObjectInitializer& ObjectInitializer)
  : Super(ObjectInitializer)
{
  // Enable ticking so we can update the backward camera each frame
  PrimaryActorTick.bCanEverTick = true;
  
  // Initialize widget pointers to null
  SplitScreenWidgetClass = nullptr;
  SplitScreenWidgetInstance = nullptr;

  // Create the forward-facing camera component
  // This is the "main" camera that the player sees through
  ForwardCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ForwardCamera"));
  
  // Attach it to the root component so it moves with the spectator
  ForwardCamera->SetupAttachment(RootComponent);
  
  // Position it at the center of the spectator (no offset)
  ForwardCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  
  // No rotation needed - it faces forward by default
  ForwardCamera->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));

  // Create the backward-facing scene capture component
  // This will render the view behind the spectator to a texture
  BackwardSceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("BackwardSceneCapture"));
  
  // Attach it to the root so it moves with the spectator
  BackwardSceneCapture->SetupAttachment(RootComponent);
  
  // Position it at the same location as the forward camera
  BackwardSceneCapture->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
  
  // Rotate it 180 degrees so it faces backward
  // FRotator(Pitch, Yaw, Roll) - we only need to rotate the yaw (turning left/right)
  BackwardSceneCapture->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));

  // Configure the scene capture settings
  // These settings control how the backward view is rendered
  
  // CaptureSource determines what kind of image we capture
  // SCS_FinalColorLDR = capture the final rendered color (what you normally see)
  BackwardSceneCapture->CaptureSource = SCS_FinalColorLDR;
  
  // bCaptureEveryFrame = true means it updates every frame (like a live camera feed)
  // If this were false, we'd have to manually trigger captures
  BackwardSceneCapture->bCaptureEveryFrame = true;
  
  // bCaptureOnMovement = true means it updates when the component moves
  // This ensures we get smooth updates as the spectator moves around
  BackwardSceneCapture->bCaptureOnMovement = true;

  // We'll create the render target in BeginPlay since it needs to know the screen resolution
  BackwardRenderTarget = nullptr;
}

/**
 * BeginPlay Implementation
 * 
 * Called when the actor enters gameplay (after the world is loaded).
 * This is where we:
 * 1. Create the render target (texture) for the backward view
 * 2. Assign it to the scene capture component
 * 3. Set up the split-screen UI widget to display both views
 */
void ACarlaSpectatorPawn::BeginPlay()
{
  Super::BeginPlay();

  // Create a render target (texture) for the backward scene capture to draw to
  // This is essentially a "canvas" that the scene capture will paint on each frame
  
  // Get the viewport size to determine the render target resolution
  // We want it to match the screen resolution for best quality
  UWorld* World = GetWorld();
  if (!World)
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaSpectatorPawn: World is null"));
    return;
  }

  APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
  if (!PC)
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaSpectatorPawn: PlayerController is null"));
    return;
  }

  // Get the viewport size (screen resolution)
  int32 ViewportWidth, ViewportHeight;
  PC->GetViewportSize(ViewportWidth, ViewportHeight);

  // Create the render target texture
  // NewObject creates a new Unreal object at runtime
  // Parameters: Outer (owner), Class, Name
  BackwardRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("BackwardRenderTarget"));
  
  if (BackwardRenderTarget)
  {
    // Initialize the render target with the viewport dimensions
    // We use full resolution for best quality
    // RTF_RGBA8 means 8 bits per color channel (standard color format)
    BackwardRenderTarget->InitAutoFormat(ViewportWidth, ViewportHeight);
    
    // Set the render target format
    // PF_B8G8R8A8 is a standard color format (Blue, Green, Red, Alpha - 8 bits each)
    BackwardRenderTarget->RenderTargetFormat = RTF_RGBA8;
    
    // Update the resource so it's ready to be used
    BackwardRenderTarget->UpdateResource();

    // Assign this render target to the scene capture component
    // Now when the scene capture renders, it will draw to this texture
    BackwardSceneCapture->TextureTarget = BackwardRenderTarget;

    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created render target %dx%d"), ViewportWidth, ViewportHeight);
  }
  else
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaSpectatorPawn: Failed to create render target"));
    return;
  }

  // Set up the split-screen UI
  // This will create a widget that displays both camera views
  CreateSplitScreenWidget();
}

/**
 * Tick Implementation
 * 
 * Called every frame. The scene capture automatically updates because we set
 * bCaptureEveryFrame = true. We also handle deferred initialization here to
 * ensure all systems are ready.
 */
void ACarlaSpectatorPawn::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);
  
  // Initialize render target and widget on first tick if not already done
  // This ensures PlayerController and viewport are available
  static bool bInitialized = false;
  if (!bInitialized && BackwardRenderTarget == nullptr)
  {
    UWorld* World = GetWorld();
    if (!World)
    {
      return;
    }

    APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
    if (!PC)
    {
      return;
    }

    // Get the viewport size (screen resolution)
    int32 ViewportWidth, ViewportHeight;
    PC->GetViewportSize(ViewportWidth, ViewportHeight);

    if (ViewportWidth > 0 && ViewportHeight > 0)
    {
      // Create the render target texture
      BackwardRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("BackwardRenderTarget"));
      
      if (BackwardRenderTarget)
      {
        // Initialize the render target with the viewport dimensions
        BackwardRenderTarget->InitAutoFormat(ViewportWidth, ViewportHeight);
        BackwardRenderTarget->RenderTargetFormat = RTF_RGBA8;
        BackwardRenderTarget->UpdateResource();

        // Assign this render target to the scene capture component
        BackwardSceneCapture->TextureTarget = BackwardRenderTarget;
        
        // Activate the scene capture component so it starts rendering
        BackwardSceneCapture->SetActive(true);

        UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Created render target %dx%d and activated scene capture"), ViewportWidth, ViewportHeight);
        
        // Set up the split-screen UI
        CreateSplitScreenWidget();
        
        bInitialized = true;
      }
    }
  }
  
  // The scene capture automatically updates each frame, so we don't need to do anything here
  // Both cameras are attached to RootComponent, so they automatically follow the spectator's movement
  
  // If we wanted to manually control when the backward view updates, we would call:
  // BackwardSceneCapture->CaptureScene();
}

/**
 * CreateSplitScreenWidget Implementation
 * 
 * This function creates and displays a UMG widget that shows the backward camera view.
 * The widget blueprint should contain an Image widget that we'll configure to display
 * the BackwardRenderTarget texture.
 * 
 * Steps:
 * 1. Check if a widget class is assigned (either in Blueprint or by loading a path)
 * 2. Create an instance of the widget
 * 3. Find the Image widget inside it (by name)
 * 4. Set the Image's brush to display our BackwardRenderTarget
 * 5. Add the widget to the viewport so it's visible on screen
 */
void ACarlaSpectatorPawn::CreateSplitScreenWidget()
{
  UWorld* World = GetWorld();
  if (!World)
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaSpectatorPawn: World is null in CreateSplitScreenWidget"));
    return;
  }

  APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
  if (!PC)
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaSpectatorPawn: PlayerController is null in CreateSplitScreenWidget"));
    return;
  }

  // If no widget class is set in Blueprint, try to load a default one
  // You should replace this path with the actual path to your widget blueprint
  // Format: "/Game/FolderName/WidgetName.WidgetName_C"
  // The "_C" suffix is important - it indicates the compiled Blueprint class
  if (!SplitScreenWidgetClass)
  {
    // Try to load a widget from a default path
    // IMPORTANT: Update this path to match your widget's location!
    FString WidgetPath = TEXT("/Game/Carla/Blueprints/UI/SplitScreenWidget.SplitScreenWidget_C");
    
    SplitScreenWidgetClass = LoadClass<UUserWidget>(nullptr, *WidgetPath);
    
    if (!SplitScreenWidgetClass)
    {
      UE_LOG(LogTemp, Warning, TEXT("CarlaSpectatorPawn: Could not load widget from path: %s"), *WidgetPath);
      UE_LOG(LogTemp, Warning, TEXT("Please set the SplitScreenWidgetClass in the Blueprint or update the path in code."));
      UE_LOG(LogTemp, Log, TEXT("Backward camera is still active and rendering to the render target."));
      return;
    }
  }

  // Create an instance of the widget
  SplitScreenWidgetInstance = CreateWidget<UUserWidget>(PC, SplitScreenWidgetClass);
  
  if (!SplitScreenWidgetInstance)
  {
    UE_LOG(LogTemp, Error, TEXT("CarlaSpectatorPawn: Failed to create widget instance"));
    return;
  }

  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Successfully created widget instance"));

  // Find the Image widget inside our UMG widget by name
  // The Image widget in your Blueprint should be named "BackwardCameraImage" or similar
  // You can change this name to match what you used in your widget blueprint
  UImage* BackwardImage = Cast<UImage>(SplitScreenWidgetInstance->GetWidgetFromName(TEXT("BackwardCameraImage")));
  
  if (BackwardImage && BackwardRenderTarget)
  {
    // Create a brush that uses our render target texture
    FSlateBrush Brush;
    Brush.SetResourceObject(BackwardRenderTarget);
    
    // Set the image size to match the render target
    Brush.ImageSize = FVector2D(BackwardRenderTarget->SizeX, BackwardRenderTarget->SizeY);
    
    // Apply the brush to the Image widget
    BackwardImage->SetBrush(Brush);
    
    UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Successfully set render target to Image widget"));
  }
  else
  {
    if (!BackwardImage)
    {
      UE_LOG(LogTemp, Warning, TEXT("CarlaSpectatorPawn: Could not find Image widget named 'BackwardCameraImage' in the widget."));
      UE_LOG(LogTemp, Warning, TEXT("Make sure your widget blueprint has an Image widget with this exact name."));
    }
    
    if (!BackwardRenderTarget)
    {
      UE_LOG(LogTemp, Error, TEXT("CarlaSpectatorPawn: BackwardRenderTarget is null!"));
    }
  }

  // Add the widget to the viewport so it's visible on screen
  // ZOrder 0 means it will be at the back (other UI elements can be on top)
  SplitScreenWidgetInstance->AddToViewport(0);
  
  UE_LOG(LogTemp, Log, TEXT("CarlaSpectatorPawn: Split-screen widget added to viewport successfully!"));
}
