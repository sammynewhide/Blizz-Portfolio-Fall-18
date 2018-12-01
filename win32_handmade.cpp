#include "handmade_platform.h"

#include <windows.h>
#include <stdio.h>
#include <malloc.h>
#include <xinput.h>
#include <dsound.h>

#include "win32_handmade.h"
// NOT A FINAL PLATFORM LAYER!!

// What we are going to do:
// - Saved game locations
// - Getting a handle to our own executable file
// - Asset loading path
// - Threading (launch a thread)
// - Raw input (support for multiple keyboards)
// - ClipCursor() (multi-monitor support)
// - QueryCancelAutoplay
// - WM_ACTIVATEAPP (for when we are not the active application)
// - Blit speed improvements (BitBLT)
// - Hardware-accelerated graphics
// - GetKeyboardLayout (French keyboard, international WASD support)

global_variable bool32 GlobalRunning;
global_variable bool32 GlobalPause;
global_variable win32_offscreen_buffer GlobalBackbuffer;
global_variable LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;
global_variable int64 GlobalPerfCountFrequency;
global_variable bool32 DEBUGGlobalShowCursor;
global_variable WINDOWPLACEMENT GlobalWindowPosition = {sizeof(GlobalWindowPosition)};

// ~~~

#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub)
{
	return(ERROR_DEVICE_NOT_CONNECTED);
}

#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub)
{
	return(ERROR_DEVICE_NOT_CONNECTED);
}

global_variable x_input_get_state *XInputGetState_ = XInputGetStateStub;
global_variable x_input_set_state *XInputSetState_ = XInputSetStateStub;

#define XInputGetState XInputGetState_
#define XInputSetState XInputSetState_

internal_func void Win32LoadXInput(void)
{
	// This should be tested on Windows 8 v
	HMODULE XInputLibrary = LoadLibraryA("x_input1_4.dll");
	if (!XInputLibrary)
	{
		// Do a diagnostic
		HMODULE XInputLibrary = LoadLibraryA("xinput9_1_0.dll");
	}
    
	if (!XInputLibrary)
	{
		// Do a diagnostic
		HMODULE XInputLibrary = LoadLibraryA("xinput1_3.dll");
	}
    
	if (XInputLibrary)
	{
		XInputGetState = (x_input_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
		if (!XInputGetState) { XInputGetState = XInputGetStateStub; }
		
		XInputSetState =
			(x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
		if (!XInputSetState) { XInputSetState = XInputSetStateStub; }
	}
	else
	{
		// Do some diagnostics
	}
}

// ~~~

internal_func void 
CatStrings(size_t SourceACount, char *SourceA,
		   size_t SourceBCount, char *SourceB,
		   size_t DestCount, char *Dest)
{
	for (int Index = 0;
         size_t(Index) < SourceACount;
         ++Index)
	{
		*Dest++ = *SourceA++;
	}
    
	for (int Index = 0;
         size_t(Index) < SourceBCount;
         ++Index)
	{
		*Dest++ = *SourceB++;
	}
    
	*Dest++ = 0;
}

internal_func void
Win32GetEXEFileName(win32_state *State)
{
	DWORD SizeOfFilename = GetModuleFileName(NULL, State->EXEFileName, sizeof(State->EXEFileName));
	State->OnePastLastEXEFileNameSlash = State->EXEFileName;
	for (char *Scan = State->EXEFileName;
         *Scan;
         ++Scan)
	{
		if (*Scan == '\\')
		{
			State->OnePastLastEXEFileNameSlash = Scan + 1;
		}
	}
}

internal_func int
StringLength(char *String)
{
	int Count = 0;
	while (*String++)
	{
		++Count;
	}
	return(Count);
}

internal_func void
Win32BuildEXEPathFileName(win32_state *State, char *FileName, int DestCount, char *Dest)
{
	CatStrings(State->OnePastLastEXEFileNameSlash - State->EXEFileName, State->EXEFileName,
			   StringLength(FileName), FileName, DestCount, Dest);
}

// ~~~

#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

// ~~~
DEBUG_PLATFORM_FREE_FILE_MEMORY(DEBUGPlatformFreeFileMemory)
{
	if (Memory)
	{
		VirtualFree(Memory, NULL, MEM_RELEASE);
	}
}

DEBUG_PLATFORM_READ_ENTIRE_FILE(DEBUGPlatformReadEntireFile)
{
	debug_read_file_result Result = {};
	
	HANDLE FileHandle = CreateFileA(Filename, GENERIC_READ, FILE_SHARE_READ,
									NULL, OPEN_EXISTING, NULL, NULL);
	if (FileHandle != INVALID_HANDLE_VALUE)
	{
		LARGE_INTEGER FileSize = {};
		if (GetFileSizeEx(FileHandle, &FileSize))
		{
			uint32 FileSize32 = SafeTruncateUInt64(FileSize.QuadPart);
			Result.Contents = VirtualAlloc(NULL, FileSize32, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            
			if (Result.Contents)
			{
				DWORD BytesRead;
				if (ReadFile(FileHandle, Result.Contents, FileSize32, &BytesRead, NULL)
					&& (FileSize32 == BytesRead))
				{
					// File read successfully
					Result.ContentsSize = FileSize32;
				}
				else
				{
					DEBUGPlatformFreeFileMemory(Thread, Result.Contents);
					Result.Contents = 0;
				}
			}
			else
			{
				// Logging
			}
			
		}
		else
		{
			// Logging
		}
		CloseHandle(FileHandle);
		
	}
	else
	{
		// Logging
	}
    
	return(Result);
}

inline FILETIME
Win32GetLastWriteTime(char *FileName)
{
	FILETIME LastWriteTime = {};
	
	WIN32_FILE_ATTRIBUTE_DATA Data;
	if(GetFileAttributesEx(FileName, GetFileExInfoStandard, &Data))
	{
		LastWriteTime = Data.ftLastWriteTime;
	}
    
	return(LastWriteTime);
}

internal_func win32_game_code
Win32LoadGameCode(char *SourceDLLName, char *TempDLLName, char *LockFileName)
{
	win32_game_code Result = {};
    
	// We need to check if our lock.tmp file exists, it saves us from an MSVC bug dll is not finished being written
	WIN32_FILE_ATTRIBUTE_DATA Ignored;
	if(!GetFileAttributesEx(LockFileName, GetFileExInfoStandard, &Ignored))
	{
		Result.DLLLastWriteTime = Win32GetLastWriteTime(SourceDLLName);
        
		CopyFile(SourceDLLName, TempDLLName, FALSE);
        
		Result.GameCodeDLL = LoadLibraryA(TempDLLName);
        
		if (Result.GameCodeDLL)
		{
			Result.UpdateAndRender = (game_update_and_render *)GetProcAddress(Result.GameCodeDLL, "GameUpdateAndRender");
			Result.GetSoundSamples = (game_get_sound_samples *)GetProcAddress(Result.GameCodeDLL, "GameGetSoundSamples");
            
			Result.IsValid = (Result.UpdateAndRender && Result.GetSoundSamples);
		}
	}
    
	if (!Result.IsValid)
	{
		Result.UpdateAndRender = 0;
		Result.GetSoundSamples = 0;
	}
    
	return(Result);
}

internal_func void
Win32UnloadGameCode(win32_game_code *GameCode)
{
	if (GameCode->GameCodeDLL)
	{
		FreeLibrary(GameCode->GameCodeDLL);
		GameCode->GameCodeDLL = 0;
	}
    
	GameCode->IsValid = false;
	GameCode->UpdateAndRender = 0;
	GameCode->GetSoundSamples = 0;
}

DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUGPlatformWriteEntireFile)
{
	bool32 Result = false;
    
	HANDLE FileHandle = CreateFileA(Filename, GENERIC_WRITE, NULL,
									NULL, CREATE_ALWAYS, NULL, NULL);
	if (FileHandle != INVALID_HANDLE_VALUE)
	{
		DWORD BytesWritten;
		if (WriteFile(FileHandle, Memory, MemorySize, &BytesWritten, NULL))
		{
			// Success
			Result = (BytesWritten == MemorySize);
		}
		else
		{
			// Logging
		}
        
		CloseHandle(FileHandle);
	}
	else
	{
        
	}
    
	return(Result);
}

internal_func void Win32InitDSound(HWND Window, int32 SamplesPerSecond, int32 BufferSize)
{
	// Load the library
	HMODULE DSoundLibrary = LoadLibraryA("dsound.dll");
    
	if (DSoundLibrary)
	{
		direct_sound_create *DirectSoundCreate = 
			(direct_sound_create *)GetProcAddress(DSoundLibrary, "DirectSoundCreate");
		// Get a DirectSound object
		LPDIRECTSOUND DirectSound;
		if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0)))
		{
			WAVEFORMATEX WaveFormat = {};
			WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
			WaveFormat.nChannels = 2;
			WaveFormat.nSamplesPerSec = SamplesPerSecond;
			WaveFormat.wBitsPerSample = 16;
			WaveFormat.nBlockAlign = (WaveFormat.nChannels*WaveFormat.wBitsPerSample) / 8;
			WaveFormat.nAvgBytesPerSec = (WaveFormat.nBlockAlign*WaveFormat.nSamplesPerSec);
			WaveFormat.cbSize = 0;
            
			if (SUCCEEDED(DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY)))
			{
				DSBUFFERDESC BufferDescription = {};
				BufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;
				BufferDescription.dwSize = sizeof(BufferDescription);
				// DSBCAPS_GLOBALFOCUS??
                
				// This creates the primary buffer V
				LPDIRECTSOUNDBUFFER PrimaryBuffer;
				if (SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDescription, &PrimaryBuffer, 0)))
				{
					HRESULT Error = PrimaryBuffer->SetFormat(&WaveFormat);
					if (SUCCEEDED(Error))
					{
						// We have finally set the format
						OutputDebugStringA("Primary buffer format was set. \n");
					}
					else
					{
						// Diagnostic
					}
				}
				else
				{
					// Diagnostic
				}
			}
			else
			{
				// Diagnostic
			}
            
			// This creates a secondary buffer
			DSBUFFERDESC BufferDescription = {};
			BufferDescription.dwSize = sizeof(BufferDescription);
			BufferDescription.dwFlags = DSBCAPS_GETCURRENTPOSITION2;
			BufferDescription.dwBufferBytes = BufferSize;
			BufferDescription.lpwfxFormat = &WaveFormat;
			HRESULT Error = DirectSound->CreateSoundBuffer(&BufferDescription, &GlobalSecondaryBuffer, 0);
			if (SUCCEEDED(Error))
			{
				OutputDebugStringA("Secondary buffer created successfully.\n");
			}
		}
		else
		{
			// Do a diagnostic
		}
	}
	else
	{
		// Do a diagnostic
	}
}

