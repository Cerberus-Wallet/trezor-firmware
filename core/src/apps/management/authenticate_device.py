from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from cerberus.messages import AuthenticateDevice, AuthenticityProof


async def authenticate_device(msg: AuthenticateDevice) -> AuthenticityProof:
    from cerberus import TR, utils, wire
    from cerberus.crypto import optiga
    from cerberus.crypto.der import read_length
    from cerberus.crypto.hashlib import sha256
    from cerberus.loop import sleep
    from cerberus.messages import AuthenticityProof
    from cerberus.ui.layouts import confirm_action
    from cerberus.ui.layouts.progress import progress
    from cerberus.utils import BufferReader, bootloader_locked

    from apps.common.writers import write_compact_size

    if not bootloader_locked():
        raise wire.ProcessError("Cannot authenticate since bootloader is unlocked.")

    await confirm_action(
        "authenticate_device",
        TR.authenticate__header,
        description=TR.authenticate__confirm_template.format(utils.MODEL_FULL_NAME),
        verb=TR.buttons__allow,
    )

    header = b"AuthenticateDevice:"
    h = utils.HashWriter(sha256())
    write_compact_size(h, len(header))
    h.extend(header)
    write_compact_size(h, len(msg.challenge))
    h.extend(msg.challenge)

    spinner = progress("", description=TR.progress__authenticity_check)
    spinner.report(0)

    try:
        signature = optiga.sign(optiga.DEVICE_ECC_KEY_INDEX, h.get_digest())
    except optiga.SigningInaccessible:
        raise wire.ProcessError("Signing inaccessible.")

    certificates = []
    r = BufferReader(optiga.get_certificate(optiga.DEVICE_CERT_INDEX))
    while r.remaining_count() > 0:
        cert_begin = r.offset
        if r.get() != 0x30:
            wire.FirmwareError("Device certificate is corrupted.")
        n = read_length(r)
        cert_len = r.offset - cert_begin + n
        r.seek(cert_begin)
        certificates.append(r.read_memoryview(cert_len))

    if not utils.DISABLE_ANIMATION:
        frame_delay = sleep(60)
        for i in range(1, 20):
            spinner.report(i * 50)
            await frame_delay

        spinner.report(1000)

    return AuthenticityProof(
        certificates=certificates,
        signature=signature,
    )
