/*
 * Copyright (c) 2017 Rafael Antoniello
 *
 * This file is part of MediaProcessors.
 *
 * MediaProcessors is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MediaProcessors is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with MediaProcessors. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file utests.cpp
 * @brief Unit-testing main function
 * @author Rafael Antoniello
 */

#include <cstdio>

#include <UnitTest++/UnitTest++.h>

extern "C" {
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
}

int main(int argc, char *argv[])
{
	int ret_code;

	if(log_module_open()!= STAT_SUCCESS) {
		printf("Could not initialize LOG module\n");
		return STAT_ERROR;
	}

	if((ret_code= UnitTest::RunAllTests())!= 0) {
		printf("Error occurred in u-testing (exit code: %d)\n", ret_code);
		return ret_code;
	}

	log_module_close();
	return 0;
}