// ~~~

// Resizing DIB Sections
internal_func void
Win32ResizeDIBSection(win32_offscreen_buffer *Buffer, int Width, int Height)
{
	//  v Frees the memory
	if (Buffer->Memory)
	{
		VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
	}
    
	Buffer->Width = Width;
	Buffer->Height = Height;
	Buffer->BytesPerPixel = 4;
    
	Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
	Buffer->Info.bmiHeader.biWidth = Buffer->Width;
	Buffer->Info.bmiHeader.biHeight = -Buffer->Height; // Top-down
	Buffer->Info.bmiHeader.biPlanes = 1;
	Buffer->Info.bmiHeader.biBitCount = 32;
	Buffer->Info.bmiHeader.biCompression = BI_RGB;
	// Bulletproof this????
	// Maybe don't free first, free after, then free first if that fails.
	int BytesPerPixel = 4;
	int BitmapMemorySize = (Buffer->Width*Buffer->Height)*Buffer->BytesPerPixel;
	Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE); 
	// ^ This is where memory is actually allocated
	Buffer->Pitch = Width*Buffer->BytesPerPixel;
}

internal_func win32_window_dimension
Win32GetWindowDimension(HWND Window)
{
	win32_window_dimension Result;
    
	RECT ClientRect;
	GetClientRect(Window, &ClientRect);
	Result.width = ClientRect.right - ClientRect.left;
	Result.height = ClientRect.bottom - ClientRect.top;
    
	return(Result);
};

// Update window with DIBits
internal_func void
Win32DisplayBuffer(win32_offscreen_buffer *Buffer, HDC DeviceContext, int WindowWidth, int WindowHeight)
{
	if((WindowWidth >= Buffer->Width*2) && (WindowHeight >= Buffer->Height*2))
	{
		StretchDIBits(
			DeviceContext,  // Device Context
			// X, Y, Width, Height, // The window
			// X, Y, Width, Height, // The buffer
			0, 0, 2*Buffer->Width, 2*Buffer->Height,
			0, 0, Buffer->Width, Buffer->Height,
			Buffer->Memory, 
			&Buffer->Info,
			DIB_RGB_COLORS, SRCCOPY);
	}
	else
	{
		int OffsetX = 10;
		int OffsetY = 10;
        
		PatBlt(DeviceContext, 0, 0, WindowWidth, OffsetY, BLACKNESS);
		PatBlt(DeviceContext, 0, OffsetY + Buffer->Height, WindowWidth, WindowHeight, BLACKNESS);
		PatBlt(DeviceContext, 0, 0, OffsetX, WindowHeight, BLACKNESS);
		PatBlt(DeviceContext, OffsetX + Buffer->Width, 0, WindowWidth, WindowHeight, BLACKNESS);
        
		StretchDIBits(
			DeviceContext,  // Device Context
			// X, Y, Width, Height, // The window
			// X, Y, Width, Height, // The buffer
			OffsetX, OffsetY, Buffer->Width, Buffer->Height,
			0, 0, Buffer->Width, Buffer->Height,
			Buffer->Memory, 
			&Buffer->Info,
			DIB_RGB_COLORS, SRCCOPY);
	}
}

