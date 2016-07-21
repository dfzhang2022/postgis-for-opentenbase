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

#include <math.h>
#include "geobuf.h"

#ifdef HAVE_LIBPROTOBUF

static const uint32_t MAX_PRECISION = 1e6;

typedef struct {
    int row;
    size_t n_keys;
    char **keys;
    size_t n_properties;
    uint32_t *properties;
    char *geom_name;
    uint32_t e;
    protobuf_c_boolean has_precision;
    uint32_t precision;
    protobuf_c_boolean has_dimensions;
    uint32_t dimensions;
} Context;

static void tupdesc_analyze(Context *ctx);
static int get_geom_index(Context *ctx);
static LWGEOM *get_lwgeom(Context *ctx);

static Data__Feature *encode_feature(Context *ctx);
static void encode_properties(Context *ctx, Data__Feature *feature);
static void set_int_value(Data__Value *value, int64 intval);

static void analyze_val(Context *ctx, double val);
static void analyze_pa(Context *ctx, POINTARRAY *pa);
static void analyze_geometry(Context *ctx, LWGEOM *lwgeom);

static Data__Geometry *encode_geometry(Context *ctx, LWGEOM *lwgeom);
static Data__Geometry *encode_point(Context *ctx, LWPOINT *lwgeom);
static Data__Geometry *encode_line(Context *ctx, LWLINE *lwline);
static Data__Geometry *encode_poly(Context *ctx, LWPOLY *lwpoly);
static Data__Geometry *encode_mpoint(Context *ctx, LWMPOINT *lwmgeom);
static Data__Geometry *encode_mline(Context *ctx, LWMLINE *lwmline);
static Data__Geometry *encode_mpoly(Context *ctx, LWMPOLY *lwmpoly);
static Data__Geometry *encode_collection(Context *ctx, LWCOLLECTION *lwcollection);

static int64_t *encode_coords(Context *ctx, POINTARRAY *pa, int64_t *coords, int len, int offset);

static Data__Geometry *galloc(Data__Geometry__Type type);

Data__Geometry *galloc(Data__Geometry__Type type) {
    Data__Geometry *geometry;
    geometry = palloc (sizeof (Data__Geometry));
    data__geometry__init(geometry);
    geometry->type = type;
    return geometry;
};

void tupdesc_analyze(Context *ctx) {
    int i, c, natts;

    natts = SPI_tuptable->tupdesc->natts;
    ctx->keys = palloc(sizeof (char *) * (natts - 1));
    ctx->properties = palloc(sizeof (uint32_t) * (natts - 1) * 2);
    c = 0;
    for (i = 0; i < natts; i++) {
        char *key = SPI_tuptable->tupdesc->attrs[i]->attname.data;
        if (strcmp(key, ctx->geom_name) == 0) continue;
        ctx->keys[c] = key;
        ctx->properties[c * 2] = c;
        ctx->properties[c * 2 + 1] = c;
        c++;
    }
    ctx->n_keys = c;
}

