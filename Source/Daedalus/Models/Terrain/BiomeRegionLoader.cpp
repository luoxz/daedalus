#include <Daedalus.h>
#include "BiomeRegionLoader.h"

#include <Utilities/Hash.h>
#include <Utilities/Noise/Perlin.h>

#include <algorithm>
#include <functional>
#include <random>
#include <cassert>

namespace terrain {
	using namespace utils;
	using namespace events;

	using VertexWithHullIndex = BiomeRegionLoader::VertexWithHullIndex;
	using DelaunayBuilderPtr = BiomeRegionLoader::DelaunayBuilderPtr;
	using UpdatedRegionSet = BiomeRegionLoader::UpdatedRegionSet;
	using BiomeRegionCache = BiomeRegionLoader::BiomeRegionCache;

	BiomeRegionLoader::BiomeRegionLoader(
		const BiomeGeneratorParameters & params,
		EventBusPtr eventBus,
		DelaunayBuilderPtr builder,
		Uint8 fetchRadius
	) : BiomeGenParams(params),
		EventBus(eventBus),
		DelaunayBuilder(builder),
		FetchRadius(fetchRadius)
	{}

	BiomeRegionLoader::~BiomeRegionLoader() {
		LoadedBiomeRegionCache.empty();
	}

	std::shared_ptr<const VertexWithHullIndex> BiomeRegionLoader::GetCornerHullVertex(
		const BiomeRegionData & data,
		const bool cornerX, const bool cornerY
	) const {
		const Int8 dirX = cornerX ? -1 : 1;
		const Int8 dirY = cornerY ? -1 : 1;
		const Uint32 startX = cornerX ? data.GetBiomeGridSize() - 1 : 0;
		const Uint32 startY = cornerY ? data.GetBiomeGridSize() - 1 : 0;
		const Uint16 cornerLimit = BiomeGenParams.BufferSize;
		const VertexWithHullIndex * vertex = NULL;

		Int8 accumX = 0, accumY = 0;
		bool found;

		do {
			found = false;
			const auto & pointIds =
				data.GetBiomeCells().Get(startX + accumX, startY + accumY).PointIds;
			for (size_t i = 0; i < pointIds.size(); i++) {
				auto value = data.DelaunayGraph.ConvexHull.FindVertexById(pointIds[i]);
				if (value != -1) {
					found = true;
					vertex = new VertexWithHullIndex(
						data.GetBiomeAt(pointIds[i])->GetLocalPosition(), value);
				}
			}
			if (accumX * accumX > accumY * accumY)
				accumY += dirY;
			else
				accumX += dirX;
		} while (!found && (accumX < cornerLimit && accumY < cornerLimit));

		if (found)
			return std::shared_ptr<const VertexWithHullIndex>(vertex);
		else
			return NULL;
	};

	bool BiomeRegionLoader::MergeRegionEdge(BiomeRegionData & r1, BiomeRegionData & r2) {
		auto direction = r2.GetBiomeRegionOffset() - r1.GetBiomeRegionOffset();
		auto * region1 = &r1, * region2 = &r2;
		std::shared_ptr<const VertexWithHullIndex> upperLimit1 = NULL;
		std::shared_ptr<const VertexWithHullIndex> upperLimit2 = NULL;
		std::shared_ptr<const VertexWithHullIndex> lowerLimit1 = NULL;
		std::shared_ptr<const VertexWithHullIndex> lowerLimit2 = NULL;

		if ((direction.X == -1 && direction.Y == 0) || (direction.X == 0 && direction.Y == -1)) {
			std::swap(region1, region2);
			direction.X = direction.X * -1;
			direction.Y = direction.Y * -1;
		}

		if (direction.X == 1 && direction.Y == 0) {
			// Merge right
			lowerLimit1 = GetCornerHullVertex(*region1, true, false);
			lowerLimit2 = GetCornerHullVertex(*region2, false, false);
			upperLimit1 = GetCornerHullVertex(*region1, true, true);
			upperLimit2 = GetCornerHullVertex(*region2, false, true);
		} else if (direction.X == 0 && direction.Y == 1) {
			// Merge up
			lowerLimit1 = GetCornerHullVertex(*region1, true, true);
			lowerLimit2 = GetCornerHullVertex(*region2, true, false);
			upperLimit1 = GetCornerHullVertex(*region1, false, true);
			upperLimit2 = GetCornerHullVertex(*region2, false, false);
		}

		// Logging output
		UE_LOG(LogTemp, Warning, TEXT("Merging edges: (%lld %lld) - (%lld %lld)"),
			region1->GetBiomeRegionOffset().X, region1->GetBiomeRegionOffset().Y,
			region2->GetBiomeRegionOffset().X, region2->GetBiomeRegionOffset().Y)
		
		// Merge the 2 regions
		if (upperLimit1 && upperLimit2 && lowerLimit1 && lowerLimit2) {
			DelaunayBuilder->MergeDelaunayTileEdge(
				region1->DelaunayGraph, region2->DelaunayGraph,
				lowerLimit1->second, lowerLimit2->second,
				upperLimit1->second, upperLimit2->second);
			
			// Set the neighbor flags
			region1->NeighboursMerged.Set(direction.X + 1, direction.Y + 1, true);
			region2->NeighboursMerged.Set(-direction.X + 1, -direction.Y + 1, true);

			return true;
		}

		return false;
	}

