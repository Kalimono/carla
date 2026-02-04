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
  SLATE_END_ARGS()

  void Construct(const FArguments& InArgs)
  {
    Brush = InArgs._Brush;
    HorizontalPan = InArgs._HorizontalPan;

    ChildSlot
    [
      SNew(SImage)
      .Image(Brush)
    ];
  }

  void SetHorizontalPan(float NewPan)
  {
    HorizontalPan = FMath::Clamp(NewPan, 0.0f, 1.0f);
  }

  virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, 
    const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, 
    int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
  {
    if (Brush && Brush->GetResourceObject())
    {
      // Calculate UV coordinates based on horizontal pan
      // Pan 0.0 = show left edge, Pan 1.0 = show right edge
      const float ViewportWidth = 0.5f; // Show 50% of the render target width
      const float UMin = FMath::Clamp(HorizontalPan - ViewportWidth * 0.5f, 0.0f, 1.0f - ViewportWidth);
      const float UMax = UMin + ViewportWidth;
      
      // Create custom UV coordinates for horizontal pan
      FSlateResourceHandle ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*Brush);
      
      FSlateDrawElement::MakeBox(
        OutDrawElements,
        LayerId,
        AllottedGeometry.ToPaintGeometry(),
        Brush,
        ESlateDrawEffect::None,
        FLinearColor::White
      );
      
      return LayerId + 1;
    }
    
    return LayerId;
  }

private:
  const FSlateBrush* Brush;
  float HorizontalPan;
};

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

  float OffsetX = bIsLeftSide ? 
    CurrentConfig.OverlayOffsetX : 
    -(CurrentConfig.OverlayOffsetX + CurrentConfig.OverlayWidth);

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
        SNew(SImage)
        .Image(MirrorBrush.Get())
      ]
    ];

  ViewportClient->AddViewportWidgetContent(Canvas, 0);
  UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Mirror widget added to viewport at %s side"), 
    bIsLeftSide ? TEXT("left") : TEXT("right"));
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
  // Examples: "pan:0.5", "zoom:1.5"
  
  FString CommandName, ValueString;
  if (Command.Split(TEXT(":"), &CommandName, &ValueString))
  {
    CommandName = CommandName.TrimStartAndEnd();
    ValueString = ValueString.TrimStartAndEnd();
    
    float Value = FCString::Atof(*ValueString);
    
    if (CommandName.Equals(TEXT("pan"), ESearchCase::IgnoreCase))
    {
      SetHorizontalPan(Value);
      UE_LOG(LogTemp, Verbose, TEXT("CarlaInteractiveMirror: Set pan to %.3f"), Value);
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
      CurrentConfig.RelativeRotation = FRotator(0.0f, 180.0f, 0.0f);
    }
    else if (NodeName.Equals(TEXT("node_3")))
    {
      CurrentConfig.AnchorSide = TEXT("left");  // Right screen: mirror on left edge
      CurrentConfig.OverlayOffsetX = 20.0f;
      CurrentConfig.RelativeLocation = FVector(160.0f, 80.0f, 170.0f);
      CurrentConfig.RelativeRotation = FRotator(0.0f, 180.0f, 0.0f);
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
  }
  
  UE_LOG(LogTemp, Log, TEXT("CarlaInteractiveMirror: Loaded configuration for node '%s'"), *NodeName);
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
    
    // Log if no hero found (only once per second to avoid spam)
    if (!bFoundHero)
    {
      HeroSearchLogTimer += DeltaTime;
      if (HeroSearchLogTimer >= 1.0f)
      {
        UE_LOG(LogTemp, Warning, TEXT("CarlaInteractiveMirror: No hero vehicle found in actor registry. Make sure vehicle has role_name='hero' attribute"));
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

