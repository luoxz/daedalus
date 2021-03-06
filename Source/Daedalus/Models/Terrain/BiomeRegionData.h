#pragma once

#include "TerrainDataStructures.h"
#include <Utilities/Graph/Delaunay.h>
#include <Utilities/DataStructures.h>

#include <array>
#include <vector>
#include <memory>
#include <unordered_map>

namespace terrain {
	typedef std::vector<Uint64> PointIds;

	/**
	 * This class represents the actual biome itself along with along data associated with the
	 * biome such as elevation and rainfall, etc. Thousands of biomes of contained within each
	 * biome region. A biome is located at each point of the Delaunay triangulation and the
	 * boundaries are found by taking the Voronoi dual of the triangulation.
	 */
	class BiomeData {
	public:
		enum TerrainTypeId {
			T_PLAINS,
			T_MOUNTAINS
		};

		enum VegetationTypeId {
			V_BARREN,
			V_GRASSY,
			V_FOREST
		};

	private:
		BiomeId GlobalId;
		BiomePositionVector GlobalPosition;
		double Elevation;                        // Average biome elevation in metres
		double Rainfall;                         // Average rainfall in millimetres
		TerrainTypeId TerrainType;              // Topology of terrain
		VegetationTypeId VegetationType;        // General vegetation within terrain

	public:
		BiomeData(const BiomeId & gid, const BiomePositionVector & position) :
			GlobalId(gid),
			GlobalPosition(position),
			Elevation(0), Rainfall(0),
			TerrainType(T_PLAINS), VegetationType(V_BARREN)
		{}
		
		const BiomePositionVector & GetGlobalPosition() const { return GlobalPosition; }
		const utils::Vector2D<> & GetLocalPosition() const { return GlobalPosition.second; }
		const BiomeId & GlobalBiomeId() const { return GlobalId; }
		
		const double GetElevation() const { return Elevation; }
		const double GetRainfall() const { return Rainfall; }
		void SetElevation(const double elevation) { Elevation = elevation; }
		void SetRainfall(const double rainfall) { Rainfall = rainfall; }
	};

	typedef std::unordered_map<Uint64, std::unique_ptr<BiomeData>> BiomeDataMap;

	/**
	 * Each biome cell contains a list of 2D points that will be used in a Delaunay
	 * triangulation. Because we need to merge Delaunay triangulations in each region,
	 * we need a buffer border around each region that isn't finalized until that buffer
	 * border has been used in an adjacent region merge.
	 */
	struct BiomeCellData {
		PointIds PointIds;

		BiomeCellData() {}

		~BiomeCellData() {}

		inline void AddPoint(const Uint64 id) { PointIds.push_back(id); }
	};

	using BiomeCellField = utils::Tensor2D<BiomeCellData>;
	using BiomeDataPtr = BiomeData *;

	/**
	 * Biomes are generated on a 2D plane separate from the terrain generation. Biome shapes
	 * are created using a tiled Delaunay triangulation. Each biome region is generated
	 * at a larger scale than terrain chunks and will be subdivided evenly into many biome
	 * cells. Each biome cell contains a least 1 Delaunay point for more even triangulations.
	 */
	class BiomeRegionData {
	public:
		using BiomeTriangleIds = std::array<BiomeId, 3>;
		using NearestBiomeResult =
			std::tuple<Uint64, BiomeRegionGridIndexVector, double, utils::Vector2D<Int8>>;

		using ContainingTriangleResult =
			std::tuple<utils::Option<BiomeTriangleIds>, utils::Vector2D<Int8>>;

	private:
		bool bIsGraphGenerated;
		bool bIsBiomeDataGenerated;

		Uint64 CurrentVertexId;            // Temporary variable that tracks the available IDs
		BiomeDataMap Biomes;               // Individual biomes mapped by an unique ID
		BiomeCellField BiomeCells;         // Subdivision of the region into cells that contain
		                                   // a number of individual biomes

		Uint32 BiomeGridSize;                     // Size of the biome in grid cells
		BiomeRegionOffsetVector BiomeOffset;        // Biome offset from (0,0)

		inline Uint64 GetNextId() { return CurrentVertexId++; }
		inline const BiomeId BuildId(Uint64 localId) {
			return BiomeId(BiomeOffset, localId);
		}

	public:
		utils::DelaunayGraph DelaunayGraph;

		// Indicates whether the 8 neighboring regions have been generated yet, (0, 0) is
		// bottom left, (2, 2) is top right.
		utils::Tensor2D<bool> NeighboursMerged;

		BiomeRegionData(
			const Uint16 buffer,
			const Uint32 biomeSize,
			const BiomeRegionOffsetVector & biomeOffset);

		~BiomeRegionData() {}


		inline BiomeDataPtr GetBiomeAt(const Uint64 localBiomeId) {
			return Biomes.at(localBiomeId).get();
		}
		inline const BiomeDataPtr GetBiomeAt(const Uint64 localBiomeId) const {
			return Biomes.at(localBiomeId).get();
		}
		inline const BiomeRegionOffsetVector & GetBiomeRegionOffset() const {
			return BiomeOffset;
		}
		inline utils::Vector2D<Int64> ToGridCoordinates(const utils::Vector2D<> & point) const {
			return {
				(Int64) std::floor(point.X * BiomeGridSize),
				(Int64) std::floor(point.Y * BiomeGridSize)
			};
		}
		inline const Uint32 & GetBiomeGridSize() const { return BiomeGridSize; }
		inline const BiomeCellField & GetBiomeCells() const { return BiomeCells; }
		inline BiomeCellField & GetBiomeCells() { return BiomeCells; }

		inline bool IsGraphGenerated() const { return bIsGraphGenerated; }
		inline bool IsBiomeDataGenerated() const { return bIsBiomeDataGenerated; }



		Uint64 AddBiome(const Uint32 x, const Uint32 y, const utils::Vector2D<> & position);
		bool IsMergedWithAllNeighbours() const;

		void GenerateDelaunayGraph(const utils::DelaunayBuilderDAC2D & builder);
		void GenerateBiomeData();

		const NearestBiomeResult FindNearestPoint(const utils::Vector2D<> & position) const;
		const ContainingTriangleResult FindContainingTriangle(
			const utils::Vector2D<> & position) const;
	};

	using BiomeRegionDataPtr = BiomeRegionData *;
}
