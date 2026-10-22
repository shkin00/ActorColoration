#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EditorSupportDelegates.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/ActorPrimitiveColorHandler.h"
#include "HAL/IConsoleManager.h"
#include "LandscapeSplinesComponent.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "UObject/ObjectKey.h"

#define LOCTEXT_NAMESPACE "ActorColoration"

namespace ActorColoration
{
	static const FName MissingLODsHandlerName(TEXT("MissingLODs"));
	static const FName DetailModeHandlerName(TEXT("DetailMode"));
	static const FName MobilityHandlerName(TEXT("Mobility"));
	static const FName LandscapeSplineMeshesHandlerName(TEXT("LandscapeSplineMeshes"));
	static const FName NavigationRelevantHandlerName(TEXT("NavigationRelevant"));
	static const FName CullDistanceHandlerName(TEXT("CullDistance"));
	static const FName InstancingCandidatesHandlerName(TEXT("InstancingCandidates"));

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

	// Static mesh components that could share one instanced component: same level, mesh and materials.
	struct FInstancingKey
	{
		TObjectKey<ULevel> Level;
		TObjectKey<UStaticMesh> StaticMesh;
		TArray<TObjectKey<UMaterialInterface>> Materials;

		bool operator==(const FInstancingKey& Other) const
		{
			return Level == Other.Level && StaticMesh == Other.StaticMesh && Materials == Other.Materials;
		}

		friend uint32 GetTypeHash(const FInstancingKey& Key)
		{
			uint32 Hash = HashCombine(GetTypeHash(Key.Level), GetTypeHash(Key.StaticMesh));
			for (const TObjectKey<UMaterialInterface>& Material : Key.Materials)
			{
				Hash = HashCombine(Hash, GetTypeHash(Material));
			}
			return Hash;
		}
	};

	// Copies per key, counted when the Instancing Candidates mode is activated or refreshed.
	static TMap<FInstancingKey, int32> StaticMeshCopyCounts;

	// Plain, visible, static mobility static mesh components; instanced and spline meshes are excluded.
	static bool IsInstancingCandidate(const UStaticMeshComponent* StaticMeshComponent)
	{
		return !StaticMeshComponent->IsA<UInstancedStaticMeshComponent>()
			&& !StaticMeshComponent->IsA<USplineMeshComponent>()
			&& StaticMeshComponent->GetStaticMesh()
			&& StaticMeshComponent->GetMobility() == EComponentMobility::Static
			&& StaticMeshComponent->IsVisible()
			&& !StaticMeshComponent->IsEditorOnly();
	}

	static FInstancingKey MakeInstancingKey(const UStaticMeshComponent* StaticMeshComponent)
	{
		FInstancingKey Key;
		Key.Level = StaticMeshComponent->GetComponentLevel();
		Key.StaticMesh = StaticMeshComponent->GetStaticMesh();
		for (int32 MaterialIndex = 0; MaterialIndex < StaticMeshComponent->GetNumMaterials(); ++MaterialIndex)
		{
			Key.Materials.Add(StaticMeshComponent->GetMaterial(MaterialIndex));
		}
		return Key;
	}

	static void CountStaticMeshCopies()
	{
		StaticMeshCopyCounts.Reset();
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			UWorld* World = WorldContext.World();
			if (!World || (WorldContext.WorldType != EWorldType::Editor && WorldContext.WorldType != EWorldType::PIE))
			{
				continue;
			}
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				It->ForEachComponent<UStaticMeshComponent>(false, [](const UStaticMeshComponent* StaticMeshComponent)
				{
					if (StaticMeshComponent->IsRegistered() && IsInstancingCandidate(StaticMeshComponent))
					{
						++StaticMeshCopyCounts.FindOrAdd(MakeInstancingKey(StaticMeshComponent));
					}
				});
			}
		}
	}

	static void ClearStaticMeshCopies()
	{
		StaticMeshCopyCounts.Empty();
	}

	// Red for static mesh components with 20 or more copies in their level, yellow for 5-19, green for components that
	// are already instanced, grey for everything else.
	static FLinearColor GetInstancingCandidatesColor(const UPrimitiveComponent* PrimitiveComponent)
	{
		if (PrimitiveComponent->IsA<UInstancedStaticMeshComponent>())
		{
			return FLinearColor::Green;
		}
		const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitiveComponent);
		if (StaticMeshComponent && IsInstancingCandidate(StaticMeshComponent))
		{
			const int32* NumCopies = StaticMeshCopyCounts.Find(MakeInstancingKey(StaticMeshComponent));
			if (NumCopies && *NumCopies >= 20)
			{
				return FLinearColor::Red;
			}
			if (NumCopies && *NumCopies >= 5)
			{
				return FLinearColor::Yellow;
			}
		}
		return FLinearColor::Gray;
	}

	// Shifts the brightness by a random amount so that neighbouring primitives of the same color stay distinguishable.
	static FLinearColor AddPrimitiveVariation(const FLinearColor& Color)
	{
		const float Offset = FMath::FRandRange(-0.1f, 0.1f);
		return (Color + FLinearColor(Offset, Offset, Offset, 0.0f)).GetClamped();
	}

	static void RegisterHandler(FName HandlerName, const FText& HandlerText, const FText& HandlerToolTipText, FLinearColor (*GetColor)(const UPrimitiveComponent*),
		void (*Activate)() = nullptr, void (*Deactivate)() = nullptr)
	{
		FActorPrimitiveColorHandler::FPrimitiveColorHandler Handler;
		Handler.HandlerName = HandlerName;
		Handler.HandlerText = HandlerText;
		Handler.HandlerToolTipText = HandlerToolTipText;
		Handler.GetColorFunc = [GetColor](const UPrimitiveComponent* PrimitiveComponent)
		{
			return AddPrimitiveVariation(GetColor(PrimitiveComponent));
		};
		if (Activate)
		{
			Handler.ActivateFunc = Activate;
		}
		if (Deactivate)
		{
			Handler.DeactivateFunc = Deactivate;
		}
		FActorPrimitiveColorHandler::Get().RegisterPrimitiveColorHandler(Handler);
	}

	// Recomputes the active mode's colors in the editor and PIE worlds. Colors are otherwise only computed when a mode
	// is activated and when a primitive's render state is created. Editing a component re-registers it, but changes
	// that reach a primitive another way don't show, such as an edited cull distance volume. A mode's ActivateFunc is
	// not run again; Instancing Candidates recounts its copies instead.
	static void RefreshActiveHandler()
	{
		const FName ActiveHandlerName = FActorPrimitiveColorHandler::Get().GetActivePrimitiveColorHandler();
		if (ActiveHandlerName.IsNone())
		{
			return;
		}
		if (ActiveHandlerName == InstancingCandidatesHandlerName)
		{
			CountStaticMeshCopies();
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
		ActorColoration::RegisterHandler(
			ActorColoration::InstancingCandidatesHandlerName,
			LOCTEXT("InstancingCandidates", "Instancing Candidates"),
			LOCTEXT("InstancingCandidates_ToolTip", "Copies of static mobility static mesh components with the same mesh and materials in the same level, counted when the mode is selected or refreshed. Red: 20 or more. Yellow: 5-19. Green: already instanced, such as foliage. Grey: everything else."),
			&ActorColoration::GetInstancingCandidatesColor,
			&ActorColoration::CountStaticMeshCopies,
			&ActorColoration::ClearStaticMeshCopies);

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
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::InstancingCandidatesHandlerName);
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
