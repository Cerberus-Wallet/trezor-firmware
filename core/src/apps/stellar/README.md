# Stellar

MAINTAINER = Tomas Susanka <tomas.susanka@satoshilabs.com>

AUTHOR = Tomas Susanka <tomas.susanka@satoshilabs.com>

REVIEWER = Jan Pochyla <jan.pochyla@satoshilabs.com>

ADVISORS = ZuluCrypto

-----

This implementation of Stellar for Cerberus Core is mostly based on the cerberus-mcu C implementation by ZuluCrypto.

Stellar has a comprehensive [developers documentation](https://www.stellar.org/developers/).

## Transactions

Stellar transaction is composed of one or more operations. We support all [operations](https://www.stellar.org/developers/guides/concepts/list-of-operations.html) except the Inflation operation (see [here](https://github.com/Cerberus-Wallet/cerberus-core/issues/202#issuecomment-392729595) for rationale). A list of supported operations:

- Account Merge
- Allow Trust
- Bump Sequence
- Change Trust
- Create Account
- Create Passive Sell Offer
- Manage Data
- Manage Buy Offer
- Manage Sell Offer
- Path Payment Strict Receive
- Path Payment Strict Send
- Payment
- Set Options

Since a simple transaction can be composed of several operations, first the StellarSignTx message is sent to Cerberus, which includes the total number of operations. Then the different operations are consecutively send to Cerberus.
