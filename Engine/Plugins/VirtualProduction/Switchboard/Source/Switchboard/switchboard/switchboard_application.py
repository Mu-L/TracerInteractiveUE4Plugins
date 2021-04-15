# Copyright Epic Games, Inc. All Rights Reserved.

from .config import CONFIG, SETTINGS
from .switchboard_logging import LOGGER
from . import switchboard_utils as sb_utils

import pythonosc.dispatcher
import pythonosc.osc_server

import os, subprocess, threading, typing


class ApplicationAbstract(object):
    def __init__(self):
        self.name = None

    # If the application is running
    def is_running(self):
        pass


class OscServer(object):
    def __init__(self):
        super().__init__()
        self.name = 'OSC Server'
        self.server_thread = None
        self.server = None

        self.dispatcher = pythonosc.dispatcher.Dispatcher()
        self.dispatcher.set_default_handler(self._default_callback, True)

        # For internal messaging
        self.internal_client = None
        self.server_port = None

    def ip_address(self):
        if not self.server:
            return None

        return self.server.server_address()

    def launch(self, ip_address, port):
        # TODO: Allow relaunch of OSC server when ip_address variable changes
        try:
            self.server = pythonosc.osc_server.ThreadingOSCUDPServer((ip_address, port), self.dispatcher)
        except OSError as e:
            if e.errno == 10048:
                LOGGER.error(f'OSC Server: Another OSC server is currently using {ip_address} {port}. Please kill and relaunch')
            elif e.errno == 10049:
                LOGGER.error(f'OSC Server: Can not connect to {ip_address} {port}. Please check address and relaunch')

            self.server = None
            return False

        # Set the server ip and port
        self.server_port = port

        LOGGER.success(f"OSC Server: receiving on {self.server.server_address}")
        self.server_thread = threading.Thread(target=self.server.serve_forever)
        self.server_thread.start()

        return True

    def close(self):
        if not self.server:
            return

        LOGGER.info(f'OSC Server: Shutting down')
        self.server.shutdown()
        self.server.server_close()
        self.server_thread.join()

    def is_running(self):
        if not self.server_thread:
            return False
        return self.server_thread.is_alive()

    def dispatcher_map(self, command, method):
        #LOGGER.osc(f'OSC Server: dispatcher map {command} to {method}')
        self.dispatcher.map(command, method, needs_reply_address=True)

    def _default_callback(self, client_address, command, *args):
        LOGGER.warning(f'Received unhandled OSC message: {command} {args}.')


class MultiUserApplication(ApplicationAbstract):
    def __init__(self):
        super().__init__()
        self.name = 'Multi User Server'

        self.lock = threading.Lock()
        self.process: typing.Optional[subprocess.Popen] = None

        # Application Options
        self.concert_ignore_cl = False

    def exe_path(self):
        return CONFIG.multiuser_server_path()

    def exe_name(self):
        return os.path.split(self.exe_path())[1]

    def poll_process(self):
        # Aside from this task_name, PollProcess is stateless, and we'd like to pick up on name changes.
        return sb_utils.PollProcess(self.exe_name())

    def launch(self):
        with self.lock:
            if self.is_running():
                return False

            if not os.path.exists(self.exe_path()):
                LOGGER.error(f"Could not find multi-user server at {self.exe_path()}. Has it been built?")
                return

            cmdline = f'start "Multi User Server" "{self.exe_path()}" -CONCERTSERVER={CONFIG.MUSERVER_SERVER_NAME} {CONFIG.MUSERVER_COMMAND_LINE_ARGUMENTS}'

            if self.concert_ignore_cl:
                cmdline += " -ConcertIgnore"

            if CONFIG.MUSERVER_CLEAN_HISTORY:
                cmdline += " -ConcertClean"

            LOGGER.debug(cmdline)
            self.process = subprocess.Popen(cmdline, shell=True, startupinfo=sb_utils.get_hidden_sp_startupinfo())

            return True

    def terminate(self):
        if self.process:
            self.process.terminate()
        else:
            self.poll_process().kill()

    def is_running(self):
        if self.process and (self.process.poll() is None):
            return True
        elif self.poll_process().poll() is None:
            return True

        return False
