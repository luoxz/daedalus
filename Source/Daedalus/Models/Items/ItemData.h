#pragma once

#include <Models/Terrain/TerrainDataStructures.h>
#include <Utilities/Algebra/Algebra3D.h>

#include <memory>

namespace items {
	enum ItemType {
		I_Chest,
		I_Sofa
	};

	struct ItemRotation {
		Uint8 Yaw, Pitch;
		ItemRotation(const Uint8 yaw, const Uint8 pitch) : Yaw(yaw), Pitch(pitch) {}

		void Bound(const ItemRotation & bound) {
			Yaw %= bound.Yaw;
			Pitch %= bound.Pitch;
		}

		void Add(const ItemRotation & rotation) {
			Yaw += rotation.Yaw;
			Pitch += rotation.Pitch;
		}
	};

	/**
	 * This class specifies the template for each item type. The template is used to initialize
	 * all the default values for a particular item data. This could be useful later when
	 * designing the modding system.
	 */
	struct ItemDataTemplate {
		ItemType Type;
		utils::AxisAlignedBoundingBox3D ItemBounds;  // Bounds of the item in grid cells
		utils::Point3D Pivot;    // Position of pivot in grid cells
		ItemRotation RotationInterval;
		std::string MeshName;
		Uint32 MaxStackSize;     // Maximum number of items possible in an item stack

		ItemDataTemplate(
			const ItemType type,
			const ItemRotation & rotInt,
			const utils::AxisAlignedBoundingBox3D & bounds,
			const utils::Point3D & pivot,
			const std::string & meshName,
			const Uint32 maxStackSize
		) : Type(type),
			RotationInterval(rotInt),
			Pivot(pivot),
			ItemBounds(bounds),
			MeshName(meshName),
			MaxStackSize(maxStackSize)
		{}
	};

	struct ItemDataId {
		Uint64 ItemId;
		terrain::ChunkOffsetVector ChunkOffset;

		ItemDataId(Uint64 id, terrain::ChunkOffsetVector offset) :
			ItemId(id), ChunkOffset(offset)
		{}
	};

	/**
	 * This data structure contains item-specific data. An item is some entity placed in the world
	 * that can be interacted with by players or AI. Items in the inventory should reference
	 * an item data structure.
	 * Items have discrete rotation about the vertical Z axis and the horizontal X axis. Not
	 * all items can be rotated about the X-axis. The interval variables define how many possible
	 * orientations are possible for each rotation axis. Each orientation angle size is equal.
	 */
	struct ItemData {
	private:
		ItemRotation Rotation;
		// Size in grid cells, 1.0 is 1 grid cell, 0.5 is half a grid cell. The reason this exists
		// here as well as in the templates is to allow individually scaled objects.
		utils::AxisAlignedBoundingBox3D ItemBounds;

	public:
		Uint64 ItemId;
		terrain::ChunkPositionVector Position;
		bool bIsPlaced;
		const ItemDataTemplate & Template;

		ItemData(
			const Uint64 itemId,
			const terrain::ChunkPositionVector & position,
			const ItemRotation & rotation,
			const bool isPlaced,
			const ItemDataTemplate & tmp
		) : ItemId(itemId),
			Position(position),
			Rotation(rotation),
			bIsPlaced(isPlaced),
			Template(tmp),
			ItemBounds(tmp.ItemBounds)
		{}

		ItemData(const ItemDataTemplate & tmp) :
			ItemData(0, terrain::ChunkPositionVector(), ItemRotation(0, 0), false, tmp)
		{}

		const ItemRotation & GetRotation() const { return Rotation; }

		void SetRotation(const ItemRotation & rotation) {
			this->Rotation = rotation;
			this->Rotation.Bound(Template.RotationInterval);
		}

		void AddRotation(const ItemRotation & rotation) {
			this->Rotation.Add(rotation);
			this->Rotation.Bound(Template.RotationInterval);
		}
		
		utils::Matrix4D<> GetRotationMatrix() const {
			const double yawV = 360 * (double) Rotation.Yaw / Template.RotationInterval.Yaw;
			const double pitchV = 360 * (double) Rotation.Pitch / Template.RotationInterval.Pitch;

			return
				utils::CreateTranslation(Template.Pivot) *
				utils::CreateRotation(yawV, utils::AXIS_Z) *
				utils::CreateRotation(pitchV, utils::AXIS_X) *
				utils::CreateTranslation(-Template.Pivot);
		}

		utils::Matrix4D<> GetPositionMatrix() const {
			return utils::CreateTranslation(Position.InnerOffset) * GetRotationMatrix();
		}

		utils::OrientedBoundingBox3D GetBoundingBox() const {
			return utils::OrientedBoundingBox3D(ItemBounds, GetPositionMatrix());
		}
	};

	using ItemDataPtr = std::shared_ptr<ItemData>;
}

namespace std {
	template <> struct hash<items::ItemType> {
		size_t operator()(const items::ItemType & tp) const {
			return std::hash<long>()(tp);
		}
	};
}

