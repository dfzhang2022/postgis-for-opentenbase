---
--- Regression tests for PostGIS SFCGAL backend
---

-- We only care about testing PostGIS prototype here
-- Behaviour is already handled by SFCGAL own tests

SELECT 'postgis_sfcgal_version', count(*) FROM (SELECT postgis_sfcgal_version()) AS f;
SELECT 'ST_Tesselate', ST_AsText(ST_Tesselate('GEOMETRYCOLLECTION(POINT(4 4),POLYGON((0 0,1 0,1 1,0 1,0 0)))'));
SELECT 'ST_3DArea', ST_3DArea('POLYGON((0 0 0,1 0 0,1 1 0,0 1 0,0 0 0))');
SELECT 'ST_Extrude_point', ST_AsText(ST_Extrude('POINT(0 0)', 1, 0, 0));
SELECT 'ST_Extrude_line', ST_AsText(ST_Extrude(ST_Extrude('POINT(0 0)', 1, 0, 0), 0, 1, 0));
-- In the first SFCGAL versions, the extruded face was wrongly oriented
-- we change the extrusion result to match the original
SELECT 'ST_Extrude_surface',
CASE WHEN postgis_sfcgal_version() = '1.0'
THEN
    ST_AsText(ST_Extrude(ST_Extrude(ST_Extrude('POINT(0 0)', 1, 0, 0), 0, 1, 0), 0, 0, 1))
ELSE
    regexp_replace(
    regexp_replace(
    ST_AsText(ST_Extrude(ST_Extrude(ST_Extrude('POINT(0 0)', 1, 0, 0), 0, 1, 0), 0, 0, 1)) ,
    '\(\(0 1 0,1 1 0,1 0 0,0 1 0\)\)', '((1 1 0,1 0 0,0 1 0,1 1 0))'),
    '\(\(0 1 0,1 0 0,0 0 0,0 1 0\)\)', '((1 0 0,0 0 0,0 1 0,1 0 0))')
END;

