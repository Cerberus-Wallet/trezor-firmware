# generated from coininfo.py.mako
# (by running `make templates` in `core`)
# do not edit manually!

# NOTE: using positional arguments saves 4500 bytes of flash size

from typing import Any

from cerberus import utils
from cerberus.crypto.base58 import blake256d_32, groestl512d_32, keccak_32, sha256d_32
from cerberus.crypto.scripts import blake256_ripemd160, sha256_ripemd160

# flake8: noqa


class CoinInfo:
    def __init__(
        self,
        coin_name: str,
        coin_shortcut: str,
        decimals: int,
        address_type: int,
        address_type_p2sh: int,
        maxfee_kb: int,
        signed_message_header: str,
        xpub_magic: int,
        xpub_magic_segwit_p2sh: int | None,
        xpub_magic_segwit_native: int | None,
        xpub_magic_multisig_segwit_p2sh: int | None,
        xpub_magic_multisig_segwit_native: int | None,
        bech32_prefix: str | None,
        cashaddr_prefix: str | None,
        slip44: int,
        segwit: bool,
        taproot: bool,
        fork_id: int | None,
        force_bip143: bool,
        decred: bool,
        negative_fee: bool,
        curve_name: str,
        extra_data: bool,
        timestamp: bool,
        overwintered: bool,
        confidential_assets: dict[str, Any] | None,
    ) -> None:
        self.coin_name = coin_name
        self.coin_shortcut = coin_shortcut
        self.decimals = decimals
        self.address_type = address_type
        self.address_type_p2sh = address_type_p2sh
        self.maxfee_kb = maxfee_kb
        self.signed_message_header = signed_message_header
        self.xpub_magic = xpub_magic
        self.xpub_magic_segwit_p2sh = xpub_magic_segwit_p2sh
        self.xpub_magic_segwit_native = xpub_magic_segwit_native
        self.xpub_magic_multisig_segwit_p2sh = xpub_magic_multisig_segwit_p2sh
        self.xpub_magic_multisig_segwit_native = xpub_magic_multisig_segwit_native
        self.bech32_prefix = bech32_prefix
        self.cashaddr_prefix = cashaddr_prefix
        self.slip44 = slip44
        self.segwit = segwit
        self.taproot = taproot
        self.fork_id = fork_id
        self.force_bip143 = force_bip143
        self.decred = decred
        self.negative_fee = negative_fee
        self.curve_name = curve_name
        self.extra_data = extra_data
        self.timestamp = timestamp
        self.overwintered = overwintered
        self.confidential_assets = confidential_assets
        if curve_name == "secp256k1-groestl":
            self.b58_hash = groestl512d_32
            self.sign_hash_double = False
            self.script_hash: type[utils.HashContextInitable] = sha256_ripemd160
        elif curve_name == "secp256k1-decred":
            self.b58_hash = blake256d_32
            self.sign_hash_double = False
            self.script_hash = blake256_ripemd160
        elif curve_name == "secp256k1-smart":
            self.b58_hash = keccak_32
            self.sign_hash_double = False
            self.script_hash = sha256_ripemd160
        else:
            self.b58_hash = sha256d_32
            self.sign_hash_double = True
            self.script_hash = sha256_ripemd160

    def __eq__(self, other: Any) -> bool:
        if not isinstance(other, CoinInfo):
            return NotImplemented
        return self.coin_name == other.coin_name


# fmt: off
<%
def hexfmt(x):
    if x is None:
        return None
    else:
        return "0x{:08x}".format(x)

def optional_dict(x):
    if x is None:
        return None
    return dict(x)

ATTRIBUTES = (
    ("coin_name", lambda _: "name"),
    ("coin_shortcut", black_repr),
    ("decimals", int),
    ("address_type", int),
    ("address_type_p2sh", int),
    ("maxfee_kb", int),
    ("signed_message_header", black_repr),
    ("xpub_magic", hexfmt),
    ("xpub_magic_segwit_p2sh", hexfmt),
    ("xpub_magic_segwit_native", hexfmt),
    ("xpub_magic_multisig_segwit_p2sh", hexfmt),
    ("xpub_magic_multisig_segwit_native", hexfmt),
    ("bech32_prefix", black_repr),
    ("cashaddr_prefix", black_repr),
    ("slip44", int),
    ("segwit", bool),
    ("taproot", bool),
    ("fork_id", black_repr),
    ("force_bip143", bool),
    ("decred", bool),
    ("negative_fee", bool),
    ("curve_name", lambda r: repr(r.replace("_", "-"))),
    ("extra_data", bool),
    ("timestamp", bool),
    ("overwintered", bool),
    ("confidential_assets", optional_dict),
)

btc_names = ["Bitcoin", "Testnet", "Regtest"]

# TODO: make this easily extendable for more models

coins_btc_t2t1 = [c for c in supported_on("T2T1", bitcoin) if c.name in btc_names]
coins_alt_t2t1 = [c for c in supported_on("T2T1", bitcoin) if c.name not in btc_names]

coins_btc_t2b1 = [c for c in supported_on("T2B1", bitcoin) if c.name in btc_names]
coins_alt_t2b1 = [c for c in supported_on("T2B1", bitcoin) if c.name not in btc_names]

%>\
def by_name(name: str) -> CoinInfo:
    if utils.MODEL_IS_T2B1:
% for coin in coins_btc_t2b1:
        if name == ${black_repr(coin["coin_name"])}:
            return CoinInfo(
                % for attr, func in ATTRIBUTES:
                ${func(coin[attr])},  # ${attr}
                % endfor
            )
% endfor
        if not utils.BITCOIN_ONLY:
% for coin in coins_alt_t2b1:
            if name == ${black_repr(coin["coin_name"])}:
                return CoinInfo(
                    % for attr, func in ATTRIBUTES:
                    ${func(coin[attr])},  # ${attr}
                    % endfor
                )
% endfor
        raise ValueError  # Unknown coin name
    else:
% for coin in coins_btc_t2t1:
        if name == ${black_repr(coin["coin_name"])}:
            return CoinInfo(
                % for attr, func in ATTRIBUTES:
                ${func(coin[attr])},  # ${attr}
                % endfor
            )
% endfor
        if not utils.BITCOIN_ONLY:
% for coin in coins_alt_t2t1:
            if name == ${black_repr(coin["coin_name"])}:
                return CoinInfo(
                    % for attr, func in ATTRIBUTES:
                    ${func(coin[attr])},  # ${attr}
                    % endfor
                )
% endfor
        raise ValueError  # Unknown coin name
