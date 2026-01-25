import asyncio
import websockets

async def echo(ws):
    async for message in ws:
        await ws.send(message)

async def main():
    async with websockets.serve(echo, "127.0.0.1", 9001):
        print("WebSocket echo server running on ws://127.0.0.1:9001")
        await asyncio.Future()  # run forever

asyncio.run(main())