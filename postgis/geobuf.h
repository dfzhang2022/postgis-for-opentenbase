/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * PostGIS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * PostGIS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PostGIS.  If not, see <http://www.gnu.org/licenses/>.
 *
 **********************************************************************
 *
 * Copyright (C) 2016 Björn Harrtell <bjorn@wololo.org>
 *
 **********************************************************************/

#include <stdlib.h>
#include "postgres.h"
#include "executor/spi.h"

#include "../postgis_config.h"

#include "liblwgeom.h"
#include "lwgeom_pg.h"
#include "lwgeom_log.h"

#ifdef HAVE_LIBPROTOBUF

#include "geobuf.pb-c.h"

void *encode_to_geobuf(size_t *len, char *geom_name);

#endif  /* HAVE_LIBPROTOBUF */