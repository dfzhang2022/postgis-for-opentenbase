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
 * Copyright (C) 2015-2024 Sandro Santilli <strk@kbt.io>
 *
 **********************************************************************/

#include "../postgis_config.h"

/*#define POSTGIS_DEBUG_LEVEL 1*/
#include "lwgeom_log.h"

#include "liblwgeom_internal.h"
#include "liblwgeom_topo_internal.h"
#include "lwgeom_geos.h"
#include "measures.h" /* for DISTPTS */

/**
 * toposnap implementation
 */

/*
 * Reference vertex
 *
 * It is the vertex of a topology edge
 * which is within snap tolerance distance from
 * a segment of the input geometry.
 *
 * We store the input geometry segment and the distance
 * (both needed to compute the distance) within the structure.
 *
 */
typedef struct {
  POINT2D pt;
  /* Closest segment in input pointarray (0-based index) */
  int segno;
  double dist;
} LWT_SNAPV;

/* An array of LWT_SNAPV structs */
typedef struct {
  LWT_SNAPV *pts;
  int size;
  int capacity;
} LWT_SNAPV_ARRAY;

#define LWT_SNAPV_ARRAY_INIT(a) { \
  (a)->size = 0; \
  (a)->capacity = 1; \
  (a)->pts = lwalloc(sizeof(LWT_SNAPV) * (a)->capacity); \
}

#define LWT_SNAPV_ARRAY_CLEAN(a) { \
  lwfree((a)->pts); \
  (a)->pts = NULL; \
  (a)->size = 0; \
  (a)->capacity = 0; \
}

#define LWT_SNAPV_ARRAY_PUSH(a, r) { \
  if ( (a)->size + 1 > (a)->capacity ) { \
    (a)->capacity *= 2; \
    (a)->pts = lwrealloc((a)->pts, sizeof(LWT_SNAPV) * (a)->capacity); \
  } \
  (a)->pts[(a)->size++] = (r); \
}

typedef struct
{

  /*
   * Input parameters / configuration
   */
  const LWT_TOPOLOGY *topo;
  double tolerance_snap;
  double tolerance_removal;
  int iterate;

  /*
   * Extent of the geometry being snapped,
   * will be updated as needed as snapping occurs
   */
  GBOX workext;
  GBOX expanded_workext;

  /*
   * Edges within workext,
   * will be updated as needed as workext extends
   * (maybe should be put in an STRtree)
   */
  LWT_ISO_EDGE *workedges;
  uint64_t num_workedges;

} lwgeom_tpsnap_state;

/*
 * Write number of edges in *num_edges, -1 on error.
 * @return edges, or NULL if none-or-error (look *num_edges to tell)
 */
static const LWT_ISO_EDGE *
lwgeom_tpsnap_state_get_edges(lwgeom_tpsnap_state *state, int *num_edges)
{
  if ( ! state->workedges ) {
    state->workedges = lwt_be_getEdgeWithinBox2D(state->topo,
              &state->expanded_workext,
              &state->num_workedges,
              LWT_COL_EDGE_ALL, 0);
  }

  *num_edges = state->num_workedges;
  return state->workedges;
}

static void
lwgeom_tpsnap_state_destroy(lwgeom_tpsnap_state *state)
{
  if ( state->workedges ) {
    _lwt_release_edges(state->workedges, state->num_workedges);
  }
}

/*
 * Find closest segment of pa to a given point
 *
 * @return -1 on error, 0 on success
 */
static int
_rt_find_closest_segment(POINT2D *pt, POINTARRAY *pa,
    int *segno, double *dist)
{
  int j;
  POINT2D s0, s1;
  DISTPTS dl;

  *segno = -1;
  *dist = FLT_MAX;

  if ( pa->npoints < 2 ) return 0;

  lw_dist2d_distpts_init(&dl, DIST_MIN);

  /* Find closest segment */
  getPoint2d_p(pa, 0, &s0);
  for (j=0; j<pa->npoints-1; ++j)
  {
    getPoint2d_p(pa, j+1, &s1);

    if ( lw_dist2d_pt_seg(pt, &s0, &s1, &dl) == LW_FALSE )
    {
      lwerror("lw_dist2d_pt_seg failed in _rt_find_closest_segment");
      return -1;
    }

    if ( dl.distance < *dist )
    {
      /* Segment is closest so far */
      *segno = j;
      *dist = dl.distance;
    }

    s0 = s1;
  }

  return 0;
}