	bool BiomeRegionLoader::MergeRegionCorner(
		BiomeRegionData & tl, BiomeRegionData & tr,
		BiomeRegionData & bl, BiomeRegionData & br
	) {
		const Uint8 size = 4;
		std::array<BiomeRegionData *, size> data = {{ &tl, &tr, &bl, &br }};
		std::array<std::shared_ptr<const VertexWithHullIndex>, size> vertices = {{
			GetCornerHullVertex(tl, true, false),
			GetCornerHullVertex(tr, false, false),
			GetCornerHullVertex(bl, true, true),
			GetCornerHullVertex(br, false, true)
		}};

		std::array<std::pair<DelaunayGraph *, Uint32>, size> input;

		for (Uint8 i = 0; i < size; i++)
			input[i] = std::make_pair(&data[i]->DelaunayGraph, vertices[i]->second);

		UE_LOG(LogTemp, Warning, TEXT("Merging corners: (%lld %lld) - (%lld %lld) - (%lld %lld) - (%lld %lld)"),
			tl.GetBiomeRegionOffset().X, tl.GetBiomeRegionOffset().Y,
			tr.GetBiomeRegionOffset().X, tr.GetBiomeRegionOffset().Y,
			bl.GetBiomeRegionOffset().X, bl.GetBiomeRegionOffset().Y,
			br.GetBiomeRegionOffset().X, br.GetBiomeRegionOffset().Y)

		DelaunayBuilder->MergeDelaunayTileCorner(input);
		
		// Set the neighbor flags
		tl.NeighboursMerged.Set(2, 0, true);
		tr.NeighboursMerged.Set(0, 0, true);
		bl.NeighboursMerged.Set(2, 2, true);
		br.NeighboursMerged.Set(0, 2, true);

		return false;
	}
	