void encode_properties(Context *ctx, Data__Feature *feature) {
    int i, c, natts;
    Data__Value **values;

    natts = SPI_tuptable->tupdesc->natts;
    values = palloc (sizeof (Data__Value *) * (natts - 1));
    c = 0;
    for (i = 0; i < natts; i++) {
        Data__Value *value;
        char *type, *string_value, *key;
        Datum datum;
        bool isnull;
        
        key = SPI_tuptable->tupdesc->attrs[i]->attname.data;
        if (strcmp(key, ctx->geom_name) == 0) continue;
        
        value = palloc (sizeof (Data__Value));
        data__value__init(value);
        
        type = SPI_gettype(SPI_tuptable->tupdesc, i + 1);
        datum = SPI_getbinval(SPI_tuptable->vals[ctx->row], SPI_tuptable->tupdesc, i + 1, &isnull);
        if (strcmp(type, "int2") == 0) {
            set_int_value(value, DatumGetInt16(datum));
        } else if (strcmp(type, "int4") == 0) {
            set_int_value(value, DatumGetInt32(datum));
        } else if (strcmp(type, "int8") == 0) {
            set_int_value(value, DatumGetInt64(datum));
        } else if (strcmp(type, "float4") == 0) {
            value->value_type_case = DATA__VALUE__VALUE_TYPE_DOUBLE_VALUE;
            value->double_value = DatumGetFloat4(datum);
        } else if (strcmp(type, "float8") == 0) {
            value->value_type_case = DATA__VALUE__VALUE_TYPE_DOUBLE_VALUE;
            value->double_value = DatumGetFloat8(datum);
        } else {
            string_value = SPI_getvalue(SPI_tuptable->vals[ctx->row], SPI_tuptable->tupdesc, i + 1);
            value->value_type_case = DATA__VALUE__VALUE_TYPE_STRING_VALUE;
            value->string_value = string_value;
        }
        values[c++] = value;
    }

    feature->n_values = c;
    feature->values = values;
    feature->n_properties = c * 2;
    feature->properties = ctx->properties;
}

void set_int_value(Data__Value *value, int64 intval) {
    if (intval >= 0) {
        value->value_type_case = DATA__VALUE__VALUE_TYPE_POS_INT_VALUE;
        value->pos_int_value = intval;
    } else {
        value->value_type_case = DATA__VALUE__VALUE_TYPE_NEG_INT_VALUE;
        value->neg_int_value = abs(intval);
    }
}

int get_geom_index(Context *ctx) {
    for (int i = 0; i < SPI_tuptable->tupdesc->natts; i++) {
        char *key = SPI_tuptable->tupdesc->attrs[i]->attname.data;
        if (strcmp(key, ctx->geom_name) == 0) return i + 1;
    }
    return -1;
}

LWGEOM *get_lwgeom(Context *ctx) {
    Datum datum;
    GSERIALIZED *geom;
    bool isnull;
    LWGEOM *lwgeom;

    datum = SPI_getbinval(SPI_tuptable->vals[ctx->row], SPI_tuptable->tupdesc, get_geom_index(ctx), &isnull);
    geom = (GSERIALIZED *) PG_DETOAST_DATUM(datum);
    lwgeom = lwgeom_from_gserialized(geom);
    return lwgeom;
}

Data__Geometry *encode_point(Context *ctx, LWPOINT *lwpoint) {
    int npoints;
    Data__Geometry *geometry;
    POINTARRAY *pa;

    geometry = galloc(DATA__GEOMETRY__TYPE__POINT);

    pa = lwpoint->point;
    npoints = pa->npoints;

    if (npoints == 0) return geometry;

    geometry->n_coords = npoints * ctx->dimensions;
    geometry->coords = encode_coords(ctx, pa, NULL, 1, 0);

    return geometry;
}

Data__Geometry *encode_mpoint(Context *ctx, LWMPOINT *lwmpoint) {
    int i, ngeoms;
    POINTARRAY *pa;
    Data__Geometry *geometry;

    geometry = galloc(DATA__GEOMETRY__TYPE__MULTIPOINT);

    ngeoms = lwmpoint->ngeoms;

    if (ngeoms == 0) return geometry;
    
    pa = ptarray_construct_empty(0, 0, ngeoms);

    for (i = 0; i < ngeoms; i++) {
        POINT4D pt;
        getPoint4d_p(lwmpoint->geoms[i]->point, 0, &pt);
        ptarray_append_point(pa, &pt, 0);
    }

    geometry->n_coords = ngeoms * ctx->dimensions;
    geometry->coords = encode_coords(ctx, pa, NULL, ngeoms, 0);

    return geometry;
}