/*
 * Extract from edge all vertices where distance from pa <= tolerance_snap
 *
 * @return -1 on error, 0 on success
 */
static int
_rt_extract_vertices_within_dist(lwgeom_tpsnap_state *state,
    LWT_SNAPV_ARRAY *vset, LWLINE *edge, POINTARRAY *pa)
{
  int i;
  POINTARRAY *epa = edge->points; /* edge's point array */

  LWT_SNAPV vert;
  for (i=0; i<epa->npoints; ++i)
  {
    int ret;

    getPoint2d_p(edge->points, i, &(vert.pt));

    /* skip if not covered by expanded_workext */
    if ( vert.pt.x < state->expanded_workext.xmin ||
         vert.pt.x > state->expanded_workext.xmax ||
         vert.pt.y < state->expanded_workext.ymin ||
         vert.pt.y > state->expanded_workext.ymax )
    {
      LWDEBUGF(3, "skip point %g,%g outside expanded workext %g,%g,%g,%g", vert.pt.x, vert.pt.y, state->expanded_workext.xmin,state->expanded_workext.ymin,state->expanded_workext.xmax,state->expanded_workext.ymax);
      continue;
    }

    ret = _rt_find_closest_segment(&(vert.pt), pa, &vert.segno, &vert.dist);
    if ( ret == -1 ) return -1;

    if ( vert.dist <= state->tolerance_snap )
    {
      /* push vert to array */
      LWT_SNAPV_ARRAY_PUSH(vset, vert);
    }

  }

  return 0;
}

/*
 * Find all topology edge vertices where distance from
 * given pointarray <= tolerance_snap
 *
 * @return -1 on error, 0 on success
 */
static int
_rt_find_vertices_within_dist(
      LWT_SNAPV_ARRAY *vset, POINTARRAY *pa,
      lwgeom_tpsnap_state *state)
{
  int num_edges;
  const LWT_ISO_EDGE *edges;
  const LWT_TOPOLOGY *topo = state->topo;
  int i;

  edges = lwgeom_tpsnap_state_get_edges(state, &num_edges);
  if ( num_edges == -1 ) {
    lwerror("Backend error: %s", lwt_be_lastErrorMessage(topo->be_iface));
    return -1;
  }

  for (i=0; i<num_edges; ++i)
  {
    int ret;
    ret = _rt_extract_vertices_within_dist(state, vset, edges[i].geom, pa);
    if ( ret < 0 ) return ret;
  }

  return 0;
}

static int
compare_snapv(const void *si1, const void *si2)
{
  LWT_SNAPV *a = (LWT_SNAPV *)si1;
  LWT_SNAPV *b = (LWT_SNAPV *)si2;

  if ( a->dist < b->dist )
    return -1;
  else if ( a->dist > b->dist )
    return 1;

  if ( a->pt.x < b->pt.x )
    return -1;
  else if ( a->pt.x > b->pt.x )
    return 1;

  if ( a->pt.y < b->pt.y )
    return -1;
  else if ( a->pt.y > b->pt.y )
    return 1;

  return 0;
}

/*
 * @return 0 on success, -1 on error
 */
typedef int (*rtptarray_visitor)(POINTARRAY *pa, void *userdata);

/*
 * Pass each PTARRAY defining linear components of LWGEOM to the given
 * visitor function
 *
 * This is a mutating visit, where pointarrays are passed as non-const
 *
 * Only (multi)linestring and (multi)polygon will be filtered, with
 * other components simply left unvisited.
 *
 * @return 0 on success, -1 on error (if visitor function ever
 *         returned an error)
 *
 * To be exported if useful
 */
static int
lwgeom_visit_lines(LWGEOM *lwgeom,
                   rtptarray_visitor visitor, void *userdata)
{
  int i;
  int ret;
  LWCOLLECTION *coll;
  LWPOLY *poly;
  LWLINE *line;

  switch (lwgeom->type)
  {
  case POLYGONTYPE:
    poly = (LWPOLY*)lwgeom;
    for (i=0; i<poly->nrings; ++i) {
      ret = visitor(poly->rings[i], userdata);
      if ( ret != 0 ) return ret;
    }
    break;

  case LINETYPE:
    line = (LWLINE*)lwgeom;
    return visitor(line->points, userdata);

  case MULTILINETYPE:
  case MULTIPOLYGONTYPE:
  case COLLECTIONTYPE:
    coll = (LWCOLLECTION *)lwgeom;
    for (i=0; i<coll->ngeoms; i++) {
      ret = lwgeom_visit_lines(coll->geoms[i], visitor, userdata);
      if ( ret != 0 ) return ret;
    }
    break;
  }

  return 0;
}