	void BiomeRegionLoader::MergeRegion(
		UpdatedRegionSet & updatedRegions,
		BiomeRegionDataPtr targetRegion
	) {
		// If this region has already been merged with all surrounding regions, then skip
		if (targetRegion->IsMergedWithAllNeighbours())
			return;

		const BiomeRegionOffsetVector & biomeOffset = targetRegion->GetBiomeRegionOffset();

		// Update neighbor's neighbor generation cache
		TensorFixed2D<BiomeRegionDataPtr, 3> neighbors(NULL);
		BiomeRegionOffsetVector currentOffset;

		// Load up the neighboring biome regions if they haven't already been cached
		// and the sides haven't already been merged
		for (Int8 offY = -1; offY <= 1; offY++) {
			for (Int8 offX = -1; offX <= 1; offX++) {
				if (offY != 0 || offX != 0) {
					currentOffset.Reset(offX + biomeOffset.X, offY + biomeOffset.Y);
					neighbors.Set(offX + 1, offY + 1, GetGeneratedBiomeRegion(currentOffset));
				}
			}
		}
		neighbors.Set(1, 1, targetRegion);

		// Merge shared edges
		for (Int8 offY = -1; offY <= 1; offY++) {
			for (Int8 offX = -1; offX <= 1; offX++) {
				if ((offY == 0) ^ (offX == 0)) { // edge
					// If the side hasn't been merged
					if (!targetRegion->NeighboursMerged.Get(offX + 1, offY + 1)) {
						auto region = neighbors.Get(offX + 1, offY + 1);
						if (region && MergeRegionEdge(*targetRegion, *region))
							updatedRegions.insert(region->GetBiomeRegionOffset());
					}
				}
			}
		}

		// Merge shared corners
		for (Int8 offY = -1; offY <= 0; offY++) {
			for (Int8 offX = -1; offX <= 0; offX++) {
				auto blr = neighbors.Get(offX + 1, offY + 1);
				auto brr = neighbors.Get(offX + 2, offY + 1);
				auto tlr = neighbors.Get(offX + 1, offY + 2);
				auto trr = neighbors.Get(offX + 2, offY + 2);

				// If all the corners exist, and the corner hasn't been merged yet
				if (blr && brr && tlr && trr &&
						!blr->NeighboursMerged.Get(2, 2) &&
						MergeRegionCorner(*tlr, *trr, *blr, *brr)) {
					updatedRegions.insert(blr->GetBiomeRegionOffset());
					updatedRegions.insert(blr->GetBiomeRegionOffset());
					updatedRegions.insert(tlr->GetBiomeRegionOffset());
					updatedRegions.insert(trr->GetBiomeRegionOffset());
				}	
			}
		}
	}

	bool BiomeRegionLoader::IsBiomeRegionGenerated(
		const BiomeRegionOffsetVector & offset
	) const {
		// TODO: implement some form of tracking which biome regions have been generated
		return LoadedBiomeRegionCache.find(offset) != LoadedBiomeRegionCache.end();
	}
	
	BiomeRegionDataPtr BiomeRegionLoader::GetGeneratedBiomeRegion(
		const BiomeRegionOffsetVector & offset
	) {
		auto found = LoadedBiomeRegionCache.find(offset);
		if (found != LoadedBiomeRegionCache.end())
			return found->second;
		else
			return LoadBiomeRegionFromDisk(offset);
	}

	BiomeRegionDataPtr BiomeRegionLoader::LoadBiomeRegionFromDisk(
		const BiomeRegionOffsetVector & offset
	) {
		// TODO: implement disk loading
		if (IsBiomeRegionGenerated(offset))
			return NULL;
		return NULL;
	}

	BiomeRegionDataPtr BiomeRegionLoader::GenerateBiomeRegion(
		const BiomeRegionOffsetVector & biomeOffset
	) {
		// TODO: implement disk saving
		auto dataRef = BiomeRegionDataPtr(
			new BiomeRegionData(
				BiomeGenParams.BufferSize, BiomeGenParams.GridCellCount, biomeOffset));

		// Initialize Mersenne Twister PRNG with seed, this guarantees each region will
		// always have the same sequence of random numbers
		auto mtSeed = Uint32(HashFromVector(BiomeGenParams.Seed, biomeOffset));
		auto randNumPoints = std::bind(
			std::uniform_int_distribution<Uint16>(
				BiomeGenParams.MinPointsPerCell,
				BiomeGenParams.MaxPointsPerCell), std::mt19937(mtSeed));
		auto randPosition = std::bind(
			std::uniform_real_distribution<double>(0.1, 0.9), std::mt19937(mtSeed));

		Uint16 numPoints = 0;
		Vector2D<> point;
		Vector2D<> offset;
		
		// Create uniform random point distribution, and insert vertices into aggregate list
		for (auto y = 0u; y < BiomeGenParams.GridCellCount; y++) {
			for (auto x = 0u; x < BiomeGenParams.GridCellCount; x++) {
				numPoints = randNumPoints();
				offset.Reset(x, y);
				for (auto n = numPoints - 1; n >= 0; n--) {
					// Set point X, Y to random point within cell
					point.Reset(randPosition(), randPosition());
					point = (point + offset) / (double) BiomeGenParams.GridCellCount;
					dataRef->AddBiome(x, y, point);
				}
			}
		}

		// Run Delaunay triangulation algorithm
		dataRef->GenerateDelaunayGraph(*DelaunayBuilder);

		LoadedBiomeRegionCache.insert({ biomeOffset, dataRef });

		return dataRef;
	}