internal_func void
Win32ProcessXInputDigitalButton(DWORD XInputButtonState,
								game_button_state *OldState, DWORD ButtonBit,
								game_button_state *NewState)
{
	NewState->EndedDown = ((XInputButtonState & ButtonBit) == ButtonBit);
	NewState->HalfTransitionCount += (OldState->EndedDown != NewState->EndedDown) ? 1 : 0;
}

internal_func real32
Win32ProcessXInputStickValue(SHORT Value, SHORT DeadZoneThreshold)
{
	real32 Result = 0;
	if (Value < -DeadZoneThreshold)
	{
		Result = (real32)(Value + DeadZoneThreshold) / (32768.0f - DeadZoneThreshold);
	}
	else if (Value > DeadZoneThreshold)
	{
		Result = (real32)(Value + DeadZoneThreshold) / (32768.0f - DeadZoneThreshold);
	}
    
	return(Result);
}

internal_func void
Win32GetInputFileLocation(win32_state *State, bool32 InputStream, int SlotIndex, int DestCount, char *Dest)
{
	char Temp[64];
	wsprintf(Temp, "loop_edit_%d.hmi", SlotIndex, InputStream ? "input" : "state");
	Win32BuildEXEPathFileName(State, Temp, DestCount, Dest);
}

internal_func win32_replay_buffer *
Win32GetReplayBuffer(win32_state *State, int unsigned Index)
{
	Assert(Index > 0);
	Assert(Index < ArrayCount(State->ReplayBuffers));
	win32_replay_buffer *Result = &State->ReplayBuffers[Index];
	return(Result);
}

internal_func void
Win32BeginRecordingInput(win32_state *State, int InputRecordingIndex)
{
	win32_replay_buffer *ReplayBuffer = Win32GetReplayBuffer(State, InputRecordingIndex);
	
	if (ReplayBuffer->MemoryBlock)
	{
		State->InputRecordingIndex = InputRecordingIndex;
        
		char FileName[WIN32_STATE_FILE_NAME_COUNT];
		Win32GetInputFileLocation(State, true, InputRecordingIndex, sizeof(FileName), FileName);
        
		State->RecordingHandle = CreateFileA(FileName, GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, NULL, NULL);
        
#if 0
		LARGE_INTEGER FilePosition;
		FilePosition.QuadPart = State->TotalSize;
		SetFilePointerEx(State->RecordingHandle, FilePosition, 0, FILE_BEGIN);
#endif
		CopyMemory(ReplayBuffer->MemoryBlock, State->GameMemoryBlock, (size_t)State->TotalSize);
	}
}

internal_func void
Win32EndRecordingInput(win32_state *State)
{
	CloseHandle(State->RecordingHandle);
	State->InputRecordingIndex = 0;
}

internal_func void
Win32BeginInputPlayback(win32_state *State, int InputPlayingIndex)
{
	win32_replay_buffer *ReplayBuffer = Win32GetReplayBuffer(State, InputPlayingIndex);
	
	if (ReplayBuffer->MemoryBlock)
	{
		State->InputPlayingIndex = InputPlayingIndex;
        
		char FileName[WIN32_STATE_FILE_NAME_COUNT];
		Win32GetInputFileLocation(State, true, InputPlayingIndex, sizeof(FileName), FileName);
        
		State->PlaybackHandle = CreateFileA(FileName, GENERIC_READ, NULL, NULL, OPEN_EXISTING, NULL, NULL);
        
#if 0
		LARGE_INTEGER FilePosition;
		FilePosition.QuadPart = State->TotalSize;
		SetFilePointerEx(State->PlaybackHandle, FilePosition, 0, FILE_BEGIN);
#endif
		CopyMemory(State->GameMemoryBlock, ReplayBuffer->MemoryBlock, (size_t)State->TotalSize);
	}
}

internal_func void
Win32EndInputPlayback(win32_state *State)
{
	CloseHandle(State->PlaybackHandle);
	State->InputPlayingIndex = 0;
}

internal_func void
Win32RecordInput(win32_state *State, game_input *NewInput)
{
	DWORD BytesWritten;
	WriteFile(State->RecordingHandle, NewInput, sizeof(*NewInput), &BytesWritten, NULL);
    
}

internal_func void
Win32PlaybackInput(win32_state *State, game_input *NewInput)
{
	DWORD BytesRead = 0;
	if (ReadFile(State->PlaybackHandle, NewInput, sizeof(*NewInput), &BytesRead, NULL))
	{
		if (BytesRead == 0)
		{
			// End of the stream
			int PlayingIndex = State->InputPlayingIndex;
			Win32EndInputPlayback(State);
			Win32BeginInputPlayback(State, PlayingIndex);
			ReadFile(State->PlaybackHandle, NewInput, sizeof(*NewInput), &BytesRead, NULL);
		}
	}
}

// Windows Message control
LRESULT CALLBACK 
Win32MainWindowCallback(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
	LRESULT Result = 0;
    
	switch (Message)
	{
		case WM_SIZE:
		{
		}	break;
        
		case WM_DESTROY:
		{
			// This should be an error
			GlobalRunning = false;
		}	break;
        
		case WM_CLOSE:
		{
			// Ask a confirmation to the user
			GlobalRunning = false;
		}	break;
        
		case WM_SETCURSOR:
		{
			if(DEBUGGlobalShowCursor)
			{
				Result = DefWindowProcA(Window, Message, wParam, lParam);
			}
			else
			{
				SetCursor(0);
			}
		}	break;
        
		case WM_ACTIVATEAPP:
		{
#if 0			
			if(wParam == TRUE)
			{
				SetLayeredWindowAttributes(Window, RGB(0,0,0), 255, LWA_ALPHA);
			}
			else
			{
				SetLayeredWindowAttributes(Window, RGB(0,0,0), 64, LWA_ALPHA);
			}
#endif			
			OutputDebugStringA("WM_ACTIVATEAPP\n");
		}	break;
        
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
		case WM_KEYDOWN:
		case WM_KEYUP:
		{
			Assert(!"Keyboard input received in non-dispatch event!");
		} break;
        
		case WM_PAINT:
		{
			PAINTSTRUCT Paint; // Calls rectangle paint
			HDC DeviceContext = BeginPaint(Window, &Paint);
            
			win32_window_dimension Dimension = Win32GetWindowDimension(Window);
			
			Win32DisplayBuffer
                (&GlobalBackbuffer, DeviceContext, Dimension.width, Dimension.height);
            
			EndPaint(Window, &Paint);
		}
        
		default:
		{
            //			OutputDebugStringA("DEFAULT\n");
			Result = DefWindowProcA(Window, Message, wParam, lParam);
		}	break;
	}
    
	return (Result);
}