/*
 * Vertex removal phase
 *
 * Remove internal vertices of `pa` that are within state.tolerance_snap
 * distance from edges of state.topo topology.
 *
 * @return -1 on error, number of points removed on success
 */
static int
_lwgeom_tpsnap_ptarray_remove(POINTARRAY *pa,
                  lwgeom_tpsnap_state *state)
{
  int num_edges, i, j, ret;
  const LWT_ISO_EDGE *edges;
  const LWT_TOPOLOGY *topo = state->topo;
  int removed = 0;

  /* Let *Eset* be the set of edges of *Topo-ref*
   *             with distance from *Gcomp* <= *TSsnap*
   */
  edges = lwgeom_tpsnap_state_get_edges(state, &num_edges);
  if ( num_edges == -1 ) {
    lwerror("Backend error: %s", lwt_be_lastErrorMessage(topo->be_iface));
    return -1;
  }

  LWDEBUG(1, "vertices removal phase starts");

  /* For each non-endpoint vertex *V* of *Gcomp* */
  for (i=1; i<pa->npoints-1; ++i)
  {
    POINT2D V;
    LWLINE *closest_segment_edge = NULL;
    int closest_segment_number;
    double closest_segment_distance = state->tolerance_removal+1;

    getPoint2d_p(pa, i, &V);

    LWDEBUGF(2, "Analyzing internal vertex POINT(%.15g %.15g)", V.x, V.y);

    /* Find closest edge segment */
    for (j=0; j<num_edges; ++j)
    {
      LWLINE *E = edges[j].geom;
      int segno;
      double dist;

      ret = _rt_find_closest_segment(&V, E->points, &segno, &dist);
      if ( ret < 0 ) return ret; /* error */

      /* Edge is too far */
      if ( dist > state->tolerance_removal ) {
        LWDEBUGF(2, " Vertex is too far (%g) from edge %d", dist, edges[j].edge_id);
        continue;
      }

      LWDEBUGF(2, " Vertex within distance from segment %d of edge %d",
        segno, edges[j].edge_id);

      if ( dist < closest_segment_distance )
      {
        closest_segment_edge = E;
        closest_segment_number = segno;
        closest_segment_distance = dist;
      }
    }

    if ( closest_segment_edge )
    {{
      POINT4D V4d, Ep1, Ep2, proj;
      POINTARRAY *epa = closest_segment_edge->points;

      /* Let *Proj* be the closest point in *closest_segment_edge* to *V* */
      V4d.x = V.x; V4d.y = V.y; V4d.m = V4d.z = 0.0;
      getPoint4d_p(epa, closest_segment_number, &Ep1);
      getPoint4d_p(epa, closest_segment_number+1, &Ep2);
      closest_point_on_segment(&V4d, &Ep1, &Ep2, &proj);

      LWDEBUGF(2, " Closest point on edge segment LINESTRING(%.15g %.15g, %.15g %.15g) is POINT(%.15g %.15g)",
        Ep1.x, Ep1.y, Ep2.x, Ep2.y, proj.x, proj.y);

      /* Closest point here matches segment endpoint */
      if ( p4d_same(&proj, &Ep1) || p4d_same(&proj, &Ep2) ) {
        LWDEBUG(2, " Closest point on edge matches segment endpoint");
        continue;
      }

      /* Remove vertex *V* from *Gcomp* */
      LWDEBUGF(1, " Removing internal point POINT(%.14g %.15g)",
        V.x, V.y);
      ret = ptarray_remove_point(pa, i);
      if ( ret == LW_FAILURE ) return -1;
      /* rewind i */
      --i;
      /* increment removed count */
      ++removed;
    }}
  }

  LWDEBUGF(1, "vertices removal phase ended (%d removed)", removed);

  return removed;
}

