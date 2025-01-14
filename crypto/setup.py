#!/usr/bin/env python
from distutils.core import setup
from distutils.extension import Extension

from Cython.Build import cythonize
from Cython.Distutils import build_ext

srcs = [
    "nist256p1",
    "base58",
    "bignum",
    "bip32",
    "ecdsa",
    "curve25519",
    "hmac",
    "rand",
    "ripemd160",
    "secp256k1",
    "sha2",
]

extensions = [
    Extension(
        "CerberusCrypto",
        sources=["CerberusCrypto.pyx", "c.pxd"] + [x + ".c" for x in srcs],
        extra_compile_args=[],
    )
]

setup(
    name="CerberusCrypto",
    version="0.0.0",
    description="Cython wrapper around cerberus-crypto library",
    author="Pavol Rusnak",
    author_email="stick@satoshilabs.com",
    url="https://github.com/Cerberus-Wallet/cerberus-crypto",
    cmdclass={"build_ext": build_ext},
    ext_modules=cythonize(extensions),
)
