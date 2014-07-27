#include "Daedalus.h"
#include "ChunkManager.h"
#include <Models/Terrain/ChunkLoader.h>

using namespace utils;
using namespace events;

AChunkManager::AChunkManager(
	const class FPostConstructInitializeProperties & PCIP
) : Super(PCIP), RenderDistance(1),
	ItemFactory(NewObject<UItemFactory>(this, UItemFactory::StaticClass()))
{}

void AChunkManager::UpdateChunksAt(const utils::Vector3D<> & playerPosition) {
	terrain::ChunkOffsetVector offset;
	FRotator defaultRotation(0, 0, 0);
	FActorSpawnParameters defaultParameters;
	auto chunkLoader = GetGameState()->ChunkLoader;
	auto genParams = chunkLoader->GetGeneratorParameters();

	defaultParameters.Owner = this;

	// Get player's current chunk location
	auto playerChunkPos = genParams.ToChunkCoordinates(playerPosition);

	Int64 fromX = playerChunkPos.first.X - RenderDistance;
	Int64 fromY = playerChunkPos.first.Y - RenderDistance;
	Int64 fromZ = playerChunkPos.first.Z - RenderDistance;

	Int64 toX = playerChunkPos.first.X + RenderDistance;
	Int64 toY = playerChunkPos.first.Y + RenderDistance;
	Int64 toZ = playerChunkPos.first.Z + RenderDistance;

	// Once the player leaves an area, the chunks are cleared
	for (auto chunkKey = LocalCache.begin(); chunkKey != LocalCache.end(); ) {
		if (chunkKey->first.X > toX || chunkKey->first.Y > toY || chunkKey->first.Z > toZ ||
			chunkKey->first.X < fromX || chunkKey->first.Y < fromY || chunkKey->first.Z < fromZ) {
			chunkKey->second->Destroy();
			chunkKey = LocalCache.erase(chunkKey);
		} else {
			++chunkKey;
		}
	}

	// Begin preloading chunks for the area the player is near
	for (Int64 x = fromX; x <= toX; x++) {
		for (Int64 y = fromY; y <= toY; y++) {
			for (Int64 z = fromZ; z <= toZ; z++) {
				offset.Reset(x, y, z);
				// Chunk is cached already
				if (LocalCache.count(offset) > 0) {
				} else {
					auto data = AChunk::ChunkDataSet();
					for (Uint8 x = 0; x < data.GetWidth(); x++) {
						for (Uint8 y = 0; y < data.GetDepth(); y++) {
							for (Uint8 z = 0; z < data.GetHeight(); z++) {
								data.Set(x, y, z, chunkLoader->GetChunkAt({
									offset.X + x, offset.Y + y, offset.Z + z
								}));
							}
						}
					}
					auto position = utils::ToFVector(genParams.ToRealCoordinates(offset));

					//UE_LOG(LogTemp, Error, TEXT("Placing chunk at %f %f %f"), position.X, position.Y, position.Z)
					AChunk * newChunk = GetWorld()->SpawnActor<AChunk>(
						AChunk::StaticClass(), position, defaultRotation, defaultParameters);
					newChunk->InitializeChunk(genParams, ItemFactory);
					newChunk->SetChunkData(data);
					LocalCache.insert({ offset, newChunk });
				}
			}
		}
	}
}

void AChunkManager::BeginPlay() {
	Super::BeginPlay();
	GetGameState()->EventBus->AddListener(events::E_PlayerPosition, this);
}

void AChunkManager::HandleEvent(const EventDataPtr & data) {
	auto castedData = std::static_pointer_cast<events::EPlayerPosition>(data);
	//UE_LOG(LogTemp, Warning, TEXT("Player position: %f %f %f"), position.X, position.Y, position.Z);
	UpdateChunksAt(castedData->Position);
}
