/**
* Copyright (C) 2020 Elisha Riedlinger
*
* This software is  provided 'as-is', without any express  or implied  warranty. In no event will the
* authors be held liable for any damages arising from the use of this software.
* Permission  is granted  to anyone  to use  this software  for  any  purpose,  including  commercial
* applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented; you must not claim that you  wrote the
*      original  software. If you use this  software  in a product, an  acknowledgment in the product
*      documentation would be appreciated but is not required.
*   2. Altered source versions must  be plainly  marked as such, and  must not be  misrepresented  as
*      being the original software.
*   3. This notice may not be removed or altered from any source distribution.
*/

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "Patches.h"
#include "Common\Utils.h"
#include "Logging\Logging.h"

BYTE AllowQuickSaveFlag = TRUE;
void *QuickSaveCmpAddr;
void *jmpSoftLockAddr;
void *TimerMemoryAddr;
void *TextMemoryAddr;
void *callSaveTimerAddr;
void *jmpSaveTimerAddr;
void *callTextOverlapAddr;
void *jmpTextOverlapAddr;
void *jmpQuickSaveAddr;

// ASM function for Quick Save Soft-Lock Fix
__declspec(naked) void __stdcall SoftLockASM()
{
	__asm
	{
		cmp dword ptr ds : [esi + 0x04], 0x00004214 // locked with flashlight on
		je near SoftLockFix1 // jumps to soft-lock fix if locked
		cmp dword ptr ds : [esi + 0x04], 0x00004014 // locked with flashlight off
		je near SoftLockFix2 // jumps to soft-lock fix if locked
		mov ecx, dword ptr ds : [esi + 0x04]
		mov dword ptr ds : [edi - 0x04], ecx
		jmp jmpSoftLockAddr

	SoftLockFix1:
		mov ecx, 0x0000214 // unlocks
		mov dword ptr ds : [edi - 0x04], ecx
		jmp jmpSoftLockAddr
			
	SoftLockFix2:
		mov ecx, 0x0000014 // unlocks
		mov dword ptr ds : [edi - 0x04], ecx
		jmp jmpSoftLockAddr
	}
}

// ASM function for Quick Save Timer Fix
__declspec(naked) void __stdcall SaveTimerASM()
{
	__asm
	{
		push eax
		mov eax, dword ptr ds : [TimerMemoryAddr]
		mov dword ptr ds : [eax], esi
		mov eax, dword ptr ds : [FullscreenImageEventAddr]
		cmp dword ptr ds : [eax], 0x02
		pop eax
		je near Exit // jumps to code exit if interactive text is displayed
		call callSaveTimerAddr

	Exit:
		jmp jmpSaveTimerAddr
	}
}

// ASM function for Quick Save Text Overlap Fix
__declspec(naked) void __stdcall TextOverlapASM()
{
	__asm
	{
		push eax
		mov eax, dword ptr ds : [TextMemoryAddr]
		mov dword ptr ds : [eax], esi
		mov eax, dword ptr ds : [FullscreenImageEventAddr]
		cmp dword ptr ds : [eax], 0x02
		pop eax
		je near Exit // jumps to code exit if interactive text is displayed
		call callTextOverlapAddr

	Exit:
		jmp jmpTextOverlapAddr
	}
}

// ASM function to disable quick saves
__declspec(naked) void __stdcall QuickSaveASM()
{
	__asm
	{
		pushf
		cmp byte ptr ds : [AllowQuickSaveFlag], TRUE
		je near AllowQuickSave
	//DisallowQuickSave:
		popf
		jmp near Exit
	AllowQuickSave:
		popf
		mov eax, dword ptr ds : [QuickSaveCmpAddr]
		cmp dword ptr ds : [eax], esi
	Exit:
		jmp jmpQuickSaveAddr
	}
}

