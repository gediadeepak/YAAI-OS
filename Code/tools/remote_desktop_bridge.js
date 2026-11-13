'use strict';
const { createServer } = require('node:https');
const { connect, isIPv4 } = require('node:net');
const { readFileSync } = require('node:fs');
const { randomBytes, timingSafeEqual } = require('node:crypto');
const { join } = require('node:path');

const [pfx, address = '127.0.0.1', serial = 'yaai-remote', port = '8443'] = process.argv.slice(2);
if (!pfx || !isIPv4(address) || !/^[1-9]\d{0,4}$/.test(port) || Number(port) > 65535 ||
    !/^(?:[a-zA-Z0-9_-]+|127\.0\.0\.1:[1-9]\d{0,4})$/.test(serial)) {
    console.error('Usage: node remote_desktop_bridge.js <certificate.pfx> [listen IPv4] [pipe name] [HTTPS port]');
    process.exit(1);
}

const assets = new Map([
    ['/', ['remote_desktop.html', 'text/html; charset=utf-8']],
    ['/viewer.css', ['remote_desktop.css', 'text/css; charset=utf-8']],
    ['/viewer.js', ['remote_desktop_client.js', 'text/javascript; charset=utf-8']],
].map(([url, [file, type]]) => [url, [readFileSync(join(__dirname, file)), type]]));
const code = randomBytes(12).toString('hex');
const pairUntil = Date.now() + 10 * 60_000;
const origin = 'https://' + address + (port === '443' ? '' : ':' + port);
let paired = false;
let session = '';
let expires = 0;

const magic = Buffer.from('YAAI');
const frame = Buffer.alloc(8 + 320 * 240 * 2);
let used = 0;
let needed = 8;
let pending;
let channel;

function finish(value) {
    if (!pending) return;
    const { resolve, timer } = pending;
    pending = undefined;
    used = 0;
    needed = 8;
    clearTimeout(timer);
    resolve(value);
}

function receive(chunk) {
    for (let pos = 0; pending && pos < chunk.length;) {
        if (used < 4) {
            const byte = chunk[pos++];
            used = byte === magic[used] ? used + 1 : Number(byte === magic[0]);
            if (used === 4) magic.copy(frame);
            continue;
        }
        const size = Math.min(needed - used, chunk.length - pos);
        chunk.copy(frame, used, pos, pos + size);
        used += size;
        pos += size;
        if (used !== needed) continue;
        if (needed === 8) {
            const width = frame.readUInt16LE(4);
            const height = frame.readUInt16LE(6);
            if (width !== 320 || height < 1 || height > 240) {
                used = 0;
                continue;
            }
            needed = 8 + width * height * 2;
        } else {
            finish(Buffer.from(frame.subarray(0, needed)));
        }
    }
}

function attach() {
    const stream = serial.startsWith('127.0.0.1:')
        ? connect(Number(serial.split(':')[1]), '127.0.0.1')
        : connect('\\\\.\\pipe\\' + serial);
    stream.on('connect', () => { channel = stream; });
    stream.on('data', receive);
    stream.on('error', () => {});
    stream.on('close', () => {
        channel = undefined;
        finish(null);
        setTimeout(attach, 1000).unref();
    });
}

function requestFrame() {
    if (!channel) return Promise.resolve(null);
    if (pending) return pending.promise;
    let resolve;
    const promise = new Promise(done => { resolve = done; });
    const timer = setTimeout(() => finish(null), 25000);
    pending = { promise, resolve, timer };
    channel.write('F');
    return promise;
}

function authorized(req) {
    return session && expires > Date.now() &&
        (req.headers.cookie || '').split(';').some(cookie => cookie.trim() === 'yaai_view=' + session);
}

function reply(res, status, body, type) {
    if (type) res.setHeader('Content-Type', type);
    res.writeHead(status);
    res.end(body);
}

async function handle(req, res) {
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('X-Content-Type-Options', 'nosniff');
    res.setHeader('Referrer-Policy', 'no-referrer');
    res.setHeader('Content-Security-Policy',
        "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'none'; form-action 'self'");
    if (req.headers.host !== new URL(origin).host) return reply(res, 421);
    if (req.method === 'POST' && req.headers.origin !== origin) return reply(res, 403);
    if (req.method === 'POST' && req.url === '/pair') {
        if (paired || Date.now() >= pairUntil) return reply(res, 403);
        let value = Buffer.alloc(0);
        for await (const chunk of req) {
            if (value.length + chunk.length > code.length) return reply(res, 400);
            value = Buffer.concat([value, chunk]);
        }
        if (paired || Date.now() >= pairUntil || value.length !== code.length ||
            !timingSafeEqual(value, Buffer.from(code))) return reply(res, 403);
        paired = true;
        session = randomBytes(32).toString('hex');
        expires = Date.now() + 30 * 60_000;
        res.setHeader('Set-Cookie', 'yaai_view=' + session + '; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age=1800');
        return reply(res, 204);
    }
    if (req.method === 'POST' && req.url === '/disconnect') {
        if (!authorized(req)) return reply(res, 401);
        session = '';
        res.setHeader('Set-Cookie', 'yaai_view=; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age=0');
        return reply(res, 204);
    }
    if (req.method !== 'GET') return reply(res, 405);
    if (req.url === '/session') return reply(res, authorized(req) ? 204 : 401);
    if (req.url === '/frame') {
        if (!authorized(req)) return reply(res, 401);
        const data = await requestFrame();
        if (!authorized(req)) return reply(res, 401);
        return data ? reply(res, 200, data, 'application/octet-stream') : reply(res, 503);
    }
    const asset = assets.get(req.url);
    return asset ? reply(res, 200, ...asset) : reply(res, 404);
}

const server = createServer({
    pfx: readFileSync(pfx), passphrase: process.env.YAAI_TLS_PASSWORD,
    minVersion: 'TLSv1.2', requestTimeout: 10000, headersTimeout: 5000,
}, (req, res) => {
    handle(req, res).catch(() => { if (!res.headersSent) reply(res, 500); });
});
server.maxConnections = 24;
server.listen(Number(port), address, () => {
    console.log('Viewer: ' + origin);
    console.log('One-time pairing code (valid for 10 minutes): ' + code);
    attach();
});