	BiomeRegionDataPtr BiomeRegionLoader::GenerateBiomeRegionArea(
		UpdatedRegionSet & updatedRegions,
		const BiomeRegionOffsetVector & offset,
		const Uint8 radius
	) {
		const Uint8 diameter = radius * 2 + 1;

		// Generate 8 surrounding regions as well as the current region
		BiomeRegionDataPtr newRegion;
		BiomeRegionOffsetVector currentOffset;
		Tensor2D<BiomeRegionDataPtr> loadedRegions(diameter, diameter);

		for (Int64 offY = 0; offY < diameter; offY++) {
			for (Int64 offX = 0; offX < diameter; offX++) {
				currentOffset.Reset(offset.X - radius + offX, offset.Y - radius + offY);
				// Don't generate region if it has already been generated
				auto currentRegion = GetGeneratedBiomeRegion(currentOffset);
				if (!currentRegion)
					currentRegion = GenerateBiomeRegion(currentOffset);
				loadedRegions.Set(offX, offY, currentRegion);
			}
		}
		newRegion = loadedRegions.Get(radius, radius);

		// Run the merge algorithm on all generated regions
		for (Int64 offY = 0; offY < diameter; offY++) {
			for (Int64 offX = 0; offX < diameter; offX++) {
				auto currentRegion = loadedRegions.Get(offX, offY);
				MergeRegion(updatedRegions, currentRegion);
				updatedRegions.insert(currentRegion->GetBiomeRegionOffset());
			}
		}

		return newRegion;
	}

	BiomeRegionDataPtr BiomeRegionLoader::GenerateBiomeDataForRegion(
		UpdatedRegionSet & updatedRegions,
		BiomeRegionDataPtr biomeRegion
	) {
		PerlinNoise2D generator(1234);
		// TODO: implement disk storage
		if (!biomeRegion->IsBiomeDataGenerated()) {
			auto & biomeCells = biomeRegion->GetBiomeCells();
			for (size_t x = 0; x < biomeCells.GetWidth(); x++) {
				for (size_t y = 0; y < biomeCells.GetDepth(); y++) {
					for (auto & id : biomeCells.Get(x, y).PointIds) {
						auto biome = biomeRegion->GetBiomeAt(id);
						auto position = biome->GetLocalPosition();
						auto height = generator.GenerateFractal(
							(position.X + biomeRegion->GetBiomeRegionOffset().X) * 0.7,
							(position.Y + biomeRegion->GetBiomeRegionOffset().Y) * 0.7,
							6, 0.5);
						biome->SetElevation(height);
					}
				}
			}

			biomeRegion->GenerateBiomeData();
			updatedRegions.insert(biomeRegion->GetBiomeRegionOffset());
		}
		return biomeRegion;
	} 

	BiomeRegionDataPtr BiomeRegionLoader::GetBiomeRegionAt(
		const BiomeRegionOffsetVector & offset
	) {
		UpdatedRegionSet updatedRegions;
		//UE_LOG(LogTemp, Error, TEXT("Loading chunk at offset: %d %d %d"), offset.X, offset.Y, offset.Z);
		auto loaded = GetGeneratedBiomeRegion(offset);
		if (!loaded || !loaded->IsMergedWithAllNeighbours()) {
			// Biome region has not been generated yet or its neighbours haven't been
			// generated yet.
			loaded = GenerateBiomeRegionArea(updatedRegions, offset, FetchRadius);
		}

		if (!loaded->IsBiomeDataGenerated())
			GenerateBiomeDataForRegion(updatedRegions, loaded);


		// We will need to fire off an update event since adjacent regions may have
		// been merged.
		if (!updatedRegions.empty()) {
			std::vector<BiomeRegionOffsetVector> updatedVec;
			updatedVec.insert(
				updatedVec.begin(), updatedRegions.cbegin(), updatedRegions.cend());
			EventBus->BroadcastEvent(
				events::EventDataPtr(new events::EBiomeRegionUpdate(updatedVec)));
		}

		return loaded;
	}

	BiomeDataPtr BiomeRegionLoader::GetBiomeAt(const BiomeId & id) {
		return GetBiomeRegionAt(id.first)->GetBiomeAt(id.second);
	}