void SetGameLoad()
{
	// Get elevator room save address
	constexpr BYTE GameLoadSearchBytes[]{ 0x83, 0xC4, 0x10, 0xF7, 0xC1, 0x00, 0x00, 0x00, 0x04, 0x5E, 0x74, 0x0F, 0xC7, 0x05 };
	DWORD GameLoadAddr = SearchAndGetAddresses(0x0058312C, 0x005839DC, 0x005832FC, GameLoadSearchBytes, sizeof(GameLoadSearchBytes), 0x94);
	if (!GameLoadAddr)
	{
		Logging::Log() << __FUNCTION__ << " Error: failed to find memory address!";
		return;
	}

	// Fix momentarily "flash" when save file is loaded
	constexpr BYTE FlashFixSearchBytes[]{ 0x5F, 0x5E, 0x5D, 0x33, 0xC0, 0x5B, 0xC3, 0x90, 0x90, 0x33, 0xC0, 0xA3 };
	DWORD FlashFixAddr = SearchAndGetAddresses(0x004EEA37, 0x004EECE7, 0x004EE5A7, FlashFixSearchBytes, sizeof(FlashFixSearchBytes), 0x0B);
	if (!FlashFixAddr)
	{
		Logging::Log() << __FUNCTION__ << " Error: failed to find memory address!";
		return;
	}

	// Disable Quick Save Reset
	constexpr BYTE QuickSaveResetSearchBytes[]{ 0x57, 0x8D, 0x7D, 0x30, 0xEB, 0x03, 0x8D, 0x49, 0x00, 0x0F, 0xBE, 0x46, 0x11, 0x85, 0xC0, 0x0F, 0x8E };
	DWORD QuickSaveResetFunction = SearchAndGetAddresses(0x0053AE37, 0x0053B167, 0x0053AA87, QuickSaveResetSearchBytes, sizeof(QuickSaveResetSearchBytes), 0x21);
	if (!QuickSaveResetFunction)
	{
		Logging::Log() << __FUNCTION__ << " Error: failed to find memory address!";
		return;
	}

	// Quick Save Soft-Lock Fix
	DWORD SoftLockFunction = QuickSaveResetFunction + 5;
	jmpSoftLockAddr = (void*)(SoftLockFunction + 6);

	// Quick Save Timer Fix
	constexpr BYTE SaveTimerSearchBytes[]{ 0x83, 0xC4, 0x04, 0x85, 0xC0, 0x74, 0x4C, 0x39, 0x35 };
	DWORD SaveTimerFunction = SearchAndGetAddresses(0x00402495, 0x00402495, 0x00402495, SaveTimerSearchBytes, sizeof(SaveTimerSearchBytes), -0x15);
	FullscreenImageEventAddr = GetFullscreenImageEventPointer();
	if (!SaveTimerFunction || !FullscreenImageEventAddr)
	{
		Logging::Log() << __FUNCTION__ << " Error: failed to find memory address!";
		return;
	}
	TimerMemoryAddr = (void*)*(DWORD*)(SaveTimerFunction - 0x1B);
	callSaveTimerAddr = (void*)(*(DWORD*)(SaveTimerFunction + 7) + SaveTimerFunction + 0x0B);
	jmpSaveTimerAddr = (void*)(SaveTimerFunction + 0x0B);

	// Quick Save Text Overlap Fix
	DWORD TextOverlapFunction = SaveTimerFunction + 0xB4;
	TextMemoryAddr = (void*)((DWORD)TimerMemoryAddr - 0x08);
	callTextOverlapAddr = (void*)(*(DWORD*)(TextOverlapFunction + 7) + TextOverlapFunction + 0x0B);
	jmpTextOverlapAddr = (void*)(TextOverlapFunction + 0x0B);

	// Location for to disabling quick saves
	constexpr BYTE QuickSaveSearchBytes[]{ 0x83, 0xC4, 0x04, 0x85, 0xC0, 0x74, 0x4C, 0x39, 0x35 };
	DWORD QuickSaveFunction = SearchAndGetAddresses(0x00402495, 0x00402495, 0x00402495, QuickSaveSearchBytes, sizeof(QuickSaveSearchBytes), 0x07);
	if (!QuickSaveFunction)
	{
		Logging::Log() << __FUNCTION__ << " Error: failed to find memory address!";
		return;
	}
	jmpQuickSaveAddr = (void*)(QuickSaveFunction + 0x06);
	QuickSaveCmpAddr = (void*)*(DWORD*)(QuickSaveFunction + 0x02);

	// Update SH2 code
	Logging::Log() << "Enabling Load Game Fix...";
	DWORD Value = 0x00;
	UpdateMemoryAddress((void*)GameLoadAddr, &Value, sizeof(DWORD));
	UpdateMemoryAddress((void*)FlashFixAddr, "\x90\x90\x90\x90\x90", 5);
	UpdateMemoryAddress((void*)QuickSaveResetFunction, "\x90\x90\x90\x90\x90", 5);
	WriteJMPtoMemory((BYTE*)SoftLockFunction, *SoftLockASM, 6);
	WriteJMPtoMemory((BYTE*)SaveTimerFunction, *SaveTimerASM, 6);
	WriteJMPtoMemory((BYTE*)TextOverlapFunction, *TextOverlapASM, 6);
	WriteJMPtoMemory((BYTE*)QuickSaveFunction, *QuickSaveASM, 6);
}

