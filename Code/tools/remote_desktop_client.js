'use strict';
const form = document.querySelector('#pair');
const code = document.querySelector('#code');
const screen = document.querySelector('#screen');
const disconnect = document.querySelector('#disconnect');
const status = document.querySelector('#status');
const context = screen.getContext('2d');
let session = 0;

function showFrame(bytes) {
    const data = new DataView(bytes);
    const width = data.getUint16(4, true);
    const height = data.getUint16(6, true);
    if (data.getUint32(0) !== 0x59414149 || width !== 320 || !height || height > 240 ||
        bytes.byteLength !== 8 + width * height * 2) throw new Error('Invalid desktop frame');
    if (screen.width !== width || screen.height !== height) {
        screen.width = width;
        screen.height = height;
    }
    const image = context.createImageData(width, height);
    for (let i = 0; i < width * height; i++) {
        const pixel = data.getUint16(8 + i * 2, true);
        const red = pixel >> 11;
        const green = (pixel >> 5) & 63;
        const blue = pixel & 31;
        const offset = i * 4;
        image.data[offset] = (red << 3) | (red >> 2);
        image.data[offset + 1] = (green << 2) | (green >> 4);
        image.data[offset + 2] = (blue << 3) | (blue >> 2);
        image.data[offset + 3] = 255;
    }
    context.putImageData(image, 0, 0);
    screen.hidden = false;
}

function end(message) {
    session++;
    form.hidden = false;
    screen.hidden = true;
    disconnect.hidden = true;
    status.textContent = message;
}

async function watch() {
    const active = ++session;
    form.hidden = true;
    disconnect.hidden = false;
    while (active === session) {
        try {
            const response = await fetch('/frame', { signal: AbortSignal.timeout(30000) });
            if (active !== session) return;
            if (response.status === 401) return end('Session ended. Restart the bridge to pair again.');
            if (response.ok) {
                const bytes = await response.arrayBuffer();
                if (active !== session) return;
                showFrame(bytes);
                status.textContent = 'Connected';
            } else {
                screen.hidden = true;
                status.textContent = 'Waiting for screen sharing in YAAI Settings…';
            }
        } catch {
            if (active !== session) return;
            screen.hidden = true;
            status.textContent = 'Waiting for the bridge…';
        }
        await new Promise(resolve => setTimeout(resolve, 400));
    }
}

form.addEventListener('submit', async event => {
    event.preventDefault();
    const button = form.querySelector('button');
    button.disabled = true;
    try {
        const response = await fetch('/pair', { method: 'POST', body: code.value.trim() });
        if (response.ok) {
            code.value = '';
            void watch();
        } else status.textContent = 'Pairing failed. Check the code shown by the bridge.';
    } catch {
        status.textContent = 'Could not reach the bridge.';
    } finally {
        button.disabled = false;
    }
});

disconnect.addEventListener('click', () => {
    end('Disconnected. Restart the bridge to pair again.');
    void fetch('/disconnect', { method: 'POST' });
});

fetch('/session').then(response => {
    if (response.ok) void watch();
}).catch(() => {});
