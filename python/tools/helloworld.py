#!/usr/bin/env python3

# This file is part of the Cerberus project.
#
# Copyright (C) 2012-2022 SatoshiLabs and contributors
#
# This library is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License version 3
# as published by the Free Software Foundation.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the License along with this library.
# If not, see <https://www.gnu.org/licenses/lgpl-3.0.html>.

from cerberuslib import btc
from cerberuslib.client import get_default_client
from cerberuslib.tools import parse_path


def main() -> None:
    # Use first connected device
    client = get_default_client()

    # Print out Cerberus's features and settings
    print(client.features)

    # Get the first address of first BIP44 account
    bip32_path = parse_path("44h/0h/0h/0/0")
    address = btc.get_address(client, "Bitcoin", bip32_path, True)
    print("Bitcoin address:", address)


if __name__ == "__main__":
    main()
