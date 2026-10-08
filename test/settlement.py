#!/usr/bin/env python3
"""Run the bytecode of src/evm.c in geth evm against balances computed here.

Needs tcc, geth evm 1.14.12 and foundry cast. Mapping slots come from
`cast index` and selectors from `cast sig`, not from src/keccak.c."""
import functools
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / '.gatework/settlement-tcc'
SENDER = '7e5f4552091a69125d5dfcb7b8c2659029395bdf'
RECEIVER = '0000000000000000000000007265636569766572'
GAS = 16_777_216
CONFIG = {name + 'Block': 0 for name in (
    'homestead', 'eip150', 'eip155', 'eip158', 'byzantium', 'constantinople',
    'petersburg', 'istanbul', 'berlin', 'london', 'mergeNetsplit')}
CONFIG.update(chainId=1, terminalTotalDifficulty=0, cancunTime=0, shanghaiTime=0)
GENESIS = dict(config=CONFIG, coinbase='0x' + '00' * 20, difficulty='0x0', gasLimit='0x1000000',
               nonce='0x0000000000000000', timestamp='0x0', number='0x0',
               excessBlobGas='0x0', blobGasUsed='0x0')
CODES = (3, 3, 2, 2, 3, 3, 2, 1, 1, 1)
MU, REGISTRY, PROFILE, CHARTER, RESERVE, INDEX, CHECKPOINT, NUM = range(8)
INTERESTC = ROOT / 'build/interestc'
DEBREU = ROOT / 'examples/arrow-debreu.lang'
IMPOSSIBILITY = ROOT / 'examples/arrow-impossibility.lang'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def tool(*args):
    argv = ['tcc', '-Isrc', 'src/evm.c', 'src/keccak.c', 'domain/entries.c', '-run', 'test/evmtool.c', *map(str, args)]
    return subprocess.run(argv, cwd=ROOT, text=True, capture_output=True, timeout=60)


def bytecode(*args):
    result = tool(*args)
    require(result.returncode == 0 and result.stderr == '', f'evmtool {args}: {result.stderr}')
    text = result.stdout
    require(text.endswith('\n') and text.count('\n') == 1 and text.strip() == text.strip().lower(),
            f'evmtool {args}: not one line of lowercase hex')
    return text.strip()


def checked(argv):
    result = subprocess.run(argv, text=True, capture_output=True, timeout=60)
    require(result.returncode == 0, f'{argv[:3]}: {result.stderr}')
    return result.stdout


@functools.cache
def slot(base, key):
    return int(checked(['cast', 'index', 'uint256', str(key), str(base)]).strip(), 16)


@functools.cache
def selector(name, words):
    signature = name + '(' + ','.join(['uint256'] * words) + ')'
    return checked(['cast', 'sig', signature]).strip()[2:]


def data(name, *values):
    return selector(name, len(values)) + ''.join(f'{value:064x}' for value in values)


def objects(text):
    decoder, tail, result = json.JSONDecoder(), text.lstrip(), []
    while tail:
        value, end = decoder.raw_decode(tail)
        result.append(value)
        tail = tail[end:].lstrip()
    return result


def words(slots):
    return {int(key, 16): int(value, 16) for key, value in slots.items() if int(value, 16)}


def wallet(number):
    return f'{number:040x}'


