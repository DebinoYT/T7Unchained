#include "tests.hpp"

int main()
{
	tests::run_pool_extension_tests();
	tests::run_config_names_tests();

	if (tests::failures)
	{
		std::printf("%d check(s) failed\n", tests::failures);
		return 1;
	}
	std::printf("all tests passed\n");
	return 0;
}
