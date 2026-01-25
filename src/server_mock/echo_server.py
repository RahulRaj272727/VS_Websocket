#!/usr/bin/env python3
"""
TallyIX WebSocket Mock Server

A comprehensive mock server for testing the TallyIX WebSocket client.
This server implements the TallyIX protocol and provides various testing features:

Features:
- Echo mode: Echoes back all messages (text and binary)
- Protocol awareness: Parses and logs TallyIX protocol messages
- Binary transfer tracking: Monitors binary data transfers
- Statistics: Tracks connection metrics and transfer stats
- Colorized logging: Easy-to-read console output
- Graceful shutdown: Clean handling of Ctrl+C

Protocol Message Types Handled:
- hello: Initial handshake message
- binary_start: Metadata for upcoming binary transfer
- ack: Acknowledgment messages
- error: Error responses

Usage:
    python echo_server.py [--port PORT] [--host HOST] [--verbose]
    
    Default: ws://127.0.0.1:9001

Requirements:
    pip install websockets

Author: TallyIX Team
Version: 2.0
"""

import asyncio
import websockets
import json
import argparse
import signal
import sys
from datetime import datetime
from dataclasses import dataclass, field
from typing import Optional, Set
from enum import Enum


# ============================================================================
# ANSI Color Codes for Console Output
# ============================================================================

