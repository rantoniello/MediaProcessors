/*
 * Copyright (c) 2015 Rafael Antoniello
 *
 * This file is part of StreamProcessor.
 *
 * StreamProcessor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * StreamProcessor is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with StreamProcessor.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tsudpsend.h
 * @brief Header file for 3rd party source code "tsudpsend.c".
 */

#ifndef SPUTIL_SRC_TSUDPSEND_H_
#define SPUTIL_SRC_TSUDPSEND_H_

int tsudpsend(int argc, char *argv[], volatile int *exit_flag);

#endif /* SPUTIL_SRC_TSUDPSEND_H_ */
