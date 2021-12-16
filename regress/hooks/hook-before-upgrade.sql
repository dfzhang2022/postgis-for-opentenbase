CREATE TABLE upgrade_test(g1 geometry, g2 geography);
INSERT INTO upgrade_test(g1,g2) VALUES
('POINT(0 0)', 'LINESTRING(0 0, 1 1)'),
('POINT(1 0)', 'LINESTRING(0 1, 1 1)');

-- We know upgrading with an st_union() based view
-- fails unless you're on PostgreSQL 12, so we don't
-- even try that.
--
-- We could re-enable this test IF we fix the upgrade
-- in pre-12 versions. Refer to
-- https://trac.osgeo.org/postgis/ticket/4386
--
DO $BODY$
DECLARE
	vernum INT;
BEGIN
	show server_version_num INTO vernum;
	IF vernum >= 120000
	THEN
		RAISE DEBUG '12+ server (%)', vernum;
    CREATE VIEW upgrade_view_test_union AS
    SELECT ST_Union(g1) FROM upgrade_test;
	END IF;
END;
$BODY$ LANGUAGE 'plpgsql';

-- Add view using overlay functions
CREATE VIEW upgrade_view_test_overlay AS
SELECT
	ST_Intersection(g1, g1) as geometry_intersection,
	ST_Intersection(g2, g2) as geography_intersection,
	ST_Difference(g1, g1) as geometry_difference,
	ST_SymDifference(g1, g1) as geometry_symdifference
FROM upgrade_test;

-- Add view using unaryunion function
-- NOTE: 2.0.0 introduced ST_UnaryUnion
CREATE VIEW upgrade_view_test_unaryunion AS
SELECT
	ST_UnaryUnion(g1) as geometry_unaryunion
FROM upgrade_test;

-- Add view using unaryunion function
-- NOTE: 2.2.0 introduced ST_Subdivide
CREATE VIEW upgrade_view_test_subdivide AS
SELECT
	ST_Subdivide(g1, 256) as geometry_subdivide
FROM upgrade_test;
