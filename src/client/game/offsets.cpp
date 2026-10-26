#include <std_include.hpp>

#include "offsets.hpp"

namespace game::offsets
{
	namespace
	{
		build identify(const std::uint32_t timestamp, const std::uint32_t image_size)
		{
			const auto aligned_size = (static_cast<std::uint64_t>(image_size) + 0xFFF) &
				~std::uint64_t(0xFFF);
			if (timestamp == 0x63F95E3C && aligned_size == 0x1FAB7000)
				return build::march_2023;
			if (timestamp == 0x693D731E && aligned_size == 0x1D74B000)
				return build::february_2026;
			if (timestamp == 0x6A7B6355 && aligned_size == 0x1D75C000)
				return build::september_2026;
			return build::unknown;
		}
	}

	build detect(const std::uintptr_t module)
	{
		__try
		{
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
			if (!module || !dos || dos->e_magic != IMAGE_DOS_SIGNATURE ||
				dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000)
				return build::unknown;

			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE ||
				nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
				nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
				nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
				return build::unknown;

			return identify(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return build::unknown;
		}
	}

	build active_build()
	{
		static const auto version = detect(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
		return version;
	}

	const char* build_name()
	{
		switch (active_build())
		{
		case build::march_2023: return "March 2023";
		case build::february_2026: return "February 2026";
		case build::september_2026: return "September 2026";
		default: return "unsupported";
		}
	}

	const char* build_id()
	{
		switch (active_build())
		{
		case build::march_2023: return "march_2023";
		case build::february_2026: return "february_2026";
		case build::september_2026: return "september_2026";
		default: return "unknown";
		}
	}
}