Data__Geometry *encode_line(Context *ctx, LWLINE *lwline) {
    POINTARRAY *pa;
    Data__Geometry *geometry;

    geometry = galloc(DATA__GEOMETRY__TYPE__LINESTRING);

    pa = lwline->points;

    if (pa->npoints == 0) return geometry;

    geometry->n_coords = pa->npoints * ctx->dimensions;
    geometry->coords = encode_coords(ctx, pa, NULL, pa->npoints, 0);

    return geometry;
}

Data__Geometry *encode_mline(Context *ctx, LWMLINE *lwmline) {
    int i, offset, ngeoms;
    POINTARRAY *pa;
    Data__Geometry *geometry;
    uint32_t *lengths;
    int64_t *coords = NULL;

    geometry = galloc(DATA__GEOMETRY__TYPE__MULTILINESTRING);

    ngeoms = lwmline->ngeoms;

    if (ngeoms == 0) return geometry;
    
    lengths = palloc (sizeof (uint32_t) * ngeoms);
    
    offset = 0;
    for (i = 0; i < ngeoms; i++) {
        pa = lwmline->geoms[i]->points;
        coords = encode_coords(ctx, pa, coords, pa->npoints, offset);
        offset += pa->npoints * ctx->dimensions;
        lengths[i] = pa->npoints;
    }

    if (ngeoms > 1) {
        geometry->n_lengths = ngeoms;
        geometry->lengths = lengths;
    }
    
    geometry->n_coords = offset;
    geometry->coords = coords;

    return geometry;
}

Data__Geometry *encode_poly(Context *ctx, LWPOLY *lwpoly) {
    int i, len, nrings, offset;
    POINTARRAY *pa;
    Data__Geometry *geometry;
    uint32_t *lengths;
    int64_t *coords = NULL;

    geometry = galloc(DATA__GEOMETRY__TYPE__POLYGON);

    nrings = lwpoly->nrings;

    if (nrings == 0) return geometry;
    
    lengths = palloc (sizeof (uint32_t) * nrings);
    
    offset = 0;
    for (i = 0; i < nrings; i++) {
        pa = lwpoly->rings[i];
        len = pa->npoints - 1;
        coords = encode_coords(ctx, pa, coords, len, offset);
        offset += len * ctx->dimensions;
        lengths[i] = len;
    }

    if (nrings > 1) {
        geometry->n_lengths = nrings;
        geometry->lengths = lengths;
    }

    geometry->n_coords = offset;
    geometry->coords = coords;

    return geometry;
}

Data__Geometry *encode_mpoly(Context *ctx, LWMPOLY* lwmpoly) {
    int i, j, c, len, offset, n_lengths, ngeoms, nrings;
    POINTARRAY *pa;
    Data__Geometry *geometry;
    uint32_t *lengths;
    int64_t *coords = NULL;

    geometry = galloc(DATA__GEOMETRY__TYPE__MULTIPOLYGON);

    ngeoms = lwmpoly->ngeoms;

    if (ngeoms == 0) return geometry;
    
    n_lengths = 1;
    for (i = 0; i < ngeoms; i++) {
        nrings = lwmpoly->geoms[i]->nrings;
        n_lengths++;
        for (j = 0; j < nrings; j++) {
            n_lengths++;
        }
    }
    
    lengths = palloc (sizeof (uint32_t) * n_lengths);
    
    c = 0;
    offset = 0;
    lengths[c++] = ngeoms;
    for (i = 0; i < ngeoms; i++) {
        nrings = lwmpoly->geoms[i]->nrings;
        lengths[c++] = nrings;
        for (j = 0; j < nrings; j++) {
            pa = lwmpoly->geoms[i]->rings[j];
            len = pa->npoints - 1;
            coords = encode_coords(ctx, pa, coords, len, offset);
            offset += len * ctx->dimensions;
            lengths[c++] = len;
        }
    }

    if (c > 1) {
        geometry->n_lengths = n_lengths;
        geometry->lengths = lengths;
    }

    geometry->n_coords = offset;
    geometry->coords = coords;

    return geometry;
}

