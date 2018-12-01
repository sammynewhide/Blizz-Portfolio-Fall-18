internal_func sim_entity_hash *
GetHashFromStorageIndex(sim_region *SimRegion, uint32 StorageIndex)
{
    Assert(StorageIndex);
    
    sim_entity_hash *Result = 0;
    
    uint32 HashValue = StorageIndex;
    for(uint32 Offset = 0;
        Offset < ArrayCount(SimRegion->Hash);
        ++Offset)
    {
        uint32 HashMask = (ArrayCount(SimRegion->Hash) - 1);
        uint32 HashIndex = ((HashValue + Offset) & HashMask);
        sim_entity_hash *Entry = SimRegion->Hash + HashIndex;
        if((Entry->Index == 0) || (Entry->Index == StorageIndex))
        {
            Result = Entry;
            break;
        }
    }
    
    return(Result);
}

inline sim_entity *
GetEntityByStorageIndex(sim_region *SimRegion, uint32 StorageIndex)
{
    sim_entity_hash *Entry = GetHashFromStorageIndex(SimRegion, StorageIndex);
    sim_entity *Result = Entry->Ptr;
    return(Result);
}

inline v3
GetSimSpacePos(sim_region *SimRegion, low_entity *Stored)
{
    v3 Result = InvalidPos;
    if(!IsSet(&Stored->Sim, EntityFlag_Nonspatial))
    {
        Result = Subtract(SimRegion->World, &Stored->Pos, &SimRegion->Origin);
    }
    return(Result);
}

inline bool32
EntityOverlapsRectangle(v3 Pos, sim_entity_collision_volume Volume, rectangle3 Rect)
{
    rectangle3 Grown = AddRadiusTo(Rect, 0.5f*Volume.Dim);
    bool32 Result = IsInRectangle(Grown, Pos + Volume.OffsetPos);
    return(Result);
}

internal_func sim_entity *
AddEntity(game_state *GameState, sim_region *SimRegion, uint32 StorageIndex, low_entity *Source, v3 *SimPos);
inline void
LoadEntityReference(game_state *GameState, sim_region *SimRegion, entity_reference *Ref)
{
    if(Ref->Index)
    {
        sim_entity_hash *Entry = GetHashFromStorageIndex(SimRegion, Ref->Index);
        if(Entry->Ptr == 0)
        {
            Entry->Index = Ref->Index;
            low_entity *LowEntity = GetLowEntity(GameState, Ref->Index);
            v3 Pos = GetSimSpacePos(SimRegion, LowEntity);
            Entry->Ptr = AddEntity(GameState, SimRegion, Ref->Index, LowEntity, &Pos);
        }
        
        Ref->Ptr = Entry->Ptr;
    }
}

inline void
StoreEntityReference(entity_reference *Ref)
{
    if(Ref->Ptr != 0)
    {
        Ref->Index = Ref->Ptr->StorageIndex;
    }
}

internal_func sim_entity *
AddEntityRaw(game_state *GameState, sim_region *SimRegion, uint32 StorageIndex, low_entity *Source)
{
    Assert(StorageIndex);
    sim_entity *Entity = 0;
    
    sim_entity_hash *Entry = GetHashFromStorageIndex(SimRegion, StorageIndex);
    if(Entry->Ptr == 0)
    {
        if(SimRegion->EntityCount < SimRegion->MaxEntityCount)
        {
            Entity = SimRegion->Entities + SimRegion->EntityCount++;
            
            Entry->Index = StorageIndex;
            Entry->Ptr = Entity;
            
            if(Source)
            {
                *Entity = Source->Sim;
                LoadEntityReference(GameState, SimRegion, &Entity->Sword);
                
                Assert(!IsSet(&Source->Sim, EntityFlag_Simming));
                AddFlags(&Source->Sim, EntityFlag_Simming);
            }
            
            Entity->StorageIndex = StorageIndex;
            Entity->Updatable = false;
        }
        else
        {
            InvalidCodePath;
        }
    }
    
    return(Entity);
}

