#include <Daedalus.h>
#include "Chunk.h"

#include <Utilities/UnrealBridge.h>
#include <Utilities/Mesh/MarchingCubes.h>
#include <Utilities/Mesh/DebugMeshHelpers.h>

#include <cmath>

using namespace utils;
using namespace terrain;
using namespace items;

namespace terrain {
	Option<TerrainRaytraceResult> TerrainResult(const ChunkPositionVector & entry) {
		return Some(TerrainRaytraceResult(E_Terrain, entry, None<ItemDataId>()));
	}

	Option<TerrainRaytraceResult> ItemResult(
		const ItemDataId & id, const ChunkPositionVector & pos
	) {
		return Some(TerrainRaytraceResult(E_PlacedItem, pos, Some(id)));
	}

	utils::Option<TerrainRaytraceResult> NoResult() {
		return None<TerrainRaytraceResult>();
	}
}

using ChunkDataSet = AChunk::ChunkDataSet;

AChunk::AChunk(const FPostConstructInitializeProperties & PCIP)
	: Super(PCIP), ChunkNeighbourData(NULL), ItemIdCounter(0)
{
	Mesh = PCIP.CreateDefaultSubobject<UGeneratedMeshComponent>(this, TEXT("GeneratedMesh"));
	TestMaterial = ConstructorHelpers::FObjectFinder<UMaterial>(
		TEXT("Material'/Game/TestMaterial.TestMaterial'")).Object;
	this->RootComponent = Mesh;
}

AItem * AChunk::SpawnItem(const ItemDataPtr & itemData) {
	const FRotator defaultRotator(0, 0, 0);
	auto params = FActorSpawnParameters();
	params.Name = *FString::Printf(TEXT("(%lld,%lld,%lld)PlacedItem%llu"),
		itemData->Position.ChunkOffset.X,
		itemData->Position.ChunkOffset.Y,
		itemData->Position.ChunkOffset.Z,
		itemData->ItemId);
	UE_LOG(LogTemp, Warning, TEXT("(%lld,%lld,%lld)PlacedItem%llu"),
		itemData->Position.ChunkOffset.X,
		itemData->Position.ChunkOffset.Y,
		itemData->Position.ChunkOffset.Z,
		itemData->ItemId);
	auto actor = GetWorld()->SpawnActor<AItem>(
		AItem::StaticClass(),
		ToFVector(TerrainGenParams->ToRealCoordSpace(itemData->Position.InnerOffset)),
		FRotator(0, 0, 0), params);
	PlacedItems.Add({ itemData->ItemId, actor });
	actor->Initialize(itemData);
	actor->AttachRootComponentToActor(this);
	return actor;
}

ItemDataPtr AChunk::RemoveItem(const ItemDataPtr & itemData) {
	Uint32 index = 0;
	ItemDataPtr removed = NULL;

	for (const auto & it : PlacedItems) {
		if (it.ItemId == itemData->ItemId) {
			it.ItemActor->Destroy();
			PlacedItems.RemoveAt(index);
			removed = it.ItemActor->GetItemData();
			break;
		}
		++index;
	}

	return removed;
}

void AChunk::ReceiveDestroyed() {
	for (const auto & it : PlacedItems)
		it.ItemActor->Destroy();
	Super::ReceiveDestroyed();
}

void AChunk::InitializeChunk(const TerrainGeneratorParameters * params) {
	SolidTerrain.Reset(params->GridCellCount, params->GridCellCount, params->GridCellCount, false);
	TerrainGenParams = params;
}

void AChunk::SetChunkData(const ChunkDataSet & chunkData) {
	ChunkNeighbourData = chunkData;
	CurrentChunkIndex = chunkData.Size() / Uint32(2);
	CurrentChunkData = chunkData.Get(CurrentChunkIndex);
	// Make sure the item ID counter is at least 1 larger than the current largest ID
	for (const auto & itemData : CurrentChunkData->PlacedItems) {
		if (itemData->ItemId >= ItemIdCounter)
			ItemIdCounter = itemData->ItemId + 1;
	}
	GenerateChunkMesh();
}

