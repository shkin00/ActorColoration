#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ActorPrimitiveColorHandler.h"
#include "LandscapeSplinesComponent.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "ActorColoration"

namespace ActorColoration
{
	static const FName MissingLODsHandlerName(TEXT("MissingLODs"));
	static const FName DetailModeHandlerName(TEXT("DetailMode"));
	static const FName MobilityHandlerName(TEXT("Mobility"));
	static const FName LandscapeSplineMeshesHandlerName(TEXT("LandscapeSplineMeshes"));

	// Red for non-Nanite static and skeletal meshes with a single LOD, grey for everything else.
	static FLinearColor GetMissingLODsColor(const UPrimitiveComponent* PrimitiveComponent)
	{
		if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitiveComponent))
		{
			const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
			if (StaticMesh && !StaticMesh->IsNaniteEnabled() && StaticMesh->GetNumLODs() == 1)
			{
				return FLinearColor::Red;
			}
		}
		else if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimitiveComponent))
		{
			const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
			if (SkeletalMesh && !SkeletalMesh->IsNaniteEnabled() && SkeletalMesh->GetLODNum() == 1)
			{
				return FLinearColor::Red;
			}
		}
		return FLinearColor::Gray;
	}

	// Red for Epic, yellow for High, green for Medium, grey for Low.
	static FLinearColor GetDetailModeColor(const UPrimitiveComponent* PrimitiveComponent)
	{
		switch (PrimitiveComponent->DetailMode.GetValue())
		{
		case DM_Epic:
			return FLinearColor::Red;
		case DM_High:
			return FLinearColor::Yellow;
		case DM_Medium:
			return FLinearColor::Green;
		default:
			return FLinearColor::Gray;
		}
	}

	// Red for Movable, yellow for Stationary, grey for Static.
	static FLinearColor GetMobilityColor(const UPrimitiveComponent* PrimitiveComponent)
	{
		switch (PrimitiveComponent->GetMobility())
		{
		case EComponentMobility::Movable:
			return FLinearColor::Red;
		case EComponentMobility::Stationary:
			return FLinearColor::Yellow;
		default:
			return FLinearColor::Gray;
		}
	}

	// Red for spline mesh components placed by landscape spline segments, grey for everything else.
	static FLinearColor GetLandscapeSplineMeshesColor(const UPrimitiveComponent* PrimitiveComponent)
	{
		if (PrimitiveComponent->IsA<USplineMeshComponent>() && Cast<ULandscapeSplinesComponent>(PrimitiveComponent->GetAttachParent()))
		{
			return FLinearColor::Red;
		}
		return FLinearColor::Gray;
	}

	// Shifts the brightness by a random amount so that neighbouring primitives of the same color stay distinguishable.
	static FLinearColor AddPrimitiveVariation(const FLinearColor& Color)
	{
		const float Offset = FMath::FRandRange(-0.1f, 0.1f);
		return (Color + FLinearColor(Offset, Offset, Offset, 0.0f)).GetClamped();
	}

	static void RegisterHandler(FName HandlerName, const FText& HandlerText, const FText& HandlerToolTipText, FLinearColor (*GetColor)(const UPrimitiveComponent*))
	{
		FActorPrimitiveColorHandler::FPrimitiveColorHandler Handler;
		Handler.HandlerName = HandlerName;
		Handler.HandlerText = HandlerText;
		Handler.HandlerToolTipText = HandlerToolTipText;
		Handler.GetColorFunc = [GetColor](const UPrimitiveComponent* PrimitiveComponent)
		{
			return AddPrimitiveVariation(GetColor(PrimitiveComponent));
		};
		FActorPrimitiveColorHandler::Get().RegisterPrimitiveColorHandler(Handler);
	}
}

class FActorColorationModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		ActorColoration::RegisterHandler(
			ActorColoration::MissingLODsHandlerName,
			LOCTEXT("MissingLODs", "Missing LODs"),
			LOCTEXT("MissingLODs_ToolTip", "Red: non-Nanite static and skeletal meshes with a single LOD. Grey: everything else."),
			&ActorColoration::GetMissingLODsColor);
		ActorColoration::RegisterHandler(
			ActorColoration::DetailModeHandlerName,
			LOCTEXT("DetailMode", "Detail Mode"),
			LOCTEXT("DetailMode_ToolTip", "Component detail mode. Red: Epic. Yellow: High. Green: Medium. Grey: Low. Components above the current r.DetailMode are not drawn."),
			&ActorColoration::GetDetailModeColor);
		ActorColoration::RegisterHandler(
			ActorColoration::MobilityHandlerName,
			LOCTEXT("Mobility", "Mobility"),
			LOCTEXT("Mobility_ToolTip", "Component mobility. Red: Movable. Yellow: Stationary. Grey: Static."),
			&ActorColoration::GetMobilityColor);
		ActorColoration::RegisterHandler(
			ActorColoration::LandscapeSplineMeshesHandlerName,
			LOCTEXT("LandscapeSplineMeshes", "Landscape Spline Meshes"),
			LOCTEXT("LandscapeSplineMeshes_ToolTip", "Red: meshes placed along landscape spline segments. Grey: everything else, including control point meshes."),
			&ActorColoration::GetLandscapeSplineMeshesColor);
	}

	virtual void ShutdownModule() override
	{
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::MissingLODsHandlerName);
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::DetailModeHandlerName);
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::MobilityHandlerName);
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::LandscapeSplineMeshesHandlerName);
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FActorColorationModule, ActorColoration)
