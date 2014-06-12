#pragma once

#include "GameFramework/Actor.h"
#include "GeneratedMeshComponent.h"
#include "BiomeRegionData.h"
#include "TerrainDataStructures.h"
#include "DataStructures.h"
#include "BiomeRegion.generated.h"

/**
 * In-game actor that visualizes the biome generation algorithm. This is more of a debug actor
 * than anything since players are not supposed to be able to see this.
 */
UCLASS()
class ABiomeRegion : public AActor {
	GENERATED_UCLASS_BODY()

private:
	TSharedPtr<terrain::BiomeRegionData> RegionData;
	float BiomeGridScale;
	float RenderHeight;
	void GenerateBiomeRegionMesh();

public:
	UPROPERTY()
		TSubobjectPtr<UGeneratedMeshComponent> Mesh;
	UPROPERTY()
		UMaterial * TestMaterial;

	void InitializeBiomeRegion(const float scale);
	void SetBiomeRegionData(const TSharedPtr<terrain::BiomeRegionData> & BiomeRegionData);
};