"""Import stub so the API can boot without compiling Oracle client."""


class Error(Exception):
    pass


class DatabaseError(Error):
    pass


class OperationalError(DatabaseError):
    pass


class InterfaceError(Error):
    pass


class IntegrityError(DatabaseError):
    pass


def makedsn(*args, **kwargs):
    raise DatabaseError("Oracle client is not installed in this local stack")


def connect(*args, **kwargs):
    raise DatabaseError("Oracle client is not installed in this local stack")


class SessionPool:
    def __init__(self, *args, **kwargs):
        raise DatabaseError("Oracle client is not installed in this local stack")
