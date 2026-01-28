#include "stdafx.h"
#include "Data/Globals.h"
#include "Util/Logger.h"
#include "Util/MemoryPatcher.h"
#include "Hooks/CameraLogicUpdateHook.h"

DWORD WINAPI InitThread(LPVOID lpParam) {
	uintptr_t base = reinterpret_cast<uintptr_t>(lpParam);
	g_moduleBase = base;

	NFS::Util::Logger::Instance().InitializeConsole();

	// Hooks
	NFS::Hooks::SetupCameraHooks();

	NFS::Util::Logger::Instance().Get()->flush();

	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/) {
	DisableThreadLibraryCalls(hModule);

	if (reason == DLL_PROCESS_ATTACH) {
		uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
		if (base == 0) {
			MessageBoxA(NULL, "Failed to get game module handle.", "NFS World Sucks", MB_ICONERROR);
			return FALSE;
		}

		HANDLE hThread = CreateThread(NULL, 0, InitThread, reinterpret_cast<LPVOID>(base), 0, NULL);
		if (hThread) {
			CloseHandle(hThread);
		}
		else {
			MessageBoxA(NULL, "Failed to create initialization thread.", "NFS World Sucks", MB_ICONERROR);
			return FALSE;
		}
	}
	else if (reason == DLL_PROCESS_DETACH) {
		NFS::Util::Logger::Instance().Shutdown();
	}
	return TRUE;
}