internal_func void Win32ClearBuffer(win32_sound_output *SoundOutput)
{
	VOID *Region1;
	DWORD Region1Size;
	VOID *Region2;
	DWORD Region2Size;
	
	if (SUCCEEDED(GlobalSecondaryBuffer->Lock(0, SoundOutput->SecondaryBufferSize,
                                              &Region1, &Region1Size,
                                              &Region2, &Region2Size, 0)))
	{
		
		uint8 *DestSample = (uint8 *)Region1;
		for (DWORD ByteIndex = 0;
             ByteIndex < Region1Size;
             ++ByteIndex)
		{
			*DestSample++ = 0;
		}
		
		DestSample = (uint8 *)Region2;
		for (DWORD ByteIndex = 0;
             ByteIndex < Region2Size;
             ++ByteIndex)
		{
			*DestSample++ = 0;
		}
		
		GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
	}
}

internal_func void Win32FillSoundBuffer(win32_sound_output *SoundOutput, DWORD BytesToLock, DWORD BytesToWrite,
										game_sound_output_buffer *SourceBuffer)
{
	VOID *Region1;
	DWORD Region1Size;
	VOID *Region2;
	DWORD Region2Size;
    
	if (SUCCEEDED(GlobalSecondaryBuffer->Lock(BytesToLock, BytesToWrite,
                                              &Region1, &Region1Size,
                                              &Region2, &Region2Size, 0)))
	{
		DWORD Region1SampleCount = Region1Size / SoundOutput->BytesPerSample;
		int16 *DestSample = (int16 *)Region1;
		int16 *SourceSample = SourceBuffer->Samples;
        
		for (DWORD SampleIndex = 0;
             SampleIndex < Region1SampleCount;
             ++SampleIndex)
		{
			*DestSample++ = *SourceSample++;
			*DestSample++ = *SourceSample++;
			++SoundOutput->RunningSampleIndex;
		}
        
		DWORD Region2SampleCount = Region2Size / SoundOutput->BytesPerSample;
		DestSample = (int16 *)Region2;
        
		for (DWORD SampleIndex = 0;
             SampleIndex < Region2SampleCount;
             ++SampleIndex)
		{
			*DestSample++ = *SourceSample++;
			*DestSample++ = *SourceSample++;
			++SoundOutput->RunningSampleIndex;
		}
        
		GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
	}
}

internal_func void
Win32ProcessKeyboardMessage(game_button_state *NewState, bool32 IsDown)
{
	if (NewState->EndedDown != IsDown)
	{
		NewState->EndedDown = IsDown;
		++NewState->HalfTransitionCount;
	}
}

