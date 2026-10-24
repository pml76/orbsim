"""The read record of a Python process of a script test (M1-138).

Python loads this file at its start when its folder is on PYTHONPATH, which
the test driver, scripts/run-tests.py, puts there for each test that CTest
runs (register decision 771). It does nothing unless ORBSIM_READ_RECORDS
names the folder of the records. Then it records, with Python's audit hook
(PEP 578) and with a replacement of the functions that examine a file, what
the process reads, so that the driver runs the test again only when one of
those files changes (decision 765):

    python-read-record 1
    argv <the command line of the process, sys.orig_argv, as JSON>
    cwd <its working folder at its start>
    read <SHA-256 | missing> <path>   a file opened to read, the source of a
                                      module that ran, or a file that a
                                      replaced function examined
    list <SHA-256 | missing> <path>   a folder listed, or examined: the hash
                                      of the sorted names in it
    child <JSON>                      a process that it started: its
                                      arguments and its working folder
    end

The hook gets each hash at the exit of the process, as the C++ records get
theirs as the test case ends (decision 527). Only paths in the repository and in the
build tree of the record go in (decision 765). The file is written at the
start of the process with its first three lines, and again at its exit, and
only the exit adds the last line `end` (decision 782): a process that is
killed, or that ends with os._exit, writes only a record with no `end`, and its
test then runs by the rules before M1-138.

**Where the records go** (decisions 781 and 783). Each record goes into
`<folder>/<test>.py-<process>-<start>.txt` for its own test, ORBSIM_TEST_NAME
in the folder ORBSIM_READ_RECORDS, and for each outer test that
ORBSIM_READ_RECORD_OWNER names. That variable holds, as JSON, each pair of a
folder and a test from the outermost one in; this file adds its own pair for
its children. Thus a child that a self-test gives a test name of its own, or
one that a driver in a test starts with no test name, also adds its reads to
the record of the outer test. The test name is escaped as
tests/TestReads.cpp and scripts/run-tests.py escape it (decision 528).

**What it replaces** (decisions 770 and 780). The audit hook sees no question
about a file without an open, thus os.stat, os.lstat, os.path.exists,
lexists, isfile and isdir are replaced; pathlib calls these. On Windows,
os.path.exists, isfile and isdir do not call os.stat, measured with Python
3.14.0 in the spike of M1-138. Each replacement goes into each set of os that
holds the function before it, because shutil reads those sets when it loads
(shutil.py of Python 3.14.0, lines 804 to 807).

scripts/check-read-record.py is its self-test, the CTest test
read_record_self_test (decision 785).
"""

import atexit
import hashlib
import json
import ntpath
import os
import posixpath
import sys
import threading
import time

_RECORDS_VARIABLE = "ORBSIM_READ_RECORDS"
_OWNER_VARIABLE = "ORBSIM_READ_RECORD_OWNER"
_TEST_NAME_VARIABLE = "ORBSIM_TEST_NAME"
_HEAD = "python-read-record 1"
# The repository: this file is <repository>/scripts/read-record/sitecustomize.py.
_REPOSITORY = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# The audit events of the processes that a process starts.
_STARTS = ("subprocess.Popen", "os.system", "os.startfile", "os.spawn", "os.exec", "os.posix_spawn")
_EVENTS = frozenset(("open", "os.listdir", "os.scandir", "exec") + _STARTS)
# The functions that examine a file, which the audit hook does not see.
_OS_FUNCTIONS = ("stat", "lstat")
_PATH_FUNCTIONS = ("exists", "lexists", "isfile", "isdir")
_OS_SETS = ("supports_fd", "supports_follow_symlinks", "supports_dir_fd", "supports_effective_ids")

_local = threading.local()
_lock = threading.Lock()
_files, _folders, _children = set(), set(), []


def _escaped_name(name):
    """A test name as a file name (decision 528): each character that Windows
    forbids in a file name, `%` itself, and a last `.` or space, as `%` and two
    uppercase hexadecimal digits of each of its UTF-8 bytes."""
    out = []
    for i, char in enumerate(name):
        last = i + 1 == len(name)
        if ord(char) < 0x20 or char in '<>:"/\\|?*%' or (last and char in ". "):
            out.extend(f"%{b:02X}" for b in char.encode("utf-8"))
        else:
            out.append(char)
    return "".join(out)


def _text(path):
    if isinstance(path, bytes):
        path = os.fsdecode(path)
    return os.path.abspath(os.fspath(path))


def _inside(path, folder):
    path, folder = os.path.normcase(path), os.path.normcase(folder)
    return path == folder or path.startswith(folder.rstrip("\\/") + os.sep)


def _busy():
    return getattr(_local, "busy", False)


def _reads_file(mode, flags):
    """Whether an open of the audit event can read: a mode with `r` or `+`, or
    flags without O_WRONLY for os.open, which gives no mode."""
    if isinstance(mode, str):
        return "r" in mode or "+" in mode
    if isinstance(flags, int):
        return flags & (os.O_WRONLY | os.O_RDWR) != os.O_WRONLY
    return True


