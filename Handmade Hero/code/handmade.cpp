#include "handmade.h"
#include "handmade_render_group.h"
#include "handmade_render_group.cpp"
#include "handmade_world.cpp"
#include "handmade_random.h"
#include "handmade_sim_region.cpp"
#include "handmade_entity.cpp"

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef uint64_t uint64;

typedef float real32;
typedef double real64;

#define internal_func static
#define local_persist static
#define global_variable static

internal_func void
GameOutputSound(game_state *GameState, game_sound_output_buffer *SoundBuffer, int ToneHz)
{
	int16 ToneVolume = 3000;
	int WavePeriod = SoundBuffer->SamplesPerSecond / ToneHz;
    
	int16 *SampleOut = SoundBuffer->Samples;
    
	for(int16 SampleIndex = 0;
        SampleIndex < SoundBuffer->SampleCount;
        ++SampleIndex)
	{
#if 0
		real32 SineValue = sinf(GameState->tSine);
		int16 SampleValue = (int16)(SineValue * ToneVolume);
#else
		int16 SampleValue = 0;
#endif
        
		if(SampleIndex > 30000)
		{
			break;
		}
        
		*SampleOut++ = SampleValue;
		*SampleOut++ = SampleValue;
        
#if 0
		GameState->tSine += 2.0f * Pi32 * 1.0f / (real32)WavePeriod;
		if(GameState->tSine > 2.0f * Pi32)
		{
			GameState->tSine -= 2.0f * Pi32;
		}
#endif
	}
}

#pragma pack(push, 1)
struct bitmap_header
{
    uint16 FileType;
    uint32 FileSize;
    uint16 Reserved1;
    uint16 Reserved2;		// The pragma packs allows the struct variables to be next to each other without padding
    uint32 BitmapOffset;	// so that the struct can be aligned with the actual header of the file.
    uint32 Size;			// Pragma push and pop allows the compiler to go back to the default packing mode
    int32 Width;			// without having to define what the default packing mode was. Janky
    int32 Height;		
    uint16 Planes;		
    uint16 BitsPerPixel;
    uint32 Compression;
    uint32 SizeOfBitmap;
    int32 HorzResolution;
    int32 VertResolution;
    uint32 ColorsUsed;
    uint32 ColorsImportant;
    
    uint32 RedMask;		// The bitmaps we use are compressed with bitfield encoding... essentially masking and moving around RGB values
    uint32 GreenMask;
    uint32 BlueMask;
};
#pragma pack(pop)

internal_func loaded_bitmap
DEBUGLoadBMP(thread_context *Thread, debug_platform_read_entire_file *ReadEntireFile, char *FileName)
{
    loaded_bitmap Result = {};
    
    debug_read_file_result ReadResult = ReadEntireFile(Thread, FileName);
    if(ReadResult.ContentsSize != 0)
    {
        bitmap_header *Header = (bitmap_header *)ReadResult.Contents; // Sick cold cast	
        uint32 *Pixels = (uint32 *)((uint8 *)ReadResult.Contents + Header->BitmapOffset); // Gives actual pointer to the pixel data
        Result.Memory = Pixels;
        Result.Width = Header->Width;
        Result.Height = Header->Height;
        
        Assert(Header->Compression == 3);
        
        // Byte order for colors is determind by the color masks in the header
        uint32 RedMask = Header->RedMask;
        uint32 BlueMask = Header->BlueMask;
        uint32 GreenMask = Header->GreenMask;
        uint32 AlphaMask = ~(RedMask | GreenMask | BlueMask); // The alpha mask is where the other masks aren't
        
        bit_scan_result RedScan = FindLeastSignificantSetBit(RedMask);
        bit_scan_result GreenScan = FindLeastSignificantSetBit(GreenMask);
        bit_scan_result BlueScan = FindLeastSignificantSetBit(BlueMask);
        bit_scan_result AlphaScan = FindLeastSignificantSetBit(AlphaMask);
        
        Assert(RedScan.Found);
        Assert(GreenScan.Found);
        Assert(BlueScan.Found);
        Assert(AlphaScan.Found);
        
        int32 RedShiftDown = (int32)RedScan.Index;
        int32 GreenShiftDown = (int32)GreenScan.Index;
        int32 BlueShiftDown = (int32)BlueScan.Index;
        int32 AlphaShiftDown = (int32)AlphaScan.Index;
        
        uint32 *SourceDest = Pixels;
        for(int32 Y = 0;
            Y < Header->Height;
            ++Y)
        {
            for(int32 X = 0;
                X < Header->Width;
                ++X)
            {
                uint32 C = *SourceDest;
                
                real32 R = (real32)((C & RedMask) >> RedShiftDown);
                real32 G = (real32)((C & GreenMask) >> GreenShiftDown);
                real32 B = (real32)((C & BlueMask) >> BlueShiftDown);
                real32 A = (real32)((C & AlphaMask) >> AlphaShiftDown);
                real32 AN = (A / 255.0f);
                
                R = R*AN;
                G = G*AN;
                B = B*AN;
                
                *SourceDest++ = ((uint32)(A + 0.5f) << 24 |
                                 ((uint32)(R + 0.5f) << 16) |
                                 ((uint32)(G + 0.5f) << 8)  |
                                 ((uint32)(B + 0.5f) << 0));
            }
        }
    }
    
    Result.Pitch = -Result.Width*BITMAP_BYTES_PER_PIXEL;
    Result.Memory = (uint8 *)Result.Memory - Result.Pitch*(Result.Height - 1);
    
    return(Result);
}

struct add_low_entity_result
{
    low_entity *Low;
    uint32 LowIndex;
};

