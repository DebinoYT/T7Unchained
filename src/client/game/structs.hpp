#pragma once

namespace game
{
	enum XAssetType : std::int32_t
	{
		ASSET_TYPE_ATTACHMENT = 0x1F,
		ASSET_TYPE_ATTACHMENT_UNIQUE = 0x20
	};

	struct AssetLink
	{
		AssetLink* next;
	};

	struct XAssetPool
	{
		void* pool;
		std::uint32_t itemSize;
		std::int32_t itemCount;
		bool isSingleton[4];
		std::int32_t itemAllocCount;
		AssetLink* freeHead;
	};

	static_assert(sizeof(XAssetPool) == 0x20);
}