internal_func sim_entity *
AddEntity(game_state *GameState, sim_region *SimRegion, uint32 StorageIndex, low_entity *Source, v3 *SimPos)
{
    sim_entity *Dest = AddEntityRaw(GameState, SimRegion, StorageIndex, Source);
    if(Dest)
    {
        if(SimPos)
        {
            Dest->Pos = *SimPos;
            Dest->Updatable = EntityOverlapsRectangle(Dest->Pos, Dest->Collision->TotalVolume, SimRegion->UpdatableBounds);
        }
        else
        {
            Dest->Pos = GetSimSpacePos(SimRegion, Source);
        }
    }
    
    return(Dest);
}

internal_func sim_region *
BeginSim(memory_arena *SimArena, game_state *GameState, world *World, world_position Origin, rectangle3 Bounds, real32 dt)
{
    sim_region *SimRegion = PushStruct(SimArena, sim_region);
    ZeroStruct(SimRegion->Hash);
    
    SimRegion->MaxEntityRadius = 5.0f;
    SimRegion->MaxEntityVelocity = 30.0f;
    real32 UpdateSafetyMargin = SimRegion->MaxEntityRadius + dt*SimRegion->MaxEntityVelocity;
    real32 UpdateSafetyMarginZ = 1.0f;
    
    SimRegion->World = World;
    SimRegion->Origin = Origin;
    SimRegion->UpdatableBounds = AddRadiusTo(Bounds, v3{SimRegion->MaxEntityRadius, SimRegion->MaxEntityRadius, SimRegion->MaxEntityRadius});
    SimRegion->Bounds = AddRadiusTo(SimRegion->UpdatableBounds, v3{UpdateSafetyMargin, UpdateSafetyMargin, UpdateSafetyMarginZ});
    
    SimRegion->MaxEntityCount = 4096;
    SimRegion->EntityCount = 0;
    SimRegion->Entities = PushArray(SimArena, SimRegion->MaxEntityCount, sim_entity);
    
    world_position MinChunkPos = MapIntoChunkSpace(World, SimRegion->Origin, GetMinCorner(SimRegion->Bounds));
    world_position MaxChunkPos = MapIntoChunkSpace(World, SimRegion->Origin, GetMaxCorner(SimRegion->Bounds));
    
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
                world_chunk *Chunk = GetWorldChunk(World, ChunkX, ChunkY, ChunkZ);
                if(Chunk)
                {
                    for(world_entity_block *Block = &Chunk->FirstBlock;
                        Block;
                        Block = Block->Next)
                    {
                        for(uint32 EntityIndexIndex = 0;
                            EntityIndexIndex < Block->EntityCount;
                            ++EntityIndexIndex)
                        {
                            uint32 LowEntityIndex = Block->LowEntityIndex[EntityIndexIndex];
                            low_entity *Low = GameState->LowEntities + LowEntityIndex;
                            if(!IsSet(&Low->Sim, EntityFlag_Nonspatial))
                            {
                                v3 SimSpacePos = GetSimSpacePos(SimRegion, Low);
                                if(EntityOverlapsRectangle(SimSpacePos, Low->Sim.Collision->TotalVolume, SimRegion->Bounds))
                                {
                                    AddEntity(GameState, SimRegion, LowEntityIndex, Low, &SimSpacePos);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    return(SimRegion);
}

internal_func void
EndSim(sim_region *Region, game_state *GameState)
{
    sim_entity *Entity = Region->Entities;
    
    for(uint32 EntityIndex = 0;
        EntityIndex < Region->EntityCount;
        ++EntityIndex, ++Entity)
    {
        low_entity *Stored = GameState->LowEntities + Entity->StorageIndex;
        
        Assert(IsSet(&Stored->Sim, EntityFlag_Simming));
        Stored->Sim = *Entity;
        Assert(!IsSet(&Stored->Sim, EntityFlag_Simming));
        
        StoreEntityReference(&Stored->Sim.Sword);
        
        world_position NewPos = IsSet(Entity, EntityFlag_Nonspatial) ? 
            NullPosition() : MapIntoChunkSpace(GameState->World, Region->Origin, Entity->Pos);
        ChangeEntityLocation(&GameState->WorldArena, GameState->World, Entity->StorageIndex,
                             Stored, NewPos);
        
        if(Entity->StorageIndex == GameState->CameraFollowingEntityIndex)
        {
            world_position NewCameraPos = GameState->CameraPos;
            
            NewCameraPos.ChunkZ = Stored->Pos.ChunkZ;	
#if 0
            if(CameraFollowingEntity.High->Pos.x > (9.0f*World->TileSideInMeters))
            {
                NewCameraPos.AbsTileX += 17;
            }
            if(CameraFollowingEntity.High->Pos.x < -(9.0f*World->TileSideInMeters))
            {
                NewCameraPos.AbsTileX -= 17;
            }
            if(CameraFollowingEntity.High->Pos.y > (5.0f*World->TileSideInMeters))
            {	
                NewCameraPos.AbsTileY += 9;
            }
            if(CameraFollowingEntity.High->Pos.y < -(5.0f*World->TileSideInMeters))
            {
                NewCameraPos.AbsTileY -= 9;
            }
#else
            real32 CamZOffset = NewCameraPos.Offset.z;
            NewCameraPos = Stored->Pos;
            NewCameraPos.Offset.z = CamZOffset;
#endif
            GameState->CameraPos = NewCameraPos;
        }
    }
}

struct test_wall
{
    real32 X;
    real32 RelX;
    real32 RelY;
    real32 DeltaX;
    real32 DeltaY;
    real32 MinY;
    real32 MaxY;
    v3 Normal;
};
internal_func bool32
TestWall(real32 WallX, real32 RelX, real32 RelY, real32 PlayerDeltaX, real32 PlayerDeltaY, real32 *tMin, real32 MinY, real32 MaxY)
{
    bool32 Hit = false;
    
    real32 tEpsilon = 0.001f;
    if(PlayerDeltaX != 0.0f)
    {
        real32 tResult = (WallX - RelX) / PlayerDeltaX;
        real32 Y = RelY + tResult * PlayerDeltaY;
        
        if((tResult >= 0.0f) && (*tMin > tResult))
        {
            if((Y >= MinY) && (Y <= MaxY))
            {
                *tMin = Maximum(0.0f, tResult - tEpsilon);
                Hit = true;
            }
        }	
    }
    
    return(Hit);
}

internal_func bool32
CanCollide(game_state *GameState, sim_entity *A, sim_entity *B)
{
    bool32 Result = false;
    
    if(A != B)
    {
        if(A->StorageIndex > B->StorageIndex)
        {
            sim_entity *Temp = A;
            A = B;
            B = Temp;
        }
        
        if(IsSet(A, EntityFlag_Collides) && IsSet(B, EntityFlag_Collides))
        {
            if(!IsSet(A, EntityFlag_Nonspatial) &&
               !IsSet(B, EntityFlag_Nonspatial))
            {
                Result = true;
            }
            
            uint32 HashBucket = A->StorageIndex & (ArrayCount(GameState->CollisionRuleHash) - 1);
            for(pairwise_collision_rule *Rule = GameState->CollisionRuleHash[HashBucket];
                Rule;
                Rule = Rule->NextInHash)
            {
                if((Rule->StorageIndexA == A->StorageIndex) &&
                   (Rule->StorageIndexB == B->StorageIndex))
                {
                    Result = Rule->CanCollide;
                    break;
                }
            }
        }
    }
    
    return(Result);
}

internal_func bool32
HandleCollision(game_state *GameState, sim_entity *A, sim_entity *B)
{
    bool32 StopsOnCollision = false;
    
    if(A->Type == EntityType_Sword)
    {
        AddCollisionRule(GameState, A->StorageIndex, B->StorageIndex, false);
        StopsOnCollision = false;
    }
    else
    {
        StopsOnCollision = true;
    }
    
    if(A->Type > B->Type)
    {
        sim_entity *Temp = A;
        A = B;
        B = Temp;
    }
    
    if((A->Type == EntityType_Monstar) && (B->Type == EntityType_Sword))
    {
        if(A->HitPointMax > 0)
        {
            --A->HitPointMax;
        }
    }
    
    return(StopsOnCollision);
}

internal_func bool32
CanOverlap(game_state *GameState, sim_entity *Mover, sim_entity *Region)
{
    bool32 Result = false;
    
    if(Mover != Region)
    {
        if(Region->Type == EntityType_Stairwell)
        {
            Result = true;
        }
    }
    
    return(Result);
}

internal_func void
HandleOverlap(game_state *GameState, sim_entity *Mover, sim_entity *Region, real32 dt, real32 *Ground)
{
    if(Region->Type == EntityType_Stairwell)
    {
        *Ground = GetStairGround(Region, GetEntityGroundPoint(Mover));
    }
}

internal_func bool32
SpeculativeCollide(sim_entity *Mover, sim_entity *Region, v3 TestPos)
{
    bool32 Result = true;
    
    if(Region->Type == EntityType_Stairwell)
    {
        
        real32 StepHeight = 0.1f;
#if 0
        Result = ((AbsoluteValue(GetEntityGroundPoint(Mover).z - Ground) > StepHeight) || ((Bary.y > 0.1f) && (Bary.y < 0.9f)));
#endif
        v3 MoverGroundPoint = GetEntityGroundPoint(Mover);
        real32 Ground = GetStairGround(Region, MoverGroundPoint);
        Result = (AbsoluteValue(GetEntityGroundPoint(Mover).z - Ground) > StepHeight);
    }
    
    return(Result);
}

internal_func bool32
EntitiesOverlap(sim_entity *Entity, sim_entity *TestEntity, v3 Epsilon = v3{0, 0, 0})
{
    bool32 Result = false;
    
    for(uint32 VolumeIndex = 0;
        !Result && (VolumeIndex < Entity->Collision->VolumeCount);
        ++VolumeIndex)
    {
        sim_entity_collision_volume *Volume = Entity->Collision->Volumes + VolumeIndex;
        
        for(uint32 TestVolumeIndex = 0;
            !Result && (TestVolumeIndex < TestEntity->Collision->VolumeCount);
            ++TestVolumeIndex)
        {
            sim_entity_collision_volume *TestVolume = TestEntity->Collision->Volumes + TestVolumeIndex;
            
            rectangle3 EntityRect = RectCenterDim(Entity->Pos + Volume->OffsetPos, Volume->Dim + Epsilon);
            rectangle3 TestEntityRect = RectCenterDim(TestEntity->Pos + TestVolume->OffsetPos, TestVolume->Dim);
            Result = RectanglesIntersect(EntityRect, TestEntityRect);
        }
    }
    
    return(Result);
}

internal_func void
MoveEntity(game_state *GameState, sim_region *SimRegion, sim_entity *Entity, real32 dt, move_spec *MoveSpec, v3 ddPos)
{
    Assert(!IsSet(Entity, EntityFlag_Nonspatial));
    
    world *World = SimRegion->World;
    
    if(MoveSpec->UnitMaxAccelVector)
    {
        real32 ddPosLength = LengthSq(ddPos);
        if(ddPosLength > 1.0f)
        {
            ddPos *= (1.0f / SquareRoot(ddPosLength));
        }
    }
    
    ddPos *= MoveSpec->Speed;
    
    v3 Drag = -MoveSpec->Drag*Entity->dPos;
    Drag.z = 0.0f;
    ddPos += Drag;
    if(!IsSet(Entity, EntityFlag_ZSupported))
    {
        ddPos += v3{0, 0, -9.8f};
    }
    
    v3 OldPlayerPos = Entity->Pos;
    v3 PlayerDelta = (0.5f*ddPos*Square(dt) + (Entity->dPos*dt));
    Entity->dPos = ddPos*dt + Entity->dPos;
    
    Assert(LengthSq(Entity->dPos) <= Square(SimRegion->MaxEntityVelocity));
    v3 NewPlayerPos = OldPlayerPos + PlayerDelta;
    
    real32 DistanceRemaining = Entity->DistanceLimit;
    if(DistanceRemaining == 0.0f)
    {
        DistanceRemaining = 10000.0f;
    }
    
    for(uint32 Iteration = 0;
        Iteration < 4;
        ++Iteration)
    {
        real32 tMin = 1.0f;
        real32 tMax = 0.0f;
        
        real32 PlayerDeltaLength = Length(PlayerDelta);
        
        if(PlayerDeltaLength > 0)
        {
            if(PlayerDeltaLength > DistanceRemaining)
            {
                tMin = (DistanceRemaining / PlayerDeltaLength);
            }
            
            v3 WallNormalMin = {};
            v3 WallNormalMax = {};
            
            sim_entity *HitEntityMin = 0;
            sim_entity *HitEntityMax = 0;
            
            v3 DesiredPosition = Entity->Pos + PlayerDelta;
            
            if(!IsSet(Entity, EntityFlag_Nonspatial))
            {
                for(uint32 TestHighEntityIndex = 0;
                    TestHighEntityIndex < SimRegion->EntityCount;
                    ++TestHighEntityIndex)
                {
                    sim_entity *TestEntity = SimRegion->Entities + TestHighEntityIndex;
                    
                    real32 OverlapEpsilon = 0.01f;
                    
                    if((IsSet(TestEntity, EntityFlag_Traversable) && EntitiesOverlap(Entity, TestEntity, OverlapEpsilon*v3{1, 1, 1}))
                       || CanCollide(GameState, Entity, TestEntity))
                    {
                        for(uint32 VolumeIndex = 0;
                            VolumeIndex < Entity->Collision->VolumeCount;
                            ++VolumeIndex)
                        {
                            sim_entity_collision_volume *Volume = Entity->Collision->Volumes + VolumeIndex;
                            
                            for(uint32 TestVolumeIndex = 0;
                                TestVolumeIndex < TestEntity->Collision->VolumeCount;
                                ++TestVolumeIndex)
                            {
                                sim_entity_collision_volume *TestVolume = TestEntity->Collision->Volumes + TestVolumeIndex;
                                v3 MinkowskiDiameter = {TestVolume->Dim.x + Volume->Dim.x, TestVolume->Dim.y + Volume->Dim.y, TestVolume->Dim.z + Volume->Dim.z};
                                
                                v3 MinCorner = -0.5f*MinkowskiDiameter;
                                v3 MaxCorner = 0.5f*MinkowskiDiameter;
                                
                                v3 Rel = ((Entity->Pos + Volume->OffsetPos) - (TestEntity->Pos + TestVolume->OffsetPos));
                                
                                if((Rel.z >= MinCorner.z) && (Rel.z < MaxCorner.z))
                                {
                                    test_wall Walls[] = 
                                    {
                                        {MinCorner.x, Rel.x, Rel.y, PlayerDelta.x, PlayerDelta.y, MinCorner.y, MaxCorner.y, v3{-1, 0, 0}},
                                        {MaxCorner.x, Rel.x, Rel.y, PlayerDelta.x, PlayerDelta.y, MinCorner.y, MaxCorner.y, v3{1, 0, 0}},
                                        {MinCorner.y, Rel.y, Rel.x, PlayerDelta.y, PlayerDelta.x, MinCorner.x, MaxCorner.x, v3{0, -1, 0}},
                                        {MaxCorner.y, Rel.y, Rel.x, PlayerDelta.y, PlayerDelta.x, MinCorner.x, MaxCorner.x, v3{0, 1, 0}},
                                    };
                                    
                                    if(IsSet(TestEntity, EntityFlag_Traversable))
                                    {
                                        real32 tMaxTest = tMax;
                                        bool32 HitThis = false;
                                        
                                        v3 TestWallNormal = {};
                                        
                                        for(uint32 WallIndex = 0;
                                            WallIndex < ArrayCount(Walls);
                                            ++WallIndex)
                                        {
                                            test_wall *Wall = Walls + WallIndex;
                                            
                                            real32 tEpsilon = 0.001f;
                                            if(Wall->DeltaX != 0.0f)
                                            {
                                                real32 tResult = (Wall->X - Wall->RelX) / Wall->DeltaX;
                                                real32 Y = Wall->RelY + tResult * Wall->DeltaY;
                                                
                                                if((tResult >= 0.0f) && (tMaxTest < tResult))
                                                {
                                                    if((Y >= Wall->MinY) && (Y <= Wall->MaxY))
                                                    {
                                                        tMaxTest = Maximum(0.0f, tResult - tEpsilon);
                                                        TestWallNormal = Wall->Normal;
                                                        HitThis = true;
                                                    }
                                                }	
                                            }
                                            
                                            if(HitThis)
                                            {
                                                tMax = tMaxTest;
                                                WallNormalMax = TestWallNormal;
                                                HitEntityMax = TestEntity;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        real32 tMinTest = tMin;
                                        bool32 HitThis = false;
                                        
                                        v3 TestWallNormal = {};
                                        
                                        for(uint32 WallIndex = 0;
                                            WallIndex < ArrayCount(Walls);
                                            ++WallIndex)
                                        {
                                            test_wall *Wall = Walls + WallIndex;
                                            
                                            real32 tEpsilon = 0.001f;
                                            if(Wall->DeltaX != 0.0f)
                                            {
                                                real32 tResult = (Wall->X - Wall->RelX) / Wall->DeltaX;
                                                real32 Y = Wall->RelY + tResult * Wall->DeltaY;
                                                
                                                if((tResult >= 0.0f) && (tMinTest > tResult))
                                                {
                                                    if((Y >= Wall->MinY) && (Y <= Wall->MaxY))
                                                    {
                                                        tMinTest = Maximum(0.0f, tResult - tEpsilon);
                                                        TestWallNormal = Wall->Normal;
                                                        HitThis = true;
                                                    }
                                                }	
                                            }
                                        }
                                        
                                        if(HitThis)
                                        {
                                            v3 TestPos = Entity->Pos + PlayerDelta*tMinTest;
                                            if(SpeculativeCollide(Entity, TestEntity, TestPos))
                                            {
                                                tMin = tMinTest;
                                                WallNormalMin = TestWallNormal;
                                                HitEntityMin = TestEntity;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            v3 WallNormal = {};
            sim_entity *HitEntity = 0;
            real32 tStop = 0;
            
            if(tMin < tMax)
            {
                tStop = tMin;
                HitEntity = HitEntityMin;
                WallNormal = WallNormalMin;
            }
            else
            {
                tStop = tMax;
                HitEntity = HitEntityMax;
                WallNormal = WallNormalMax;
            }
            
            Entity->Pos += tStop*PlayerDelta;
            DistanceRemaining -= tStop*PlayerDeltaLength;
            
            if(HitEntity)
            {
                PlayerDelta = DesiredPosition - Entity->Pos;
                bool32 StopsOnCollision = HandleCollision(GameState, Entity, HitEntity);
                
                if(StopsOnCollision)
                {
                    PlayerDelta = PlayerDelta - 1*Inner(PlayerDelta, WallNormal)*WallNormal;
                    Entity->dPos = Entity->dPos - 1*Inner(Entity->dPos, WallNormal)*WallNormal;
                }
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }
    }
    
    real32 Ground = 0.0f;
    
    {
        for(uint32 TestHighEntityIndex = 0;
            TestHighEntityIndex < SimRegion->EntityCount;
            ++TestHighEntityIndex)
        {
            sim_entity *TestEntity = SimRegion->Entities + TestHighEntityIndex;
            //if((Entity->Type == EntityType_Hero) && (TestEntity->Type == EntityType_Stairwell))
            //{
            //int foo = 5;
            //}
            
            if(CanOverlap(GameState, Entity, TestEntity) && EntitiesOverlap(Entity, TestEntity))
            {
                HandleOverlap(GameState, Entity, TestEntity, dt, &Ground);
            }
        }
    }
    
    Ground += Entity->Pos.z - GetEntityGroundPoint(Entity).z;
    if((Entity->Pos.z <= Ground) ||
       ((IsSet(Entity, EntityFlag_ZSupported)) &&
        (Entity->dPos.z == 0.0f)))
    {
        Entity->Pos.z = Ground;
        Entity->dPos.z = 0;
        AddFlags(Entity, EntityFlag_ZSupported);
    }
    else
    {
        ClearFlags(Entity, EntityFlag_ZSupported);
    }
    
    if(Entity->DistanceLimit != 0.0f)
    {
        Entity->DistanceLimit = DistanceRemaining;
    }
    
    if((Entity->dPos.x == 0.0f) && (Entity->dPos.y == 0.0f))
    {
        
    }
    else if(AbsoluteValue(Entity->dPos.x) > AbsoluteValue(Entity->dPos.y))
    {
        if(Entity->dPos.x > 0)
        {
            Entity->FacingDirection = 0;
        }
        else
        {
            Entity->FacingDirection = 2;
        }
        
    }
    else
    {
        if(Entity->dPos.y > 0)
        {
            Entity->FacingDirection = 1;
        }
        else
        {
            Entity->FacingDirection = 3;
        }
    }
}