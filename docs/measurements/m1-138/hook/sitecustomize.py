"""M1-138, the spike of decision 766: a measurement tool, not the code of the
task. Python loads this file at its start when its folder is on PYTHONPATH.
Each Python process writes one log into the folder that M1138_SPIKE_LOGS
names, at its exit. The log holds what decisions 765 and 770 put into a read
record, and more, so that the spike can compare it with Process Monitor:

    open    <path> <mode> <flags>   a file that the process opened
    list    <folder> <hash>         a folder that it listed, and the SHA-256
                                    of the sorted names in it at that time
    glob    <pattern>               a pattern given to glob
    start   <arguments> <folder>    a process that it started, and its folder
    probe   <function> <path> <answer>  a question about a file: os.stat and
                                    the functions that decision 770 names
    module  <path>                  the source file of each module, at exit
    end                             the last line: the log is complete

The first lines name the test: the owner, which this file keeps in
M1138_SPIKE_OWNER for each child process, and ORBSIM_TEST_NAME as this
process got it."""
import atexit
import hashlib
import importlib.util
import ntpath
import os
import posixpath
import sys
import threading
import time

_FOLDER = os.environ.get("M1138_SPIKE_LOGS")
_OWNER = "M1138_SPIKE_OWNER"
_WATCHED = {"open", "os.listdir", "os.scandir", "glob.glob", "subprocess.Popen", "os.chdir", "os.startfile",
            "os.system", "os.posix_spawn", "os.spawn", "os.exec", "_winapi.CreateProcess", "exec"}
_local = threading.local()
_lock = threading.Lock()
_events = []
_started = time.time_ns()


def _inside():
    return getattr(_local, "inside", False)


def _text(path):
    if isinstance(path, bytes):
        path = path.decode("utf-8", "surrogateescape")
    return os.path.abspath(os.fspath(path))


def _add(*fields):
    line = "\t".join(str(f).replace("\t", " ").replace("\n", " ") for f in fields)
    with _lock:
        _events.append(line)


def _names(folder):
    try:
        names = sorted(os.listdir(folder))
    except OSError as error:
        return f"error:{type(error).__name__}"
    return hashlib.sha256("\n".join(names).encode("utf-8", "surrogateescape")).hexdigest()


def _hook(event, args):
    if event not in _WATCHED or _inside():
        return
    _local.inside = True
    try:
        if event == "open":
            path, mode, flags = args[0], args[1], args[2]
            if path is not None and not isinstance(path, int):
                full = _text(path)
                _add("open", full, mode, flags)
                # A cached .pyc file stands for its .py file, which Python
                # then does not open (N3).
                if full.endswith(".pyc") and "__pycache__" in full:
                    try:
                        _add("module", importlib.util.source_from_cache(full))
                    except ValueError:
                        pass
        elif event == "exec":
            # Each module that runs, also one that spec_from_file_location
            # and exec_module load and that sys.modules does not hold.
            name = getattr(args[0], "co_filename", None)
            if isinstance(name, str) and not name.startswith("<"):
                _add("module", _text(name))
        elif event in ("os.listdir", "os.scandir"):
            path = args[0]
            if not isinstance(path, int):
                folder = _text("." if path in (None, "", b"") else path)
                _add("list", folder, _names(folder))
        elif event == "glob.glob":
            _add("glob", args[0], "")
        elif event == "subprocess.Popen":
            _add("start", repr(args[1]), args[2] or os.getcwd())
        elif event == "os.chdir":
            _add("chdir", _text(args[0]), "")
        else:
            _add("start", repr(args), os.getcwd())
    except Exception as error:  # a measurement tool must not change the result of a test
        _add("hookerror", event, repr(error))
    finally:
        _local.inside = False


def _wrap(module, name):
    original = getattr(module, name, None)
    if original is None or getattr(original, "_m1138", False):
        return

    def wrapper(path, *args, **kwargs):
        try:
            result = original(path, *args, **kwargs)
        except OSError:
            if not _inside() and not isinstance(path, int):
                _add("probe", name, _text(path), "missing")
            raise
        if not _inside() and not isinstance(path, int):
            _add("probe", name, _text(path), result if isinstance(result, bool) else "found")
        return result

    wrapper._m1138 = True
    wrapper.__wrapped__ = original
    wrapper.__name__ = name
    setattr(module, name, wrapper)


def _write():
    _local.inside = True
    modules = sorted({os.path.abspath(f) for m in list(sys.modules.values())
                      for f in [getattr(m, "__file__", None)] if isinstance(f, str)})
    head = [("owner", os.environ.get(_OWNER, "")), ("test", _test_name), ("pid", os.getpid()),
            ("ppid", os.getppid()), ("argv", repr(sys.argv)), ("cwd", os.getcwd()), ("started", _started),
            ("ended", time.time_ns())]
    try:
        os.makedirs(_FOLDER, exist_ok=True)
        with open(os.path.join(_FOLDER, f"{os.getpid()}-{_started}.log"), "w", encoding="utf-8",
                  errors="surrogateescape") as out:
            for key, value in head:
                out.write(f"{key}\t{value}\n")
            for line in _events:
                out.write(line + "\n")
            for path in modules:
                out.write(f"module\t{path}\n")
            out.write("end\n")
    except OSError:
        pass


_test_name = os.environ.get("ORBSIM_TEST_NAME", "")
if _FOLDER:
    if _OWNER not in os.environ and _test_name:
        os.environ[_OWNER] = _test_name
    for _name in ("stat", "lstat", "access"):
        _wrap(os, _name)
    _path_module = ntpath if os.name == "nt" else posixpath
    for _name in ("exists", "lexists", "isfile", "isdir", "islink", "isjunction"):
        _wrap(_path_module, _name)
    sys.addaudithook(_hook)
    atexit.register(_write)
