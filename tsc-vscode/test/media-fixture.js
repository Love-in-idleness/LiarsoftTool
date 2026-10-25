// Synthetic fixtures only; no original game assets.
function bmp(width, height, color) {
    const stride = (width * 3 + 3) & ~3, data = Buffer.alloc(54 + stride * height);
    data.write('BM'); data.writeUInt32LE(data.length, 2); data.writeUInt32LE(54, 10); data.writeUInt32LE(40, 14);
    data.writeInt32LE(width, 18); data.writeInt32LE(height, 22); data.writeUInt16LE(1, 26); data.writeUInt16LE(24, 28);
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
        const at = 54 + y * stride + x * 3; data[at] = color[2]; data[at + 1] = color[1]; data[at + 2] = color[0];
    }
    return data;
}
function wav() {
    const data = Buffer.alloc(44 + 16000);
    data.write('RIFF'); data.writeUInt32LE(data.length - 8, 4); data.write('WAVEfmt ', 8); data.writeUInt32LE(16, 16);
    data.writeUInt16LE(1, 20); data.writeUInt16LE(1, 22); data.writeUInt32LE(8000, 24); data.writeUInt32LE(16000, 28);
    data.writeUInt16LE(2, 32); data.writeUInt16LE(16, 34); data.write('data', 36); data.writeUInt32LE(16000, 40);
    for (let i = 0; i < 8000; i++) data.writeInt16LE(Math.round(1000 * Math.sin(i * 2 * Math.PI * 440 / 8000)), 44 + i * 2);
    return data;
}
module.exports = { bmp, wav };