def run(name, code, calldata, *, before=None, value=0, create=False, sender=SENDER, balance=0):
    state = dict(GENESIS, alloc={
        sender: dict(balance=hex(10**24)),
        RECEIVER: dict(balance=hex(balance), storage={f'0x{k:064x}': f'0x{v:064x}'
                                               for k, v in (before or {}).items() if v})})
    genesis = WORK / (name + '-prestate.json')
    genesis.write_text(json.dumps(state))
    argv = ['evm', '--verbosity', '0', 'run', '--prestate', str(genesis), '--gas', str(GAS),
            '--sender', '0x' + sender, '--receiver', '0x' + RECEIVER, '--code', code,
            '--input', calldata, '--value', str(value), '--json', '--dump']
    text = checked(argv + (['--create'] if create else []))
    (WORK / (name + '.out')).write_text(text)
    records = objects(text)
    require(len(records) >= 2 and 'accounts' in records[-1], f'{name}: missing state dump')
    errors = [row['error'] for row in records if row.get('error')]
    require(all(error == 'execution reverted' for error in errors), f'{name}: EVM fault {errors}')
    stores = {key.lower().removeprefix('0x'): words(account.get('storage', {}))
              for key, account in records[-1]['accounts'].items()}
    return dict(status='revert' if errors else 'success',
                output=records[-2]['output'].lower().removeprefix('0x'),
                storage=stores.get(RECEIVER, {}),
                created={key: value for key, value in stores.items() if value and key != RECEIVER},
                balances={key.lower().removeprefix('0x'): int(str(account.get('balance', '0')), 0)
                          for key, account in records[-1]['accounts'].items()})


def expect(name, code, calldata, before, after, result, *, value=0, sender=SENDER):
    actual = run(name, code, calldata, before=before, value=value, sender=sender)
    wanted = dict(status='revert' if result is None else 'success',
                  output='' if result is None else f'{result:064x}',
                  storage={k: v for k, v in after.items() if v})
    got = {key: actual[key] for key in wanted}
    require(got == wanted, f'{name}: EVM {got} != {wanted}')


def paid_case(name, code, calldata, before, after, result, *, balance, sender):
    """withdraw with RECEIVER funded by BALANCE wei: on success the sender gains RESULT wei
    and RECEIVER keeps BALANCE - RESULT."""
    actual = run(name, code, calldata, before=before, sender=sender, balance=balance)
    paid = 0 if result is None else result
    wanted = dict(status='revert' if result is None else 'success',
                  output='' if result is None else f'{result:064x}',
                  storage={k: v for k, v in after.items() if v},
                  receiver=balance - paid, sender=10**24 + paid)
    got = dict(status=actual['status'], output=actual['output'], storage=actual['storage'],
               receiver=actual['balances'].get(RECEIVER, 0), sender=actual['balances'].get(sender, 0))
    require(got == wanted, f'{name}: EVM {got} != {wanted}')
    return 1


def tally_index(members, release, refund):
    return sum(members + 1 - r for r in range(release)) + refund


def verdict(members, codes, ballots):
    return codes[tally_index(members, ballots.count(1), ballots.count(2))]


def deploy(name, creation, runtime, storage):
    made = run(name, creation, '', create=True)
    require(made['status'] == 'success' and made['output'] == runtime,
            f'{name}: creation did not return the runtime')
    written = list(made['created'].values())
    require(made['storage'] == {} and written == [storage],
            f'{name}: genesis storage {written} != [{storage}]')
    paid = run(name + '-value', creation, '', value=1, create=True)
    require(paid['status'] == 'revert' and paid['output'] == '', f'{name}: creation took a value')


def genesis_storage(start, rows):
    """The storage that the genesis writes of domain/entries.c leave: rows w:h:p:u."""
    store = {CHARTER: start}
    for w, h, p, u in rows:
        store[slot(REGISTRY, w)] = h + 1
        store[slot(MU, h)] = store.get(slot(MU, h), 0) + u
        store[slot(PROFILE, h)] = p
    return {key: value for key, value in store.items() if value}


def interestc(*args, binary=None):
    result = subprocess.run([str(binary or INTERESTC), *map(str, args)], cwd=ROOT, text=True,
                            capture_output=True, timeout=120)
    require(result.returncode == 0 and result.stderr == '',
            f'interestc {args[0]}: exit {result.returncode}: {result.stderr.strip()[:400]}')
    return result.stdout


def data_storage(program, binary=None):
    """genesis_storage of the `interestc data PROGRAM` lines."""
    lines = dict((line + ' ').split(' ', 1) for line in interestc('data', program, binary=binary).splitlines())
    rows = [tuple(map(int, row.split(':'))) for row in lines['genesis'].split()]
    return genesis_storage(int(lines['start']), rows)


