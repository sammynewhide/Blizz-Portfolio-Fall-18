#if !defined(HANDMADE_WORLD_H)

struct world_position
{
	int32 ChunkX;
	int32 ChunkY;
	int32 ChunkZ;
    
	// Tile-relativity is measured in meters, NOT PIXELS.. coverted at drawrectangle calls
	// Offsets are also from the center
	v3 Offset;
};

struct world_entity_block
{
	uint32 EntityCount;
	uint32 LowEntityIndex[16];
	world_entity_block *Next;
};

struct world_chunk
{
	int32 ChunkX;
	int32 ChunkY;
	int32 ChunkZ;
    
	world_entity_block FirstBlock;
    
	world_chunk *NextInHash;
};

struct world
{
    v3 ChunkDimInMeters;
    
    world_chunk ChunkHash[4096];
    
    world_entity_block *FirstFree;
};

#define HANDMADE_WORLD_H
#endif