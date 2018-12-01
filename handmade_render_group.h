#if !defined(HANDMADE_RENDER_GROUP_H)

struct render_basis
{
    v3 Pos;
};

struct render_entity_basis
{
    render_basis *Basis;
    v2 Offset;
    real32 OffsetZ;
    real32 EntityZC;
};

// render_group_entry is an exmaple of a discriminated union, a 'memory efficient one'
enum render_group_entry_type
{   // The ridiculous naming is for macro purposes (see PushRenderElement)
    RenderGroupEntryType_render_entry_clear,
    RenderGroupEntryType_render_entry_bitmap,
    RenderGroupEntryType_render_entry_rectangle,
};

struct render_group_entry_header
{
    render_group_entry_type Type;
};

struct render_entry_clear
{
    render_group_entry_header Header;
    v4 Color;
};

struct render_entry_bitmap
{
    render_group_entry_header Header;

    render_entity_basis EntityBasis;

    loaded_bitmap *Bitmap;
    real32 R, G, B, A;
};

struct render_entry_rectangle
{
    render_group_entry_header Header;
    
    render_entity_basis EntityBasis;

    real32 R, G, B, A;
    v2 Dim;
};

struct render_group
{
    render_basis *DefaultBasis;
    real32 MetersToPixels;

    uint32 MaxPushBufferSize;
    uint32 PushBufferSize;
    uint8 *PushBufferBase;
};

#define HANDMADE_RENDER_GROUP_H
#endif