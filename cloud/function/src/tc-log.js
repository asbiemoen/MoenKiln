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

app.http('tc-log', {
  methods: ['GET'],
  authLevel: 'anonymous',
  route: 'tc-log',
  handler: async (req, ctx) => {
    if (!(await verifyToken(req))) return { status: 401, body: 'Unauthorized' };

    const hours = Math.min(24, Math.max(1, parseInt(req.query.get('hours') || '6', 10)));

    try {
      const r = await getPool().query(
        `SELECT received_at, state, sample_interval_ms, readings
         FROM tc_log
         WHERE received_at > NOW() - ($1 * interval '1 hour')
         ORDER BY received_at ASC`,
        [hours]
      );
      return { status: 200, jsonBody: { batches: r.rows, hours } };
    } catch (err) {
      ctx.error('tc-log query:', err.message);
      return { status: 500, body: 'Database error' };
    }
  },
});