class Colors:
    """ANSI color codes for colorized console output."""
    RESET = "\033[0m"
    BOLD = "\033[1m"
    
    # Foreground colors
    RED = "\033[91m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    MAGENTA = "\033[95m"
    CYAN = "\033[96m"
    WHITE = "\033[97m"
    GRAY = "\033[90m"


# ============================================================================
# Logging Utilities
# ============================================================================

class LogLevel(Enum):
    """Log severity levels."""
    DEBUG = "DBG"
    INFO = "INF"
    WARNING = "WRN"
    ERROR = "ERR"


def get_timestamp() -> str:
    """Get current timestamp in HH:MM:SS.mmm format."""
    now = datetime.now()
    return now.strftime("%H:%M:%S") + f".{now.microsecond // 1000:03d}"


def log(level: LogLevel, tag: str, message: str, verbose: bool = True):
    """
    Log a message with timestamp, level, and tag.
    
    Args:
        level: Log severity level
        tag: Component or module tag
        message: Message content
        verbose: If False, only show INFO and above
    """
    if not verbose and level == LogLevel.DEBUG:
        return
    
    # Color mapping for levels
    level_colors = {
        LogLevel.DEBUG: Colors.GRAY,
        LogLevel.INFO: Colors.GREEN,
        LogLevel.WARNING: Colors.YELLOW,
        LogLevel.ERROR: Colors.RED,
    }
    
    color = level_colors.get(level, Colors.WHITE)
    timestamp = get_timestamp()
    
    print(f"{Colors.GRAY}{timestamp}{Colors.RESET} "
          f"[{color}{level.value}{Colors.RESET}]"
          f"[{Colors.CYAN}{tag}{Colors.RESET}] "
          f"{message}")


# ============================================================================
# Statistics Tracking
# ============================================================================

@dataclass
class ConnectionStats:
    """Statistics for a single WebSocket connection."""
    client_id: str
    connected_at: datetime = field(default_factory=datetime.now)
    messages_received: int = 0
    messages_sent: int = 0
    bytes_received: int = 0
    bytes_sent: int = 0
    binary_transfers_completed: int = 0
    current_binary_expected: int = 0
    current_binary_received: int = 0


@dataclass 
class ServerStats:
    """Global server statistics."""
    total_connections: int = 0
    active_connections: int = 0
    total_messages: int = 0
    total_bytes: int = 0
    started_at: datetime = field(default_factory=datetime.now)


# ============================================================================
# Protocol Message Handling
# ============================================================================

def parse_protocol_message(data: str) -> Optional[dict]:
    """
    Parse a JSON protocol message.
    
    Args:
        data: JSON string to parse
        
    Returns:
        Parsed dictionary or None if parsing fails
    """
    try:
        return json.loads(data)
    except json.JSONDecodeError:
        return None


def create_ack_message(msg_id: str, content: str = "Message received") -> str:
    """
    Create an acknowledgment message.
    
    Args:
        msg_id: Original message ID to acknowledge
        content: Optional acknowledgment content
        
    Returns:
        JSON string for ACK message
    """
    return json.dumps({
        "type": "ack",
        "msg_id": f"ack_{msg_id}",
        "content": content,
        "original_msg_id": msg_id
    })


def create_error_message(msg_id: str, reason: str) -> str:
    """
    Create an error response message.
    
    Args:
        msg_id: Message ID for the error
        reason: Error description
        
    Returns:
        JSON string for error message
    """
    return json.dumps({
        "type": "error",
        "msg_id": msg_id,
        "content": reason
    })


# ============================================================================
# WebSocket Handler
# ============================================================================

class TallyIXMockServer:
    """
    TallyIX Mock WebSocket Server.
    
    Handles WebSocket connections, implements protocol logic,
    and provides echo functionality for testing.
    """
    
    def __init__(self, host: str = "127.0.0.1", port: int = 9001, verbose: bool = False):
        """
        Initialize the mock server.
        
        Args:
            host: Host address to bind to
            port: Port number to listen on
            verbose: Enable debug logging
        """
        self.host = host
        self.port = port
        self.verbose = verbose
        self.server_stats = ServerStats()
        self.active_connections: Set[websockets.WebSocketServerProtocol] = set()
        self.connection_stats: dict[str, ConnectionStats] = {}
        self._shutdown_event = asyncio.Event()
    
    def _log(self, level: LogLevel, tag: str, message: str):
        """Helper to log with server's verbose setting."""
        log(level, tag, message, self.verbose or level != LogLevel.DEBUG)
    
    def _get_client_id(self, websocket: websockets.WebSocketServerProtocol) -> str:
        """Generate a unique client identifier."""
        return f"{websocket.remote_address[0]}:{websocket.remote_address[1]}"
    
    async def handle_text_message(
        self, 
        websocket: websockets.WebSocketServerProtocol,
        message: str,
        stats: ConnectionStats
    ):
        """
        Handle an incoming text message.
        
        Args:
            websocket: The WebSocket connection
            message: The text message received
            stats: Connection statistics to update
        """
        stats.messages_received += 1
        stats.bytes_received += len(message)
        self.server_stats.total_messages += 1
        self.server_stats.total_bytes += len(message)
        
        # Try to parse as protocol message
        parsed = parse_protocol_message(message)
        
        if parsed:
            msg_type = parsed.get("type", "unknown")
            msg_id = parsed.get("msg_id", "unknown")
            content = parsed.get("content", "")
            
            self._log(LogLevel.INFO, "Protocol", 
                     f"[{stats.client_id}] Received {msg_type} (id={msg_id})")
            
            if msg_type == "hello":
                # Handle hello message - send acknowledgment
                self._log(LogLevel.INFO, "Protocol",
                         f"[{stats.client_id}] Hello: {content}")
                
                # Echo back the hello message
                await websocket.send(message)
                stats.messages_sent += 1
                stats.bytes_sent += len(message)
                
                # Also send an ACK
                ack = create_ack_message(msg_id, f"Hello received from {stats.client_id}")
                await websocket.send(ack)
                stats.messages_sent += 1
                stats.bytes_sent += len(ack)
                
                self._log(LogLevel.DEBUG, "Protocol",
                         f"[{stats.client_id}] Sent hello echo and ACK")
            
            elif msg_type == "binary_start":
                # Handle binary transfer metadata
                binary_size = parsed.get("size", 0)
                stats.current_binary_expected = binary_size
                stats.current_binary_received = 0
                
                self._log(LogLevel.INFO, "Protocol",
                         f"[{stats.client_id}] Binary transfer announced: {binary_size:,} bytes")
                
                # Echo back the metadata
                await websocket.send(message)
                stats.messages_sent += 1
                stats.bytes_sent += len(message)
            
            elif msg_type == "ack":
                self._log(LogLevel.DEBUG, "Protocol",
                         f"[{stats.client_id}] ACK received: {content}")
                # Echo ACKs back
                await websocket.send(message)
                stats.messages_sent += 1
                stats.bytes_sent += len(message)
            
            elif msg_type == "error":
                self._log(LogLevel.WARNING, "Protocol",
                         f"[{stats.client_id}] Error received: {content}")
                # Echo errors back
                await websocket.send(message)
                stats.messages_sent += 1
                stats.bytes_sent += len(message)
            
            else:
                self._log(LogLevel.WARNING, "Protocol",
                         f"[{stats.client_id}] Unknown type: {msg_type}")
                # Echo unknown messages
                await websocket.send(message)
                stats.messages_sent += 1
                stats.bytes_sent += len(message)
        else:
            # Not valid JSON - just echo it back
            self._log(LogLevel.DEBUG, "Server",
                     f"[{stats.client_id}] Non-JSON text ({len(message)} bytes)")
            await websocket.send(message)
            stats.messages_sent += 1
            stats.bytes_sent += len(message)
    
    async def handle_binary_message(
        self,
        websocket: websockets.WebSocketServerProtocol,
        data: bytes,
        stats: ConnectionStats
    ):
        """
        Handle an incoming binary message.
        
        Args:
            websocket: The WebSocket connection
            data: The binary data received
            stats: Connection statistics to update
        """
        stats.messages_received += 1
        stats.bytes_received += len(data)
        stats.current_binary_received += len(data)
        self.server_stats.total_messages += 1
        self.server_stats.total_bytes += len(data)
        
        # Calculate progress
        if stats.current_binary_expected > 0:
            progress = (stats.current_binary_received / stats.current_binary_expected) * 100
            self._log(LogLevel.DEBUG, "Binary",
                     f"[{stats.client_id}] Chunk: {len(data):,} bytes "
                     f"({stats.current_binary_received:,}/{stats.current_binary_expected:,} "
                     f"= {progress:.1f}%)")
            
            # Check if transfer is complete
            if stats.current_binary_received >= stats.current_binary_expected:
                stats.binary_transfers_completed += 1
                self._log(LogLevel.INFO, "Binary",
                         f"[{stats.client_id}] Transfer complete: "
                         f"{stats.current_binary_received:,} bytes "
                         f"(Transfer #{stats.binary_transfers_completed})")
                stats.current_binary_expected = 0
                stats.current_binary_received = 0
        else:
            self._log(LogLevel.DEBUG, "Binary",
                     f"[{stats.client_id}] Received {len(data):,} bytes (no size announced)")
        
        # Echo binary data back
        await websocket.send(data)
        stats.messages_sent += 1
        stats.bytes_sent += len(data)
    
    async def handle_connection(self, websocket: websockets.WebSocketServerProtocol):
        """
        Handle a WebSocket connection lifecycle.
        
        Args:
            websocket: The WebSocket connection
        """
        client_id = self._get_client_id(websocket)
        stats = ConnectionStats(client_id=client_id)
        self.connection_stats[client_id] = stats
        self.active_connections.add(websocket)
        self.server_stats.total_connections += 1
        self.server_stats.active_connections += 1
        
        self._log(LogLevel.INFO, "Server",
                 f"{Colors.GREEN}Client connected:{Colors.RESET} {client_id} "
                 f"(Active: {self.server_stats.active_connections})")
        
        try:
            async for message in websocket:
                if isinstance(message, bytes):
                    await self.handle_binary_message(websocket, message, stats)
                else:
                    await self.handle_text_message(websocket, message, stats)
        
        except websockets.exceptions.ConnectionClosed as e:
            self._log(LogLevel.INFO, "Server",
                     f"Connection closed: {client_id} (code={e.code}, reason={e.reason})")
        
        except Exception as e:
            self._log(LogLevel.ERROR, "Server",
                     f"Error handling {client_id}: {type(e).__name__}: {e}")
        
        finally:
            # Cleanup
            self.active_connections.discard(websocket)
            self.server_stats.active_connections -= 1
            
            # Log session summary
            duration = datetime.now() - stats.connected_at
            self._log(LogLevel.INFO, "Server",
                     f"{Colors.YELLOW}Client disconnected:{Colors.RESET} {client_id}")
            self._log(LogLevel.INFO, "Stats",
                     f"[{client_id}] Session: {duration.total_seconds():.1f}s, "
                     f"Msgs: {stats.messages_received}↓/{stats.messages_sent}↑, "
                     f"Bytes: {stats.bytes_received:,}↓/{stats.bytes_sent:,}↑, "
                     f"Binary transfers: {stats.binary_transfers_completed}")
            
            del self.connection_stats[client_id]
    
    async def start(self):
        """Start the WebSocket server."""
        self._log(LogLevel.INFO, "Server",
                 f"{Colors.BOLD}{'='*50}{Colors.RESET}")
        self._log(LogLevel.INFO, "Server",
                 f"{Colors.BOLD}TallyIX Mock WebSocket Server v2.0{Colors.RESET}")
        self._log(LogLevel.INFO, "Server",
                 f"{Colors.BOLD}{'='*50}{Colors.RESET}")
        self._log(LogLevel.INFO, "Server",
                 f"Starting on {Colors.CYAN}ws://{self.host}:{self.port}{Colors.RESET}")
        self._log(LogLevel.INFO, "Server",
                 f"Verbose mode: {Colors.GREEN if self.verbose else Colors.YELLOW}"
                 f"{'ON' if self.verbose else 'OFF'}{Colors.RESET}")
        self._log(LogLevel.INFO, "Server",
                 f"Press {Colors.BOLD}Ctrl+C{Colors.RESET} to stop")
        self._log(LogLevel.INFO, "Server", "")
        
        async with websockets.serve(
            self.handle_connection,
            self.host,
            self.port,
            ping_interval=30,      # Send ping every 30 seconds
            ping_timeout=10,       # Wait 10 seconds for pong
            close_timeout=5,       # Wait 5 seconds for close handshake
            max_size=100 * 1024 * 1024,  # 100MB max message size
        ) as server:
            self._log(LogLevel.INFO, "Server",
                     f"{Colors.GREEN}Server is ready and listening!{Colors.RESET}")
            
            # Wait for shutdown signal
            await self._shutdown_event.wait()
            
            self._log(LogLevel.INFO, "Server", "Shutting down...")
    
    def shutdown(self):
        """Signal the server to shut down gracefully."""
        self._shutdown_event.set()
    
    def print_final_stats(self):
        """Print final server statistics."""
        duration = datetime.now() - self.server_stats.started_at
        print()
        self._log(LogLevel.INFO, "Stats", f"{Colors.BOLD}Final Server Statistics:{Colors.RESET}")
        self._log(LogLevel.INFO, "Stats", f"  Uptime: {duration}")
        self._log(LogLevel.INFO, "Stats", f"  Total connections: {self.server_stats.total_connections}")
        self._log(LogLevel.INFO, "Stats", f"  Total messages: {self.server_stats.total_messages:,}")
        self._log(LogLevel.INFO, "Stats", f"  Total bytes: {self.server_stats.total_bytes:,}")


# ============================================================================
# Main Entry Point
# ============================================================================

def main():
    """Main entry point with argument parsing."""
    parser = argparse.ArgumentParser(
        description="TallyIX WebSocket Mock Server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python echo_server.py                    # Default: ws://127.0.0.1:9001
  python echo_server.py --port 8080        # Custom port
  python echo_server.py --verbose          # Enable debug logging
  python echo_server.py --host 0.0.0.0     # Listen on all interfaces
        """
    )
    parser.add_argument(
        "--host", 
        default="127.0.0.1",
        help="Host address to bind to (default: 127.0.0.1)"
    )
    parser.add_argument(
        "--port", 
        type=int, 
        default=9001,
        help="Port number to listen on (default: 9001)"
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Enable verbose (debug) logging"
    )
    
    args = parser.parse_args()
    
    # Create server instance
    server = TallyIXMockServer(
        host=args.host,
        port=args.port,
        verbose=args.verbose
    )
    
    # Set up signal handlers for graceful shutdown
    def signal_handler(signum, frame):
        print()  # New line after ^C
        server.shutdown()
    
    signal.signal(signal.SIGINT, signal_handler)
    if sys.platform != 'win32':
        signal.signal(signal.SIGTERM, signal_handler)
    
    # Run the server
    try:
        asyncio.run(server.start())
    except KeyboardInterrupt:
        pass
    finally:
        server.print_final_stats()
        log(LogLevel.INFO, "Server", f"{Colors.GREEN}Goodbye!{Colors.RESET}", True)


if __name__ == "__main__":
    main()