def program_code(name, program):
    """Deploys the creation code of `interestc build PROGRAM` against its data; -> the runtime."""
    def part(label, *flags):
        out = WORK / f'{name}-{label}.hex'
        interestc('build', program, *flags, '-o', out)
        return out.read_text().strip()

    creation, runtime = part('creation'), part('runtime', '--runtime')
    deploy(f'{name}-deploy', creation, runtime, data_storage(program))
    return runtime


def refusals():
    rows = [(('runtime', 0, 'debreu', 3), 'EVM_LIMIT'),
            (('runtime', 15, 'debreu', *([1] * 136)), 'EVM_LIMIT'),
            (('runtime', 0, 'impossibility'), 'EVM_LIMIT'),
            (('runtime', 3, 'debreu', *CODES[:-1]), 'EVM_TABLE'),
            (('runtime', 3, 'debreu', *CODES[:-1], 4), 'EVM_TABLE'),
            (('creation', 3, 'debreu', 0, *CODES[1:]), 'EVM_TABLE'),
            (('runtime', 3, 'impossibility', 1), 'EVM_TABLE')]
    for args, code in rows:
        result = tool(*args)
        require(result.returncode == 1 and result.stdout == '' and
                result.stderr.startswith(f'interestc: {code}: ') and result.stderr.count('\n') == 1,
                f'refusal {args[:3]}: {result.returncode} {result.stderr!r}')
    usage = tool('runtime', 'x', 'debreu')
    require(usage.returncode == 2, 'evmtool usage exit')
    return len(rows)


def table_cases(prefix, runtime, rows):
    """Each row: label, calldata, before, after (None: before), result (None: revert), value, sender."""
    for label, calldata, before, after, result, value, sender in rows:
        expect(f'{prefix}-{label}', runtime, calldata, before, before if after is None else after,
               result, value=value, sender=sender)
    return len(rows)


def default_cases(runtime, regime, constituting):
    """No program data: charter 1, deny everywhere, no issuers, S = 0."""
    me = int(SENDER, 16)
    before = {CHARTER: 1, slot(REGISTRY, me): 1, slot(MU, 0): 5}
    rows = [('deposit', data('deposit', 1), before, {**before, slot(RESERVE, 1): 2}, 2, 2, SENDER),
            ('deposit-kind', data('deposit', 2), before, None, None, 1, SENDER),
            ('transfer', data('transfer', 1, 1), before, None, None, 0, SENDER),
            ('attest', data('attest', 4099, 3, 1), before, None, None, 0, SENDER),
            ('supply', data('supply'), before, None, 0, 0, SENDER),
            ('charter', data('charter'), before, None, 1, 0, SENDER),
            ('self', data('selfConstituting'), before, None, constituting, 0, SENDER),
            ('distribute', data('distribute', 0), {**before, slot(RESERVE, 0): 4}, None, None, 0, SENDER),
            ('withdraw', data('withdraw'), before, None, None, 0, SENDER),
            ('claim', data('claimOf', 0), {**before, INDEX: 3}, None, 15, 0, SENDER),
            ('no-selector', 'aabbcc', before, None, None, 0, SENDER),
            ('unknown', 'ffffffff', before, None, None, 0, SENDER)]
    return table_cases(f'default-{regime}', runtime, rows)


def vector_index(vector, k):
    return sum((b - 1) * k ** (len(vector) - 1 - i) for i, b in enumerate(vector))


