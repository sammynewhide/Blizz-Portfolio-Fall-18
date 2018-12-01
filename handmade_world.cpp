#define TILE_CHUNK_SAFE_MARGIN (INT32_MAX / 64)
#define TILE_CHUNK_UNINITIALIZED INT32_MAX

#define TILES_PER_CHUNK 8

inline world_position
NullPosition(void)
{
    world_position Result = {};
    
    Result.ChunkX = TILE_CHUNK_UNINITIALIZED;
    
    return(Result);
}

inline bool32
IsValid(world_position Pos)
{
    bool32 Result = (Pos.ChunkX != TILE_CHUNK_UNINITIALIZED);
    return(Result);
}

inline bool32
IsCanonical(real32 ChunkDim, real32 TileRel)
{
    real32 Epsilon = 0.01f;
    bool32 Result = (TileRel >= -(0.5f * ChunkDim  + Epsilon)) && (TileRel <= (0.5f * ChunkDim  + Epsilon));
    return(Result);
}

inline bool32
IsCanonical(world *World, v3 Offset)
{
    bool32 Result = (IsCanonical(World->ChunkDimInMeters.x, Offset.x) && 
                     IsCanonical(World->ChunkDimInMeters.y, Offset.y) &&
                     IsCanonical(World->ChunkDimInMeters.z, Offset.z));
    return(Result);
}

inline bool32
AreOnSameChunk(world *World, world_position *TestPosA, world_position *TestPosB)
{
    Assert(IsCanonical(World, TestPosA->Offset));
    Assert(IsCanonical(World, TestPosB->Offset));
	bool32 Result = ((TestPosA->ChunkX == TestPosB->ChunkX) &&
					 (TestPosA->ChunkY == TestPosB->ChunkY) &&
					 (TestPosA->ChunkZ == TestPosB->ChunkZ));
    
	return(Result);
}

inline world_chunk *
GetWorldChunk(world *World, int32 ChunkX, int32 ChunkY, int32 ChunkZ, memory_arena *Arena = 0)
{
	Assert(ChunkX > -TILE_CHUNK_SAFE_MARGIN);
	Assert(ChunkY > -TILE_CHUNK_SAFE_MARGIN);
	Assert(ChunkZ > -TILE_CHUNK_SAFE_MARGIN);
	Assert(ChunkX < TILE_CHUNK_SAFE_MARGIN);
	Assert(ChunkY < TILE_CHUNK_SAFE_MARGIN);
	Assert(ChunkZ < TILE_CHUNK_SAFE_MARGIN);
    
	uint32 HashValue = 19*ChunkX + 7*ChunkY + 3*ChunkZ;
	uint32 HashSlot = HashValue & (ArrayCount(World->ChunkHash) - 1);
	Assert(HashSlot < ArrayCount(World->ChunkHash));
    
	world_chunk *Chunk = World->ChunkHash + HashSlot;
    
	do
	{
		if((ChunkX == Chunk->ChunkX) && (ChunkY == Chunk->ChunkY) && (ChunkZ == Chunk->ChunkZ))
		{
			break;
		}
        
		if(Arena && (!Chunk->NextInHash) && (Chunk->ChunkX != TILE_CHUNK_UNINITIALIZED))
		{
			Chunk->NextInHash = PushStruct(Arena, world_chunk);
			Chunk = Chunk->NextInHash;
			Chunk->ChunkX = TILE_CHUNK_UNINITIALIZED;
			
		}
        
		if((Arena && (Chunk->ChunkX == TILE_CHUNK_UNINITIALIZED)))
		{
			Chunk->ChunkX = ChunkX;
			Chunk->ChunkY = ChunkY;
			Chunk->ChunkZ = ChunkZ;
            
			Chunk->NextInHash = 0;
            
			break;
		}
        
		Chunk = Chunk->NextInHash;
	} while(Chunk);
	
	return(Chunk);
}

internal_func void
InitializeWorld(world *World, v3 ChunkDimInMeters)
{
    World->ChunkDimInMeters = ChunkDimInMeters;
    World->FirstFree = 0;
    
	for(uint32 ChunkIndex = 0;
		ChunkIndex < ArrayCount(World->ChunkHash);
		++ChunkIndex)
	{
		World->ChunkHash[ChunkIndex].ChunkX = TILE_CHUNK_UNINITIALIZED;
        World->ChunkHash[ChunkIndex].FirstBlock.EntityCount = 0;
	}
}

inline void
ReCanonicalizeCoordinate(real32 ChunkDim, int32 *Tile, real32 *TileRelative)
{
	// Reference operators allow the position values to be changed directly while having pointers passed in
	
	int32 Offset = RoundReal32ToInt32(*TileRelative / ChunkDim); /* This is how many tiles off the tilerelative from the side
                       ...flooring the division gives us a whole number */
    
	*Tile += Offset;
	*TileRelative -= Offset * ChunkDim; // This readjusts the tilerelative position... 
	
    Assert(IsCanonical(ChunkDim, *TileRelative));
}