internal_func add_low_entity_result
AddLowEntity(game_state *GameState, entity_type Type, world_position Pos)
{
    Assert(GameState->LowEntityCount < ArrayCount(GameState->LowEntities));
    uint32 EntityIndex = GameState->LowEntityCount++;
    
    low_entity *LowEntity = GameState->LowEntities + EntityIndex;
    *LowEntity = {};
    LowEntity->Sim.Type = Type;
    LowEntity->Sim.Collision = GameState->NullCollision;
    LowEntity->Pos = NullPosition();
    
    ChangeEntityLocation(&GameState->WorldArena, GameState->World, EntityIndex, LowEntity, Pos);
    
    add_low_entity_result Result = {};
    Result.Low = LowEntity;
    Result.LowIndex = EntityIndex;
    
    return(Result);
}

inline world_position
ChunkPositionFromTilePosition(world *World, int32 AbsTileX, int32 AbsTileY, int32 AbsTileZ, 
                              v3 AdditionalOffset = v3{0, 0, 0})
{
    world_position BasePos = {};

    real32 TileSideInMeters = 1.4f;
    real32 TileDepthInMeters = 3.0f;
    
    v3 TileDim = v3{TileSideInMeters, TileSideInMeters, TileDepthInMeters};
    v3 Offset = Hadamard(TileDim, v3{(real32)AbsTileX, (real32)AbsTileY, (real32)AbsTileZ});
    world_position Result = MapIntoChunkSpace(World, BasePos, AdditionalOffset + Offset);
    
    Assert(IsCanonical(World, Result.Offset));
    
    return(Result);
}

internal_func add_low_entity_result
AddGroundedEntity(game_state *GameState, entity_type Type, world_position Pos, 
                  sim_entity_collision_volume_group *Collision)
{
    add_low_entity_result Entity = AddLowEntity(GameState, Type, Pos);
    Entity.Low->Sim.Collision = Collision;
    return(Entity);
}

internal_func void
InitHitPoints(low_entity *LowEntity, uint32 HitPointCount)
{
    Assert(HitPointCount <= ArrayCount(LowEntity->Sim.HitPoint));
    LowEntity->Sim.HitPointMax = HitPointCount;
    for(uint32 HitPointIndex = 0;
        HitPointIndex < LowEntity->Sim.HitPointMax;
        ++HitPointIndex)
    {
        hit_point *HitPoint = LowEntity->Sim.HitPoint + HitPointIndex;
        HitPoint->Flags = 0;
        HitPoint->FilledAmount = HIT_POINT_SUB_COUNT;
    }
}

internal_func add_low_entity_result
AddSword(game_state *GameState)
{
    add_low_entity_result Entity = AddLowEntity(GameState, EntityType_Sword, NullPosition());
    
    Entity.Low->Sim.Collision = GameState->SwordCollision;
    
    AddFlags(&Entity.Low->Sim, EntityFlag_Moveable);
    
    return(Entity);
}

internal_func add_low_entity_result
AddPlayer(game_state *GameState)
{
    world_position Pos = GameState->CameraPos;
    add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Hero, Pos, GameState->PlayerCollision);
    
    AddFlags(&Entity.Low->Sim, EntityFlag_Collides | EntityFlag_Moveable);
    
    InitHitPoints(Entity.Low, 3);
    
    add_low_entity_result Sword = AddSword(GameState);
    Entity.Low->Sim.Sword.Index = Sword.LowIndex;
    
    if(GameState->CameraFollowingEntityIndex == 0)
    {
        GameState->CameraFollowingEntityIndex = Entity.LowIndex;
    }
    
    return(Entity);
}

internal_func add_low_entity_result
AddMonstar(game_state *GameState, uint32 AbsTileX, uint32 AbsTileY, uint32 AbsTileZ)
{
    world_position Pos = ChunkPositionFromTilePosition(GameState->World, AbsTileX, AbsTileY, AbsTileZ);
    add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Monstar, Pos, GameState->MonstarCollision);
    
    AddFlags(&Entity.Low->Sim, EntityFlag_Collides | EntityFlag_Moveable);
    
    InitHitPoints(Entity.Low, 3);
    
    return(Entity);
}

internal_func add_low_entity_result
AddFamiliar(game_state *GameState, uint32 AbsTileX, uint32 AbsTileY, uint32 AbsTileZ)
{
    world_position Pos = ChunkPositionFromTilePosition(GameState->World, AbsTileX, AbsTileY, AbsTileZ);
    add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Familiar, Pos, GameState->FamiliarCollision);
    
    AddFlags(&Entity.Low->Sim, EntityFlag_Collides | EntityFlag_Moveable);
    
    return(Entity);
}

internal_func add_low_entity_result
AddStandardRoom(game_state *GameState, uint32 AbsTileX, uint32 AbsTileY, uint32 AbsTileZ)
{
    world_position Pos = ChunkPositionFromTilePosition(GameState->World, AbsTileX, AbsTileY, AbsTileZ);
    add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Space, Pos, GameState->StandardRoomCollision);
    
    AddFlags(&Entity.Low->Sim, EntityFlag_Traversable);
    
    return(Entity);
}

internal_func add_low_entity_result
AddWall(game_state *GameState, uint32 AbsTileX, uint32 AbsTileY, uint32 AbsTileZ)
{
    world_position Pos = ChunkPositionFromTilePosition(GameState->World, AbsTileX, AbsTileY, AbsTileZ);
    add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Wall, Pos, GameState->WallCollision);
    
    AddFlags(&Entity.Low->Sim, EntityFlag_Collides);
    
    return(Entity);
}

internal_func add_low_entity_result
AddStair(game_state *GameState, uint32 AbsTileX, uint32 AbsTileY, uint32 AbsTileZ)
{
    world_position Pos = ChunkPositionFromTilePosition(GameState->World, AbsTileX, AbsTileY, AbsTileZ);
    add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Stairwell, Pos, GameState->StairwellCollision);
    
    AddFlags(&Entity.Low->Sim, EntityFlag_Collides);
    Entity.Low->Sim.WalkableDim = Entity.Low->Sim.Collision->TotalVolume.Dim.xy;
    Entity.Low->Sim.WalkableHeight = GameState->BaselineFloorHeight;
    
    return(Entity);
}

