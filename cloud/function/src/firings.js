const { app } = require('@azure/functions');
const { getPool } = require('./db');

async function verifyToken(req) {
  const auth  = req.headers.get('authorization') || '';
  const token = auth.startsWith('Bearer ') ? auth.slice(7) : null;
  if (!token) return false;
  const r = await getPool().query(
    'SELECT 1 FROM auth_tokens WHERE token=$1 AND expires_at>NOW()', [token]
  );
  return r.rowCount > 0;
}

app.http('firings', {
  methods: ['GET'],
  authLevel: 'anonymous',
  route: 'firings',
  handler: async (req, ctx) => {
    if (!(await verifyToken(req))) return { status: 401, body: 'Unauthorized' };

    try {
      const r = await getPool().query(`
        SELECT
          fl.firing_id,
          MIN(fl.ts)     AS started_at,
          MAX(fl.ts)     AS last_seen_at,
          COUNT(*)       AS point_count,
          MAX(fl.temp_c) AS max_temp_c,
          BOOL_OR(fl.is_err) AS had_error,
          fm.ended_at,
          fm.end_reason
        FROM firing_log fl
        LEFT JOIN firing_meta fm ON fm.firing_id = fl.firing_id
        GROUP BY fl.firing_id, fm.ended_at, fm.end_reason
        ORDER BY fl.firing_id DESC
      `);
      return { status: 200, jsonBody: r.rows };
    } catch (err) {
      ctx.error('firings query:', err.message);
      return { status: 500, body: 'Database error' };
    }
  },
});