def debreu_example_cases():
    """B2 entries at the genesis of examples/arrow-debreu.lang: start restricted (upTo 4
    everywhere), open any, frozen deny; identity 1 (wallet 4096, 5 units, profile 0) is
    the issuer at open and restricted; identity 2 (wallets 4097, 4098, 5 units, profile 3)."""
    runtime = program_code('example-debreu', DEBREU)
    base = data_storage(DEBREU)
    one, two, three = wallet(4096), wallet(4097), wallet(4098)
    mu1, mu2 = slot(MU, 1), slot(MU, 2)
    opened, frozen = {**base, CHARTER: 1}, {**base, CHARTER: 3}
    attested = {slot(REGISTRY, 4099): 4, slot(PROFILE, 3): 2}
    rows = [('deposit-rent', data('deposit', 0), base, {**base, slot(RESERVE, 0): 7}, 7, 7, one),
            ('deposit-sale', data('deposit', 1), {**base, slot(RESERVE, 1): 3},
             {**base, slot(RESERVE, 1): 7}, 7, 4, SENDER),
            ('deposit-kind', data('deposit', 2), base, None, None, 1, one),
            ('deposit-overflow', data('deposit', 0), {**base, slot(RESERVE, 0): 2**256 - 3}, None, None, 5, one),
            ('transfer-admit', data('transfer', 2, 4), base, {**base, mu1: 1, mu2: 9}, 1, 0, one),
            ('transfer-limit', data('transfer', 2, 5), base, None, None, 0, one),
            ('transfer-frozen', data('transfer', 2, 1), frozen, None, None, 0, one),
            ('transfer-open-all', data('transfer', 2, 5), opened, {**opened, mu1: 0, mu2: 10}, 1, 0, one),
            ('transfer-over-mass', data('transfer', 2, 6), opened, None, None, 0, one),
            ('transfer-self', data('transfer', 1, 3), base, None, 1, 0, one),
            ('transfer-second-wallet', data('transfer', 1, 4), base, {**base, mu1: 9, mu2: 1}, 1, 0, three),
            ('transfer-no-identity', data('transfer', 2, 1), base, None, None, 0, SENDER),
            ('transfer-value', data('transfer', 2, 1), base, None, None, 1, one),
            ('transfer-short', data('transfer', 2, 1)[:-2], base, None, None, 0, one),
            ('attest-issuer', data('attest', 4099, 3, 2), base, {**base, **attested}, 1, 0, one),
            ('attest-open', data('attest', 4099, 3, 2), opened, {**opened, **attested}, 1, 0, one),
            ('attest-non-issuer', data('attest', 4099, 3, 2), base, None, None, 0, two),
            ('attest-frozen', data('attest', 4099, 3, 2), frozen, None, None, 0, one),
            ('attest-no-identity', data('attest', 4099, 3, 2), base, None, None, 0, SENDER),
            ('attest-wallet', data('attest', 2**160, 3, 2), base, None, None, 0, one),
            ('attest-profile', data('attest', 4099, 3, 4), base, None, None, 0, one),
            ('attest-identity-wrap', data('attest', 4099, 2**256 - 1, 0), base, None, None, 0, one),
            ('mass-1', data('mass', 1), base, None, 5, 0, SENDER),
            ('mass-none', data('mass', 3), base, None, 0, 0, SENDER),
            ('supply', data('supply'), base, None, 10, 0, SENDER),
            ('charter', data('charter'), base, None, 2, 0, SENDER),
            ('reserve', data('reserve', 1), {**base, slot(RESERVE, 1): 9}, None, 9, 0, SENDER),
            ('reserve-kind', data('reserve', 2), base, None, None, 0, SENDER),
            ('self', data('selfConstituting'), base, None, 1, 0, SENDER),
            ('view-value', data('mass', 1), base, None, None, 1, SENDER),
            ('no-selector', 'aabbcc', base, None, None, 0, one),
            ('unknown', 'ffffffff', base, None, None, 0, one)]
    digits = interestc('verdicts', DEBREU, 'F').strip()
    for b in (1, 2, 3):
        vector = (b, b, b)
        code = int(digits[vector_index(vector, 3)])
        rows += [(f'cast-{b}', data('cast', *vector), base, None, code, 0, SENDER),
                 (f'amend-{b}', data('amend', *vector), base, {**base, CHARTER: code}, code, 0, SENDER),
                 (f'charter-after-amend-{b}', data('charter'), {**base, CHARTER: code}, None, code, 0, SENDER)]
    rows += [('amend-value', data('amend', 1, 1, 1), base, None, None, 1, SENDER),
             ('amend-ballot', data('amend', 0, 1, 1), base, None, None, 0, SENDER),
             ('amend-short', data('amend', 1, 1, 1)[:-64], base, None, None, 0, SENDER)]
    return table_cases('example-debreu', runtime, rows) + accrual_cases(runtime, base)


