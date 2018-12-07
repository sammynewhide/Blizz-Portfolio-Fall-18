### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ###
### ~~~ For more context of the project please view the "read_me_first" document in 'Blizz-Portfolio-Fall-18/Handmade Hero' ~~~ ###
### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ### ###

### Explanation of the code in here might be limited due to Finals Week studying. To see code regarding data structures,
### please see 'Blizz-Portfolio-Fall-18/Handmade Hero/code/handmade.cpp' and CRTL+F search for 'AddLowEntity'. Thank you for the understanding.

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