SET client_min_messages to WARNING;

SELECT NULL FROM topology.CreateTopology('t');
CREATE FUNCTION t.checkConstraints(lbl text)
RETURNS SETOF TEXT AS $BODY$
  SELECT format('%s|%s|%s', lbl, conrelid::regclass::text, conname::text)
  FROM pg_catalog.pg_constraint
  WHERE contype != 'p' AND conrelid IN (
    't.node'::regclass,
    't.edge_data'::regclass,
    't.face'::regclass
  )
  ORDER BY 1;
$BODY$ LANGUAGE 'sql';

SELECT topology.DropTopologyForeignKeyConstraints('t');
SELECT t.checkConstraints('drop1');

SELECT topology.DropTopologyForeignKeyConstraints('t');
SELECT t.checkConstraints('drop2');

SELECT topology.AddTopologyForeignKeyConstraints('t');
SELECT t.checkConstraints('add1');

SELECT topology.AddTopologyForeignKeyConstraints('t');
SELECT t.checkConstraints('add2');


SELECT NULL FROM topology.DropTopology('t');