def accrual_cases(runtime, base):
    """B3 at the Debreu example (S = 10; waterfall open pp, restricted pr, frozen rr):
    distribute, withdraw, claimOf and the settle of both identities in transfer."""
    one, two = wallet(4096), wallet(4097)
    rent, sale = slot(RESERVE, 0), slot(RESERVE, 1)
    mu1, mu2 = slot(MU, 1), slot(MU, 2)
    num1, num2, cp1, cp2 = slot(NUM, 1), slot(NUM, 2), slot(CHECKPOINT, 1), slot(CHECKPOINT, 2)
    funded = {**base, rent: 30, sale: 8}
    accrued = {**base, INDEX: 3, num1: 4}
    rows = [('distribute-pass', data('distribute', 0), funded, {**funded, rent: 0, INDEX: 30}, 30, 0, SENDER),
            ('distribute-retain', data('distribute', 1), funded, None, 0, 0, SENDER),
            ('distribute-open', data('distribute', 1), {**funded, CHARTER: 1},
             {**funded, CHARTER: 1, sale: 0, INDEX: 8}, 8, 0, two),
            ('distribute-frozen', data('distribute', 0), {**funded, CHARTER: 3}, None, 0, 0, SENDER),
            ('distribute-again', data('distribute', 0), {**funded, INDEX: 5},
             {**funded, rent: 0, INDEX: 35}, 30, 0, SENDER),
            ('distribute-empty', data('distribute', 0), base, None, 0, 0, SENDER),
            ('distribute-kind', data('distribute', 2), funded, None, None, 0, SENDER),
            ('distribute-wrap', data('distribute', 0), {**funded, INDEX: 2**256 - 2}, None, None, 0, SENDER),
            ('distribute-value', data('distribute', 0), funded, None, None, 1, SENDER),
            ('claim-after-distribute', data('claimOf', 1), {**funded, rent: 0, INDEX: 30}, None, 150, 0, SENDER),
            ('claim-checkpoint', data('claimOf', 2), {**base, INDEX: 30, cp2: 10, num2: 7}, None, 107, 0, SENDER),
            ('claim-none', data('claimOf', 3), {**base, INDEX: 30}, None, 0, 0, SENDER),
            ('claim-wrap', data('claimOf', 1), {**base, INDEX: 2**255}, None, None, 0, SENDER),
            ('transfer-settles', data('transfer', 2, 4), accrued,
             {**accrued, mu1: 1, mu2: 9, num1: 19, cp1: 3, num2: 15, cp2: 3}, 1, 0, one),
            ('transfer-settle-wrap', data('transfer', 2, 4), {**base, INDEX: 2**255}, None, None, 0, one)]
    cases = table_cases('accrual-debreu', runtime, rows)
    for label, before, after, result, balance, sender in (
            ('floor', accrued, {**accrued, num1: 9, cp1: 3}, 1, 5, one),
            ('exact', {**base, INDEX: 4}, {**base, INDEX: 4, cp2: 4}, 2, 2, two),
            ('kept', {**base, num1: 9}, None, 0, 0, one),
            ('second-wallet', {**base, INDEX: 6}, {**base, INDEX: 6, cp2: 6}, 3, 3, wallet(4098)),
            ('unfunded', accrued, None, None, 0, one),
            ('no-identity', accrued, None, None, 5, SENDER),
            ('wrap', {**base, INDEX: 2**255}, None, None, 5, one)):
        cases += paid_case(f'accrual-debreu-withdraw-{label}', runtime, data('withdraw'), before,
                           before if after is None else after, result, balance=balance, sender=sender)
    return cases