inline world_position
MapIntoChunkSpace(world *World, world_position BasePos, v3 Offset)
{
    world_position Result = BasePos;
    
    Result.Offset += Offset;
    ReCanonicalizeCoordinate(World->ChunkDimInMeters.x, &Result.ChunkX, &Result.Offset.x);
    ReCanonicalizeCoordinate(World->ChunkDimInMeters.y, &Result.ChunkY, &Result.Offset.y);
    ReCanonicalizeCoordinate(World->ChunkDimInMeters.z, &Result.ChunkZ, &Result.Offset.z);
    
    return(Result);
}

inline v3
Subtract(world *World, world_position *A, world_position *B)
{
    v3 dTile = {(real32)A->ChunkX - (real32)B->ChunkX, (real32)A->ChunkY - (real32)B->ChunkY, (real32)A->ChunkZ - (real32)B->ChunkZ};	
    
    v3 Result = Hadamard(World->ChunkDimInMeters, dTile) + (A->Offset - B->Offset);
    
    return(Result);
}

inline world_position
CenteredChunkPoint(uint32 ChunkX, uint32 ChunkY, uint32 ChunkZ)
{
    world_position Result = {};
    
    Result.ChunkX = ChunkX;
    Result.ChunkY = ChunkY;
    Result.ChunkZ = ChunkZ;
    
    return(Result);
}

inline world_position
CenteredChunkPoint(world_chunk *Chunk)
{
    world_position Result = CenteredChunkPoint(Chunk->ChunkX, Chunk->ChunkY, Chunk->ChunkZ);

    return(Result);
}

inline void
ChangeEntityLocationRaw(memory_arena *Arena, world *World, uint32 LowEntityIndex, world_position *OldPos, world_position *NewPos)
{
    Assert(!OldPos || IsValid(*OldPos));
    Assert(!NewPos || IsValid(*NewPos));
    
    if(OldPos && NewPos && AreOnSameChunk(World, OldPos, NewPos))
    {
        // Leave entity where it is
    }
    else
    {
        if(OldPos)
        {
            // Pull entity out of its old location
            world_chunk *Chunk = GetWorldChunk(World, OldPos->ChunkX, OldPos->ChunkY, OldPos->ChunkZ);
            Assert(Chunk);
            
            if(Chunk)
            {
                bool32 NotFound = true;
                world_entity_block *FirstBlock = &Chunk->FirstBlock;
                for(world_entity_block *Block = FirstBlock;
                    Block && NotFound;
                    Block = Block->Next)
                {
                    for(uint32 Index = 0;
                        (Index < Block->EntityCount) && NotFound;
                        ++Index)
                    {
                        if(Block->LowEntityIndex[Index] == LowEntityIndex)
                        {
                            Assert(FirstBlock->EntityCount > 0);
                            Block->LowEntityIndex[Index] = FirstBlock->LowEntityIndex[--FirstBlock->EntityCount];
                            if(FirstBlock->EntityCount == 0)
                            {
                                if(FirstBlock->Next)
                                {
                                    world_entity_block *NextBlock = FirstBlock->Next;
                                    *FirstBlock = *NextBlock;
                                    
                                    NextBlock->Next = World->FirstFree;
                                    World->FirstFree = NextBlock;
                                }
                            }
                            
                            NotFound = false;
                        }
                    }
                }
            }
        }
        
        // Inset entity into its new entity block
        if(NewPos)
        {
            world_chunk *Chunk = GetWorldChunk(World, NewPos->ChunkX, NewPos->ChunkY, NewPos->ChunkZ, Arena);
            Assert(Chunk);
            
            world_entity_block *Block = &Chunk->FirstBlock;
            if(Block->EntityCount == ArrayCount(Block->LowEntityIndex))
            {
                world_entity_block *OldBlock = World->FirstFree;
                if(OldBlock)
                {
                    World->FirstFree = OldBlock->Next;
                }
                else
                {
                    OldBlock = PushStruct(Arena, world_entity_block);
                }
                
                *OldBlock = *Block;
                Block->Next = OldBlock;
                Block->EntityCount = 0;
            }
            
            //Assert(Block->EntityCount == ArrayCount(Block->LowEntityIndex));
            Block->LowEntityIndex[Block->EntityCount++] = LowEntityIndex;
        }
    }
}

internal_func void
ChangeEntityLocation(memory_arena *Arena, world *World, uint32 LowEntityIndex, low_entity *LowEntity, world_position NewPInit)
{
    world_position *OldPos = 0;
    world_position *NewPos = 0;
    
    if(!IsSet(&LowEntity->Sim, EntityFlag_Nonspatial) && IsValid(LowEntity->Pos))
    {
        OldPos = &LowEntity->Pos;
    }
    
    if(IsValid(NewPInit))
    {
        NewPos = &NewPInit;
    }
    
    ChangeEntityLocationRaw(Arena, World, LowEntityIndex, OldPos, NewPos);
    
    if(NewPos)
    {
        LowEntity->Pos = *NewPos;
        ClearFlags(&LowEntity->Sim, EntityFlag_Nonspatial);
    }
    else
    {
        LowEntity->Pos = NullPosition();
        AddFlags(&LowEntity->Sim, EntityFlag_Nonspatial);
    }
}