bool AChunk::IsSolidTerrainAt(const Point3D & point) const {

	//if (TerrainGenParams->WithinGridBounds(point)) {
	//	cdata = CurrentChunkData;
	//} else {
	//	// If this incoming point is outside the current chunk, we should check the neighbours
	//	// to check for collisions.
	//	const auto startingPos = ChunkPositionVector(CurrentChunkIndex.Cast<Int64>(), point);
	//	const auto offsetPos = TerrainGenParams->Normalize(startingPos);
	//	const auto nsize = ChunkNeighbourData.Size().Cast<Int64>();

	//	if (offsetPos.ChunkOffset.IsBoundedBy(Vector3D<Int64>(0), nsize)) {
	//		cdata = ChunkNeighbourData.Get(offsetPos.ChunkOffset.Cast<Uint32>());
	//		position = offsetPos.InnerOffset;
	//	}
	//}
	//if (!cdata)
	//	return false;

	// Check for solidness of coordinate, depending on the algorithm used, we will probably need
	// to change this section.
	// TODO: change this to suit terrain mesh generation algorithm
	const auto startIndex = EFloor(point);
	const auto nsize = ChunkNeighbourData.Size().Cast<Int64>();
	const auto curIndex = CurrentChunkIndex.Cast<Int64>();
	double sum = 0;

	for (Int64 x = startIndex.X; x <= startIndex.X + 1; x++) {
		for (Int64 y = startIndex.Y; y <= startIndex.Y + 1; y++) {
			for (Int64 z = startIndex.Z; z <= startIndex.Z + 1; z++) {
				// If this incoming point is outside the current chunk, we should check the
				// neighbours to check for collisions.
				const auto startingPos = ChunkPositionVector(curIndex, Point3D(x, y, z));
				const auto offsetPos = TerrainGenParams->Normalize(startingPos);

				if (offsetPos.ChunkOffset.IsBoundedBy(Vector3D<Int64>(0), nsize)) {
					ChunkDataPtr cdata = ChunkNeighbourData.Get(
						offsetPos.ChunkOffset.X, offsetPos.ChunkOffset.Y, offsetPos.ChunkOffset.Z);

					sum += cdata->DensityData.Get(
						offsetPos.InnerOffset.X, offsetPos.InnerOffset.Y, offsetPos.InnerOffset.Z);
				} else {
					// If this point doesn't fall within the current chunk, nor within its
					// immediate neighbours, then we return true as a conservative estimate.
					return true;
				}
			}
		}
	}
	return EGT(sum, 0);
}

bool AChunk::IsSolidTerrainAt(const AxisAlignedBoundingBox3D & bound) const {
	const auto lower = EFloor(bound.MinPoint);
	const auto upper = ECeil(bound.MaxPoint);
	for (Int64 x = lower.X; x < upper.X; x++) {
		for (Int64 y = lower.Y; y < upper.Y; y++) {
			for (Int64 z = lower.Z; z < upper.Z; z++) {
				if (IsSolidTerrainAt(Point3D(x, y, z)))
					return true;
			}
		}
	}
	return false;
}

Option<ItemDataId> AChunk::FindItemCollision(const AxisAlignedBoundingBox3D & bound) const {
	// Check items to make sure nothing occupies this location. Perhaps using an octree
	// might be wise here.
	for (const auto & item : PlacedItems) {
		const auto & itemData = item.ItemActor->GetItemData();
		if (bound.BoundingBoxIntersection(itemData->GetBoundingBox(), false))
			return Some(ItemDataId(item.ItemId, CurrentChunkData->ChunkOffset));
	}

	// Check against items in neighbouring chunks since some items may span multiple grid
	// cells. This section will likely need to be optimized, since it could become very
	// costly with large numbers of items.
	const auto nsize = ChunkNeighbourData.Size();
	const auto curIndex = nsize / Uint32(2);

	const double gcc = (double) TerrainGenParams->GridCellCount;

	AxisAlignedBoundingBox3D offsetBox;
	Point3D offsetVector;

	for (Uint8 x = 0; x < nsize.X; x++) {
		for (Uint8 y = 0; y < nsize.Y; y++) {
			for (Uint8 z = 0; z < nsize.Z; z++) {
				if (x != curIndex.X || y != curIndex.Y || z != curIndex.Z) {
					offsetVector.X = ((double) x - curIndex.X) * gcc;
					offsetVector.Y = ((double) y - curIndex.Y) * gcc;
					offsetVector.Z = ((double) z - curIndex.Z) * gcc;

					offsetBox.MinPoint = bound.MinPoint - offsetVector;
					offsetBox.MaxPoint = bound.MaxPoint - offsetVector;

					const auto & chunkData = ChunkNeighbourData.Get(x, y, z);
					for (const auto & item : chunkData->PlacedItems) {
						if (offsetBox.BoundingBoxIntersection(item->GetBoundingBox(), false))
							return Some(ItemDataId(item->ItemId, CurrentChunkData->ChunkOffset));
					}
				}
			}
		}
	}

	return None<ItemDataId>();
}

bool AChunk::IsSpaceOccupied(const AxisAlignedBoundingBox3D & bound) const {
	return IsSolidTerrainAt(bound) || FindItemCollision(bound).IsValid();
}

