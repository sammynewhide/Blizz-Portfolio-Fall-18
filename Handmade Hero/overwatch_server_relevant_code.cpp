### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ###
### ~~~ For more context of the project please view the "read_me_first" document in 'Blizz-Portfolio-Fall-18/Handmade Hero' ~~~ ###
### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ###

### Explanation of the code in here might be limited due to Finals Week studying. To see code regarding data structures,
### please see 'Blizz-Portfolio-Fall-18/Handmade Hero/code/handmade.cpp' and CRTL+F search for 'AddLowEntity'. Thank you for the understanding.

### 'Windows Platform Layer' ###

Please see 'Blizz-Portfolio-Fall-18/Handmade Hero/code/win32_handmade.cpp'
// I have experience calling Windows functions in order to set up the game.
// I imagine dealing with Windows and Xbox/PS platform layers are important for accessing networking functions


### ~~~ Sparse Texture Storage and Random Texture Generation ~~~ ###
# code/handmade.cpp # Around lines 989-1044

// The in-game ground with the dirt and grass is all randomly generated.
// There is no giant, repeating texture and you check inside the 'test' folders of the game
// that contain the textures.

internal_func void // Located in handmade.cpp, starts around line 443
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
// The random drawing is effective for our renderer also.
// Instead of drawing each 'stamp' of grass or dirt, we push them onto stack
// and then the stack is drawn at the end.
PushBitmap(GroundRenderGroup, Stamp, Pos, 0.0f, v2{0.0f, 0.0f});

// And then, at the end of the stamping, the finished background is pushed into the final
// drawing pipeline
RenderGroupToOutput(GroundRenderGroup, Buffer);

// This is efficient because we do not have to draw parts of the background
// that will be overwritten by another piece.

// To be even more efficient, not all of the background is loaded into memory at once.
// We have enough memory space to bring ~32 chunks of land from memory.
// After walking for a while and running off of land, 
// We already have a concept of separating the world into chunks of land.

// This code calculates which chunk is furthest from the camera,
// which is the chunk we reuse for memory.
real32 FurthestBufferLengthSq = 0.0f; // Located in handmade.cpp starting around line 1005
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
// The game stutters after moving for a little bit, because it is writing
// new textures into this previously used chunk.

#

### "Sparse Entity Storage" ###

// There was once a time in the game where every position in the world
// was described by a tile that was filled or unfilled.
// Like this:
uinsigned int Tiles[] = [[0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1],
                         [0, 1, 1, 0, 1, 0, 0, 1, 0, 0, 1],
                         [0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1],
                         [0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1]]
// Where 0 represented unfilled tiles and 1 represented a filled tile

// When thinking about whether this solution is OK for shipping the game,
// the clear answer was no.
// 
// A lot of memory space is wasted when we have to store the locations of the unfilled tiles.
// It is more efficient to just store the locations of the filled tiles, and ignore other locations.

// Before solving this problem once and for all, we gave each tile a coordinate, and got rid of a fixed list
// and started randomly generating the tiles.

for(unsigned int TileY = 0;
    TileY < TILE_Y_MAX_COUNT;
    TileY++)
{
    for(unsigned int TileX = 0;
        TileX < TILE_X_MAX_COUNT;
        TileX++)
    {
        if RANDOM_NUMBER % 2 == 0       // This was a very basic and inefficient system for filling the tiles
        {                               // And this still didn't solve the problem, since 0 tiles are still stored.
            Tile[TileY][TileX] = 1
        }
    }
}

// 


### "world_entity_block" ###

// Each game object is an entity that has its own characteristics.
// Since our world is big, we need a way for 
// 


