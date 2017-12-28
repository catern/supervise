"""A small demo of supervise."""
import supervise_api
import select
import linuxfd
import time
numbers = [
    2,
    3,
    5,
    7,
    11,
    13,
    17,
    19,
    23,
    29,
]

class Sleeper(object):
    """It sleeps. And repeats."""
    start_count = 0
    backoff = 0.1
    def __init__(self, callbacker, duration):
        self.callbacker = callbacker
        self.duration = duration
        self.start()

    def __real_start(self):
        print("starting sleeper {}".format(self.duration))
        self.proc = supervise.Process(["sleep", str(self.duration)])

    def start(self):
        self.start_count += 1
        if self.start_count < 3:
            self.__real_start()
        elif self.start_count == 3:
            print("deferring restarting sleeper {} for 10".format(self.duration))
            self.callbacker.register(time.time() + 10, self.__real_start)
        elif self.start_count > 3:
            print("deferring restarting sleeper {} with backoff {}".format(self.duration, self.backoff))
            self.callbacker.register(time.time() + self.backoff, self.__real_start)
            self.backoff = self.backoff * 2

    def fileno(self):
        return self.proc.fileno()

    def work(self):
        self.proc.poll()
        if self.proc.closed():
            print("sleeper {} has awakened...".format(self.duration))
            self.start()


class Callbacker(object):
    """It does callbacks."""
    start_count = 0
    def __init__(self):
        self.fd = linuxfd.timerfd()
        self.callbacks = []

    def fileno(self):
        return self.fd.fileno()

    def work(self):
        try:
            self.fd.read()
            flush_callbacks()
        except OSError as e:
            if e.errno == errno.EAGAIN:
                return
            raise

    def flush_callbacks(self):
        curtime = time.time()
        unexpired = []
        nexttime = None
        for t, callback in self.callbacks:
            if t <= curtime:
                callback()
            else:
                unexpired.append((t, callback))
                if nexttime is None or nexttime > t:
                    nexttime = t
        self.callbacks = unexpired
        if nexttime:
            self.fd.settime(nexttime, absolute=True)

    def register(self, time, callback):
        self.callbacks.append((time, callback))
        self.flush_callbacks()

def make_sleepers(numlist):
    cb = Callbacker()
    return [cb] + [Sleeper(cb, num) for num in numlist]

def work_on_list(lst):
    for elem in select.select([elem for elem in lst if elem.fileno() > 0], [], [])[0]:
        elem.work()

def main():
    sleepers = make_sleepers(numbers)
    while True:
        work_on_list(sleepers)


if __name__ == "__main__":
    # no zombies, please
    supervise.ignore_sigchld()
    main()
