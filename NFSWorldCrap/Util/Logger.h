#pragma once

#include "../stdafx.h"
#include <memory>

namespace spdlog { class logger; }

namespace NFS {
	namespace Util {
		class Logger {
		public:
			static Logger& Instance();

			spdlog::logger* Get();
			void InitializeConsole();
			void Shutdown();

		private:
			Logger();
			~Logger();

			Logger(const Logger&) = delete;
			Logger& operator=(const Logger&) = delete;

			struct Deleter {
				void operator()(Logger* p) const { delete p; }
			};
			static std::unique_ptr<Logger, Deleter> _instance;

			void PositionConsoleOnSecondaryMonitor();

			std::shared_ptr<spdlog::logger> _logger;
			bool _consoleAllocated = false;
			static uintptr_t _moduleBase;
		};
	}
}