internal_func void
DrawHitPoints(sim_entity *Entity, render_group *RenderGroup)
{
    if(Entity->HitPointMax >= 1)
    {
        v2 HealthDim = {0.2f, 0.2f};
        real32 SpacingX = 1.5f*HealthDim.x;
        v2 HitPos = {-0.5f*(Entity->HitPointMax - 1)*SpacingX, -0.25f};
        v2 dHitPos = {SpacingX, 0.0f};
        for(uint32 HealthIndex = 0;
            HealthIndex < Entity->HitPointMax;
            ++HealthIndex)
        {
            hit_point *HitPoint = Entity->HitPoint + HealthIndex;
            v4 Color = {1.0f, 0.0f, 0.0f, 1.0f}; 
            if(HitPoint->FilledAmount == 0)
            {
                Color = v4{0.2f, 0.2f, 0.2f, 1.0f};
            }
            
            PushRect(RenderGroup, HitPos, 0, HealthDim, Color, 0.0f);
            HitPos += dHitPos;
        }
    }
}

internal_func void
ClearCollisionRulesFor(game_state *GameState, uint32 StorageIndex)
{
    for(uint32 HashBucket = 0;
        HashBucket < ArrayCount(GameState->CollisionRuleHash);
        ++HashBucket)
    {
        for(pairwise_collision_rule **Rule = &GameState->CollisionRuleHash[HashBucket];
            *Rule;
            ) // Interesting
        {
            if(((*Rule)->StorageIndexA == StorageIndex) || 
               ((*Rule)->StorageIndexB == StorageIndex))
            {
                pairwise_collision_rule *RemovedRule = *Rule;
                *Rule = (*Rule)->NextInHash;
                
                RemovedRule->NextInHash = GameState->FirstFreeCollisionRule;
                GameState->FirstFreeCollisionRule = RemovedRule;
            }
            else
            {
                Rule = &(*Rule)->NextInHash;
            }
        }
    }
}

internal_func void
AddCollisionRule(game_state *GameState, uint32 StorageIndexA, uint32 StorageIndexB, bool32 CanCollide)
{
    if(StorageIndexA > StorageIndexB)
    {
        uint32 Temp = StorageIndexA;
        StorageIndexA = StorageIndexB;
        StorageIndexB = Temp;
    }
    
    pairwise_collision_rule *Found = 0;
    uint32 HashBucket = StorageIndexA & (ArrayCount(GameState->CollisionRuleHash) - 1);
    for(pairwise_collision_rule *Rule = GameState->CollisionRuleHash[HashBucket];
        Rule;
        Rule = Rule->NextInHash)
    {
        if((Rule->StorageIndexA == StorageIndexA) &&
           (Rule->StorageIndexB == StorageIndexB))
        {
            Found = Rule;
            break;
        }
    }
    
    if(!Found)
    {
        Found = GameState->FirstFreeCollisionRule;
        if(Found)
        {
            GameState->FirstFreeCollisionRule = Found->NextInHash;
        }
        else
        {
            Found = PushStruct(&GameState->WorldArena, pairwise_collision_rule);
        }
        Found->NextInHash = GameState->CollisionRuleHash[HashBucket];
        GameState->CollisionRuleHash[HashBucket] = Found;
    }
    
    if(Found)
    {
        Found->StorageIndexA = StorageIndexA;
        Found->StorageIndexB = StorageIndexB;
        Found->CanCollide = CanCollide;
    }
}

sim_entity_collision_volume_group *
MakeSimpleGroundedCollision(game_state *GameState, real32 DimX, real32 DimY, real32 DimZ)
{
    sim_entity_collision_volume_group *Group = PushStruct(&GameState->WorldArena, sim_entity_collision_volume_group);
    Group->VolumeCount = 1;
    Group->Volumes = PushArray(&GameState->WorldArena, Group->VolumeCount, sim_entity_collision_volume);
    Group->TotalVolume.OffsetPos = v3{0, 0, 0.5f*DimZ};
    Group->TotalVolume.Dim = v3{DimX, DimY, DimZ};
    Group->Volumes[0] = Group->TotalVolume;
    
    return(Group);
}

sim_entity_collision_volume_group *
MakeNullCollision(game_state *GameState)
{
    sim_entity_collision_volume_group *Group = PushStruct(&GameState->WorldArena, sim_entity_collision_volume_group);
    Group->VolumeCount = 0;
    Group->Volumes = 0;
    Group->TotalVolume.OffsetPos = v3{0, 0, 0};
    Group->TotalVolume.Dim = v3{0, 0, 0};
    
    return(Group);
}

