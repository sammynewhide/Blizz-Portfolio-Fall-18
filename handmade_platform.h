#if !defined(HANDMADE_PLATFORM_H)

// C compliant :^)
#ifdef __cplusplus
extern "C" {
#endif
    
#define internal_func static
#define local_persist static
#define global_variable static
    
#define Pi32 3.14159265359f
    
#if HANDMADE_SLOW
#define Assert(Expression) if (!(Expression)) { *(int *)0 = 0; }
#else
#define Assert(Expression)
#endif
    
#define InvalidCodePath Assert(!"InvalidCodePath");
#define InvalidDefaultCase default: {InvalidCodePath;} break
    
#define Kilobytes(Value) ((Value)*1024LL)
#define Megabytes(Value) (Kilobytes(Value)*1024LL)
#define Gigabytes(Value) (Megabytes(Value)*1024LL)
#define Terabytes(Value) (Gigabytes(Value)*1024LL)
    
#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))
    
    //
#if !defined(COMPILER_MSVC)
#define COMPILER_MSVC 0
#endif
    
#if !defined(COMPILER_LLVM)
#define COMPILER_LLVM 0
#endif
    
#if !COMPILER_MSVC && !COMPILER_LLVM
#if _MSC_VER
#undef COMPILER_MSVC
#define COMPILER_MSVC 1
#else
#undef COMPILER_LLVM
#define COMPILER_LLVM 1
#endif
#endif
    
#if COMPILER_MSVC
#include <intrin.h>
#endif
    //
    
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <float.h>

#define Real32Maximum FLT_MAX
    
    typedef int8_t int8;
    typedef int16_t int16;
    typedef int32_t int32;
    typedef int64_t int64;
    typedef int32 bool32;
    
    typedef uint8_t uint8;
    typedef uint16_t uint16;
    typedef uint32_t uint32;
    typedef uint64_t uint64;
    
    typedef size_t memory_index;
    
    typedef float real32;
    typedef double real64;
    
    typedef struct thread_context
    {
        int Placeholder;
    } thread_context;
    
    // timing, controller,
    // bitmap buffer to use
    // sound buffer to use
    
    inline uint32 SafeTruncateUInt64(uint64 Value)
    
    {
        Assert(Value <= 0xFFFFFFFF);
        uint32 Result = (uint32)Value;
        return(Result);
    }
    
#if HANDMADE_INTERNAL
    typedef struct debug_read_file_result
    {
        uint32 ContentsSize;
        void *Contents;
    } debug_read_file_result;
    
#define DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(thread_context *Thread, void *Memory)
    typedef DEBUG_PLATFORM_FREE_FILE_MEMORY(debug_platform_free_file_memory);
    
#define DEBUG_PLATFORM_READ_ENTIRE_FILE(name) debug_read_file_result name(thread_context *Thread, char *Filename)
    typedef DEBUG_PLATFORM_READ_ENTIRE_FILE(debug_platform_read_entire_file);
    
#define DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) bool32 name(thread_context *Thread, char *Filename, uint32 MemorySize, void *Memory)
    typedef DEBUG_PLATFORM_WRITE_ENTIRE_FILE(debug_platform_write_entire_file);
#endif
    
    // Services that the platform layer provides to the game
    
    // Keyboard input, bitmap buffer, sound buffer
#define BITMAP_BYTES_PER_PIXEL 4
    typedef struct game_offscreen_buffer
    {
        // Pixels are always 32-bits wide, BB GG RR xx
        void *Memory;
        int Width;
        int Height;
        int Pitch;
    } game_offscreen_buffer;
    
    typedef struct game_sound_output_buffer
    {
        int SamplesPerSecond;
        int SampleCount;
        int16 *Samples;
    } game_sound_output_buffer;
    
    typedef struct game_button_state
    {
        int HalfTransitionCount;
        bool32 EndedDown;
    } game_button_state;
    
    typedef struct game_controller_input
    {
        bool32 IsConnected;
        bool32 IsAnalog;
        
        real32 StickAverageX;
        real32 StickAverageY;
        union
        {
            game_button_state Buttons[12];
            struct
            {
                game_button_state MoveUp;
                game_button_state MoveDown;
                game_button_state MoveLeft;
                game_button_state MoveRight;
                
                game_button_state ActionUp;
                game_button_state ActionDown;
                game_button_state ActionLeft;
                game_button_state ActionRight;
                
                game_button_state LeftShoulder;
                game_button_state RightShoulder;
                
                game_button_state Start;
                game_button_state Back;
                
                game_button_state Terminator;
            };
        };
    } game_controller_input;
    
    typedef struct game_input
    {
        game_button_state MouseButtons[5];
        int32 MouseX, MouseY, MouseZ;
        
        bool32 ExecutableReloaded;
        real32 dtForFrame;
        
        game_controller_input Controllers[5];
    } game_input;
    
    typedef struct game_memory
    {
        bool32 IsInitialized;
        uint64 PermanentStorageSize;
        void *PermanentStorage; // Must be initilalized to ZERO on startup!
        
        uint64 TransientStorageSize;
        void *TransientStorage;
        
        debug_platform_free_file_memory *DEBUGPlatformFreeFileMemory;
        debug_platform_read_entire_file *DEBUGPlatformReadEntireFile;
        debug_platform_write_entire_file *DEBUGPlatformWriteEntireFile;
    } game_memory;
    
#define GAME_UPDATE_AND_RENDER(name) void name(thread_context *Thread, game_memory *Memory, game_input *Input, game_offscreen_buffer *Buffer)
    typedef GAME_UPDATE_AND_RENDER(game_update_and_render);
    
    // This has to be a very fast function
#define GAME_GET_SOUND_SAMPLES(name) void name(thread_context *Thread, game_memory *Memory, game_sound_output_buffer *SoundBuffer)
    typedef GAME_GET_SOUND_SAMPLES(game_get_sound_samples);
    
    inline game_controller_input *GetController(game_input *Input, int unsigned ControllerIndex)
    {
        Assert(ControllerIndex < ArrayCount(Input->Controllers));
        game_controller_input *Result = &Input->Controllers[ControllerIndex];
        return(Result);
    }
    
#ifdef __cplusplus
}
#endif

#define HANDMADE_PLATFORM_H
#endif