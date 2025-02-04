from typing import TYPE_CHECKING

from apps.common.keychain import auto_keychain

if TYPE_CHECKING:
    from cerberus.messages import EosGetPublicKey, EosPublicKey

    from apps.common.keychain import Keychain


@auto_keychain(__name__)
async def get_public_key(msg: EosGetPublicKey, keychain: Keychain) -> EosPublicKey:
    from cerberus.crypto.curve import secp256k1
    from cerberus.messages import EosPublicKey

    from apps.common import paths

    from .helpers import public_key_to_wif
    from .layout import require_get_public_key

    await paths.validate_path(keychain, msg.address_n)

    node = keychain.derive(msg.address_n)

    public_key = secp256k1.publickey(node.private_key(), True)
    wif = public_key_to_wif(public_key)

    if msg.show_display:
        from . import PATTERN, SLIP44_ID

        path = paths.address_n_to_str(msg.address_n)
        account = paths.get_account_name("EOS", msg.address_n, PATTERN, SLIP44_ID)
        await require_get_public_key(wif, path, account)
    return EosPublicKey(wif_public_key=wif, raw_public_key=public_key)