SELECT 'ST_ForceLHR', ST_AsText(ST_ForceLHR('POLYGON((0 0,0 1,1 1,1 0,0 0))'));
SELECT 'ST_Orientation_1', ST_Orientation(ST_ForceLHR('POLYGON((0 0,0 1,1 1,1 0,0 0))'));
SELECT 'ST_Orientation_2', ST_Orientation(ST_ForceRHR('POLYGON((0 0,0 1,1 1,1 0,0 0))'));
SELECT 'ST_MinkowskiSum', ST_AsText(ST_MinkowskiSum('LINESTRING(0 0,4 0)','POLYGON((0 0,1 0,1 1,0 1,0 0))'));
SELECT 'ST_StraightSkeleton', ST_AsText(ST_StraightSkeleton('POLYGON((1 1,2 1,2 2,1 2,1 1))'));
SELECT 'ST_ConstrainedDelaunayTriangles', ST_AsText(ST_ConstrainedDelaunayTriangles('GEOMETRYCOLLECTION(POINT(0 0), POLYGON((2 2, 2 -2, 4 0, 2 2)))'));
SELECT 'postgis_sfcgal_noop', ST_NPoints(postgis_sfcgal_noop(ST_Buffer('POINT(0 0)', 5)));
SELECT 'ST_3DConvexHull', ST_AsText(ST_3DConvexHull('MULTIPOINTZ(0 0 5, 1 5 3, 5 7 6, 9 5 3 , 5 7 5, 6 3 5)'));
-- Result of ST_3DUnion is not deterministic and cannot be checked by
-- simple string comparison
SELECT 'ST_3DUnion', ABS(ST_Volume(ST_3DUnion(g1, g2)) - 40) < 1e-12 FROM (
    SELECT ST_Extrude(ST_GeomFromText('POLYGON((-2 -2 -1, 2 -2 -1, 2 2 -1, -2 2 -1, -2 -2 -1))'), 0, 0, 2) AS g1,
    ST_Extrude(ST_GeomFromText('POLYGON((-1 -1 -2, 1 -1 -2, 1 1 -2, -1 1 -2, -1 -1 -2))'), 0, 0, 4) AS g2
) AS f;
SELECT 'ST_3DUnion (aggregate)', ABS(ST_Volume(ST_3DUnion(g))) - 40 < 1e-12 FROM (
    SELECT ST_Extrude(ST_GeomFromText('POLYGON((-2 -2 -1, 2 -2 -1, 2 2 -1, -2 2 -1, -2 -2 -1))'), 0, 0, 2) AS g
    UNION
    SELECT ST_Extrude(ST_GeomFromText('POLYGON((-1 -1 -2, 1 -1 -2, 1 1 -2, -1 1 -2, -1 -1 -2))'), 0, 0, 4) AS g
) AS f;
SELECT 'ST_AlphaShape_default', ST_AsText(ST_AlphaShape('MultiPoint ((6.3 8.4),(7.6 8.8),(6.8 7.3),(5.3 1.8),(9.1 5),(8.1 7),(8.8 2.9),(2.4 8.2),(3.2 5.1),(3.7 2.3),(2.7 5.4),(8.4 1.9),(7.5 8.7),(4.4 4.2),(7.7 6.7),(9 3),(3.6 6.1),(3.2 6.5),(8.1 4.7),(8.8 5.8),(6.8 7.3),(4.9 9.5),(8.1 6),(8.7 5),(7.8 1.6),(7.9 2.1),(3 2.2),(7.8 4.3),(2.6 8.5),(4.8 3.4),(3.5 3.5),(3.6 4),(3.1 7.9),(8.3 2.9),(2.7 8.4),(5.2 9.8),(7.2 9.5),(8.5 7.1),(7.5 8.4),(7.5 7.7),(8.1 2.9),(7.7 7.3),(4.1 4.2),(8.3 7.2),(2.3 3.6),(8.9 5.3),(2.7 5.7),(5.7 9.7),(2.7 7.7),(3.9 8.8),(6 8.1),(8 7.2),(5.4 3.2),(5.5 2.6),(6.2 2.2),(7 2),(7.6 2.7),(8.4 3.5),(8.7 4.2),(8.2 5.4),(8.3 6.4),(6.9 8.6),(6 9),(5 8.6),(4.3 8),(3.6 7.3),(3.6 6.8),(4 7.5),(2.4 6.7),(2.3 6),(2.6 4.4),(2.8 3.3),(4 3.2),(4.3 1.9),(6.5 1.6),(7.3 1.6),(3.8 4.6),(3.1 5.9),(3.4 8.6),(4.5 9),(6.4 9.7))'));
SELECT 'ST_AlphaShape_hole', ST_AsText(ST_AlphaShape('MultiPoint ((6.3 8.4),(7.6 8.8),(6.8 7.3),(5.3 1.8),(9.1 5),(8.1 7),(8.8 2.9),(2.4 8.2),(3.2 5.1),(3.7 2.3),(2.7 5.4),(8.4 1.9),(7.5 8.7),(4.4 4.2),(7.7 6.7),(9 3),(3.6 6.1),(3.2 6.5),(8.1 4.7),(8.8 5.8),(6.8 7.3),(4.9 9.5),(8.1 6),(8.7 5),(7.8 1.6),(7.9 2.1),(3 2.2),(7.8 4.3),(2.6 8.5),(4.8 3.4),(3.5 3.5),(3.6 4),(3.1 7.9),(8.3 2.9),(2.7 8.4),(5.2 9.8),(7.2 9.5),(8.5 7.1),(7.5 8.4),(7.5 7.7),(8.1 2.9),(7.7 7.3),(4.1 4.2),(8.3 7.2),(2.3 3.6),(8.9 5.3),(2.7 5.7),(5.7 9.7),(2.7 7.7),(3.9 8.8),(6 8.1),(8 7.2),(5.4 3.2),(5.5 2.6),(6.2 2.2),(7 2),(7.6 2.7),(8.4 3.5),(8.7 4.2),(8.2 5.4),(8.3 6.4),(6.9 8.6),(6 9),(5 8.6),(4.3 8),(3.6 7.3),(3.6 6.8),(4 7.5),(2.4 6.7),(2.3 6),(2.6 4.4),(2.8 3.3),(4 3.2),(4.3 1.9),(6.5 1.6),(7.3 1.6),(3.8 4.6),(3.1 5.9),(3.4 8.6),(4.5 9),(6.4 9.7))', allow_holes => true));
SELECT 'ST_OptimalAlphaShape_default', ST_AsText(ST_OptimalAlphaShape('MultiPoint ((6.3 8.4),(7.6 8.8),(6.8 7.3),(5.3 1.8),(9.1 5),(8.1 7),(8.8 2.9),(2.4 8.2),(3.2 5.1),(3.7 2.3),(2.7 5.4),(8.4 1.9),(7.5 8.7),(4.4 4.2),(7.7 6.7),(9 3),(3.6 6.1),(3.2 6.5),(8.1 4.7),(8.8 5.8),(6.8 7.3),(4.9 9.5),(8.1 6),(8.7 5),(7.8 1.6),(7.9 2.1),(3 2.2),(7.8 4.3),(2.6 8.5),(4.8 3.4),(3.5 3.5),(3.6 4),(3.1 7.9),(8.3 2.9),(2.7 8.4),(5.2 9.8),(7.2 9.5),(8.5 7.1),(7.5 8.4),(7.5 7.7),(8.1 2.9),(7.7 7.3),(4.1 4.2),(8.3 7.2),(2.3 3.6),(8.9 5.3),(2.7 5.7),(5.7 9.7),(2.7 7.7),(3.9 8.8),(6 8.1),(8 7.2),(5.4 3.2),(5.5 2.6),(6.2 2.2),(7 2),(7.6 2.7),(8.4 3.5),(8.7 4.2),(8.2 5.4),(8.3 6.4),(6.9 8.6),(6 9),(5 8.6),(4.3 8),(3.6 7.3),(3.6 6.8),(4 7.5),(2.4 6.7),(2.3 6),(2.6 4.4),(2.8 3.3),(4 3.2),(4.3 1.9),(6.5 1.6),(7.3 1.6),(3.8 4.6),(3.1 5.9),(3.4 8.6),(4.5 9),(6.4 9.7))'));
SELECT 'ST_OptimalAlphaShape_hole', ST_AsText(ST_OptimalAlphaShape('MultiPoint ((6.3 8.4),(7.6 8.8),(6.8 7.3),(5.3 1.8),(9.1 5),(8.1 7),(8.8 2.9),(2.4 8.2),(3.2 5.1),(3.7 2.3),(2.7 5.4),(8.4 1.9),(7.5 8.7),(4.4 4.2),(7.7 6.7),(9 3),(3.6 6.1),(3.2 6.5),(8.1 4.7),(8.8 5.8),(6.8 7.3),(4.9 9.5),(8.1 6),(8.7 5),(7.8 1.6),(7.9 2.1),(3 2.2),(7.8 4.3),(2.6 8.5),(4.8 3.4),(3.5 3.5),(3.6 4),(3.1 7.9),(8.3 2.9),(2.7 8.4),(5.2 9.8),(7.2 9.5),(8.5 7.1),(7.5 8.4),(7.5 7.7),(8.1 2.9),(7.7 7.3),(4.1 4.2),(8.3 7.2),(2.3 3.6),(8.9 5.3),(2.7 5.7),(5.7 9.7),(2.7 7.7),(3.9 8.8),(6 8.1),(8 7.2),(5.4 3.2),(5.5 2.6),(6.2 2.2),(7 2),(7.6 2.7),(8.4 3.5),(8.7 4.2),(8.2 5.4),(8.3 6.4),(6.9 8.6),(6 9),(5 8.6),(4.3 8),(3.6 7.3),(3.6 6.8),(4 7.5),(2.4 6.7),(2.3 6),(2.6 4.4),(2.8 3.3),(4 3.2),(4.3 1.9),(6.5 1.6),(7.3 1.6),(3.8 4.6),(3.1 5.9),(3.4 8.6),(4.5 9),(6.4 9.7))', allow_holes => true));
