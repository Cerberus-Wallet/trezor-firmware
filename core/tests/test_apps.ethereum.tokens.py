from common import *

if not utils.BITCOIN_ONLY:
    from apps.ethereum import tokens
    from ethereum_common import builtin_token_by_chain_address


@unittest.skipUnless(not utils.BITCOIN_ONLY, "altcoin")
class TestEthereumTokens(unittest.TestCase):
    def setUp(self):
        # use mockup function for built-in tokens
        tokens.token_by_chain_address = builtin_token_by_chain_address

    def test_token_by_chain_address(self):

        token = tokens.token_by_chain_address(1, b"\x7f\xc6\x65\x00\xc8\x4a\x76\xad\x7e\x9c\x93\x43\x7b\xfc\x5a\xc3\x3e\x2d\xda\xe9")
        self.assertEqual(token.symbol, 'AAVE')

        # invalid adress, invalid chain
        token = tokens.token_by_chain_address(999, b'\x00\xFF')
        self.assertIs(token, tokens.UNKNOWN_TOKEN)
        self.assertEqual(token.symbol, 'Wei UNKN')
        self.assertEqual(token.decimals, 0)


if __name__ == '__main__':
    unittest.main()
