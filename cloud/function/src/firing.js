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

app.http('firing', {
  methods: ['GET'],
  authLevel: 'anonymous',
  route: 'firing/{id}',
  handler: async (req, ctx) => {
    if (!(await verifyToken(req))) return { status: 401, body: 'Unauthorized' };

    const firingId = parseInt(req.params.id);
    if (isNaN(firingId)) return { status: 400, body: 'Invalid firing ID' };

    try {
      const r = await getPool().query(
        `SELECT ts, sec_elapsed, temp_c, setpoint_c, relay, duty_pct, segment, is_err
         FROM firing_log
         WHERE firing_id = $1
         ORDER BY ts ASC`,
        [firingId]
      );
      return { status: 200, jsonBody: r.rows };
    } catch (err) {
      ctx.log.error('firing query:', err.message);
      return { status: 500, body: 'Database error' };
    }
  },
});
