#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EditorSupportDelegates.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/ActorPrimitiveColorHandler.h"
#include "HAL/IConsoleManager.h"
#include "LandscapeSplinesComponent.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "ActorColoration"

namespace ActorColoration
{
	static const FName MissingLODsHandlerName(TEXT("MissingLODs"));
	static const FName DetailModeHandlerName(TEXT("DetailMode"));
	static const FName MobilityHandlerName(TEXT("Mobility"));
	static const FName LandscapeSplineMeshesHandlerName(TEXT("LandscapeSplineMeshes"));
	static const FName NavigationRelevantHandlerName(TEXT("NavigationRelevant"));
	static const FName CullDistanceHandlerName(TEXT("CullDistance"));

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

	// Red for primitives whose owning actor has a navigation relevant primitive component, grey for everything else.
	// The whole actor is tested because navigation often comes from components that are not drawn, such as landscape
	// collision.
	static FLinearColor GetNavigationRelevantColor(const UPrimitiveComponent* PrimitiveComponent)
	{
		const AActor* Owner = PrimitiveComponent->GetOwner();
		if (!Owner)
		{
			return PrimitiveComponent->IsNavigationRelevant() ? FLinearColor::Red : FLinearColor::Gray;
		}
		for (const UPrimitiveComponent* Component : TInlineComponentArray<UPrimitiveComponent*>(Owner))
		{
			if (Component->IsNavigationRelevant())
			{
				return FLinearColor::Red;
			}
		}
		return FLinearColor::Gray;
	}

	// Red for primitives that are never distance culled, yellow beyond 500 m, green beyond 100 m, grey within 100 m.
	// Primitives in an HLOD cluster use the distance at which the HLOD replaces them; instanced meshes also use their
	// per-instance cull distance.
	static FLinearColor GetCullDistanceColor(const UPrimitiveComponent* PrimitiveComponent)
	{
		// The distance the renderer culls at: desired max draw distance, cull distance volumes and Never Distance Cull.
		// 0 means never.
		float CullDistance = PrimitiveComponent->CachedMaxDrawDistance;

		const UPrimitiveComponent* LODParent = PrimitiveComponent->GetLODParentPrimitive();
		if (LODParent && LODParent->MinDrawDistance > 0.0f)
		{
			CullDistance = LODParent->MinDrawDistance;
		}

		if (const UInstancedStaticMeshComponent* InstancedComponent = Cast<UInstancedStaticMeshComponent>(PrimitiveComponent))
		{
			int32 InstanceStartCullDistance = 0;
			int32 InstanceEndCullDistance = 0;
			InstancedComponent->GetCullDistances(InstanceStartCullDistance, InstanceEndCullDistance);
			if (InstanceEndCullDistance > 0)
			{
				CullDistance = CullDistance > 0.0f ? FMath::Min(CullDistance, float(InstanceEndCullDistance)) : float(InstanceEndCullDistance);
			}
		}

		if (CullDistance <= 0.0f)
		{
			return FLinearColor::Red;
		}
		if (CullDistance > 50000.0f)
		{
			return FLinearColor::Yellow;
		}
		if (CullDistance > 10000.0f)
		{
			return FLinearColor::Green;
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

	// Recomputes the active mode's colors in the editor and PIE worlds. Colors are otherwise only computed when a mode
	// is activated and when a primitive's render state is created. Editing a component re-registers it, but changes
	// that reach a primitive another way don't show, such as an edited cull distance volume. The mode's ActivateFunc is
	// not run again.
	static void RefreshActiveHandler()
	{
		const FName ActiveHandlerName = FActorPrimitiveColorHandler::Get().GetActivePrimitiveColorHandler();
		if (ActiveHandlerName.IsNone())
		{
			return;
		}
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			UWorld* World = WorldContext.World();
			if (World && (WorldContext.WorldType == EWorldType::Editor || WorldContext.WorldType == EWorldType::PIE))
			{
				FActorPrimitiveColorHandler::Get().RefreshPrimitiveColorHandler(ActiveHandlerName, World);
			}
		}
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}

	static FAutoConsoleCommand RefreshCommand(
		TEXT("ActorColoration.Refresh"),
		TEXT("Recomputes the colors of the active actor coloration mode in the editor and PIE worlds."),
		FConsoleCommandDelegate::CreateStatic(&RefreshActiveHandler));
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
		ActorColoration::RegisterHandler(
			ActorColoration::NavigationRelevantHandlerName,
			LOCTEXT("NavigationRelevant", "Navigation Relevant"),
			LOCTEXT("NavigationRelevant_ToolTip", "Red: actors with a navigation relevant component, including components that are not drawn, such as landscape collision. Grey: everything else."),
			&ActorColoration::GetNavigationRelevantColor);
		ActorColoration::RegisterHandler(
			ActorColoration::CullDistanceHandlerName,
			LOCTEXT("CullDistance", "Cull Distance"),
			LOCTEXT("CullDistance_ToolTip", "Distance at which the primitive is culled or replaced by its HLOD, including cull distance volumes. Red: never. Yellow: beyond 500 m. Green: 100-500 m. Grey: within 100 m."),
			&ActorColoration::GetCullDistanceColor);

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FActorColorationModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::MissingLODsHandlerName);
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::DetailModeHandlerName);
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::MobilityHandlerName);
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::LandscapeSplineMeshesHandlerName);
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::NavigationRelevantHandlerName);
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::CullDistanceHandlerName);
	}

private:
	// Adds Refresh Colors below the mode list in the level viewport's View Mode > Actor Coloration submenu.
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.ViewportToolbar.ViewModes.VisualizeActorColorationViewMode");
		FToolMenuSection& Section = Menu->AddSection(
			"ActorColorationRefresh",
			FText::GetEmpty(),
			FToolMenuInsert("LevelViewportActorColoration", EToolMenuInsertType::After));
		Section.AddMenuEntry(
			"RefreshColors",
			LOCTEXT("RefreshColors", "Refresh Colors"),
			LOCTEXT("RefreshColors_ToolTip", "Recompute the active mode's colors, for example after editing a cull distance volume. Same as the ActorColoration.Refresh console command."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&ActorColoration::RefreshActiveHandler),
				FCanExecuteAction::CreateLambda([]()
				{
					return !FActorPrimitiveColorHandler::Get().GetActivePrimitiveColorHandler().IsNone();
				})));
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FActorColorationModule, ActorColoration)
