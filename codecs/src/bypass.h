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
 * @file bypass.h
 * @brief "Bypass" or dummy processor.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_CODECS_SRC_BYPASS_H_
#define MEDIAPROCESSORS_CODECS_SRC_BYPASS_H_

/* **** Definitions **** */

/* Forward definitions */
typedef struct proc_if_s proc_if_t;

/* **** prototypes **** */

/**
 * Processor interface implementing the "bypass" processor.
 */
extern const proc_if_t proc_if_bypass;

#endif /* MEDIAPROCESSORS_CODECS_SRC_BYPASS_H_ */
