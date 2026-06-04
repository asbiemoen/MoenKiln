const { app } = require('@azure/functions');
const { getPool } = require('./db');
const crypto = require('crypto');

app.http('auth-request', {
  methods: ['POST'],
  authLevel: 'anonymous',
  route: 'auth/request',
  handler: async (req, ctx) => {
    let body;
    try { body = await req.json(); } catch { body = {}; }

    const email = (body.email || '').toLowerCase().trim();
    const ok = { status: 200, body: 'If that address is registered, a login link has been sent.' };

    if (email !== (process.env.ALLOWED_EMAIL || '').toLowerCase()) return ok;

    const token   = crypto.randomBytes(32).toString('hex');
    const expires = new Date(Date.now() + 24 * 60 * 60 * 1000);

    try {
      await getPool().query(
        'INSERT INTO auth_tokens (token, expires_at) VALUES ($1, $2)',
        [token, expires]
      );
    } catch (err) {
      ctx.error('Token insert:', err.message);
      return { status: 500, body: 'Server error' };
    }

    const link = `${process.env.MAGIC_LINK_BASE_URL}/dashboard.html?token=${token}`;

    try {
      const res = await fetch('https://api.resend.com/emails', {
        method: 'POST',
        headers: {
          Authorization: `Bearer ${process.env.RESEND_API_KEY}`,
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({
          from: 'Moen Kiln Cloud Log <post@kirkepasset.no>',
          to: email,
          subject: 'Logg inn – Moen Kiln Cloud Log',
          html: `<p>Klikk lenken for å logge inn i Moen Kiln Cloud Log:</p>
                 <p><a href="${link}">${link}</a></p>
                 <p>Lenken er gyldig i 24 timer.</p>`,
        }),
      });
      if (!res.ok) ctx.error('Resend error:', await res.text());
    } catch (err) {
      ctx.error('Email send:', err.message);
    }

    return ok;
  },
});
