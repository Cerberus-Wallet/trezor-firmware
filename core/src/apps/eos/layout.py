async def require_get_public_key(
    public_key: str, path: str, account: str | None
) -> None:
    from cerberus.ui.layouts import show_pubkey

    await show_pubkey(public_key, path=path, account=account)


async def require_sign_tx(num_actions: int) -> None:
    from cerberus import TR
    from cerberus.enums import ButtonRequestType
    from cerberus.strings import format_plural
    from cerberus.ui.layouts import confirm_action

    await confirm_action(
        "confirm_tx",
        TR.eos__sign_transaction,
        description=TR.eos__about_to_sign_template,
        description_param=format_plural(
            "{count} {plural}", num_actions, TR.plurals__sign_x_actions
        ),
        br_code=ButtonRequestType.SignTx,
    )
