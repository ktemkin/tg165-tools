/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2010 Thomas Otto <tommi@viadmin.org>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Define memory regions. */
MEMORY
{
    /* This is a region _past_ the main firmware. We're using a tiny region
     * to build in our "on-reset" selector stub. */
	rom (rx) : ORIGIN = 0x08040000, LENGTH = 2K
	/* rom (rx) : ORIGIN = 0x08010000, LENGTH = 1K */
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* Include the common ld script. */
/* INCLUDE libopencm3_stm32f1.ld */

