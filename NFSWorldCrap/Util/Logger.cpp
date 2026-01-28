#include "Logger.h"
#include <iostream>
#include <cstdio>

struct MonitorInfo {
	bool foundSecondary = false;
	RECT secondaryMonitorRect{};
};

BOOL CALLBACK MonitorEnumProc(HMONITOR h, HDC, LPRECT, LPARAM dwData) {
	auto* mi = reinterpret_cast<MonitorInfo*>(dwData);
	MONITORINFOEXA monInfo;
	monInfo.cbSize = sizeof(MONITORINFOEXA);
	GetMonitorInfoA(h, &monInfo);
	if (!(monInfo.dwFlags & MONITORINFOF_PRIMARY)) {
		mi->foundSecondary = true;
		mi->secondaryMonitorRect = monInfo.rcWork;
		return FALSE;
	}
	return TRUE;
}

namespace NFS {
	namespace Util {
		std::unique_ptr<Logger, Logger::Deleter> Logger::_instance;
		uintptr_t Logger::_moduleBase = 0;

		Logger& Logger::Instance() {
			if (!_instance) {
				_instance = std::unique_ptr<Logger, Deleter>(new Logger());
			}
			return *_instance;
		}

		void Logger::InitializeConsole() {
			if (!_consoleAllocated) {
				if (AllocConsole()) {
					FILE* fp;
					freopen_s(&fp, "CONOUT$", "w", stdout);
					freopen_s(&fp, "CONOUT$", "w", stderr);
					std::cout.clear();
					std::cerr.clear();
					std::cin.clear();

					PositionConsoleOnSecondaryMonitor();

					auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
					_logger->sinks().push_back(console_sink);
					_consoleAllocated = true;
					Get()->info("Console logger initialized.");
				}
				else {
					Get()->error("Failed to allocate console.");
				}
			}
		}

		void Logger::PositionConsoleOnSecondaryMonitor() {
			SetConsoleTitleA("NFS World Log");

			MonitorInfo mi;
			EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&mi));

			if (mi.foundSecondary) {
				HWND consoleWindow = GetConsoleWindow();
				if (consoleWindow) {
					SetWindowPos(consoleWindow, NULL,
						mi.secondaryMonitorRect.left,
						mi.secondaryMonitorRect.top,
						0, 0,
						SWP_NOSIZE | SWP_NOZORDER);

					Get()->info("Console positioned on secondary monitor at ({}, {})",
						mi.secondaryMonitorRect.left,
						mi.secondaryMonitorRect.top);
				}
			}
			else {
				Get()->info("Secondary monitor not found, console remains on primary monitor");
			}
		}

		void Logger::Shutdown() {
			Get()->info("Logger shutting down.");
			Get()->flush();
			spdlog::shutdown();
			if (_consoleAllocated) {
				FreeConsole();
				_consoleAllocated = false;
			}
		}

		Logger::Logger() : _consoleAllocated(false) {
			try {
				auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("../NFSWorld.log", true);
				_logger = std::make_shared<spdlog::logger>("NFSWorld", file_sink);
				_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
				_logger->set_level(spdlog::level::trace);
				_logger->flush_on(spdlog::level::info);
				_logger->info("Logger initialized.");
			}
			catch (const spdlog::spdlog_ex& ex) {
				MessageBoxA(NULL, ("Logger initialization failed: " + std::string(ex.what())).c_str(), "Error", MB_ICONERROR);
				std::exit(EXIT_FAILURE);
			}
			catch (...) {
				MessageBoxA(NULL, "Logger initialization failed due to an unknown error!", "Error", MB_ICONERROR);
				std::exit(EXIT_FAILURE);
			}
		}

		Logger::~Logger() {}

		spdlog::logger* Logger::Get() {
			return _logger.get();
		}
	}
}