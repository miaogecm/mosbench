from mparts.manager import Task
from mparts.host import HostInfo
from support import ResultsProvider, SetCPUs, FileSystem, SystemMonitor, waitForLog

import os, signal, re

__all__ = []

__all__.append("EximDaemon")
class EximDaemon(Task):
    __info__ = ["host", "eximPath", "eximBuild", "mailDir", "spoolDir", "port", "numInstances"]

    def __init__(self, host, eximPath, eximBuild, mailDir, spoolDir, port, numInstances):
        Task.__init__(self, host = host)
        self.host = host
        self.eximPath = eximPath
        self.eximBuild = eximBuild
        self.mailDir = mailDir
        self.spoolDir = spoolDir
        self.port = port
        self.numInstances = numInstances
        self.__proc = []

    def __iPath(self, string, i):
        return string + "-" + str(i)

    def __start(self, i):
        # Create configuration
        config = self.host.outDir(self.__iPath(self.name + ".configure", i))
        self.host.r.run(
            [os.path.join(self.eximPath, "mkconfig"),
             os.path.join(self.eximPath, self.__iPath(self.eximBuild, i)),
             self.__iPath(self.mailDir, i), self.__iPath(self.spoolDir, i)],
            stdout = config)

        # Start Exim
        proc  = self.host.r.run(
            [os.path.join(self.eximPath, self.__iPath(self.eximBuild, i), "bin", "exim"),
             "-bdf", "-oX", str(self.port + i), "-C", config],
            wait = False)
        self.__proc.append(proc)

        waitForLog(self.host, os.path.join(self.__iPath(self.spoolDir, i), "log", "mainlog"),
                   "exim", 5, "listening for SMTP")

    def start(self):
        for i in range(0, self.numInstances):
            self.__start(i)

    def stop(self):
        for p in self.__proc:
            # Ugh, there's no way to cleanly shut down Exim, so we can't
            # check for a sensible exit code.
            p.kill(signal.SIGTERM)

    def reset(self):
        if len(self.__proc) > 0:
            self.stop()

__all__.append("EximLoad")
class EximLoad(Task, ResultsProvider):
    __info__ = ["host", "trial", "eximPath", "clients", "port", "*sysmonOut"]

    # XXX Control warmup/duration
    def __init__(self, host, trial, eximPath, cores, clients, port, sysmon):
        Task.__init__(self, host = host, trial = trial)
        ResultsProvider.__init__(self, cores)
        self.host = host
        self.trial = trial
        self.eximPath = eximPath
        self.clients = clients
        self.port = port
        self.sysmon = sysmon

    def wait(self):
        # We may want to wipe out old mail files, but it doesn't seem
        # to make a difference.

        cmd = [os.path.join(self.eximPath, "run-smtpbm"),
               str(self.clients), str(self.port)]
        cmd = self.sysmon.wrap(cmd, "Starting", "Stopped")

        # Run
        logPath = self.host.getLogPath(self)
        self.host.r.run(cmd, stdout = logPath)

        # XXX Sanity check no paniclog or rejectlog, non-empty mboxes,
        # non-empty mainlog

        # Get result
        log = self.host.r.readFile(logPath)
        self.sysmonOut = self.sysmon.parseLog(log)
        ms = re.findall("(?m)^([0-9]+) messages", log)
        if len(ms) != 1:
            raise RuntimeError("Expected 1 message count in log, got %d",
                               len(ms))
        self.setResults(int(ms[0]), "message", "messages",
                        self.sysmonOut["time.real"])

class EximRunner(object):
    def __str__(self):
        return "exim"

    @staticmethod
    def run(m, cfg):
        if not cfg.hotplug:
            raise RuntimeError("The Exim benchmark requires hotplug = True.  "
                               "Either enable hotplug or disable the Exim "
                               "benchmark in config.py.")

        host = cfg.primaryHost
        m += host
        m += HostInfo(host)
        fs = FileSystem(host, cfg.fs, clean = True)
        m += fs
        eximPath = os.path.join(cfg.benchRoot, "exim")
        m += SetCPUs(host = host, num = cfg.cores)
        m += EximDaemon(host, eximPath, cfg.eximBuild,
                        os.path.join(fs.path + "0"),
                        os.path.join(fs.path + "spool"),
                        cfg.eximPort,
                        cfg.numInstances)
        sysmon = SystemMonitor(host)
        m += sysmon
        for trial in range(cfg.trials):
            # XXX It would be a pain to make clients dependent on
            # cfg.cores.
            m += EximLoad(host, trial, eximPath, cfg.cores,
                          cfg.clients, cfg.eximPort, sysmon)
        # m += cfg.monitors
        m.run()

__all__.append("runner")
runner = EximRunner()