def _hook(event, args):
    if event not in _EVENTS or _busy():
        return
    _local.busy = True
    try:
        if event == "open":
            path, mode, flags = args[0], args[1], args[2]
            if path is None or isinstance(path, int):
                return
            if _reads_file(mode, flags):
                with _lock:
                    _files.add(_text(path))
        elif event in ("os.listdir", "os.scandir"):
            path = args[0]
            if not isinstance(path, int):
                with _lock:
                    _folders.add(_text("." if path in (None, "", b"") else path))
        elif event == "exec":
            # Each module that runs, also one that spec_from_file_location
            # and exec_module load, which sys.modules does not hold, and one
            # loaded from a cached .pyc file, when Python does not open its
            # .py file (the record of M1-138's spike). Python's import system
            # gives such code the path of its source.
            name = getattr(args[0], "co_filename", None)
            if isinstance(name, str) and not name.startswith("<"):
                with _lock:
                    _files.add(_text(name))
        elif event == "subprocess.Popen":
            argv = args[1]
            argv = [os.fsdecode(a) if isinstance(a, bytes) else str(a) for a in argv] \
                if isinstance(argv, (list, tuple)) else (os.fsdecode(argv) if isinstance(argv, bytes) else str(argv))
            with _lock:
                _children.append({"argv": argv, "cwd": _text(args[2]) if args[2] else os.getcwd()})
        else:
            with _lock:
                _children.append({"event": event, "argv": repr(args), "cwd": os.getcwd()})
    except Exception:  # noqa: BLE001 -- the record must not change what the process does
        _local.failed = True
    finally:
        _local.busy = False


def _wrap(module, name, sets):
    original = getattr(module, name)

    def replacement(path, *args, **kwargs):
        if not _busy() and not isinstance(path, int):
            _local.busy = True
            try:
                with _lock:
                    _files.add(_text(path))
            except Exception:  # noqa: BLE001
                _local.failed = True
            finally:
                _local.busy = False
        return original(path, *args, **kwargs)

    replacement.__wrapped__ = original
    replacement.__name__ = getattr(original, "__name__", name)
    replacement.__doc__ = getattr(original, "__doc__", None)
    for set_name in sets:
        members = getattr(os, set_name, None)
        if isinstance(members, set) and original in members:
            members.add(replacement)
    setattr(module, name, replacement)


def _digest(data):
    return hashlib.sha256(data).hexdigest()


def _names(folder):
    return _digest("\n".join(sorted(os.listdir(folder))).encode("utf-8", "surrogateescape"))


def _lines_for(tree):
    """The lines of the record for one destination: only paths in the
    repository and in `tree`, each hashed at this time."""
    def wanted(path):
        return _inside(path, _REPOSITORY) or _inside(path, tree)

    lines = []
    with _lock:
        files, folders, children = sorted(_files), sorted(_folders), list(_children)
    for path in files:
        if not wanted(path):
            continue
        try:
            if os.path.isdir(path):
                lines.append(f"list {_names(path)} {path}")
            else:
                with open(path, "rb") as stream:
                    lines.append(f"read {_digest(stream.read())} {path}")
        except FileNotFoundError:
            lines.append(f"read missing {path}")
        except OSError:
            lines.append(f"read unreadable {path}")
    for path in folders:
        if not wanted(path):
            continue
        try:
            lines.append(f"list {_names(path)} {path}")
        except FileNotFoundError:
            lines.append(f"list missing {path}")
        except OSError:
            lines.append(f"list unreadable {path}")
    for child in children:
        lines.append("child " + json.dumps(child, sort_keys=True))
    return lines


def _write(destinations, body):
    for folder, test in destinations:
        target = os.path.join(folder, f"{_escaped_name(test)}.py-{os.getpid()}-{_STARTED}.txt")
        try:
            lines = [_HEAD, "argv " + json.dumps(_ARGV), "cwd " + _CWD]
            if body:
                lines += _lines_for(os.path.dirname(os.path.abspath(folder)))
                if not getattr(_local, "failed", False):
                    lines.append("end")
            with open(target, "w", encoding="utf-8", errors="surrogateescape", newline="\n") as out:
                out.write("\n".join(lines) + "\n")
        except OSError:
            pass


def _at_exit():
    _local.busy = True
    _write(_DESTINATIONS, body=True)


def _destinations():
    """[(folder, test)] from the outermost test in, this process's own test
    last; empty where the driver named no folder."""
    pairs = []
    try:
        pairs = [tuple(p) for p in json.loads(os.environ.get(_OWNER_VARIABLE, "[]"))
                 if isinstance(p, list) and len(p) == 2]
    except ValueError:
        pairs = []
    folder, test = os.environ.get(_RECORDS_VARIABLE), os.environ.get(_TEST_NAME_VARIABLE)
    if folder and test and (folder, test) not in pairs:
        pairs.append((folder, test))
    return pairs


_STARTED = time.time_ns()
# All of the command line, the interpreter and `-c <code>` included, which
# sys.argv does not hold: the driver finds the interpreter of the test's own
# script by it (decision 782).
_ARGV = [os.fsdecode(a) if isinstance(a, bytes) else str(a) for a in getattr(sys, "orig_argv", sys.argv)]
_CWD = os.getcwd()
_DESTINATIONS = _destinations() if os.environ.get(_RECORDS_VARIABLE) else []
if _DESTINATIONS:
    os.environ[_OWNER_VARIABLE] = json.dumps(_DESTINATIONS)
    _files.add(os.path.abspath(__file__))
    for _name in _OS_FUNCTIONS:
        _wrap(os, _name, _OS_SETS)
    _path_module = ntpath if os.name == "nt" else posixpath
    for _name in _PATH_FUNCTIONS:
        _wrap(_path_module, _name, ())
    _write(_DESTINATIONS, body=False)
    sys.addaudithook(_hook)
    atexit.register(_at_exit)
