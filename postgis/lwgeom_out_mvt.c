#include "mvt.h"

/**
 * @file
 * Geobuf export functions
 */

#include "postgres.h"
#include "utils/builtins.h"
#include "executor/spi.h"

#include "../postgis_config.h"
#include "lwgeom_pg.h"
#include "lwgeom_log.h"
#include "liblwgeom.h"
#include "mvt.h"

/**
 * Encode query result to Mapbox Vector Tile
 */
PG_FUNCTION_INFO_V1(LWGEOM_asMVT);
Datum LWGEOM_asMVT(PG_FUNCTION_ARGS)
{
#ifndef HAVE_LIBPROTOBUF
	lwerror("Missing libprotobuf-c");
	PG_RETURN_NULL();
#else
	bytea *buf;
	size_t buf_size;
	text *query_text;
	char *query;
	text *geom_name_text;
	char *geom_name;

	query_text = PG_GETARG_TEXT_P(0);
	query = text_to_cstring(query_text);
	geom_name_text = PG_GETARG_TEXT_P(1);
	geom_name = text_to_cstring(geom_name_text);

	SPI_connect();
	SPI_execute(query, true, 0);

	buf = encode_to_mvt(&buf_size, geom_name);

	SPI_finish();

	SET_VARSIZE(buf, buf_size + VARHDRSZ);
	PG_RETURN_BYTEA_P(buf);
#endif
}