/* Return NULL on error, or a GEOSGeometry on success */
static GEOSGeometry *
_rt_segment_to_geosgeom(POINT4D *p1, POINT4D *p2)
{
  POINTARRAY *pa = ptarray_construct(0, 0, 2);
  LWLINE *line;
  GEOSGeometry *ret;
  ptarray_set_point4d(pa, 0, p1);
  ptarray_set_point4d(pa, 1, p2);
  line = lwline_construct(0, NULL, pa);
  ret = LWGEOM2GEOS(lwline_as_lwgeom(line), 0);
  lwline_free(line);
  return ret;
}

/*
 *
 * @return -1 on error, 1 if covered, 0 if not covered
 */
static int
_rt_segment_covered(lwgeom_tpsnap_state *state,
    POINT4D *p1, POINT4D *p2)
{
  const LWT_TOPOLOGY *topo = state->topo;
  int num_edges, i;
  const LWT_ISO_EDGE *edges;
  GEOSGeometry *sg;

  edges = lwgeom_tpsnap_state_get_edges(state, &num_edges);
  if ( num_edges == -1 ) {
    lwerror("Backend error: %s", lwt_be_lastErrorMessage(topo->be_iface));
    return -1;
  }

  /* OPTIMIZE: use prepared geometries */
  /* OPTIMIZE: cache cover state of segments */

  sg = _rt_segment_to_geosgeom(p1, p2);
  for (i=0; i<num_edges; ++i)
  {
    LWGEOM *eg = lwline_as_lwgeom(edges[i].geom);
    GEOSGeometry *geg = LWGEOM2GEOS(eg, 0);
    int covers = GEOSCovers(geg, sg);
    GEOSGeom_destroy(geg);
    if (covers == 2) {
      GEOSGeom_destroy(sg);
      lwerror("Covers error: %s", lwgeom_geos_errmsg);
      return -1;
    }
    if ( covers ) {
      GEOSGeom_destroy(sg);
      return 1;
    }
  }
  GEOSGeom_destroy(sg);

  return 0;
}

/*
  Let *Point.Proj* be the closest point in *Gcomp* to the point
  Let *Point.InSeg* be the segment of *Gcomp* containing *Point.Proj*'
  IF *Point.InSeg* is NOT COVERED BY *Topo-ref* edges:
      IF *Point.Proj* is NOT cohincident with a vertex of *Gcomp*:
         Insert *Point* after the first vertex of *Point.InSeg*

@return 0 if no valid snap was found, <0 on error, >0 if snapped

*/
static int
_rt_snap_to_valid_vertex(POINTARRAY *pa,
  const LWT_SNAPV *v, lwgeom_tpsnap_state *state)
{
  int ret;
  POINT4D p, sp1, sp2, proj;

  p.x = v->pt.x; p.y = v->pt.y; p.m = p.z = 0.0;
  getPoint4d_p(pa, v->segno, &sp1);
  getPoint4d_p(pa, v->segno+1, &sp2);

  LWDEBUGF(2, "Analyzing snap vertex POINT(%.15g %.15g)", p.x, p.y);
  LWDEBUGF(2, " Closest segment %d is LINESTRING(%.15g %.15g, %.15g %.15g)",
    v->segno, sp1.x, sp1.y, sp2.x, sp2.y);

  closest_point_on_segment(&p, &sp1, &sp2, &proj);

  LWDEBUGF(2, " Closest point on segment is POINT(%.15g %.15g)",
    proj.x, proj.y);


  /* Check if closest point matches segment endpoint (could be cached) */
  if ( p4d_same(&proj, &sp1) || p4d_same(&proj, &sp2) )
  {
    LWDEBUG(2, " Closest point matches a segment's endpoint");
    return 0;
  }

  /* Skip if closest segment is covered by topo-ref */
  ret = _rt_segment_covered(state, &sp1, &sp2);
  if ( ret == -1 ) return -1;
  if ( ret == 1 )
  {
    LWDEBUG(2, " Closest segment is covered by topo edges");
    /* it is covered */
    return 0;
  }

  /* Snap ! */
  LWDEBUGF(2, "Snapping input segment %d to POINT(%.15g %.15g)",
    v->segno, p.x, p.y);
  ret = ptarray_insert_point(pa, &p, v->segno+1);
  if ( ret == LW_FAILURE ) return -1;

  return 1;

}