def impossibility_example_cases():
    """examples/arrow-impossibility.lang: identity 1 (wallet 4096, 5 units) issues at every
    charter; identity 2 (wallet 4097) holds 0 units; no transfer, cast or amend entry."""
    runtime = program_code('example-impossibility', IMPOSSIBILITY)
    base = data_storage(IMPOSSIBILITY)
    one, two = wallet(4096), wallet(4097)
    rows = [('deposit', data('deposit', 0), base, {**base, slot(RESERVE, 0): 3}, 3, 3, one),
            ('attest-issuer', data('attest', 4099, 3, 1), base,
             {**base, slot(REGISTRY, 4099): 4, slot(PROFILE, 3): 1}, 1, 0, one),
            ('attest-non-issuer', data('attest', 4099, 3, 1), base, None, None, 0, two),
            ('transfer', data('transfer', 2, 1), base, None, None, 0, one),
            ('cast', data('cast', 1, 1, 1), base, None, None, 0, one),
            ('amend', data('amend', 1, 1, 1), base, None, None, 0, one),
            ('mass', data('mass', 1), base, None, 5, 0, SENDER),
            ('supply', data('supply'), base, None, 5, 0, SENDER),
            ('charter', data('charter'), base, None, 1, 0, SENDER),
            ('self', data('selfConstituting'), base, None, 0, 0, SENDER),
            ('distribute', data('distribute', 0), {**base, slot(RESERVE, 0): 3}, None, None, 0, one),
            ('claim', data('claimOf', 1), {**base, INDEX: 2}, None, 10, 0, SENDER)]
    cases = table_cases('example-impossibility', runtime, rows)
    cases += paid_case('example-impossibility-withdraw', runtime, data('withdraw'), base, base, 0,
                       balance=0, sender=one)
    accrued = {**base, INDEX: 2}
    return cases + paid_case('example-impossibility-withdraw-index', runtime, data('withdraw'), accrued,
                             {**accrued, slot(CHECKPOINT, 1): 2}, 2, balance=2, sender=one)


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    require(bytecode('keccak', '') ==
            'c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470', 'keccak256("")')
    require(bytecode('keccak', 'transfer(address,uint256)')[:8] == 'a9059cbb', 'transfer selector')
    require(INTERESTC.exists(), f'{INTERESTC} is missing: run make')
    cases = refusals()
    runtime = bytecode('runtime', 3, 'debreu', *CODES)
    deploy('debreu-deploy', bytecode('creation', 3, 'debreu', *CODES), runtime, {CHARTER: 1})
    cases += default_cases(runtime, 'debreu', 1)
    impossible = bytecode('runtime', 3, 'impossibility')
    deploy('impossibility-deploy', bytecode('creation', 3, 'impossibility'), impossible, {CHARTER: 1})
    cases += default_cases(impossible, 'impossibility', 0)
    expect('impossibility-cast', impossible, data('cast', 1, 1, 3), {}, {}, None)
    expect('cast-value', runtime, data('cast', 1, 1, 3), {}, {}, None, value=1)
    cases += 2
    for members, codes, vectors in (
            (1, (3, 2, 1), [(1,), (2,), (3,)]),
            (14, tuple(i % 3 + 1 for i in range(120)),
             [tuple((m * k) % 3 + 1 for m in range(14)) for k in range(5)] + [(1,) * 14, (2,) * 14])):
        code = bytecode('runtime', members, 'debreu', *codes)
        for k, ballots in enumerate(vectors):
            expect(f'cast-n{members}-{k}', code, data('cast', *ballots), {}, {},
                   verdict(members, codes, ballots))
            cases += 1
        decision = verdict(members, codes, vectors[0])
        expect(f'amend-n{members}', code, data('amend', *vectors[0]), {}, {CHARTER: decision}, decision)
        cases += 1
    cases += debreu_example_cases() + impossibility_example_cases()
    print(f'SETTLEMENT cases={cases} deploy=4 geth=expected OK (logs: {WORK})')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'SETTLEMENT FAIL: {error}', file=sys.stderr)
        sys.exit(1)
