# Copyright Epic Games, Inc. All Rights Reserved.
from . import message_protocol
from .switchboard_logging import LOGGER

import datetime, select, socket, uuid, traceback, typing

from collections import deque
from threading import Thread


class ListenerClient(object):
    '''Connects to a server running SwitchboardListener.

    Runs a thread to service its socket, and upon receiving complete messages,
    invokes a handler callback from that thread (`handle_connection()`).

    `disconnect_delegate` is invoked on `disconnect()` or on socket errors.

    Handlers in the `delegates` map are passed a dict containing the entire
    JSON response from the listener, routed according to the "command" field
    string value. All new messages and handlers should follow this pattern.

    The other, legacy (VCS/file) delegates are each passed different (or no)
    arguments; for details, see `route_message()`.
    '''
    def __init__(self, ip_address, port, buffer_size=1024):
        self.ip_address = ip_address
        self.port = port
        self.buffer_size = buffer_size

        self.message_queue = deque()
        self.close_socket = False

        self.socket = None
        self.handle_connection_thread = None

        #TODO: Consider converting these delegates to Signals and sending dict.

        self.disconnect_delegate = None

        self.command_accepted_delegate = None
        self.command_declined_delegate = None

        self.vcs_init_completed_delegate = None
        self.vcs_init_failed_delegate = None
        self.vcs_report_revision_completed_delegate = None
        self.vcs_report_revision_failed_delegate = None
        self.vcs_sync_completed_delegate = None
        self.vcs_sync_failed_delegate = None

        self.send_file_completed_delegate = None
        self.send_file_failed_delegate = None
        self.receive_file_completed_delegate = None
        self.receive_file_failed_delegate = None

        self.delegates: typing.Dict[ str, typing.Optional[ typing.Callable[[typing.Dict], None] ] ] = {
            "state" : None,
            "get sync status" : None,
        }

        self.last_activity = datetime.datetime.now()

    @property
    def server_address(self):
        if self.ip_address:
            return (self.ip_address, self.port)
        return None

    @property
    def is_connected(self):
        # I ran into an issue where running disconnect in a thread was causing the socket maintain it's reference
        # But self.socket.getpeername() fails because socket is sent to none. I am assuming that is due
        # it python's threading. Adding a try except to handle this
        try:
            if self.socket and self.socket.getpeername():
                return True
        except:
            return False
        return False

    def connect(self, ip_address=None):
        self.disconnect()

        if ip_address:
            self.ip_address = ip_address
        elif not self.ip_address:
            LOGGER.debug('No ip_address has been set. Cannot connect')
            return False

        self.close_socket = False
        self.last_activity = datetime.datetime.now()

        try:
            LOGGER.info(f"Connecting to {self.ip_address}:{self.port}")

            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect(self.server_address)

            # Create a thread that waits for messages from the server
            self.handle_connection_thread = Thread(target=self.handle_connection)
            self.handle_connection_thread.start()

        except OSError:
            LOGGER.error(f"Socket error: {self.ip_address}:{self.port}")
            self.socket = None
            return False

        return True

    def disconnect(self, unexpected=False, exception=None):
        if self.is_connected:
            _, msg = message_protocol.create_disconnect_message()
            self.send_message(msg)

            self.close_socket = True
            self.handle_connection_thread.join()

    def handle_connection(self):
        buffer = []
        keepalive_timeout = 1.0

        while self.is_connected:
            try:
                rlist = [self.socket]
                wlist = []
                xlist = []
                read_timeout = 0.1

                read_sockets, _, _ = select.select(rlist, wlist, xlist, read_timeout)

                while len(self.message_queue):
                    message_bytes = self.message_queue.pop()
                    self.socket.sendall(message_bytes)
                    self.last_activity = datetime.datetime.now()
                
                for rs in read_sockets:
                    received_data = rs.recv(self.buffer_size).decode()
                    self.process_received_data(buffer, received_data)

                delta = datetime.datetime.now() - self.last_activity

                if delta.total_seconds() > keepalive_timeout:
                    _, msg = message_protocol.create_keep_alive_message()
                    self.socket.sendall(msg)

                if self.close_socket and len(self.message_queue) == 0:
                    self.socket.shutdown(socket.SHUT_RDWR)
                    self.socket.close()
                    self.socket = None

                    if self.disconnect_delegate:
                        self.disconnect_delegate(unexpected=False, exception=None)

                    break

            except ConnectionResetError as e:
                self.socket.shutdown(socket.SHUT_RDWR)
                self.socket.close()
                self.socket = None

                if self.disconnect_delegate:
                    self.disconnect_delegate(unexpected=True, exception=e)

                return # todo: this needs to send a signal back to the main thread so the thread can be joined

            except OSError as e: # likely a socket error, so self.socket is not useable any longer
                self.socket = None

                if self.disconnect_delegate:
                    self.disconnect_delegate(unexpected=True, exception=e)

                return

    def route_message(self, message):
        ''' Routes the received message to its delegate '''
        delegate = self.delegates.get(message['command'], None)
        if delegate:
            delegate(message)
            return

        if "command accepted" in message:
            message_id = uuid.UUID(message['id'])
            if message['command accepted'] == True:
                if self.command_accepted_delegate:
                    self.command_accepted_delegate(message_id)
            else:
                if self.command_declined_delegate:
                    self.command_declined_delegate(message_id, message["error"])

        elif "vcs init complete" in message:
            if message['vcs init complete'] == True:
                if self.vcs_init_completed_delegate:
                    self.vcs_init_completed_delegate()
            else:
                if self.vcs_init_failed_delegate:
                    self.vcs_init_failed_delegate(message['error'])

        elif "vcs report revision complete" in message:
            if message['vcs report revision complete'] == True:
                if self.vcs_report_revision_completed_delegate:
                    self.vcs_report_revision_completed_delegate(message['revision'])
            else:
                if self.vcs_report_revision_failed_delegate:
                    self.vcs_report_revision_failed_delegate(message['error'])

        elif "vcs sync complete" in message:
            if message['vcs sync complete'] == True:
                if self.vcs_sync_completed_delegate:
                    self.vcs_sync_completed_delegate(message['revision'])
            else:
                if self.vcs_sync_failed_delegate:
                    self.vcs_sync_failed_delegate(message['error'])

        elif "send file complete" in message:
            if message['send file complete'] == True:
                if self.send_file_completed_delegate:
                    self.send_file_completed_delegate(message['destination'])
            else:
                if self.send_file_failed_delegate:
                    self.send_file_failed_delegate(message['destination'], message['error'])
                    
        elif "receive file complete" in message:
            if message['receive file complete'] == True:
                if self.receive_file_completed_delegate:
                    self.receive_file_completed_delegate(message['source'], message['content'])
            else:
                if self.receive_file_failed_delegate:
                    self.receive_file_failed_delegate(message['source'], message['error'])
        else:
            LOGGER.error(f'Unhandled message: {message}')
            raise ValueError

    def process_received_data(self, buffer, received_data):
        for symbol in received_data:
            buffer.append(symbol)

            if symbol == '\x00': # found message end
                buffer.pop() # remove terminator
                message = message_protocol.decode_message(buffer)
                buffer.clear()

                # route message to its assigned delegate
                try:
                    self.route_message(message)
                except:
                    LOGGER.error(f"Error while parsing message: \n\n=== Traceback BEGIN ===\n{traceback.format_exc()}=== Traceback END ===\n")

    def send_message(self, message_bytes):
        if self.is_connected:
            LOGGER.message(f'Message: Sending ({self.ip_address}): {message_bytes}')
            self.message_queue.appendleft(message_bytes)
        else:
            LOGGER.error(f'Message: Failed to send ({self.ip_address}): {message_bytes}. No socket connected')
            if self.disconnect_delegate:
                self.disconnect_delegate(unexpected=True, exception=None)
