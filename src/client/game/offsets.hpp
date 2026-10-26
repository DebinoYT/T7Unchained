#pragma once

#include <cstdint>

namespace game::offsets
{
	enum class build
	{
		unknown,
		march_2023,
		february_2026,
		september_2026
	};

	build detect(std::uintptr_t module);
	build active_build();
	const char* build_name();
	const char* build_id();
}