internal_func void
FillGroundChunk(transient_state *TranState, game_state *GameState, ground_buffer *GroundBuffer, world_position *ChunkPos)
{
    temporary_memory GroundMemory = BeginTemporaryMemory(&TranState->TranArena);
    render_group *GroundRenderGroup = AllocateRenderGroup(&TranState->TranArena, Megabytes(4), 1);

    Clear(GroundRenderGroup, v4{1.0f, 1.0f, 0.0f, 1.0f});

    loaded_bitmap *Buffer = &GroundBuffer->Bitmap;  
    
    GroundBuffer->Pos = *ChunkPos;

    DrawRectangle(Buffer, v2{0.0f, 0.0f}, v2{(real32)Buffer->Width, (real32)Buffer->Height}, 0.0f, 0.0f, 0.0f);
    
    real32 Width = (real32)Buffer->Width;
    real32 Height = (real32)Buffer->Height;

    for(int32 ChunkOffsetY = -1;
        ChunkOffsetY <= 1;
        ++ChunkOffsetY)
    {
        for(int32 ChunkOffsetX = -1;
            ChunkOffsetX <= 1;
            ++ChunkOffsetX)
        {
            int32 ChunkX = ChunkPos->ChunkX + ChunkOffsetX;
            int32 ChunkY = ChunkPos->ChunkY + ChunkOffsetY;
            int32 ChunkZ = ChunkPos->ChunkZ;

            random_series Series = RandomSeed(213*ChunkX + 643*ChunkY + 119*ChunkZ);

            v2 Center = v2{ChunkOffsetX*Width, -ChunkOffsetY*Height};

            for(uint32 GrassIndex = 0;
                GrassIndex < 100;
                ++GrassIndex)
            {
                loaded_bitmap *Stamp;
                if(RandomChoice(&Series, 2))
                {
                    Stamp = GameState->Grass + RandomChoice(&Series, ArrayCount(GameState->Grass));
                }
                else
                {
                    Stamp = GameState->Stone + RandomChoice(&Series, ArrayCount(GameState->Stone));
                }

                v2 BitmapCenter = 0.5f*V2i(Stamp->Width, Stamp->Width);
                v2 Offset = {Width*RandomUnilateral(&Series), Height*RandomUnilateral(&Series)};

                v2 Pos = Center + Offset - BitmapCenter;

                PushBitmap(GroundRenderGroup, Stamp, Pos, 0.0f, v2{0.0f, 0.0f});
            }
        }
    }

    for(int32 ChunkOffsetY = -1;
        ChunkOffsetY <= 1;
        ++ChunkOffsetY)
    {
        for(int32 ChunkOffsetX = -1;
            ChunkOffsetX <= 1;
            ++ChunkOffsetX)
        {
            int32 ChunkX = ChunkPos->ChunkX + ChunkOffsetX;
            int32 ChunkY = ChunkPos->ChunkY + ChunkOffsetY;
            int32 ChunkZ = ChunkPos->ChunkZ;

            random_series Series = RandomSeed(213*ChunkX + 643*ChunkY + 119*ChunkZ);

            v2 Center = v2{ChunkOffsetX*Width, -ChunkOffsetY*Height};
        
            for(uint32 GrassIndex = 0;
                GrassIndex < 50;
                ++GrassIndex)
            {
                loaded_bitmap *Stamp = GameState->Tuft + RandomChoice(&Series, ArrayCount(GameState->Tuft));

                v2 BitmapCenter = 0.5f*V2i(Stamp->Width, Stamp->Width);
                v2 Offset = {Width*RandomUnilateral(&Series), Height*RandomUnilateral(&Series)};

                v2 Pos = Center + Offset - BitmapCenter;

                PushBitmap(GroundRenderGroup, Stamp, Pos, 0.0f, v2{0.0f, 0.0f});
            }
        
        }
    }

    RenderGroupToOutput(GroundRenderGroup, Buffer);
    EndTemporaryMemory(GroundMemory);
}

internal_func void
ClearBitmap(loaded_bitmap *Bitmap)
{
    if(Bitmap->Memory)
    {
        int32 TotalBitmapSize = Bitmap->Width*Bitmap->Height*BITMAP_BYTES_PER_PIXEL;
        ZeroSize(TotalBitmapSize, Bitmap->Memory);
    }
}

internal_func loaded_bitmap
MakeEmptyBitmap(memory_arena *Arena, int32 Width, int32 Height, bool32 ClearToZero = true)
{
    loaded_bitmap Result = {};
    
    Result.Width = Width;
    Result.Height = Height;
    Result.Pitch = Result.Width*BITMAP_BYTES_PER_PIXEL;
    int32 TotalBitmapSize = Width*Height*BITMAP_BYTES_PER_PIXEL;
    Result.Memory = PushSize_(Arena, TotalBitmapSize);
    if(ClearToZero)
    {
        ClearBitmap(&Result);
    }

    return(Result);
}

#if 0
internal_func void
RequestGroundBuffers(world_position CenterPos, rectangle3 Bounds)
{
    Bounds = Offset(Bounds, CenterPos.Offset);
    CenterPos.Offset = v3{0, 0, 0};

    for(int32 )
    {

    }

    FillGroundChunk(TransState, GameState, TransState->GroudnBuffer, &GameState->CameraPos);
}
#endif

extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    Assert((&Input->Controllers[0].Terminator - &Input->Controllers[0].Buttons[0]) == (ArrayCount(Input->Controllers[0].Buttons)));
    
    uint32 GroundBufferWidth = 256;
    uint32 GroundBufferHeight = 256;

    Assert(sizeof(game_state) <= Memory->PermanentStorageSize);
    game_state *GameState = (game_state *)Memory->PermanentStorage;
    if (!Memory->IsInitialized)
    {
        uint32 TilesPerWidth = 17;
        uint32 TilesPerHeight = 9;

        GameState->BaselineFloorHeight = 3.0f;
        GameState->MetersToPixels = 42.0f;
        GameState->PixelsToMeters = 1.0f / GameState->MetersToPixels;

        v3 WorldChunkDimInMeters = {GameState->PixelsToMeters*(real32)GroundBufferWidth,
                                    GameState->PixelsToMeters*(real32)GroundBufferHeight,
                                    GameState->BaselineFloorHeight};

        InitializeArena(&GameState->WorldArena, (memory_index)(Memory->PermanentStorageSize - sizeof(game_state)),
                        (uint8 *)Memory->PermanentStorage + sizeof(game_state));
        
        AddLowEntity(GameState, EntityType_Null, NullPosition()); // Reserve entity slot 0 for a null entity
        
        GameState->World = PushStruct(&GameState->WorldArena, world);
        world *World = GameState->World;
        InitializeWorld(World, WorldChunkDimInMeters);
        
        real32 TileSideInMeters = 1.4f;
        real32 TileDepthInMeters = GameState->BaselineFloorHeight;

        GameState->NullCollision = MakeNullCollision(GameState);
        GameState->SwordCollision = MakeSimpleGroundedCollision(GameState, 1.0f, 0.5f, 0.1f);
        GameState->StairwellCollision = MakeSimpleGroundedCollision(GameState,
                                                                    TileSideInMeters,
                                                                    2.0f*TileSideInMeters,
                                                                    1.1f*TileDepthInMeters);
        GameState->PlayerCollision = MakeSimpleGroundedCollision(GameState, 1.0f, 0.5f, 1.2f);
        GameState->MonstarCollision = MakeSimpleGroundedCollision(GameState, 1.0f, 0.5f, 0.5f);
        GameState->FamiliarCollision = MakeSimpleGroundedCollision(GameState, 1.0f, 0.5f, 0.5f);
        GameState->WallCollision = MakeSimpleGroundedCollision(GameState,
                                                               TileSideInMeters,
                                                               TileSideInMeters,
                                                               TileDepthInMeters);
        GameState->StandardRoomCollision = MakeSimpleGroundedCollision(GameState,
                                                                       TilesPerWidth*TileSideInMeters,
                                                                       TilesPerHeight*TileSideInMeters,
                                                                       0.9f*TileDepthInMeters);
        
        GameState->Grass[0] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/grass00.bmp");
        GameState->Grass[1] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/grass01.bmp");
        
        GameState->Tuft[0] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/tuft00.bmp");
        GameState->Tuft[1] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/tuft01.bmp");
        GameState->Tuft[2] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/tuft02.bmp");
        
        GameState->Stone[0] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/ground00.bmp");
        GameState->Stone[1] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/ground01.bmp");
        GameState->Stone[2] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/ground02.bmp");
        GameState->Stone[3] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/ground03.bmp");
        
        GameState->Backdrop = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_background.bmp");
        GameState->Shadow = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_shadow.bmp");
        GameState->Tree = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/tree00.bmp");
        GameState->Sword = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/rock03.bmp");
        GameState->Stairwell = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/rock02.bmp");
        
        // This ++bitmap command is interesting
        hero_bitmaps *Bitmap;
        
        Bitmap = GameState->HeroBitmaps;
        Bitmap->Head = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_right_head.bmp");
        Bitmap->Cape = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_right_cape.bmp");
        Bitmap->Torso = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_right_torso.bmp");
        Bitmap->Align = v2{72, 182};
        ++Bitmap;
        
        Bitmap->Head = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_back_head.bmp");
        Bitmap->Cape = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_back_cape.bmp");
        Bitmap->Torso = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_back_torso.bmp");
        Bitmap->Align = v2{72, 182};
        ++Bitmap;
        
        Bitmap->Head = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_left_head.bmp");
        Bitmap->Cape = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_left_cape.bmp");
        Bitmap->Torso = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_left_torso.bmp");
        Bitmap->Align = v2{72, 182};
        ++Bitmap;
        
        Bitmap->Head = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_front_head.bmp");
        Bitmap->Cape = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_front_cape.bmp");
        Bitmap->Torso = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_front_torso.bmp");
        Bitmap->Align = v2{72, 182};
        ++Bitmap;
        
        random_series Series = {1234};
        // This is where the tile map is generated!!!
        uint32 ScreenBaseX = 0;
        uint32 ScreenBaseY = 0;
        uint32 ScreenBaseZ = 0;
        uint32 ScreenX = ScreenBaseX;
        uint32 ScreenY = ScreenBaseY;
        uint32 AbsTileZ = ScreenBaseZ;
        
        bool32 DoorLeft = false;
        bool32 DoorRight = false;
        bool32 DoorTop = false;
        bool32 DoorBottom = false;
        bool32 DoorUp = false;
        bool32 DoorDown = false;
        // for loops for days
        for(uint32 ScreenIndex = 0;
            ScreenIndex < 2000;
            ++ScreenIndex)
        {
            // RNG!!!!
            //uint32 DoorDirection = RandomChoice(&Series, (DoorUp || DoorDown) ? 2 : 3);
            uint32 DoorDirection = RandomChoice(&Series, 2);
            
            bool32 CreatedZDoor = false;
#if 1        
            if(DoorDirection == 2)
            {
                CreatedZDoor = true;
                
                if(AbsTileZ == ScreenBaseZ)
                {
                    DoorUp = true;
                }
                else
                {
                    DoorDown = true;
                }
            }
#endif
            if(DoorDirection == 1)
            {
                DoorRight = true;
            }
            else
            {
                DoorTop = true;
            }
            
            AddStandardRoom(GameState, ScreenX*TilesPerWidth + TilesPerWidth/2, ScreenY*TilesPerHeight + TilesPerHeight/2, AbsTileZ);
            
            for(uint32 TileY = 0;
                TileY < TilesPerHeight;
                ++TileY)
            {
                for(uint32 TileX = 0;
                    TileX < TilesPerWidth;
                    ++TileX)
                {
                    uint32 AbsTileX = ScreenX*TilesPerWidth + TileX;
                    uint32 AbsTileY = ScreenY*TilesPerHeight + TileY;
                    
                    bool32 ShouldBeDoor = false;
                    if((TileX == 0) && (!DoorLeft || (TileY != TilesPerHeight / 2))) // <---Dual
                    {
                        ShouldBeDoor = true;
                    }
                    
                    if((TileX == TilesPerWidth - 1) && (!DoorRight || TileY != TilesPerHeight / 2))
                    {
                        ShouldBeDoor = true;
                    }
                    
                    if((TileY == 0) && (!DoorBottom || (TileX != TilesPerWidth / 2)))
                    {
                        ShouldBeDoor = true;
                    }
                    
                    if((TileY == TilesPerHeight - 1) && (!DoorTop || TileX != TilesPerWidth/2))
                    {
                        ShouldBeDoor = true;
                    }
                    
                    if(ShouldBeDoor)
                    {
                        AddWall(GameState, AbsTileX, AbsTileY, AbsTileZ);
                    }
                    else if(CreatedZDoor)
                    {
                        if((TileX == 10) && (TileY == 5))
                        {
                            AddStair(GameState, AbsTileX, AbsTileY, DoorDown ? AbsTileZ - 1 : AbsTileZ);
                        }
                    }
                }
            }
            
            DoorLeft = DoorRight;
            DoorBottom = DoorTop;
            
            if(CreatedZDoor)
            {
                DoorDown = !DoorDown;
                DoorUp = !DoorUp; // Toggle doors if we created one
            }
            else
            {
                DoorUp = false;
                DoorDown = false;
            }
            // Reset the door logic; In the next room, there needs to be doors that connect
            DoorRight = false;
            DoorTop = false;
            
            if(DoorDirection == 2)
            {
                if(AbsTileZ == ScreenBaseZ)
                {
                    AbsTileZ = ScreenBaseZ + 1;
                }
                else
                {
                    AbsTileZ = ScreenBaseZ;
                }
            }
            else if(DoorDirection == 1)
            {
                ScreenX += 1;
            }
            else
            {
                ScreenY += 1;
            }
        }
        
        world_position NewCameraPos = {};
        uint32 CameraTileX = ScreenBaseX*TilesPerWidth + 17/2;
        uint32 CameraTileY = ScreenBaseY*TilesPerHeight + 9/2;
        uint32 CameraTileZ = ScreenBaseZ;
        NewCameraPos = ChunkPositionFromTilePosition(GameState->World, CameraTileX, CameraTileY, CameraTileZ);
        
        GameState->CameraPos = NewCameraPos;
        
        AddMonstar(GameState, CameraTileX - 3, CameraTileY + 2, CameraTileZ);
        
        for(int FamiliarIndex = 0;
            FamiliarIndex < 1;
            ++FamiliarIndex)
        {
            int32 FamiliarOffsetX = RandomBetween(&Series, -7, 7);
            int32 FamiliarOffsetY = RandomBetween(&Series, -3, -1);
            if((FamiliarOffsetX != 0) ||
               (FamiliarOffsetY != 0))
            {
                AddFamiliar(GameState, CameraTileX + FamiliarOffsetX, CameraTileY + FamiliarOffsetY, CameraTileZ);
            }
        }
        
        Memory->IsInitialized = true;
    }

    // Transient memory initialization
    Assert(sizeof(transient_state) <= Memory->TransientStorageSize);
    transient_state *TranState = (transient_state *)Memory->TransientStorage;
    if(!TranState->IsInitialized)
    {
        InitializeArena(&TranState->TranArena, (memory_index)(Memory->TransientStorageSize - sizeof(transient_state)),
                        (uint8 *)Memory->TransientStorage + sizeof(transient_state));

        TranState->GroundBufferCount = 64; //128;
        TranState->GroundBuffers = PushArray(&TranState->TranArena, TranState->GroundBufferCount, ground_buffer);
        for(uint32 GroundBufferIndex = 0;
            GroundBufferIndex < TranState->GroundBufferCount;
            ++GroundBufferIndex)
        {
            ground_buffer *GroundBuffer = TranState->GroundBuffers + GroundBufferIndex;
            GroundBuffer->Bitmap = MakeEmptyBitmap(&TranState->TranArena, GroundBufferWidth, GroundBufferHeight, false);
            GroundBuffer->Pos = NullPosition();
        }

        TranState->IsInitialized = true;
    }

    if(Input->ExecutableReloaded)
    {
        for(uint32 GroundBufferIndex = 0;
            GroundBufferIndex < TranState->GroundBufferCount;
            ++GroundBufferIndex)
        {
            ground_buffer *GroundBuffer = TranState->GroundBuffers + GroundBufferIndex;
            GroundBuffer->Pos = NullPosition();
        }
    }
    
    world *World = GameState->World;
    
    real32 MetersToPixels = GameState->MetersToPixels;
    real32 PixelsToMeters = 1.0f / MetersToPixels;
    
    for(int ControllerIndex = 0;
        ControllerIndex < ArrayCount(Input->Controllers);
        ++ControllerIndex)
    {
        game_controller_input *Controller = GetController(Input, ControllerIndex);
        controlled_hero *ConHero = GameState->ControlledHeroes + ControllerIndex;
        if(ConHero->EntityIndex == 0)
        {
            if(Controller->Start.EndedDown)
            {
                *ConHero = {};
                ConHero->EntityIndex = AddPlayer(GameState).LowIndex;
            }	
        }
        else
        {
            ConHero->dZ = 0;
            ConHero->ddPos = {};
            ConHero->dSword = {};
            
            if(Controller->IsAnalog)
            {
                ConHero->ddPos = v2{Controller->StickAverageX, Controller->StickAverageY};
            }
            else
            {
                if(Controller->MoveUp.EndedDown)
                {
                    ConHero->ddPos.y = 1.0f;
                }
                if(Controller->MoveDown.EndedDown)
                {
                    ConHero->ddPos.y = -1.0f;
                }
                if(Controller->MoveLeft.EndedDown)
                {
                    ConHero->ddPos.x = -1.0f;
                }
                if(Controller->MoveRight.EndedDown)
                {
                    ConHero->ddPos.x = 1.0f;
                }			
            }
            
            if(Controller->Start.EndedDown)
            {
                ConHero->dZ = 3.0f;
            }
            
            ConHero->dSword = {};
            if(Controller->ActionUp.EndedDown)
            {
                ConHero->dSword = v2{0.0f, 1.0f};
            }
            if(Controller->ActionDown.EndedDown)
            {
                ConHero->dSword = v2{0.0f, -1.0f};
            }
            if(Controller->ActionLeft.EndedDown)
            {
                ConHero->dSword = v2{-1.0f, 0.0f};
            }
            if(Controller->ActionRight.EndedDown)
            {
                ConHero->dSword = v2{1.0f, 0.0f};
            }
        }
    }
    