	const BiomeId BiomeRegionLoader::FindNearestBiomeId(const Vector2D<> point) {
		// Convert into region coordinates
		const auto position = BiomeGenParams.ToBiomeRegionCoordinates(point);
		const auto biomeRegion = GetBiomeRegionAt(position.first);
		const auto foundResults = biomeRegion->FindNearestPoint(position.second);

		const auto & foundGridOffset = std::get<1>(foundResults);
		const Int8 startX = foundGridOffset.X < 2 ? -1 : 0;
		const Int8 startY = foundGridOffset.Y < 2 ? -1 : 0;
		const Int8 endX = foundGridOffset.X >= BiomeGenParams.GridCellCount - 2 ? 1 : 0;
		const Int8 endY = foundGridOffset.Y >= BiomeGenParams.GridCellCount - 2 ? 1 : 0;

		BiomeRegionOffsetVector foundGlobalOffset(position.first);
		Uint64 vid = std::get<0>(foundResults);
		double min = std::get<2>(foundResults);

		// If the found vertex is on the very edge of that particular region, then we must
		// search surrounding regions to ensure we have the nearest possible vertex to
		// the given location.
		for (Int8 x = startX; x <= endX; x++) {
			for (Int8 y = startY; y <= endY; y++) {
				if (x != 0 || y != 0) {
					auto newOffsetVector =
						BiomeRegionOffsetVector(position.first.X + x, position.first.Y + y);
					if (IsBiomeRegionGenerated(newOffsetVector)) {
						auto compare = GetBiomeRegionAt(newOffsetVector)
							->FindNearestPoint({ position.second.X - x, position.second.Y - y });
						auto dist = std::get<2>(compare);
						if (min > dist) {
							min = dist;
							vid = std::get<0>(compare);
							foundGlobalOffset.Reset(newOffsetVector);
						}
					}
				}
			}
		}

		return BiomeId(foundGlobalOffset, vid);
	}

	const BiomeTriangle BiomeRegionLoader::FindContainingBiomeTriangle(
		const Vector2D<> point
	) {
		const auto position = BiomeGenParams.ToBiomeRegionCoordinates(point);
		const auto biomeRegion = GetBiomeRegionAt(position.first);
		const auto foundResults = biomeRegion->FindContainingTriangle(position.second);
		const auto triOpt = std::get<0>(foundResults);

		if (triOpt.IsValid()) {
			const auto & results = *triOpt;
			return BiomeTriangle {
				GetBiomeAt(results[0]),
				GetBiomeAt(results[1]),
				GetBiomeAt(results[2])
			};
		} else {
			// Search neighbours if current graph doesn't contain this point
			const auto searchOffset = std::get<1>(foundResults);
			const Int8 startX = std::min(searchOffset.X, Int8(0));
			const Int8 startY = std::min(searchOffset.Y, Int8(0));
			const Int8 endX = std::max(searchOffset.X, Int8(0));
			const Int8 endY = std::max(searchOffset.Y, Int8(0));

			for (Int8 x = startX; x <= endX; x++) {
				for (Int8 y = startY; y <= endY; y++) {
					if (x != 0 || y != 0) {
						auto newOffsetVector =
							BiomeRegionOffsetVector(position.first.X + x, position.first.Y + y);
						if (IsBiomeRegionGenerated(newOffsetVector)) {

							const auto found = std::get<0>(GetBiomeRegionAt(newOffsetVector)
								->FindContainingTriangle({
									position.second.X - x,
									position.second.Y - y
								}));
							if (found.IsValid()) {
								const auto & results = *found;
								return BiomeTriangle {
									GetBiomeAt(results[0]),
									GetBiomeAt(results[1]),
									GetBiomeAt(results[2])
								};
							}
						}
					}
				}
			}

			assert(!"BiomeRegionLoader::FindContainingBiomeTriangle: Point should always resolve to a valid Delaunay triangle in either the current region or a neighbouring region, but it does not.");
			throw StringException("BiomeRegionLoader::FindContainingBiomeTriangle: Point should always resolve to a valid Delaunay triangle in either the current region or a neighbouring region, but it does not.");
		}
	}
}
