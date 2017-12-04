/*
 * Copyright (c) 2017, 2018 Rafael Antoniello
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
 * @file utests_crc.cpp
 * @brief CRC library unit-testing
 * @author Rafael Antoniello
 */

#include <UnitTest++/UnitTest++.h>

extern "C" {
#include <libmediaprocscrc/crc.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include <libmediaprocsutils/log.h>
}

#define ENABLE_DEBUG_LOGS //uncomment to trace logs
#ifdef ENABLE_DEBUG_LOGS
	#define LOGD_CTX_INIT(CTX) LOG_CTX_INIT(CTX)
	#define LOGD(FORMAT, ...) LOG(FORMAT, ##__VA_ARGS__)
#else
	#define LOGD_CTX_INIT(CTX)
	#define LOGD(...)
#endif

SUITE(UTESTS_CRC_CALCULATOR)
{
	TEST(SIMPLE_CRC_CALCULATION)
	{
#define BUF_FIRST_DIMENSION 4
		int i;
		uint8_t buffer1[]=
			{0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
			 0x39},
		buffer2[]=
			{0x00, 0xB0, 0x0D, 0x59, 0x81, 0xEB, 0x00, 0x00,
			 0x00, 0x01, 0xE0, 0x42},
		buffer3[]=
			{0x00, 0xb0, 0x0d ,0x00, 0x01, 0xc3, 0x00, 0x00,
			 0x00, 0x01, 0xe1, 0x00, 0x76, 0x57, 0x8e, 0x5f},
		buffer4[]=
			{0x00, 0xb0, 0x11, 0x00, 0xbb, 0xc1, 0x00, 0x00,
			 0x00, 0x00, 0xe0, 0x10, 0x03, 0xe8, 0xe0, 0xff,
			 0x74, 0x90, 0x46, 0xca};
		uint8_t *buffer[BUF_FIRST_DIMENSION]=
			{buffer1, buffer2, buffer3, buffer4};
		int buffer_len[BUF_FIRST_DIMENSION]=
			{9, 12, 16, 20};
		crc crc_result;
	    crc crc_expected_result[BUF_FIRST_DIMENSION]=
	    	{0x0376E6E7, 0x5E44059A, 0, 0};
	    LOG_CTX_INIT(NULL);

	    LOGV("Executing UTESTS_CRC_CALCULATOR::SIMPLE_CRC_CALCULATION...\n");

		/* Initialize table (only once) */
		F_CRC_InicializaTabla();

		for(i= 0; i< BUF_FIRST_DIMENSION; i++)
		{
			crc_result= F_CRC_CalculaCheckSum(buffer[i], buffer_len[i]);
			//LOGD("Result[%d]: 0x%X\n", i, crc_result);
			if(crc_result!= crc_expected_result[i]) {
    			LOGE("Error while computing CRC. Computation returned 0x%0x, "
    					"while expected value is 0x%0x\n",
						crc_result, crc_expected_result[i]);
    			CHECK(false); // Set test to fail
			}
		}
		LOGV("... passed O.K.\n");
	}
}