//
// ~~ Render Code
//
    temporary_memory RenderMemory = BeginTemporaryMemory(&TranState->TranArena);

    render_group *RenderGroup = AllocateRenderGroup(&TranState->TranArena, Megabytes(4), GameState->MetersToPixels);

    loaded_bitmap DrawBuffer_ = {};
    loaded_bitmap *DrawBuffer = &DrawBuffer_;
    DrawBuffer->Width = Buffer->Width;
    DrawBuffer->Height = Buffer->Height;
    DrawBuffer->Pitch = Buffer->Pitch;
    DrawBuffer->Memory = Buffer->Memory;

    Clear(RenderGroup, v4{1.0f, 0.0f, 1.0f, 0.0f});

    v2 ScreenCenter = {0.5f * (real32)DrawBuffer->Width, 0.5f * (real32)DrawBuffer->Height};

    real32 ScreenWidthInMeters = DrawBuffer->Width * PixelsToMeters;
    real32 ScreenHeightInMeters = DrawBuffer->Height * PixelsToMeters;
    rectangle3 CameraBoundsInMeters = RectCenterDim(v3{0, 0, 0}, v3{ScreenWidthInMeters, ScreenHeightInMeters, 0.0f});

    for(uint32 GroundBufferIndex = 0;
        GroundBufferIndex < TranState->GroundBufferCount;
        ++GroundBufferIndex)
    {    
        ground_buffer *GroundBuffer = TranState->GroundBuffers + GroundBufferIndex;
        
        if(IsValid(GroundBuffer->Pos))
        {
            loaded_bitmap *Bitmap = &GroundBuffer->Bitmap;
            v3 Delta = Subtract(GameState->World, &GroundBuffer->Pos, &GameState->CameraPos);
            PushBitmap(RenderGroup, Bitmap, Delta.xy, Delta.z, 0.5f*V2i(Bitmap->Width, Bitmap->Height));
        }
    }

    {
        

        world_position MinChunkPos = MapIntoChunkSpace(World, GameState->CameraPos, GetMinCorner(CameraBoundsInMeters));
        world_position MaxChunkPos = MapIntoChunkSpace(World, GameState->CameraPos, GetMaxCorner(CameraBoundsInMeters));
    
        for(int32 ChunkZ = MinChunkPos.ChunkZ;
            ChunkZ <= MaxChunkPos.ChunkZ;
            ++ChunkZ)
        {
            for(int32 ChunkY = MinChunkPos.ChunkY;
                ChunkY <= MaxChunkPos.ChunkY;
                ++ChunkY)
            {
                for(int32 ChunkX = MinChunkPos.ChunkX;
                    ChunkX <= MaxChunkPos.ChunkX;
                    ++ChunkX)
                {
                    {
                        world_position ChunkCenterPos = CenteredChunkPoint(ChunkX, ChunkY, ChunkZ);
                        v3 RelCenterPos = Subtract(World, &ChunkCenterPos, &GameState->CameraPos);            

                        real32 FurthestBufferLengthSq = 0.0f;
                        ground_buffer *FurthestBuffer = 0;
                        for(uint32 GroundBufferIndex = 0;
                            GroundBufferIndex < TranState->GroundBufferCount;
                            ++GroundBufferIndex)
                        {
                            ground_buffer *GroundBuffer = TranState->GroundBuffers + GroundBufferIndex;
                            if(AreOnSameChunk(World, &GroundBuffer->Pos, &ChunkCenterPos))
                            {
                                FurthestBuffer = 0;
                                break;
                            }
                            else if(IsValid(GroundBuffer->Pos))
                            {
                                v3 RelCenterPos = Subtract(World, &GroundBuffer->Pos, &GameState->CameraPos);
                                real32 BufferLengthSq = LengthSq(RelCenterPos.xy);
                                if(FurthestBufferLengthSq < BufferLengthSq)
                                {
                                    FurthestBufferLengthSq = BufferLengthSq;
                                    FurthestBuffer = GroundBuffer;
                                }
                            }
                            else
                            {
                                FurthestBufferLengthSq = Real32Maximum;
                                FurthestBuffer = GroundBuffer;
                            }
                        }

                        if(FurthestBuffer)
                        {
                            FillGroundChunk(TranState, GameState, FurthestBuffer, &ChunkCenterPos);                            
                        }
 
                        PushRectOutline(RenderGroup, RelCenterPos.xy, 0.0f, World->ChunkDimInMeters.xy, v4{1.0f, 1.0f, 0.0f, 1.0f});
                    }
                }
            }
        }
    }

    v3 SimBoundsExpansion = v3{15.0f, 15.0f, 15.0f};
    rectangle3 SimBounds = AddRadiusTo(CameraBoundsInMeters, SimBoundsExpansion);
    
    temporary_memory SimMemory = BeginTemporaryMemory(&TranState->TranArena);
    sim_region *SimRegion = BeginSim(&TranState->TranArena, GameState, GameState->World, GameState->CameraPos, SimBounds, Input->dtForFrame);
    
    for(uint32 EntityIndex = 0;
        EntityIndex < SimRegion->EntityCount;
        ++EntityIndex)
    {
        sim_entity *Entity = SimRegion->Entities + EntityIndex;

        if(Entity->Updatable)
        {
            real32 dt = Input->dtForFrame;
            
            real32 ShadowAlpha = 1.0f - 0.5f*Entity->Pos.z;
            if(ShadowAlpha < 0.0f)
            {
                ShadowAlpha = 0.0f;
            }
            
            move_spec MoveSpec = DefaultMoveSpec();
            v3 ddPos = {};
            
            render_basis *Basis = PushStruct(&TranState->TranArena, render_basis);
            RenderGroup->DefaultBasis = Basis;

            hero_bitmaps *HeroBitmaps = &GameState->HeroBitmaps[Entity->FacingDirection];
            
            switch(Entity->Type)
            {
                case EntityType_Hero:
                {
                    for(uint32 ControlIndex = 0;
                        ControlIndex < ArrayCount(GameState->ControlledHeroes);
                        ++ControlIndex)
                    {
                        controlled_hero *ConHero = GameState->ControlledHeroes + ControlIndex;
                        
                        if(Entity->StorageIndex == ConHero->EntityIndex)
                        {
                            if(ConHero->dZ != 0.0f)
                            {
                                Entity->dPos.z = ConHero->dZ;
                            }
                            
                            MoveSpec.UnitMaxAccelVector = true;
                            MoveSpec.Speed = 50.0f;
                            MoveSpec.Drag = 8.0f;
                            ddPos = V3(ConHero->ddPos, 0);
                            
                            if((ConHero->dSword.x != 0.0f) || (ConHero->dSword.y != 0.0f))
                            {
                                sim_entity *Sword = Entity->Sword.Ptr;
                                if(Sword && IsSet(Sword, EntityFlag_Nonspatial))
                                {
                                    Sword->DistanceLimit = 5.0f;
                                    MakeEntitySpatial(Sword, Entity->Pos, Entity->dPos + V3(5.0f*ConHero->dSword, 0.0f));
                                    AddCollisionRule(GameState, Sword->StorageIndex, Entity->StorageIndex, false);
                                }
                            }
                        }
                    }
                    
                    PushBitmap(RenderGroup, &GameState->Shadow, v2{0, 0}, 0, HeroBitmaps->Align,  ShadowAlpha, 0.0f);
                    PushBitmap(RenderGroup, &HeroBitmaps->Torso, v2{0, 0}, 0, HeroBitmaps->Align);
                    PushBitmap(RenderGroup, &HeroBitmaps->Cape, v2{0, 0}, 0, HeroBitmaps->Align);
                    PushBitmap(RenderGroup, &HeroBitmaps->Head, v2{0, 0}, 0, HeroBitmaps->Align);
                    
                    DrawHitPoints(Entity, RenderGroup);
                } break;
                
                case EntityType_Wall:
                {
                    PushBitmap(RenderGroup, &GameState->Tree, v2{0, 0}, 0, v2{40, 80});
                } break;
                
                case EntityType_Stairwell:
                {
                    PushRect(RenderGroup, v2{0, 0}, 0, Entity->WalkableDim, v4{1, 0.5f, 0, 1}, 0.0f);
                    PushRect(RenderGroup, v2{0, 0}, Entity->WalkableHeight, Entity->WalkableDim,  v4{1, 1, 0, 1}, 0.0f);
                } break;
                
                case EntityType_Sword:
                {
                    MoveSpec.UnitMaxAccelVector = false;
                    MoveSpec.Speed = 0.0f;
                    MoveSpec.Drag = 0.0f;
                    
                    if(Entity->DistanceLimit == 0.0f)
                    {
                        ClearCollisionRulesFor(GameState, Entity->StorageIndex);
                        MakeEntityNonSpatial(Entity);
                    }
                    
                    PushBitmap(RenderGroup, &GameState->Shadow, v2{0, 0}, 0, HeroBitmaps->Align,  ShadowAlpha, 0.0f);
                    PushBitmap(RenderGroup, &GameState->Sword, v2{0, 0}, 0, v2{29, 10});
                } break;
                
                case EntityType_Familiar:
                {
                    sim_entity *ClosestHero = 0;
                    real32 ClosestHeroDSq = Square(10.0f);
#if 0
                    sim_entity *TestEntity = SimRegion->Entities;
                    for(uint32 TestEntityIndex = 0;
                        TestEntityIndex < SimRegion->EntityCount;
                        ++TestEntityIndex, ++TestEntity)
                    {
                        if(TestEntity->Type == EntityType_Hero)
                        {
                            real32 TestDSq = LengthSq(TestEntity->Pos - Entity->Pos);
                            
                            if(ClosestHeroDSq > TestDSq)
                            {
                                ClosestHero = TestEntity;
                                ClosestHeroDSq = TestDSq;
                            }
                        }
                    }
#endif
                    if(ClosestHero && (ClosestHeroDSq > Square(3.0f)))
                    {
                        real32 Acceleration = 0.5f;
                        real32 OneOverLength = Acceleration / SquareRoot(ClosestHeroDSq);
                        ddPos = OneOverLength * (ClosestHero->Pos - Entity->Pos);
                    }
                    
                    MoveSpec.UnitMaxAccelVector = true;
                    MoveSpec.Speed = 50.0f;
                    MoveSpec.Drag = 8.0f;
                    
                    Entity->tBob += dt;
                    if(Entity->tBob > (2.0f*Pi32))
                    {
                        Entity->tBob -= (2.0f*Pi32);
                    }
                    real32 BobSin = Sin(3.0f*Entity->tBob);
                    PushBitmap(RenderGroup, &GameState->Shadow, v2{0, 0}, 0, HeroBitmaps->Align, (0.5f*ShadowAlpha) + 0.2f*BobSin, 0.0f);
                    PushBitmap(RenderGroup, &HeroBitmaps->Head, v2{0, 0}, 0.5f*BobSin, HeroBitmaps->Align);
                } break;
                
                case EntityType_Monstar:
                {
                    PushBitmap(RenderGroup, &GameState->Shadow, v2{0, 0}, 0, HeroBitmaps->Align, ShadowAlpha, 0.0f);
                    PushBitmap(RenderGroup, &HeroBitmaps->Torso, v2{0, 0}, 0, HeroBitmaps->Align);
                    
                    DrawHitPoints(Entity, RenderGroup);
                } break;
                
                case EntityType_Space:
                {
#if 1
                    for(uint32 VolumeIndex = 0;
                        VolumeIndex < Entity->Collision->VolumeCount;
                        ++VolumeIndex)
                    {
                        sim_entity_collision_volume *Volume = Entity->Collision->Volumes + VolumeIndex;
                        PushRectOutline(RenderGroup, Volume->OffsetPos.xy, 0, Volume->Dim.xy, v4{0, 0.5f, 0, 1}, 0.0f);
                    }
#endif
                } break;
                
                default:
                {
                    InvalidCodePath;
                } break;
            }
            
            if(!IsSet(Entity, EntityFlag_Nonspatial) &&
               IsSet(Entity, EntityFlag_Moveable))
            {
                MoveEntity(GameState, SimRegion, Entity, Input->dtForFrame, &MoveSpec, ddPos);
            }

            Basis->Pos = GetEntityGroundPoint(Entity);
        }
    }

    RenderGroupToOutput(RenderGroup, DrawBuffer);
    
    EndSim(SimRegion, GameState);
    EndTemporaryMemory(SimMemory);
    EndTemporaryMemory(RenderMemory);

    CheckArena(&GameState->WorldArena);
    CheckArena(&TranState->TranArena);
}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    game_state *GameState = (game_state *)Memory->PermanentStorage;
    GameOutputSound(GameState, SoundBuffer, 400);
}