internal_func void 
ToggleFullscreen(HWND Window)
{
	// ~~~ Raymond Chen wrote this winapi fullscreen code
	// https://blogs.msdn.microsoft.com/oldnewthing/20100412-00/?p=14353
    
    DWORD Style = GetWindowLong(Window, GWL_STYLE);
    
    if (Style & WS_OVERLAPPEDWINDOW) 
	{
        MONITORINFO MonitorInfo = {sizeof(MonitorInfo)};
        if (GetWindowPlacement(Window, &GlobalWindowPosition) && GetMonitorInfo(MonitorFromWindow(Window, MONITOR_DEFAULTTOPRIMARY), &MonitorInfo)) 
		{
			SetWindowLong(Window, GWL_STYLE, Style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(Window, HWND_TOP,
						 MonitorInfo.rcMonitor.left, MonitorInfo.rcMonitor.top,
                         MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left,
                         MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    } 
	else 
	{
        SetWindowLong(Window, GWL_STYLE, Style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(Window, &GlobalWindowPosition);
        SetWindowPos(Window, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

internal_func void
Win32ProcessPendingMessages(win32_state *State, game_controller_input *KeyboardController)
{
	MSG Message;
    
	while (PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
	{
		switch (Message.message)
		{
			case WM_QUIT:
			{
				GlobalRunning = false;
			} break;
            
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
			case WM_KEYDOWN:
			case WM_KEYUP:
			{
				uint32 VKCode = (uint32)Message.wParam;
				bool32 WasDown = ((Message.lParam & (1 << 30)) != 0);
				bool32 IsDown = ((Message.lParam & (1 << 31)) == 0);
                
				if (WasDown != IsDown)
				{
					if (VKCode == 'W')
					{
						Win32ProcessKeyboardMessage(&KeyboardController->MoveUp, IsDown);
					}
					else if (VKCode == 'A')
					{
						Win32ProcessKeyboardMessage(&KeyboardController->MoveLeft, IsDown);
					}
					else if (VKCode == 'S')
					{
						Win32ProcessKeyboardMessage(&KeyboardController->MoveDown, IsDown);
					}
					else if (VKCode == 'D')
					{
						Win32ProcessKeyboardMessage(&KeyboardController->MoveRight, IsDown);
					}
					else if (VKCode == 'Q')
					{
						Win32ProcessKeyboardMessage(&KeyboardController->LeftShoulder, IsDown);
					}
					else if (VKCode == 'E')
					{
						Win32ProcessKeyboardMessage(&KeyboardController->RightShoulder, IsDown);
					}
					else if (VKCode == VK_UP)
					{
						Win32ProcessKeyboardMessage(&KeyboardController->ActionUp, IsDown);
					}
					else if (VKCode == VK_LEFT)
					{
						Win32ProcessKeyboardMessage(&KeyboardController->ActionLeft, IsDown);
					}
					else if (VKCode == VK_DOWN)
					{
						Win32ProcessKeyboardMessage(&KeyboardController->ActionDown, IsDown);
					}
					else if (VKCode == VK_RIGHT)
					{
						Win32ProcessKeyboardMessage(&KeyboardController->ActionRight, IsDown);
					}
					else if (VKCode == VK_ESCAPE)
					{
						Win32ProcessKeyboardMessage(&KeyboardController->Back, IsDown);
					}
					else if (VKCode == VK_SPACE)
					{
						Win32ProcessKeyboardMessage(&KeyboardController->Start, IsDown);
					}
#if HANDMADE_INTERNAL
					else if (VKCode == 'P')
					{
						if (IsDown)
						{
							GlobalPause = !GlobalPause;
						}
					}
					else if (VKCode == 'L')
					{
						if (IsDown)
						{
							if (State->InputPlayingIndex == 0)
							{
								if (State->InputRecordingIndex == 0)
								{
									Win32BeginRecordingInput(State, 1);
								}
								else
								{
									Win32EndRecordingInput(State);
									Win32BeginInputPlayback(State, 1);
								}
							}
							else
							{
								Win32EndInputPlayback(State);
							}
						}
					}
                    
					if(IsDown)
					{
						bool32 AltKeyWasDown = ((Message.lParam & (1 << 29)) != 0);
						if ((VKCode == VK_F4) && AltKeyWasDown)
						{
							GlobalRunning = false;
						}
						if ((VKCode == VK_RETURN) && AltKeyWasDown)
						{
							if(Message.hwnd)
							{
                                ToggleFullscreen(Message.hwnd);
							}
						}
					}
#endif
				}		
			} break;
            
			default:
			{
				TranslateMessage(&Message);
				DispatchMessageA(&Message);
			} break;
		}
	}
}


inline LARGE_INTEGER Win32GetWallClock(void)
{
	LARGE_INTEGER Result;
	QueryPerformanceCounter(&Result);
	return(Result);
}

inline real32 Win32GetSecondsElapsed(LARGE_INTEGER Start, LARGE_INTEGER End)
{
	real32 Result = ((real32)(End.QuadPart - Start.QuadPart)
                     / (real32)(GlobalPerfCountFrequency));
	return(Result);
}

#if 0
internal_func void
Win32DebugDrawVertical(win32_offscreen_buffer *BackBuffer,
					   int X, int Top, int Bottom, uint32 Color)
{
	if (Top <= 0)
	{
		Top = 0;
	}
	
	if (Bottom > BackBuffer->Height)
	{
		Bottom = BackBuffer->Height;
	}
    
	if ((X >= 0) && (X < BackBuffer->Width))
	{
		uint8 *Pixel = ((uint8 *)BackBuffer->Memory +
                        X * BackBuffer->BytesPerPixel +
                        Top * BackBuffer->Pitch);
        
		for (int Y = Top;
             Y < Bottom;
             ++Y)
		{
			*(uint32 *)Pixel = Color;
			Pixel += BackBuffer->Pitch;
		}
	}
}

inline void
Win32DrawSoundBufferMarker(win32_offscreen_buffer *BackBuffer,
						   win32_sound_output *SoundOutput, 
						   real32 C, int PadX, int Top, int Bottom,
						   DWORD Value, uint32 Color)
{
	real32 XReal32 = (C * (real32)Value);
	int X = PadX + (int)(XReal32);
	Win32DebugDrawVertical(BackBuffer, X, Top, Bottom, Color);
}

internal_func void
Win32DebugSyncDisplay(win32_offscreen_buffer *BackBuffer,
					  int MarkerCount, win32_debug_time_marker *Markers,
					  int CurrentMarkerIndex,
				      win32_sound_output *SoundOutput, real32 TargetSecondsPerFrame)
{
	int PadX = 16;
	int PadY = 16;
	
	int LineHeight = 64;
    
	real32 C = (real32)(BackBuffer->Width - 2*PadX) / (real32)SoundOutput->SecondaryBufferSize;
    
	for (int MarkerIndex = 0;
         MarkerIndex < MarkerCount;
         ++MarkerIndex)
	{
		win32_debug_time_marker *ThisMarker = &Markers[MarkerIndex];
		Assert(ThisMarker->OutputPlayCursor < DWORD(SoundOutput->SecondaryBufferSize));
		Assert(ThisMarker->OutputWriteCursor < DWORD(SoundOutput->SecondaryBufferSize));
		Assert(ThisMarker->OutputLocation < DWORD(SoundOutput->SecondaryBufferSize));
		Assert(ThisMarker->OutputByteCount < DWORD(SoundOutput->SecondaryBufferSize));
		Assert(ThisMarker->FlipPlayCursor < DWORD(SoundOutput->SecondaryBufferSize));
		Assert(ThisMarker->FlipWriteCursor < DWORD(SoundOutput->SecondaryBufferSize));
        
		DWORD PlayColor = 0xFFFFFFFF;
		DWORD WriteColor = 0xFFFF0000;
		DWORD ExpectedFlipColor = 0xFFFFFF00;
		DWORD PlayWindowColor = 0xFFFF00FF;
		
		int Top = PadY;
		int Bottom = PadY + LineHeight;
		
		if (MarkerIndex == CurrentMarkerIndex)
		{
			int FirstTop = Top;
			
			Top += LineHeight + PadY;
			Bottom += LineHeight + PadY;
            
			Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->OutputPlayCursor, PlayColor);
			Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->OutputWriteCursor, WriteColor);
			
			Top += LineHeight + PadY;
			Bottom += LineHeight + PadY;
			
			Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->OutputLocation, PlayColor);
			Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->OutputLocation + ThisMarker->OutputByteCount, WriteColor);
            
			Top += LineHeight + PadY;
			Bottom += LineHeight + PadY;
            
			Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, FirstTop, Bottom, ThisMarker->ExpectedFlipPlayCursor, ExpectedFlipColor);
		}
        
		Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->FlipPlayCursor, PlayColor);
		Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->FlipPlayCursor + 480 * SoundOutput->BytesPerSample, PlayWindowColor);
		Win32DrawSoundBufferMarker(BackBuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->FlipWriteCursor, WriteColor);
	}
}
#endif
// Opens window and create the window loop
int CALLBACK
WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CommandLine, int ShowCode)
{
	LARGE_INTEGER PerfCountFrequencyResult;
	QueryPerformanceFrequency(&PerfCountFrequencyResult);
	GlobalPerfCountFrequency = PerfCountFrequencyResult.QuadPart;
    
	win32_state Win32State = {};
	
	Win32GetEXEFileName(&Win32State);
	
	char SourceGameCodeDLLFullPath[WIN32_STATE_FILE_NAME_COUNT];
	Win32BuildEXEPathFileName(&Win32State, "handmade.dll",
							  sizeof(SourceGameCodeDLLFullPath), SourceGameCodeDLLFullPath);
	
	char TempGameCodeDLLFullPath[WIN32_STATE_FILE_NAME_COUNT];
	Win32BuildEXEPathFileName(&Win32State, "handmade_temp.dll",
							  sizeof(TempGameCodeDLLFullPath), TempGameCodeDLLFullPath);
    
	char GameCodeLockFullPath[WIN32_STATE_FILE_NAME_COUNT];
	Win32BuildEXEPathFileName(&Win32State, "lock.tmp",
							  sizeof(GameCodeLockFullPath), GameCodeLockFullPath);
	// This sets the Windows scheduler granularity to 1ms for our Sleep() call
	UINT DesiredScheduleMS = 1;
	bool32 SleepIsGranular = (timeBeginPeriod(DesiredScheduleMS) == TIMERR_NOERROR);
    
	Win32LoadXInput();
    
#if HANDMADE_INTERNAL
	DEBUGGlobalShowCursor = true;
#endif
	WNDCLASSA WindowClass{};
    
	Win32ResizeDIBSection(&GlobalBackbuffer, 960, 540);
    
	WindowClass.style = CS_HREDRAW|CS_VREDRAW;
	WindowClass.lpfnWndProc = Win32MainWindowCallback;
	WindowClass.hInstance = Instance;
	WindowClass.hCursor = LoadCursor(0, IDC_ARROW);
	//	WindowClass.hIcon
	WindowClass.lpszClassName = "HandmadeHeroWindowClass";
    
    
	if (RegisterClassA(&WindowClass))
	{
		HWND Window =
			CreateWindowExA(
            0,	//WS_EX_TOPMOST|WS_EX_LAYERED, // Extended Style
            WindowClass.lpszClassName, // Name of the class ^
            "Handmade Hero", // Name of the window to create
            WS_OVERLAPPEDWINDOW|WS_VISIBLE, // Flags for window style
            CW_USEDEFAULT, // x position
            CW_USEDEFAULT, // y position
            CW_USEDEFAULT, // width	
            CW_USEDEFAULT, // height
            0, // Parent window option, not needed
            0, // Menu option, not needed
            Instance, // The instance of window class
            0); // Parameter passage, not needed
        
		if (Window)
		{
			int MonitorRefreshHz = 60;
			HDC RefreshDC = GetDC(Window);			
			int Win32RefreshRate = GetDeviceCaps(RefreshDC, VREFRESH);
			ReleaseDC(Window, RefreshDC);
			if (Win32RefreshRate > 1)
			{
				MonitorRefreshHz = Win32RefreshRate;
			}
			real32 GameUpdateHz = ((real32)MonitorRefreshHz / 2.0f);
			real32 TargetSecondsPerFrame = (1.0f / (real32)MonitorRefreshHz);
            
			win32_sound_output SoundOutput = {};
			SoundOutput.SamplesPerSecond = 48000;
			SoundOutput.RunningSampleIndex = 0;
			SoundOutput.BytesPerSample = sizeof(int16) * 2;
			SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample;
			SoundOutput.SafetyBytes = (int)(((real32)SoundOutput.SamplesPerSecond * (real32)SoundOutput.BytesPerSample) / GameUpdateHz / 3.0f);
			Win32InitDSound(Window, SoundOutput.SamplesPerSecond, SoundOutput.SecondaryBufferSize);
			Win32ClearBuffer(&SoundOutput);
			
			GlobalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
            
			GlobalRunning = true;
            
#if 0
			// This is testing for the PlayCursor/WriteCursor frequency
			while (GlobalRunning)
			{
				DWORD PlayCursor;
				DWORD WriteCursor;
				GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor);
                
				char DebugBuffer[256];
				_snprintf_s(DebugBuffer, sizeof(DebugBuffer), "%.02fMS/F, %.02fFPS, %.02fMC/F\n", MSPerFrame, FPS, MCPF);
				OutputDebugStringA(DebugBuffer);
			}
#endif
			int16 *Samples = (int16 *)VirtualAlloc(0, SoundOutput.SecondaryBufferSize,
                                                   MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            
#if HANDMADE_INTERNAL
			LPVOID BaseAddress = (LPVOID)Terabytes(2);
#else
			LPVOID BaseAddress = 0;
#endif
			game_memory GameMemory = {};
			GameMemory.PermanentStorageSize = Megabytes(256);
			GameMemory.TransientStorageSize = Megabytes(500);
            
			GameMemory.DEBUGPlatformFreeFileMemory = DEBUGPlatformFreeFileMemory;
			GameMemory.DEBUGPlatformReadEntireFile = DEBUGPlatformReadEntireFile;
			GameMemory.DEBUGPlatformWriteEntireFile = DEBUGPlatformWriteEntireFile;
            
			// 900 megabytes to prevent hang, virtualalloc runs out of space
			Win32State.TotalSize = GameMemory.PermanentStorageSize + GameMemory.TransientStorageSize;
			Win32State.GameMemoryBlock = VirtualAlloc(BaseAddress, (size_t)Win32State.TotalSize, 
													  MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
			DWORD Error = GetLastError();
			GameMemory.PermanentStorage = Win32State.GameMemoryBlock;
			GameMemory.TransientStorage = ((uint8 *)GameMemory.PermanentStorage + GameMemory.PermanentStorageSize);
            
			for (int ReplayIndex = 1;
				 ReplayIndex < ArrayCount(Win32State.ReplayBuffers);
				 ++ReplayIndex)
			{
				win32_replay_buffer *ReplayBuffer = &Win32State.ReplayBuffers[ReplayIndex];
                
				Win32GetInputFileLocation(&Win32State, false, ReplayIndex, sizeof(ReplayBuffer->FileName), ReplayBuffer->FileName);
                
				ReplayBuffer->FileHandle = CreateFileA(ReplayBuffer->FileName, GENERIC_WRITE | GENERIC_READ, 
                                                       NULL, NULL, CREATE_ALWAYS, NULL, NULL);
				
				LARGE_INTEGER MaxSize;
				MaxSize.QuadPart = Win32State.TotalSize;
				ReplayBuffer->MemoryMap = 
					CreateFileMapping(ReplayBuffer->FileHandle, NULL, PAGE_READWRITE,
                                      MaxSize.HighPart, MaxSize.LowPart, NULL);
				ReplayBuffer->MemoryBlock = 
					MapViewOfFile(ReplayBuffer->MemoryMap, FILE_MAP_ALL_ACCESS, 
								  NULL, NULL, (size_t)Win32State.TotalSize);
				
                //		DWORD Error = GetLastError();
                //		Assert(ReplayBuffer->MemoryBlock);
				
				if (ReplayBuffer->MemoryBlock)
				{
                    
				}
				else
				{
					// Diagnostic
				}
			}
            
			if (Samples && GameMemory.PermanentStorage && GameMemory.TransientStorage)
			{
                
				game_input Input[2] = {};
				game_input *NewInput = &Input[0];
				game_input *OldInput = &Input[1];
                
				LARGE_INTEGER LastCounter = Win32GetWallClock();
				LARGE_INTEGER FlipWallClock = Win32GetWallClock();
                
				int DebugTimeMarkerIndex = 0;
				win32_debug_time_marker DebugTimeMarkers[30] = {};
                
				DWORD AudioLatencyBytes = 0;
				real32 AudioLatencySeconds = 0;
				bool32 SoundIsValid = false;
                
				win32_game_code Game = Win32LoadGameCode(SourceGameCodeDLLFullPath, 
														 TempGameCodeDLLFullPath,
														 GameCodeLockFullPath);
				
				uint64 LastCycleCount = __rdtsc();
                
				while (GlobalRunning)
				{
					NewInput->dtForFrame = TargetSecondsPerFrame;
					
					NewInput->ExecutableReloaded = false;
					FILETIME NewDLLWriteTime = Win32GetLastWriteTime(SourceGameCodeDLLFullPath);
					if (CompareFileTime(&NewDLLWriteTime, &Game.DLLLastWriteTime) == 1)
					{
						Win32UnloadGameCode(&Game);
						Game = Win32LoadGameCode(SourceGameCodeDLLFullPath,
												 TempGameCodeDLLFullPath,
												 GameCodeLockFullPath); 
						NewInput->ExecutableReloaded = true;
					}
                    
					game_controller_input *OldKeyboardController = GetController(OldInput, 0);
					game_controller_input *NewKeyboardController = GetController(NewInput, 0);
					*NewKeyboardController = {};
					NewKeyboardController->IsConnected = true;
                    
					for (int ButtonIndex = 0;
                         ButtonIndex < ArrayCount(NewKeyboardController->Buttons);
                         ++ButtonIndex)
					{
						NewKeyboardController->Buttons[ButtonIndex].EndedDown =
							OldKeyboardController->Buttons[ButtonIndex].EndedDown;
					}
                    
					Win32ProcessPendingMessages(&Win32State, NewKeyboardController);
                    
					if (!GlobalPause)
					{
						POINT MouseP;
						GetCursorPos(&MouseP);
						ScreenToClient(Window, &MouseP);
                        
						NewInput->MouseX = MouseP.x;
						NewInput->MouseY = MouseP.y;
						NewInput->MouseZ = 0;
						Win32ProcessKeyboardMessage(&NewInput->MouseButtons[0],
													GetKeyState(VK_LBUTTON) & (1 << 15));
						Win32ProcessKeyboardMessage(&NewInput->MouseButtons[1],
													GetKeyState(VK_MBUTTON) & (1 << 15));
						Win32ProcessKeyboardMessage(&NewInput->MouseButtons[2],
													GetKeyState(VK_RBUTTON) & (1 << 15));
						Win32ProcessKeyboardMessage(&NewInput->MouseButtons[3],
													GetKeyState(VK_XBUTTON1) & (1 << 15));
						Win32ProcessKeyboardMessage(&NewInput->MouseButtons[4],
													GetKeyState(VK_XBUTTON2) & (1 << 15));
                        
						DWORD MaxControllerCount = XUSER_MAX_COUNT;
						if (MaxControllerCount > (ArrayCount(NewInput->Controllers) - 1))
						{
							MaxControllerCount = (ArrayCount(NewInput->Controllers) - 1);
						}
                        
						for (DWORD ControllerIndex = 0;
                             ControllerIndex < XUSER_MAX_COUNT;
                             ++ControllerIndex)
						{
							DWORD OurControllerIndex = ControllerIndex + 1;
							game_controller_input *OldController = GetController(OldInput, OurControllerIndex);
							game_controller_input *NewController = GetController(NewInput, OurControllerIndex);
                            
							XINPUT_STATE ControllerState;
							if (XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS)
							{
								NewController->IsConnected = true;
								NewController->IsAnalog = OldController->IsAnalog;
								// Controller is plugged in
								XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;
                                
								bool32 LeftShoulder = (Pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
								bool32 RightShoulder = (Pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
                                
								NewController->StickAverageX = Win32ProcessXInputStickValue(Pad->sThumbLX,
                                                                                            XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                                
								NewController->StickAverageY = Win32ProcessXInputStickValue(Pad->sThumbLY,
                                                                                            XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
								if ((NewController->StickAverageX != 0.0f) ||
									NewController->StickAverageY != 0.0f)
								{
									NewController->IsAnalog = true;
								}
                                
								if (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP)
								{
									NewController->StickAverageY = 1.0f;
									NewController->IsAnalog = false;
								}
								if (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)
								{
									NewController->StickAverageY = -1.0f;
									NewController->IsAnalog = false;
								}
								if (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)
								{
									NewController->StickAverageX = -1.0f;
									NewController->IsAnalog = false;
								}
								if (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)
								{
									NewController->StickAverageX = 1.0f;
									NewController->IsAnalog = false;
								}
                                
								real32 Threshold = 0.5f;
								Win32ProcessXInputDigitalButton((NewController->StickAverageX < -Threshold ? 1 : 0),
                                                                &OldController->MoveLeft, 1,
                                                                &NewController->MoveLeft);
								Win32ProcessXInputDigitalButton((NewController->StickAverageX > Threshold ? 1 : 0),
                                                                &OldController->MoveRight, 1,
                                                                &NewController->MoveRight);
								Win32ProcessXInputDigitalButton((NewController->StickAverageY < -Threshold ? 1 : 0),
                                                                &OldController->MoveDown, 1,
                                                                &NewController->MoveDown);
								Win32ProcessXInputDigitalButton((NewController->StickAverageY > Threshold ? 1 : 0),
                                                                &OldController->MoveUp, 1,
                                                                &NewController->MoveUp);
                                
								Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->ActionDown, XINPUT_GAMEPAD_A,
                                                                &NewController->ActionDown);
								Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->ActionRight, XINPUT_GAMEPAD_B,
                                                                &NewController->ActionRight);
								Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->ActionLeft, XINPUT_GAMEPAD_X,
                                                                &NewController->ActionLeft);
								Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->ActionUp, XINPUT_GAMEPAD_Y,
                                                                &NewController->ActionUp);
								Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->LeftShoulder, XINPUT_GAMEPAD_LEFT_SHOULDER,
                                                                &NewController->LeftShoulder);
								Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->RightShoulder, XINPUT_GAMEPAD_RIGHT_SHOULDER,
                                                                &NewController->RightShoulder);
								Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->Start, XINPUT_GAMEPAD_START,
                                                                &NewController->Start);
								Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                                &OldController->Back, XINPUT_GAMEPAD_BACK,
                                                                &NewController->Back);
							}
							else
							{
								NewController->IsConnected = false;
							}
						}
                        
						thread_context Thread = {};
                        
						game_offscreen_buffer Buffer;
						Buffer.Memory = GlobalBackbuffer.Memory;
						Buffer.Width = GlobalBackbuffer.Width;
						Buffer.Height = GlobalBackbuffer.Height;
						Buffer.Pitch = GlobalBackbuffer.Pitch;
						
						if (Win32State.InputRecordingIndex)
						{
							Win32RecordInput(&Win32State, NewInput);
						}
						if (Win32State.InputPlayingIndex)
						{
							Win32PlaybackInput(&Win32State, NewInput);
						}
                        
						if (Game.UpdateAndRender)
						{
							Game.UpdateAndRender(&Thread, &GameMemory, NewInput, &Buffer);
						}
                        
						LARGE_INTEGER AudioWallClock = Win32GetWallClock();
						real32 FromBeginToAudioSeconds = Win32GetSecondsElapsed(FlipWallClock, AudioWallClock);
						
						DWORD PlayCursor;
						DWORD WriteCursor;
						if (GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor) == DS_OK)
						{
							/*
       Here is how the sound output computation works.
       
       When we are ready to write audio, we check where the play cursor is. We will use its position
       to predict where the cursor will be on the next boundary/check.
       
       We will check to see if the write cursor is before the nearest frame flip. If it is before the flip,
       we will write audio up to the flip and one frame further. In one frame flip, the new play cursor should be
       at the position where we finished writing, therefore creating a sync. This should work if the soundcard's latency
       is somewhat low and consistent.
       
       If the write cursor is after the flip, we assume that the audio can never be in sync.
       We write one frame worth of audio plus a certain number of samples to compensate and make a prediction.
       (1ms worth of samples or something).
       */
                            
							if (!SoundIsValid)
							{
								SoundOutput.RunningSampleIndex = WriteCursor / SoundOutput.BytesPerSample;
								SoundIsValid = true;
							}
							
							DWORD ByteToLock =
								(SoundOutput.RunningSampleIndex * SoundOutput.BytesPerSample) % SoundOutput.SecondaryBufferSize;
                            
							DWORD ExpectedSoundBytesPerFrame =
								(DWORD)((real32)(SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample) / GameUpdateHz);
                            
							real32 SecondsLeftUntilFlip = (TargetSecondsPerFrame - FromBeginToAudioSeconds);
							DWORD ExpectedBytesUntilFlip = (DWORD)((SecondsLeftUntilFlip / TargetSecondsPerFrame) * (real32)ExpectedSoundBytesPerFrame);
                            
							DWORD ExpectedFrameBoundaryByte = PlayCursor + ExpectedBytesUntilFlip;
                            
							DWORD SafeWriteCursor = WriteCursor;
							if (SafeWriteCursor < PlayCursor)
							{
								SafeWriteCursor += SoundOutput.SecondaryBufferSize;
							}
							
							Assert(SafeWriteCursor >= PlayCursor);
							SafeWriteCursor += SoundOutput.SafetyBytes;
                            
							bool32 AudioCardIsLowLatency = (SafeWriteCursor < ExpectedFrameBoundaryByte);
                            
							DWORD TargetCursor = 0;
							if (AudioCardIsLowLatency)
							{
								TargetCursor = (ExpectedFrameBoundaryByte + ExpectedSoundBytesPerFrame);
							}
							else
							{
								TargetCursor = (WriteCursor + ExpectedSoundBytesPerFrame + SoundOutput.SafetyBytes);
							}
							
							TargetCursor = (TargetCursor % SoundOutput.SecondaryBufferSize);
                            
							DWORD BytesToWrite = 0;
							if (ByteToLock > TargetCursor)
							{
								BytesToWrite = (SoundOutput.SecondaryBufferSize - ByteToLock);
								BytesToWrite += TargetCursor;
							}
							else
							{
								BytesToWrite = TargetCursor - ByteToLock;
							}
                            
							game_sound_output_buffer SoundBuffer = {};
							SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
							SoundBuffer.SampleCount = (BytesToWrite / SoundOutput.BytesPerSample);
							SoundBuffer.Samples = Samples;
                            
							if (Game.GetSoundSamples)
							{
								Game.GetSoundSamples(&Thread, &GameMemory, &SoundBuffer);
							}
#if HANDMADE_INTERNAL
							win32_debug_time_marker *Marker = &DebugTimeMarkers[DebugTimeMarkerIndex];
							Marker->OutputPlayCursor = PlayCursor;
							Marker->OutputWriteCursor = WriteCursor;
							Marker->OutputLocation = ByteToLock;
							Marker->OutputByteCount = BytesToWrite;
							Marker->ExpectedFlipPlayCursor = ExpectedFrameBoundaryByte;
                            
							DWORD UnwrappedWriteCursor = WriteCursor;
							if (UnwrappedWriteCursor < PlayCursor)
							{
								UnwrappedWriteCursor += SoundOutput.SecondaryBufferSize;
							}
							AudioLatencyBytes = UnwrappedWriteCursor - PlayCursor;
							AudioLatencySeconds = (((real32)AudioLatencyBytes / (real32)SoundOutput.BytesPerSample) /
                                                   (real32)SoundOutput.SamplesPerSecond);
                            
#if 0
							char DebugSoundBuffer[256];
							_snprintf_s(DebugSoundBuffer, sizeof(DebugSoundBuffer),
                                        "BTL:%u TC:%u BTW:%u - PC:%u WC:%u DELTA:%u (%fs)\n",
                                        ByteToLock, TargetCursor, BytesToWrite, PlayCursor, WriteCursor,
                                        AudioLatencyBytes, AudioLatencySeconds);
							OutputDebugStringA(DebugSoundBuffer);
#endif
#endif							
							Win32FillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite, &SoundBuffer);
						}
						else
						{
							SoundIsValid = false;
						}
                        
                        
						LARGE_INTEGER WorkCounter = Win32GetWallClock();
						real32 WorkSecondsElapsed = Win32GetSecondsElapsed(LastCounter, WorkCounter);
                        
						real32 SecondsElapsedForFrame = WorkSecondsElapsed;
						if (SecondsElapsedForFrame < TargetSecondsPerFrame)
						{
							if (SleepIsGranular)
							{
								DWORD SleepMS = (DWORD)((TargetSecondsPerFrame - SecondsElapsedForFrame) * 1000.0f);
								if (SleepMS > 0)
								{
									Sleep(SleepMS);
								}
                                
							}
                            
							real32 TestSecondsElapsedForFrame = Win32GetSecondsElapsed(LastCounter,
                                                                                       Win32GetWallClock());
							if (TestSecondsElapsedForFrame < TargetSecondsPerFrame)
							{
								// Log missed sleep here
							}
                            
							while (SecondsElapsedForFrame < TargetSecondsPerFrame)
							{
                                
								SecondsElapsedForFrame = Win32GetSecondsElapsed(LastCounter,
                                                                                Win32GetWallClock());
							}
						}
						else
						{
							// We missed the frame rate!!
						}
                        
						LARGE_INTEGER EndCounter = Win32GetWallClock();
						real64 MSPerFrame = (1000.0f * Win32GetSecondsElapsed(LastCounter, EndCounter));
						LastCounter = EndCounter;
                        
						win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                        
						HDC DeviceContext = GetDC(Window);
						Win32DisplayBuffer(&GlobalBackbuffer, DeviceContext, Dimension.width, Dimension.height);
						ReleaseDC(Window, DeviceContext);
                        
						FlipWallClock = Win32GetWallClock();
#if HANDMADE_INTERNAL
						{
							DWORD PlayCursor;
							DWORD WriteCursor;
							if (GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor) == DS_OK)
							{
								Assert(DebugTimeMarkerIndex < ArrayCount(DebugTimeMarkers));
								win32_debug_time_marker *Marker = &DebugTimeMarkers[DebugTimeMarkerIndex];
                                
								Marker->FlipPlayCursor = PlayCursor;
								Marker->FlipWriteCursor = WriteCursor;
							}
						}