void RunGameLoad()
{
	// Update save code elevator room
	RUNCODEONCE(SetGameLoad());

	// Get game save address
	static BYTE *SaveGameAddress = nullptr;
	if (!SaveGameAddress)
	{
		RUNONCE();

		// Get address for game save
		constexpr BYTE SearchBytes[]{ 0x3C, 0x1B, 0x74, 0x27, 0x3C, 0x25, 0x74, 0x23, 0x3C, 0x30, 0x74, 0x1F, 0x3C, 0x31, 0x74, 0x1B, 0x3C, 0x32, 0x74, 0x17, 0x3C, 0x33, 0x74, 0x13, 0x3C, 0x34, 0x74, 0x0F };
		SaveGameAddress = (BYTE*)ReadSearchedAddresses(0x0044C648, 0x0044C7E8, 0x0044C7E8, SearchBytes, sizeof(SearchBytes), -0x0D);
		if (!SaveGameAddress)
		{
			Logging::Log() << __FUNCTION__ " Error: failed to find memory address!";
			return;
		}
	}

	// Get elevator running address
	static BYTE *ElevatorRunning = nullptr;
	if (!ElevatorRunning)
	{
		RUNONCE();

		// Get address for game save
		constexpr BYTE SearchBytes[]{ 0xF7, 0xC6, 0x00, 0x0C, 0x00, 0x00, 0x0F, 0x95, 0xC0, 0x49, 0x74, 0x0A, 0x49, 0x75, 0x25, 0x84, 0xC0 };
		ElevatorRunning = (BYTE*)ReadSearchedAddresses(0x0052EA81, 0x0052EDB1, 0x0052E6D1, SearchBytes, sizeof(SearchBytes), -0x0E);
		if (!ElevatorRunning)
		{
			Logging::Log() << __FUNCTION__ " Error: failed to find memory address!";
			return;
		}
	}

	// Get in-game voice event address
	static BYTE *InGameVoiceEvent = nullptr;
	if (!InGameVoiceEvent)
	{
		RUNONCE();

		// Get address for in game voice event
		constexpr BYTE SearchBytes[]{ 0xB9, 0xA0, 0x02, 0x00, 0x00, 0x33, 0xC0, 0xBF };
		InGameVoiceEvent = (BYTE*)ReadSearchedAddresses(0x00563CB4, 0x00562804, 0x00562124, SearchBytes, sizeof(SearchBytes), 0x08);
		if (!InGameVoiceEvent)
		{
			Logging::Log() << __FUNCTION__ " Error: failed to find memory address!";
			return;
		}

		InGameVoiceEvent = (BYTE*)((DWORD)InGameVoiceEvent + 0x90);
	}

	// Set static variables
	static bool ValueSet = false;
	static bool ValueUnSet = false;

	bool DisableQuickSave = false;

	// Enable game saves for specific rooms
	if (GetRoomID() == 0x29)
	{
		*SaveGameAddress = 1;
		ValueSet = true;
	}
	// Disable game saves for specific rooms
	else if (GetRoomID() == 0x13 || GetRoomID() == 0x17 || GetRoomID() == 0xAA || GetRoomID() == 0xC7 ||
		(GetRoomID() == 0x78 && GetJamesPosX() < -18600.0f) ||
		(GetRoomID() == 0x04 && GetJamesPosZ() > 49000.0f))
	{
		*SaveGameAddress = 0;
		ValueUnSet = true;
	}
	// Disable game saves for specific rooms and disable quick save if the Elevator is not running or there is in-game voice event happening
	else if (GetRoomID() == 0x2A || GetRoomID() == 0x46 ||
		(GetRoomID() == 0x9D && GetJamesPosX() < 60650.0f) ||
		(GetRoomID() == 0xB8 && GetJamesPosX() > -15800.0f))
	{
		*SaveGameAddress = 0;
		ValueUnSet = true;

		// Disable game saves for specific rooms and disable quick save if the Elevator is running or there is in-game voice event happening
		if (*ElevatorRunning == 0 || *InGameVoiceEvent == 1)
		{
			DisableQuickSave = true;
			AllowQuickSaveFlag = FALSE;
		}
	}
	// Reset static variables
	else
	{
		if (ValueSet)
		{
			ValueSet = false;
		}
		if (ValueUnSet)
		{
			*SaveGameAddress = 1;
			ValueUnSet = false;
		}
	}

	// Disable quick save during certian in-game voice events and during fullscreen image events
	if ((((GetRoomID() == 0x0A) || (GetRoomID() == 0xBA)) && *InGameVoiceEvent == 1) || GetFullscreenImageEvent() == 2)
	{
		DisableQuickSave = true;
		AllowQuickSaveFlag = FALSE;
	}

	// Reset quick save when needed
	if (!DisableQuickSave && !AllowQuickSaveFlag)
	{
		AllowQuickSaveFlag = TRUE;
	}
}
