from micropython import const
from typing import TYPE_CHECKING

import storage.device
from cerberus import io, utils
from cerberus.sdcard import with_filesystem

if TYPE_CHECKING:
    from typing import Callable, TypeVar

    T = TypeVar("T", bound=Callable)

if utils.USE_SD_CARD:
    fatfs = io.fatfs  # global_import_cache

SD_CARD_HOT_SWAPPABLE = False
SD_SALT_LEN_BYTES = const(32)
_SD_SALT_AUTH_TAG_LEN_BYTES = const(16)


class WrongSdCard(Exception):
    pass


def is_enabled() -> bool:
    return storage.device.get_sd_salt_auth_key() is not None


def compute_auth_tag(salt: bytes, auth_key: bytes) -> bytes:
    from cerberus.crypto import hmac

    digest = hmac(hmac.SHA256, auth_key, salt).digest()
    return digest[:_SD_SALT_AUTH_TAG_LEN_BYTES]


def _get_device_dir() -> str:
    return f"/cerberus/device_{storage.device.get_device_id().lower()}"


def _get_salt_path(new: bool = False) -> str:
    ext = ".new" if new else ""
    return f"{_get_device_dir()}/salt{ext}"


@with_filesystem
def _load_salt(auth_key: bytes, path: str) -> bytearray | None:
    from cerberus.utils import consteq

    # Load the salt file if it exists.
    try:
        with fatfs.open(path, "r") as f:
            salt = bytearray(SD_SALT_LEN_BYTES)
            stored_tag = bytearray(_SD_SALT_AUTH_TAG_LEN_BYTES)
            f.read(salt)
            f.read(stored_tag)
    except fatfs.FatFSError:
        return None

    # Check the salt's authentication tag.
    computed_tag = compute_auth_tag(salt, auth_key)
    if not consteq(computed_tag, stored_tag):
        return None

    return salt


@with_filesystem
def load_sd_salt() -> bytearray | None:
    salt_auth_key = storage.device.get_sd_salt_auth_key()
    if salt_auth_key is None:
        return None

    salt_path = _get_salt_path()
    new_salt_path = _get_salt_path(new=True)

    salt = _load_salt(salt_auth_key, salt_path)
    if salt is not None:
        return salt

    # Check if there is a new salt.
    salt = _load_salt(salt_auth_key, new_salt_path)
    if salt is None:
        # No valid salt file on this SD card.
        raise WrongSdCard

    # Normal salt file does not exist, but new salt file exists. That means that
    # SD salt regeneration was interrupted earlier. Bring into consistent state.
    # TODO Possibly overwrite salt file with random data.
    try:
        fatfs.unlink(salt_path)
    except fatfs.FatFSError:
        pass

    # fatfs.rename can fail with a write error, which falls through as an FatFSError.
    # This should be handled in calling code, by allowing the user to retry.
    fatfs.rename(new_salt_path, salt_path)
    return salt


@with_filesystem
def set_sd_salt(salt: bytes, salt_tag: bytes, stage: bool = False) -> None:
    salt_path = _get_salt_path(stage)
    fatfs.mkdir("/cerberus", True)
    fatfs.mkdir(_get_device_dir(), True)
    with fatfs.open(salt_path, "w") as f:
        f.write(salt)
        f.write(salt_tag)


@with_filesystem
def commit_sd_salt() -> None:
    salt_path = _get_salt_path(new=False)
    new_salt_path = _get_salt_path(new=True)

    try:
        fatfs.unlink(salt_path)
    except fatfs.FatFSError:
        pass
    fatfs.rename(new_salt_path, salt_path)


@with_filesystem
def remove_sd_salt() -> None:
    salt_path = _get_salt_path()
    # TODO Possibly overwrite salt file with random data.
    fatfs.unlink(salt_path)