#endif	
						game_input *Temp = NewInput;
						NewInput = OldInput;
						OldInput = Temp;
                        
#if 0
						uint64 EndCycleCount = __rdtsc();
						int64 CyclesElapsed = EndCycleCount - LastCycleCount;
						LastCycleCount = EndCycleCount;
                        
						real64 FPS = 0.0f;
						real64 MCPF = (real64)(CyclesElapsed / (1000.0f * 1000.0f));
                        
						char DebugBuffer[256];
						_snprintf_s(DebugBuffer, sizeof(DebugBuffer), "%.02fMS/F, %.02fFPS, %.02fMC/F\n", MSPerFrame, FPS, MCPF);
						OutputDebugStringA(DebugBuffer);
#endif
#if HANDMADE_INTERNAL
						++DebugTimeMarkerIndex;
						if (DebugTimeMarkerIndex == (int)(ArrayCount(DebugTimeMarkers)))
						{
							DebugTimeMarkerIndex = 0;
						}
#endif
                    }
                }
                
                // End of while loop
            }
            else
            {
                OutputDebugStringA("Failure allocating Game/Sample memory!\n");
            }
        }	
        else
        {
            OutputDebugStringA("Failure creating window!\n");
        }
    }
	else
	{
		OutputDebugStringA("Registering Windows Class Failure!\n");
	}
    
	return 0;
    
}