AItem * AChunk::CreateItem(const items::ItemDataPtr & itemData, const bool preserveId) {
	const auto & position = itemData->Position.InnerOffset;
	if (!IsSpaceOccupied(itemData->GetBoundingBox().GetEnclosingBoundingBox())) {
		if (!preserveId)
			itemData->ItemId = ItemIdCounter++;
		itemData->bIsPlaced = true;
		CurrentChunkData->PlacedItems.push_back(itemData);
		return SpawnItem(itemData);
	}
	return NULL;
}

void AChunk::GenerateChunkMesh() {
	auto material = UMaterialInstanceDynamic::Create((UMaterial *) TestMaterial, this);

	Uint32 cellCount = TerrainGenParams->GridCellCount;

	double scale = TerrainGenParams->ChunkScale / cellCount;

	std::vector<Triangle3D> tempTris;
	Vector3D<double> displacementVector;

	GridCell gridCell;
	Tensor3D<float> densityDataPoints(cellCount + 1);
	
	// Populate the density data
	for (Uint32 x = 0; x < cellCount + 1; x++) {
		for (Uint32 y = 0; y < cellCount + 1; y++) {
			for (Uint32 z = 0; z < cellCount + 1; z++) {
				ChunkDataPtr mainData =
					ChunkNeighbourData.Get(x / cellCount + 1, y / cellCount + 1, z / cellCount + 1);
				densityDataPoints.Set(
					x, y, z, mainData->DensityData.Get(
						x % cellCount, y % cellCount, z % cellCount));
			}
		}
	}

	std::vector<Triangle3D> triangles;

	// Build the mesh
	for (Uint32 x = 0; x < cellCount; x++) {
		for (Uint32 y = 0; y < cellCount; y++) {
			for (Uint32 z = 0; z < cellCount; z++) {
				gridCell.Initialize(
					densityDataPoints.Get(x, y, z),
					densityDataPoints.Get(x, y, z + 1),
					densityDataPoints.Get(x, y + 1, z),
					densityDataPoints.Get(x, y + 1, z + 1),
					densityDataPoints.Get(x + 1, y, z),
					densityDataPoints.Get(x + 1, y, z + 1),
					densityDataPoints.Get(x + 1, y + 1, z),
					densityDataPoints.Get(x + 1, y + 1, z + 1));

				// Set the solid terrain cache if this grid cell is filled
				if (gridCell.Sum() > FLOAT_ERROR)
					SolidTerrain.Set(x, y, z, true);

				tempTris.clear();
				MarchingCube(tempTris, 0, gridCell);

				displacementVector.Reset((double) x, (double) y, (double) z);

				for (auto & trit : tempTris) {
					Triangle3D tri(
						(trit.Point1 + displacementVector) * scale,
						(trit.Point2 + displacementVector) * scale,
						(trit.Point3 + displacementVector) * scale);
					triangles.push_back(tri);
				}
			}
		}
	}

	if (triangles.size() > 0) {
		TArray<FMeshTriangle> meshTriangles;
		for (auto it : triangles)
			meshTriangles.Add(FMeshTriangle(it, material));
		Mesh->SetGeneratedMeshTriangles(meshTriangles);
	}
}

Option<TerrainRaytraceResult> AChunk::Raytrace(const Ray3D & ray, const double maxDistance) {
	FastVoxelTraversalIterator fvt(
		Vector3D<>(1.0), Vector3D<Int64>(0), Vector3D<Int64>(TerrainGenParams->GridCellCount),
		ray, maxDistance / TerrainGenParams->ChunkGridUnitSize);

	for (Uint16 i = 0; fvt.IsValid(); i++) {
		const auto & current = fvt.GetCurrentCell();
		if (current.IsBoundedBy(0, TerrainGenParams->GridCellCount)) {
			// TODO: make this work as single ray cast instead
			const double inset = 0.01; // Accounts for rounding errors
			const AxisAlignedBoundingBox3D bb(
				Point3D(current.X + inset, current.Y + inset, current.Z + inset),
				Point3D(current.X + 1 - inset, current.Y + 1 - inset, current.Z + 1 - inset));
			const auto & precollisionIndex = fvt.GetPreviousCell();
			const ChunkPositionVector prevPos(
				CurrentChunkData->ChunkOffset, precollisionIndex.Cast<double>());
			if (IsSolidTerrainAt(bb)) {
				return TerrainResult(prevPos);
			} else {
				const auto item = FindItemCollision(bb);
				if (item.IsValid())
					return ItemResult(*item, prevPos);
			}
				/*const auto & collisionIndex = fvt.GetCurrentCell();
				const auto & precollisionIndex = fvt.GetPreviousCell();
				return TerrainRaytraceResult(
					found.Type, found.Result,
					ChunkPositionVector(CurrentChunkData->ChunkOffset, precollisionIndex.Cast<double>()));*/
		}

		fvt.Next();
	}

	return NoResult();
}
