"""Explicit live transport check: python test_vps_live.py DNS_NAME [PORT]."""
import asyncio
from pathlib import Path
import secrets
import struct
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from remote.network import (Companion, LOCAL, START, DATA, READY, ACK,
                            DISCONNECT, FINISH, WINDOW, local_frame, unpack_local)


async def main(host, port):
    secret = secrets.token_hex(24)
    clients, servers, tasks, locals_ = [], [], [], []
    epochs = (31, 19)
    async def write(role, kind, payload=b''):
        locals_[role][1].write(local_frame(kind, role, epochs[role], payload))
        await locals_[role][1].drain()
    async def read(role, expected):
        frame = await asyncio.wait_for(locals_[role][0].readexactly(LOCAL.size), 10)
        kind, player, epoch, body = unpack_local(frame)
        assert (kind, player, epoch) == (expected, role, epochs[role]), (kind, player, epoch)
        return body
    try:
        for role in (0, 1):
            client = Companion(host, port, role, secret, tls=True)
            clients.append(client)
            server = await asyncio.start_server(client.local_handle, '127.0.0.1', 0)
            servers.append(server)
            locals_.append(await asyncio.open_connection('127.0.0.1', server.sockets[0].getsockname()[1]))
            tasks.append(asyncio.create_task(client.connect_forever()))
            for _ in range(300):
                if client.joined:
                    break
                await asyncio.sleep(0.05)
            assert client.joined, 'Cannot join TLS relay'
        started = time.monotonic()
        for role in (0, 1):
            await write(role, START, b'TEST')
        for role in (0, 1):
            await read(role, READY)
        packets = {}
        for base in range(0, 188, WINDOW):
            count = min(WINDOW, 188 - base)
            for role in (0, 1):
                for seq in range(base, base + count):
                    packet = bytearray(64)
                    struct.pack_into('<H', packet, 12, seq)
                    packet[16], packet[17] = role, 2
                    packets[role, seq] = bytes(packet)
                    await write(role, DATA, packet)
            for role in (0, 1):
                for seq in range(base, base + count):
                    assert await read(role, DATA) == packets[role ^ 1, seq]
                    await write(role, ACK, struct.pack('<I', seq))
            for role in (0, 1):
                for seq in range(base, base + count):
                    assert await read(role, ACK) == struct.pack('<I', seq)
        for role in (0, 1):
            await write(role, FINISH)
        elapsed = time.monotonic() - started
        await asyncio.sleep(0.1)
        epochs = (41, 29)
        for role in (0, 1):
            await write(role, START, b'TEST')
        for role in (0, 1):
            await read(role, READY)
        clients[1].wan.close()
        for role in (0, 1):
            await read(role, DISCONNECT)
        for _ in range(300):
            if all(c.joined for c in clients):
                break
            await asyncio.sleep(0.05)
        assert all(c.joined for c in clients), 'Reconnect failed'
        epochs = (51, 39)
        for role in (0, 1):
            await write(role, START, b'TEST')
        for role in (0, 1):
            await read(role, READY)
        print(f'LIVE TLS OK: 188 bidirectional packet rounds, window {WINDOW}, {elapsed:.2f}s; disconnect, reconnect and fresh session passed')
    finally:
        for _, writer in locals_:
            writer.close()
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        for server in servers:
            server.close()
            await server.wait_closed()


if __name__ == '__main__':
    asyncio.run(main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 8443))
