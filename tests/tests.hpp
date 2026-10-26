#pragma once

#include <cstdio>

namespace tests
{
	inline int failures = 0;

	void run_pool_extension_tests();
	void run_config_names_tests();
}

#define CHECK(condition) \
	do { if (!(condition)) { ++tests::failures; std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)
