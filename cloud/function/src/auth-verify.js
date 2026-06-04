const { app } = require('@azure/functions');
const { getPool } = require('./db');

app.http('auth-verify', {
  methods: ['GET'],
  authLevel: 'anonymous',
  route: 'auth/verify',
  handler: async (req, ctx) => {
    const token = req.query.get('token');
    if (!token) return { status: 400, body: 'No token' };

    try {
      const r = await getPool().query(
        'SELECT 1 FROM auth_tokens WHERE token = $1 AND expires_at > NOW()',
        [token]
      );
      return r.rowCount > 0
        ? { status: 200, jsonBody: { valid: true } }
        : { status: 401, body: 'Invalid or expired token' };
    } catch (err) {
      ctx.error('Auth verify:', err.message);
      return { status: 500, body: 'Server error' };
    }
  },
});