/* @return 0 if no valid snap was found, <0 on error, >0 if snapped */
static int
_rt_snap_to_first_valid_vertex(POINTARRAY *pa,
  LWT_SNAPV_ARRAY *vset, lwgeom_tpsnap_state *state)
{
  int foundSnap = 0;
  int i;

  for (i=0; i<vset->size; ++i)
  {
    LWT_SNAPV *v = &(vset->pts[i]);
    foundSnap = _rt_snap_to_valid_vertex(pa, v, state);
    if ( foundSnap ) {
      if ( foundSnap < 0 ) {
        LWDEBUGF(1, "vertex %d/%d triggered an error while snapping",
          i, vset->size);
        return -1;
      }
      LWDEBUGF(1, "vertex %d/%d was a valid snap",
        i, vset->size);
      break;
    }
  }

  return foundSnap;
}

/*
 * Vertex addition phase
 *
 * @return 0 on success, -1 on error.
 *
 */
static int
_lwgeom_tpsnap_ptarray_add(POINTARRAY *pa,
                  lwgeom_tpsnap_state *state)
{
  int ret;
  int lookingForSnap = 1;

  LWDEBUG(1, "vertices addition phase starts");
  while (lookingForSnap)
  {
    int foundSnap;
    LWT_SNAPV_ARRAY vset;

    lookingForSnap = 0;
    LWT_SNAPV_ARRAY_INIT(&vset);

    ret = _rt_find_vertices_within_dist(&vset, pa, state);
    if ( ret < 0 ) {
      LWT_SNAPV_ARRAY_CLEAN(&vset);
      return -1;
    }
    LWDEBUGF(1, "vertices within dist: %d", vset.size);
    if ( vset.size < 1 ) {
      LWT_SNAPV_ARRAY_CLEAN(&vset);
      break;
    }

    qsort(vset.pts, vset.size, sizeof(LWT_SNAPV), compare_snapv);

    foundSnap = _rt_snap_to_first_valid_vertex(pa, &vset, state);
    LWDEBUGF(1, "foundSnap: %d", foundSnap);

    LWT_SNAPV_ARRAY_CLEAN(&vset);

    if ( foundSnap < 0 ) return foundSnap; /* error */
    if ( foundSnap && state->iterate ) {
      lookingForSnap = 1;
    }
  }
  LWDEBUG(1, "vertices addition phase ends");

  return 0;
}

/*
 * Process a single pointarray with the snap algorithm
 *
 * @return 0 on success, -1 on error.
 */
static int
_lwgeom_tpsnap_ptarray(POINTARRAY *pa, void *udata)
{
  int ret;
  lwgeom_tpsnap_state *state = udata;

  /* Set work extent to that of the POINTARRAY bounding box */
  ptarray_calculate_gbox_cartesian(pa, &(state->workext));
  state->expanded_workext = state->workext;
  gbox_expand(&(state->expanded_workext), state->tolerance_snap);

  LWDEBUGF(1, "Snapping pointarray with %d points", pa->npoints);

  do {
    ret = _lwgeom_tpsnap_ptarray_add(pa, state);
    if ( ret == -1 ) return -1;

    if ( state->tolerance_removal >= 0 )
    {
      ret = _lwgeom_tpsnap_ptarray_remove(pa, state);
      if ( ret == -1 ) return -1;
    }
  } while (ret && state->iterate);

  LWDEBUGF(1, "Snapped pointarray has %d points", pa->npoints);

  return 0;

}


/* public, exported */
static LWGEOM *
lwt_tpsnap(LWT_TOPOLOGY *topo, const LWGEOM *gin,
                         double tolerance_snap,
                         double tolerance_removal,
                         int iterate)
{
  lwgeom_tpsnap_state state;
  LWGEOM *gtmp = lwgeom_clone_deep(gin);
  int ret;

  LWDEBUGF(1, "snapping: tol %g, iterate %d, remtol %g",
    tolerance_snap, iterate, tolerance_removal);

  state.topo = topo;
  state.tolerance_snap = tolerance_snap;
  state.tolerance_removal = tolerance_removal;
  state.iterate = iterate;
  state.workedges = NULL;

  ret = lwgeom_visit_lines(gtmp, _lwgeom_tpsnap_ptarray, &state);

  lwgeom_tpsnap_state_destroy(&state);

  if ( ret ) {
    lwgeom_free(gtmp);
    return NULL;
  }

  return gtmp;
}
