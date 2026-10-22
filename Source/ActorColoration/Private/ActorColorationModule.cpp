#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ActorPrimitiveColorHandler.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "ActorColoration"

namespace ActorColoration
{
	static const FName MissingLODsHandlerName(TEXT("MissingLODs"));

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

	// Shifts the brightness by a random amount so that neighbouring primitives of the same color stay distinguishable.
	static FLinearColor AddPrimitiveVariation(const FLinearColor& Color)
	{
		const float Offset = FMath::FRandRange(-0.1f, 0.1f);
		return (Color + FLinearColor(Offset, Offset, Offset, 0.0f)).GetClamped();
	}
}

class FActorColorationModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FActorPrimitiveColorHandler::FPrimitiveColorHandler MissingLODsHandler;
		MissingLODsHandler.HandlerName = ActorColoration::MissingLODsHandlerName;
		MissingLODsHandler.HandlerText = LOCTEXT("MissingLODs", "Missing LODs");
		MissingLODsHandler.HandlerToolTipText = LOCTEXT("MissingLODs_ToolTip", "Red: non-Nanite static and skeletal meshes with a single LOD. Grey: everything else.");
		MissingLODsHandler.GetColorFunc = [](const UPrimitiveComponent* PrimitiveComponent)
		{
			return ActorColoration::AddPrimitiveVariation(ActorColoration::GetMissingLODsColor(PrimitiveComponent));
		};
		FActorPrimitiveColorHandler::Get().RegisterPrimitiveColorHandler(MissingLODsHandler);
	}

	virtual void ShutdownModule() override
	{
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(ActorColoration::MissingLODsHandlerName);
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FActorColorationModule, ActorColoration)