Data__Geometry *encode_collection(Context *ctx, LWCOLLECTION* lwcollection) {
    int i, ngeoms;
    Data__Geometry *geometry, **geometries;

    geometry = galloc(DATA__GEOMETRY__TYPE__GEOMETRYCOLLECTION);

    ngeoms = lwcollection->ngeoms;

    if (ngeoms == 0) return geometry;
    
    geometries = palloc (sizeof (Data__Geometry *) * ngeoms);
    for (i = 0; i < ngeoms; i++) {
        LWGEOM *lwgeom = lwcollection->geoms[i];
        Data__Geometry *geom = encode_geometry(ctx, lwgeom);
        geometries[i] = geom;
    }

    geometry->n_geometries = ngeoms;
    geometry->geometries = geometries;

    return geometry;
}

int64_t *encode_coords(Context *ctx, POINTARRAY *pa, int64_t *coords, int len, int offset) {
    int i, c;
    POINT4D pt;
    int64_t sum[] = { 0, 0, 0, 0 };

    if (offset == 0) {
        coords = palloc(sizeof (int64_t) * len * ctx->dimensions);
    } else {
        coords = repalloc(coords, sizeof (int64_t) * ((len * ctx->dimensions) + offset));
    }

    c = offset;
    for (i = 0; i < len; i++) {
        getPoint4d_p(pa, i, &pt);
        sum[0] += coords[c++] = ceil(pt.x * ctx->e) - sum[0];
        sum[1] += coords[c++] = ceil(pt.y * ctx->e) - sum[1];
        if (ctx->dimensions == 3) {
            sum[2] += coords[c++] = ceil(pt.z * ctx->e) - sum[2];
        } else if (ctx->dimensions == 4) {
            sum[3] += coords[c++] = ceil(pt.m * ctx->e) - sum[3];
        }
    }
    return coords;
}

Data__Geometry *encode_geometry(Context *ctx, LWGEOM *lwgeom) {
    int type = lwgeom->type;
    switch (type)
	{
	case POINTTYPE:
		return encode_point(ctx, (LWPOINT*)lwgeom);
	case LINETYPE:
		return encode_line(ctx, (LWLINE*)lwgeom);
	case POLYGONTYPE:
		return encode_poly(ctx, (LWPOLY*)lwgeom);
	case MULTIPOINTTYPE:
		return encode_mpoint(ctx, (LWMPOINT*)lwgeom);
	case MULTILINETYPE:
		return encode_mline(ctx, (LWMLINE*)lwgeom);
	case MULTIPOLYGONTYPE:
		return encode_mpoly(ctx, (LWMPOLY*)lwgeom);
	case COLLECTIONTYPE:
		return encode_collection(ctx, (LWCOLLECTION*)lwgeom);
	default:
		lwerror("encode_geometry: '%s' geometry type not supported",
		        lwtype_name(type));
	}
    return NULL;
}

void analyze_val(Context *ctx, double val) {
    if (ceil(val * ctx->e) / ctx->e != val && ctx->e < MAX_PRECISION) {
        ctx->e *= 10;
    }
}

void analyze_pa(Context *ctx, POINTARRAY *pa) {
    POINT4D pt;
    for (int i = 0; i < pa->npoints; i++) {
        getPoint4d_p(pa, i, &pt);
        analyze_val(ctx, pt.x);
        analyze_val(ctx, pt.y);
        if (ctx->dimensions == 3) {
            analyze_val(ctx, pt.z);
        } else if (ctx->dimensions == 4) {
            analyze_val(ctx, pt.m);
        }
    }
}

