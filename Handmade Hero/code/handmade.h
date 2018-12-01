#if !defined(HANDMADE_H)

/* 
TODO:
---Architecture exploration---
- Fix 3d minkowski test
- Collision detection
- Allow multiple sim regions per frame (Per-entity clocking)
- Debug code (Logging, visual tools)
- Audio (Music, SFX)
- Asset streaming
- Game saving (Thinking about structure and implementation)
- Test code for world generation
- Map display!?!?
- AI and Pathfinding
- Animation, skeletal animation, particle systems

---Finishing production---
- Rendering
- Industrial game code: Entity system, world generation
*/

#include "handmade_platform.h"

#define Minimum(A, B) ((A < B) ? (A) : (B))
#define Maximum(A, B) ((A > B) ? (A) : (B))

// Services that the game provides to the platform layer
struct memory_arena
{
	memory_index Size;
	uint8 *Base;
	memory_index Used;

    int32 TempCount;
};

struct temporary_memory
{
    memory_arena *Arena;
    memory_index Used;
};

inline void
InitializeArena(memory_arena *Arena, memory_index Size, void *Base)
{
	Arena->Size = Size;
	Arena->Base = (uint8 *)Base;
	Arena->Used = 0;
    Arena->TempCount = 0;
}

#define PushStruct(Arena, type) (type *)PushSize_(Arena, sizeof(type)) // Preprocessor macro that allows us to call using a struct title (type) as parameter
#define PushArray(Arena, Count, type) (type *)PushSize_(Arena, (Count)*sizeof(type)) // Another one that defines PushArray!! Magic
#define PushSize(Arena, Size) PushSize_(Arena, Size)
inline void *
PushSize_(memory_arena *Arena, memory_index Size)
{
	Assert((Arena->Used + Size) <= Arena->Size);
	void *Result = Arena->Base + Arena->Used;
	Arena->Used += Size;
    
	return(Result);
}

inline temporary_memory
BeginTemporaryMemory(memory_arena *Arena)
{
    temporary_memory Result;

    Result.Arena = Arena;
    Result.Used = Arena->Used;

    ++Arena->TempCount;

    return(Result);
}

inline void
EndTemporaryMemory(temporary_memory TempMem)
{
    memory_arena *Arena = TempMem.Arena;
    Assert(Arena->Used >= TempMem.Used);
    Arena->Used = TempMem.Used;
    Assert(Arena->TempCount > 0);
    --Arena->TempCount;
}

inline void
CheckArena(memory_arena *Arena)
{
    Assert(Arena->TempCount == 0);
}

#define ZeroStruct(Instance) ZeroSize(sizeof(Instance), &Instance)
inline void
ZeroSize(memory_index Size, void *Ptr)
{
    uint8 *Byte = (uint8 *)Ptr;
    while(Size--)
    {
        *Byte++ = 0;
    }
}

#include "handmade_intrinsics.h"
#include "handmade_math.h"
#include "handmade_world.h"
#include "handmade_sim_region.h"
#include "handmade_entity.h"

struct loaded_bitmap
{
	int32 Width;
	int32 Height;
    int32 Pitch;
    void *Memory;
};

struct hero_bitmaps
{
    v2 Align;
	
	loaded_bitmap Head;
	loaded_bitmap Cape;
	loaded_bitmap Torso;
};

struct low_entity
{
    world_position Pos;
	sim_entity Sim;
};

struct controlled_hero
{
    uint32 EntityIndex;
    v2 ddPos;
    v2 dSword;
    real32 dZ;
};

struct pairwise_collision_rule
{
    bool32 CanCollide;
    uint32 StorageIndexA;
    uint32 StorageIndexB;
    
    pairwise_collision_rule *NextInHash;
};
struct game_state;
internal_func void AddCollisionRule(game_state *GameState, uint32 StorageIndexA, uint32 StorageIndexB, bool32 ShouldCollide);
internal_func void ClearCollisionRulesFor(game_state *GameState, uint32 StorageIndex);

struct ground_buffer
{
    world_position Pos;
    loaded_bitmap Bitmap;
};

struct game_state
{
	memory_arena WorldArena;
	world *World;
	
    real32 BaselineFloorHeight;

	uint32 CameraFollowingEntityIndex;
	world_position CameraPos;
    
    controlled_hero ControlledHeroes[ArrayCount(((game_input *)0)->Controllers)];
    
	uint32 LowEntityCount;
	low_entity LowEntities[100000];
    
    loaded_bitmap Grass[2];
    loaded_bitmap Stone[4];
    loaded_bitmap Tuft[3];
    
    loaded_bitmap Backdrop;
	loaded_bitmap Shadow;
	hero_bitmaps HeroBitmaps[4];
    
    loaded_bitmap Tree;
    loaded_bitmap Sword;
    loaded_bitmap Stairwell;
    real32 MetersToPixels;
    real32 PixelsToMeters;
    
    pairwise_collision_rule *CollisionRuleHash[256];
    pairwise_collision_rule *FirstFreeCollisionRule;
    
    sim_entity_collision_volume_group *NullCollision;
    sim_entity_collision_volume_group *SwordCollision;
    sim_entity_collision_volume_group *StairwellCollision;
    sim_entity_collision_volume_group *PlayerCollision;
    sim_entity_collision_volume_group *MonstarCollision;
    sim_entity_collision_volume_group *FamiliarCollision;
    sim_entity_collision_volume_group *WallCollision;
    sim_entity_collision_volume_group *StandardRoomCollision;
};

struct transient_state
{
    bool32 IsInitialized;
    memory_arena TranArena;
    uint32 GroundBufferCount;
    ground_buffer *GroundBuffers;
};

inline low_entity *
GetLowEntity(game_state *GameState, uint32 Index)
{
    low_entity *Result = 0;
    
    if((Index > 0) && (Index < GameState->LowEntityCount))
    {
        Result = GameState->LowEntities + Index;
    }
    
    return(Result);
}

#define HANDMADE_H
#endif
