from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from cerberus.messages import EthereumVerifyMessage, Success


async def verify_message(msg: EthereumVerifyMessage) -> Success:
    from cerberus import TR
    from cerberus.crypto.curve import secp256k1
    from cerberus.crypto.hashlib import sha3_256
    from cerberus.messages import Success
    from cerberus.ui.layouts import confirm_signverify, show_success
    from cerberus.wire import DataError

    from apps.common.signverify import decode_message

    from .helpers import address_from_bytes, bytes_from_address
    from .sign_message import message_digest

    digest = message_digest(msg.message)
    if len(msg.signature) != 65:
        raise DataError("Invalid signature")
    sig = bytearray([msg.signature[64]]) + msg.signature[:64]

    pubkey = secp256k1.verify_recover(sig, digest)

    if not pubkey:
        raise DataError("Invalid signature")

    pkh = sha3_256(pubkey[1:], keccak=True).digest()[-20:]

    address_bytes = bytes_from_address(msg.address)
    if address_bytes != pkh:
        raise DataError("Invalid signature")

    address = address_from_bytes(address_bytes)

    await confirm_signverify(decode_message(msg.message), address, verify=True)

    await show_success("verify_message", TR.ethereum__valid_signature)
    return Success(message="Message verified")