void analyze_geometry(Context *ctx, LWGEOM *lwgeom) {
    int i;
    LWLINE *lwline;
    LWPOLY *lwpoly;
    LWCOLLECTION *lwcollection;
    int type = lwgeom->type;
    switch (type)
	{
	case POINTTYPE:
	case LINETYPE:
        lwline = (LWLINE*) lwgeom;
		analyze_pa(ctx, lwline->points);
        break;
	case POLYGONTYPE:
        lwpoly = (LWPOLY*) lwgeom;
        for (i = 0; i < lwpoly->nrings; i++) {
            analyze_pa(ctx, lwpoly->rings[i]);
        }
        break;
	case MULTIPOINTTYPE:
	case MULTILINETYPE:
	case MULTIPOLYGONTYPE:
	case COLLECTIONTYPE:
		lwcollection = (LWCOLLECTION*) lwgeom;
        for (i = 0; i < lwcollection->ngeoms; i++) {
            analyze_geometry(ctx, lwcollection->geoms[i]);
        }
        break;
	default:
		lwerror("analyze_geometry: '%s' geometry type not supported",
		        lwtype_name(type));
	}
}

Data__Feature *encode_feature(Context *ctx) {
    Data__Feature *feature;
    LWGEOM *lwgeom;

    feature = palloc (sizeof (Data__Feature));
    data__feature__init(feature);
    lwgeom = get_lwgeom(ctx);

    if (!ctx->has_dimensions) {
        if (FLAGS_GET_Z(lwgeom->flags) || FLAGS_GET_M(lwgeom->flags)) {
            ctx->dimensions = 3;
        } else if (FLAGS_GET_ZM(lwgeom->flags)) {
            ctx->dimensions = 4;
        } else {
            ctx->dimensions = 2;
        }
        ctx->has_dimensions = 1;
    }

    feature->geometry = encode_geometry(ctx, lwgeom);
    if (ctx->properties != NULL) {
        encode_properties(ctx, feature);
    }
    return feature;
}

void *encode_to_geobuf(size_t *len, char *geom_name) {
    int i, count;
    uint8_t *buf;
    Context ctx;
    
    Data data = DATA__INIT;
    Data__FeatureCollection feature_collection = DATA__FEATURE_COLLECTION__INIT;
    
    ctx.has_dimensions = 0;
    ctx.dimensions = 2;
    ctx.has_precision = 0;
    ctx.precision = MAX_PRECISION;
    ctx.e = 1;
    ctx.geom_name = geom_name;

    count = SPI_processed;

    /* parse columns other than geom to properties */
    if (SPI_tuptable->tupdesc->natts > 1) {
        tupdesc_analyze(&ctx);
        data.n_keys = ctx.n_keys;
        data.keys = ctx.keys;
    }

    /* analyze geometries to find precision */
    lwdebug(3, "analyzing geometries");
    for (i = 0; i < count; i++) {
        ctx.row = i;
        analyze_geometry(&ctx, get_lwgeom(&ctx));
    }
    lwdebug(3, "ctx.e: %d", ctx.e);

    /* create feature collection and encode rows as features in it */
    data.data_type_case = DATA__DATA_TYPE_FEATURE_COLLECTION;
    data.feature_collection = &feature_collection;
    feature_collection.n_features = count;
    feature_collection.features = palloc (sizeof (Data__Feature *) * count);
    for (i = 0; i < count; i++) {
        ctx.row = i;
        feature_collection.features[i] = encode_feature(&ctx);
    }

    /* check and set dimensions if not default */
    if (ctx.dimensions != 2) {
        data.has_dimensions = ctx.has_dimensions;
        data.dimensions = ctx.dimensions;
    }
    lwdebug(3, "data.dimensions: %d", data.dimensions);

    /* check and set precision if not default */
    if (ctx.e > MAX_PRECISION) {
        ctx.e = MAX_PRECISION;
    }
    ctx.precision = ceil(log(ctx.e) / log(10));
    lwdebug(3, "ctx.precision: %d", ctx.precision);
    if (ctx.precision != 6) {
        data.has_precision = 1;
        data.precision = ctx.precision;
    }

    *len = data__get_packed_size(&data);

    buf = SPI_palloc(*len + VARHDRSZ);
    data__pack(&data, buf + VARHDRSZ);
    return buf;
}

#endif