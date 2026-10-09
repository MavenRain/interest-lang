#!/usr/bin/env python3
"""Run the bytecode of src/evm.c in geth evm against balances computed here.

Needs tcc, geth evm 1.14.12 and foundry cast. Mapping slots come from
`cast index` and selectors from `cast sig`, not from src/keccak.c. The gas
that each EVM call uses must not be more than its line in test/gas-baseline.txt;
`--write-gas` writes that file again."""
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
MU, REGISTRY, PROFILE, CHARTER, RESERVE, INDEX, CHECKPOINT, NUM, DUST, CARRIER = range(10)
INTERESTC = ROOT / 'build/interestc'
BASELINE = ROOT / 'test/gas-baseline.txt'
USED = {}
DEBREU = ROOT / 'examples/arrow-debreu.lang'
DEBREU_TOKEN = ROOT / 'examples/arrow-debreu-token.lang'
IMPOSSIBILITY = ROOT / 'examples/arrow-impossibility.lang'
TOKEN = '00' * 19 + 'b1'
SIGNATURES = {'balanceOf': 'balanceOf(address)'}
TRANSFER = 'Transfer(address,address,uint256)'
PAID = 'Paid(uint256,address,uint256,uint256)'
LOGS = '#### LOGS ####'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def tool(*args):
    argv = ['tcc', '-Isrc', 'src/evm.c', 'src/keccak.c', 'domain/entries.c', 'src/check.c', 'src/arena.c', 'src/diag.c', 'src/printer.c', '-run', 'test/evmtool.c', *map(str, args)]
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
def selector(name, words, text=None):
    signature = text or name + '(' + ','.join(['uint256'] * words) + ')'
    return checked(['cast', 'sig', signature]).strip()[2:]


def data(name, *values):
    return selector(name, len(values), SIGNATURES.get(name)) + ''.join(f'{value:064x}' for value in values)


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


@functools.cache
def topic0(signature=TRANSFER):
    return checked(['cast', 'keccak', signature]).strip()[2:]


def transfer_log(source, target, value, address=RECEIVER):
    """The Transfer(source, target, value) record of the account at ADDRESS."""
    return (address, (topic0(), f'{source:064x}', f'{target:064x}'), f'{value:064x}')


def paid_log(h, wallet, paid, moved, address=RECEIVER):
    """The Paid(h, wallet, paid, moved) record of withdraw (L6) at ADDRESS: topic1 is the identity,
    topic2 the calling wallet, the data the paid wei and the dust that moved."""
    return (address, (topic0(PAID), f'{h:064x}', f'{wallet:064x}'), f'{paid:064x}{moved:064x}')


def log_records(text):
    """The records of the LOGS block that `evm --debug` writes to stderr, in order:
    (address, topics, data) per LOG. A reverted call has the block and no record."""
    lines = text.splitlines()
    require(lines.count(LOGS) <= 1, f'evm --debug: {lines.count(LOGS)} LOGS blocks')
    tail = lines[lines.index(LOGS) + 1:] if LOGS in lines else []
    return [log_record(block.splitlines()) for block in '\n'.join(tail).split('\n\n') if block.strip()]


def log_record(lines):
    """`LOGn: <address> bn=0 txi=0`, n topic lines `%08d  <word>`, then the data as a hexdump."""
    head = lines[0].split()
    require(len(head) == 4 and head[0][:3] == 'LOG' and head[0][-1] == ':' and head[2:] == ['bn=0', 'txi=0'],
            f'LOG head {lines[0]!r}')
    count = int(head[0][3:-1])
    topics = [line.split() for line in lines[1:1 + count]]
    require(len(topics) == count and all(len(t) == 2 and len(t[1]) == 64 for t in topics),
            f'LOG topics {lines!r}')
    data = ''.join(line.split('|')[0][8:].replace(' ', '') for line in lines[1 + count:])
    return (head[1].lower().removeprefix('0x'), tuple(t[1] for t in topics), data)


def hexed(store):
    return {f'0x{k:064x}': f'0x{v:064x}' for k, v in store.items() if v}


def run(name, code, calldata, *, before=None, value=0, create=False, sender=SENDER, balance=0, token=None):
    """TOKEN: None, or the (address, code, storage) of one more account, the carrier (O5b).
    The status comes from the depth-1 rows only: a reverted inner call writes a depth-2 error row."""
    carrier = {} if token is None else {token[0]: dict(balance='0x0', code='0x' + token[1], storage=hexed(token[2]))}
    state = dict(GENESIS, alloc={
        sender: dict(balance=hex(10**24)),
        RECEIVER: dict(balance=hex(balance), storage=hexed(before or {})), **carrier})
    genesis = WORK / (name + '-prestate.json')
    genesis.write_text(json.dumps(state))
    argv = ['evm', '--verbosity', '0', 'run', '--prestate', str(genesis), '--gas', str(GAS),
            '--sender', '0x' + sender, '--receiver', '0x' + RECEIVER, '--code', code,
            '--input', calldata, '--value', str(value), '--json', '--debug', '--dump']
    result = subprocess.run(argv + (['--create'] if create else []), text=True, capture_output=True,
                            timeout=60)
    require(result.returncode == 0, f'{name}: evm exit {result.returncode}: {result.stderr[-400:]}')
    text = result.stdout
    (WORK / (name + '.out')).write_text(text)
    (WORK / (name + '.log')).write_text(result.stderr)
    records = objects(text)
    require(len(records) >= 2 and 'accounts' in records[-1], f'{name}: missing state dump')
    errors = [row['error'] for row in records if row.get('error')]
    require(all(error == 'execution reverted' for error in errors), f'{name}: EVM fault {errors}')
    require(name not in USED, f'{name}: two EVM calls with this name')
    USED[name] = int(records[-2]['gasUsed'], 16)
    stores = {key.lower().removeprefix('0x'): words(account.get('storage', {}))
              for key, account in records[-1]['accounts'].items()}
    outer = [row for row in records if row.get('error') and row.get('depth', 1) == 1]
    return dict(status='revert' if outer else 'success',
                output=records[-2]['output'].lower().removeprefix('0x'),
                logs=log_records(result.stderr),
                storage=stores.get(RECEIVER, {}),
                token={} if token is None else stores.get(token[0], {}),
                created={key: value for key, value in stores.items()
                         if value and key not in (RECEIVER, *(token or ())[:1])},
                balances={key.lower().removeprefix('0x'): int(str(account.get('balance', '0')), 0)
                          for key, account in records[-1]['accounts'].items()})


def expect(name, code, calldata, before, after, result, *, value=0, sender=SENDER, logs=()):
    actual = run(name, code, calldata, before=before, value=value, sender=sender)
    wanted = dict(status='revert' if result is None else 'success',
                  output='' if result is None else f'{result:064x}',
                  storage={k: v for k, v in after.items() if v},
                  logs=list(logs))
    got = {key: actual[key] for key in wanted}
    require(got == wanted, f'{name}: EVM {got} != {wanted}')


def paid_case(name, code, calldata, before, after, result, *, balance, sender, logs=()):
    """withdraw with RECEIVER funded by BALANCE wei: on success the sender gains RESULT wei,
    RECEIVER keeps BALANCE - RESULT and the call logs LOGS."""
    actual = run(name, code, calldata, before=before, sender=sender, balance=balance)
    paid = 0 if result is None else result
    wanted = dict(status='revert' if result is None else 'success',
                  output='' if result is None else f'{result:064x}',
                  storage={k: v for k, v in after.items() if v},
                  receiver=balance - paid, sender=10**24 + paid, logs=list(logs))
    got = dict(status=actual['status'], output=actual['output'], storage=actual['storage'],
               receiver=actual['balances'].get(RECEIVER, 0), sender=actual['balances'].get(sender, 0), logs=actual['logs'])
    require(got == wanted, f'{name}: EVM {got} != {wanted}')
    return 1


def tallies(members, k):
    """The tallies (ballots of 1, .., ballots of k - 1) of MEMBERS ballots in the order of
    lang_tally_next: part 0 outer, the ballots of k the remainder."""
    if k == 1:
        return [()]
    return [(first, *rest) for first in range(members + 1) for rest in tallies(members - first, k - 1)]


@functools.cache
def tally_order(members, k):
    return {tally: i for i, tally in enumerate(tallies(members, k))}


def verdict(members, codes, ballots, k=3):
    return codes[tally_order(members, k)[tuple(ballots.count(b) for b in range(1, k))]]


def l1_case(k, members):
    """An L1 contract: code i % k + 1 for tally i; five mixed ballot vectors, all 1, all k."""
    codes = tuple(i % k + 1 for i in range(len(tallies(members, k))))
    vectors = [tuple((m * j) % k + 1 for m in range(members)) for j in range(1, 6)]
    return k, members, codes, vectors + [(1,) * members, (k,) * members]


def push(word):
    """asm_push_word: the shortest PUSH of WORD, PUSH0 for zero."""
    size = (word.bit_length() + 7) // 8
    return f'{0x5f + size:02x}' + (f'{word:0{2 * size}x}' if size else '')


def amend_pins():
    """The body of lang_entry_amend (`evmtool amend`, Alternative A): at k = 3 n = 14 the
    120 codes fit one word (code i at bit 2i) and amend returns that word; at n = 15 the
    136 codes do not fit and amend reverts with no output."""
    fits = l1_case(3, 14)[2]
    word = sum(code << (2 * i) for i, code in enumerate(fits))
    require(bytecode('amend', 14, 'debreu', *fits) == push(word) + '5f5260205ff3',
            'amend body n14: not the packed word and its return')
    require(bytecode('amend', 15, 'debreu', *l1_case(3, 15)[2]) == '5f5ffd',
            'amend body n15: not the revert')
    return 2


def deploy(name, creation, runtime, storage, logs=(), *, suffix='', token=None):
    """LOGS: the (identity, units) of each genesis Transfer record, in genesis order. SUFFIX: the
    constructor word after the init code (token mode); TOKEN: the carrier account of run."""
    made = run(name, creation + suffix, '', create=True, token=token)
    require(made['status'] == 'success' and made['output'] == runtime,
            f'{name}: creation did not return the runtime')
    written = list(made['created'].values())
    require(made['storage'] == {} and written == [storage],
            f'{name}: genesis storage {written} != [{storage}]')
    wanted = [transfer_log(0, h, units, address) for address in made['created'] for h, units in logs]
    require(made['logs'] == wanted, f'{name}: genesis logs {made["logs"]} != {wanted}')
    paid = run(name + '-value', creation + suffix, '', value=1, create=True, token=token)
    require(paid['status'] == 'revert' and paid['output'] == '' and paid['logs'] == [], f'{name}: creation took a value')


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


def data_rows(program, binary=None):
    """The start and the genesis rows w:h:p:u of the `interestc data PROGRAM` lines."""
    lines = dict((line + ' ').split(' ', 1) for line in interestc('data', program, binary=binary).splitlines())
    return int(lines['start']), [tuple(map(int, row.split(':'))) for row in lines['genesis'].split()]


def data_storage(program, binary=None):
    """genesis_storage of the `interestc data PROGRAM` lines."""
    return genesis_storage(*data_rows(program, binary))


def data_logs(program, binary=None):
    """The (identity, units) of each genesis Transfer record of PROGRAM: one record for each identity
    with units, in the order of its first row (lang_domain_genesis)."""
    rows = data_rows(program, binary)[1]
    sums = [(h, sum(u for _, k, _, u in rows if k == h)) for h in dict.fromkeys(h for _, h, _, _ in rows)]
    return [(h, u) for h, u in sums if u]


def program_code(name, program, logs=None, *, suffix='', token=None):
    """Deploys the creation code of `interestc build PROGRAM` against its data; -> the runtime.
    LOGS: the genesis records (None: data_logs of PROGRAM). SUFFIX (token mode): the constructor
    word, which the genesis writes to CARRIER; TOKEN: the carrier account of run."""
    def part(label, *flags):
        out = WORK / f'{name}-{label}.hex'
        interestc('build', program, *flags, '-o', out)
        return out.read_text().strip()

    creation, runtime = part('creation'), part('runtime', '--runtime')
    storage = data_storage(program) | ({CARRIER: int(suffix, 16)} if suffix else {})
    deploy(f'{name}-deploy', creation, runtime, storage, data_logs(program) if logs is None else logs,
           suffix=suffix, token=token)
    return runtime


def refusals():
    rows = [(('runtime', 0, 'debreu', 3), 'EVM_LIMIT'),
            (('runtime', 64, 'debreu', *([1] * len(tallies(64, 3)))), 'EVM_LIMIT'),
            (('-k', 4, 'runtime', 16, 'debreu', *([1] * len(tallies(16, 4)))), 'EVM_LIMIT'),
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
    """Each row: label, calldata, before, after (None: before), result (None: revert), value, sender, and
    optionally the LOG records of the call (default: none)."""
    for label, calldata, before, after, result, value, sender, *logs in rows:
        expect(f'{prefix}-{label}', runtime, calldata, before, before if after is None else after,
               result, value=value, sender=sender, logs=logs[0] if logs else ())
    return len(rows)


def default_cases(runtime, regime, constituting):
    """No program data: charter 1, deny everywhere, no issuers, S = 0."""
    me = int(SENDER, 16)
    before = {CHARTER: 1, slot(REGISTRY, me): 1, slot(MU, 0): 5}
    rows = [('deposit', data('deposit', 1), before, {**before, slot(RESERVE, 1): 2}, 2, 2, SENDER),
            ('deposit-kind', data('deposit', 2), before, None, None, 1, SENDER),
            ('transfer', data('transfer', 1, 1), before, None, None, 0, SENDER),
            ('attest', data('attest', 4099, 3, 1), before, None, None, 0, SENDER),
            ('recover', data('recover', 0, 1, 1), before, None, None, 0, SENDER),
            ('supply', data('supply'), before, None, 0, 0, SENDER),
            ('charter', data('charter'), before, None, 1, 0, SENDER),
            ('self', data('selfConstituting'), before, None, constituting, 0, SENDER),
            ('distribute', data('distribute', 0), {**before, slot(RESERVE, 0): 4}, None, None, 0, SENDER),
            ('withdraw', data('withdraw'), before, None, None, 0, SENDER),
            ('claim', data('claimOf', 0), {**before, INDEX: 3}, None, 15, 0, SENDER),
            ('balance', data('balanceOf', 0), before, None, 5, 0, SENDER),
            ('total-supply', data('totalSupply'), before, None, 0, 0, SENDER),
            ('no-selector', 'aabbcc', before, None, None, 0, SENDER),
            ('unknown', 'ffffffff', before, None, None, 0, SENDER)]
    return table_cases(f'default-{regime}', runtime, rows)


def vector_index(vector, k):
    return sum((b - 1) * k ** (len(vector) - 1 - i) for i, b in enumerate(vector))


def debreu_example_cases():
    """B2 entries at the genesis of examples/arrow-debreu.lang: start restricted (upTo 4
    everywhere), open any, frozen deny; identity 1 (wallet 4096, 5 units, profile 0) is
    the issuer at open and restricted; identity 2 (wallets 4097, 4098, 5 units, profile 3)."""
    runtime = program_code('example-debreu', DEBREU, [(1, 5), (2, 5)])
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
            ('transfer-admit', data('transfer', 2, 4), base, {**base, mu1: 1, mu2: 9}, 1, 0, one, [transfer_log(1, 2, 4)]),
            ('transfer-limit', data('transfer', 2, 5), base, None, None, 0, one),
            ('transfer-frozen', data('transfer', 2, 1), frozen, None, None, 0, one),
            ('transfer-open-all', data('transfer', 2, 5), opened, {**opened, mu1: 0, mu2: 10}, 1, 0, one, [transfer_log(1, 2, 5)]),
            ('transfer-over-mass', data('transfer', 2, 6), opened, None, None, 0, one),
            ('transfer-self', data('transfer', 1, 3), base, None, 1, 0, one, [transfer_log(1, 1, 3)]),
            ('transfer-second-wallet', data('transfer', 1, 4), base, {**base, mu1: 9, mu2: 1}, 1, 0, three, [transfer_log(2, 1, 4)]),
            ('transfer-no-identity', data('transfer', 2, 1), base, None, None, 0, SENDER),
            ('transfer-to-zero', data('transfer', 0, 1), base, None, None, 0, one),
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
            ('attest-identity-zero', data('attest', 4099, 0, 2), base, None, None, 0, one),
            ('recover-issuer', data('recover', 2, 3, 2), base, {**base, mu2: 3, slot(MU, 3): 2}, 1, 0, one, [transfer_log(2, 3, 2)]),
            ('recover-open', data('recover', 2, 1, 5), opened, {**opened, mu1: 10, mu2: 0}, 1, 0, one, [transfer_log(2, 1, 5)]),
            ('recover-r-free', data('recover', 1, 2, 5), base, {**base, mu1: 0, mu2: 10}, 1, 0, one, [transfer_log(1, 2, 5)]),
            ('recover-self', data('recover', 2, 2, 3), base, None, 1, 0, one, [transfer_log(2, 2, 3)]),
            ('recover-over-mass', data('recover', 2, 3, 6), base, None, None, 0, one),
            ('recover-non-issuer', data('recover', 1, 2, 1), base, None, None, 0, two),
            ('recover-frozen', data('recover', 2, 3, 1), frozen, None, None, 0, one),
            ('recover-no-identity', data('recover', 2, 3, 1), base, None, None, 0, SENDER),
            ('recover-identity-wrap', data('recover', 2, 2**256 - 1, 1), base, None, None, 0, one),
            ('recover-from-wrap', data('recover', 2**256 - 1, 3, 0), base, None, None, 0, one),
            ('recover-to-zero', data('recover', 2, 0, 1), base, None, None, 0, one),
            ('recover-from-zero', data('recover', 0, 3, 0), base, None, None, 0, one),
            ('recover-value', data('recover', 2, 3, 1), base, None, None, 1, one),
            ('recover-short', data('recover', 2, 3, 1)[:-2], base, None, None, 0, one),
            ('mass-1', data('mass', 1), base, None, 5, 0, SENDER),
            ('mass-none', data('mass', 3), base, None, 0, 0, SENDER),
            ('supply', data('supply'), base, None, 10, 0, SENDER),
            ('charter', data('charter'), base, None, 2, 0, SENDER),
            ('reserve', data('reserve', 1), {**base, slot(RESERVE, 1): 9}, None, 9, 0, SENDER),
            ('reserve-kind', data('reserve', 2), base, None, None, 0, SENDER),
            ('self', data('selfConstituting'), base, None, 1, 0, SENDER),
            ('view-value', data('mass', 1), base, None, None, 1, SENDER),
            ('balance-1', data('balanceOf', 1), base, None, 5, 0, SENDER),
            ('balance-none', data('balanceOf', 3), base, None, 0, 0, SENDER),
            ('balance-wide', data('balanceOf', 2**200), base, None, 0, 0, SENDER),
            ('total-supply', data('totalSupply'), base, None, 10, 0, SENDER),
            ('balance-value', data('balanceOf', 1), base, None, None, 1, SENDER),
            ('transfer-zero', data('transfer', 2, 0), base, None, 1, 0, one, [transfer_log(1, 2, 0)]),
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
             {**accrued, mu1: 1, mu2: 9, num1: 19, cp1: 3, num2: 15, cp2: 3}, 1, 0, one, [transfer_log(1, 2, 4)]),
            ('transfer-settle-wrap', data('transfer', 2, 4), {**base, INDEX: 2**255}, None, None, 0, one)]
    cases = table_cases('accrual-debreu', runtime, rows)
    for label, before, after, result, balance, sender, logs in (
            ('floor', accrued, {**base, INDEX: 3, cp1: 3, DUST: 9}, 1, 5, one, [paid_log(1, 4096, 1, 9)]),
            ('exact', {**base, INDEX: 4}, {**base, INDEX: 4, cp2: 4}, 2, 2, two, [paid_log(2, 4097, 2, 0)]),
            ('kept', {**base, num1: 9}, None, 0, 0, one, [paid_log(1, 4096, 0, 0)]),
            ('second-wallet', {**base, INDEX: 6}, {**base, INDEX: 6, cp2: 6}, 3, 3, wallet(4098),
             [paid_log(2, 4098, 3, 0)]),
            ('unfunded', accrued, None, None, 0, one, []),
            ('no-identity', accrued, None, None, 5, SENDER, []),
            ('wrap', {**base, INDEX: 2**255}, None, None, 5, one, []),
            ('recycle', {**accrued, DUST: 5}, {**base, INDEX: 3, cp1: 3, rent: 1, DUST: 4}, 1, 5, one,
             [paid_log(1, 4096, 1, 9)]),
            ('recycle-wrap', {**accrued, DUST: 9, rent: 2**256 - 1}, None, None, 5, one, []),
            ('kept-dust', {**base, num1: 9, DUST: 5}, None, 0, 0, one, [paid_log(1, 4096, 0, 0)])):
        cases += paid_case(f'accrual-debreu-withdraw-{label}', runtime, data('withdraw'), before,
                           before if after is None else after, result, balance=balance, sender=sender,
                           logs=logs)
    return cases


def impossibility_example_cases():
    """examples/arrow-impossibility.lang: identity 1 (wallet 4096, 5 units) issues at every
    charter; identity 2 (wallet 4097) holds 0 units; no transfer, recover, cast or amend
    entry."""
    runtime = program_code('example-impossibility', IMPOSSIBILITY, [(1, 5)])
    base = data_storage(IMPOSSIBILITY)
    one, two = wallet(4096), wallet(4097)
    rows = [('deposit', data('deposit', 0), base, {**base, slot(RESERVE, 0): 3}, 3, 3, one),
            ('attest-issuer', data('attest', 4099, 3, 1), base,
             {**base, slot(REGISTRY, 4099): 4, slot(PROFILE, 3): 1}, 1, 0, one),
            ('attest-non-issuer', data('attest', 4099, 3, 1), base, None, None, 0, two),
            ('transfer', data('transfer', 2, 1), base, None, None, 0, one),
            ('recover', data('recover', 1, 2, 1), base, None, None, 0, one),
            ('cast', data('cast', 1, 1, 1), base, None, None, 0, one),
            ('amend', data('amend', 1, 1, 1), base, None, None, 0, one),
            ('mass', data('mass', 1), base, None, 5, 0, SENDER),
            ('supply', data('supply'), base, None, 5, 0, SENDER),
            ('charter', data('charter'), base, None, 1, 0, SENDER),
            ('self', data('selfConstituting'), base, None, 0, 0, SENDER),
            ('distribute', data('distribute', 0), {**base, slot(RESERVE, 0): 3}, None, None, 0, one),
            ('claim', data('claimOf', 1), {**base, INDEX: 2}, None, 10, 0, SENDER),
            ('balance-1', data('balanceOf', 1), base, None, 5, 0, SENDER),
            ('total-supply', data('totalSupply'), base, None, 5, 0, SENDER),
            ('total-supply-value', data('totalSupply'), base, None, None, 1, SENDER)]
    cases = table_cases('example-impossibility', runtime, rows)
    cases += paid_case('example-impossibility-withdraw', runtime, data('withdraw'), base, base, 0,
                       balance=0, sender=one, logs=[paid_log(1, 4096, 0, 0)])
    accrued = {**base, INDEX: 2}
    return cases + paid_case('example-impossibility-withdraw-index', runtime, data('withdraw'), accrued,
                             {**accrued, slot(CHECKPOINT, 1): 2}, 2, balance=2, sender=one,
                             logs=[paid_log(1, 4096, 2, 0)])


@functools.cache
def token_code(variant):
    return bytecode('token', variant)


@functools.cache
def allowance(owner, spender):
    """The slot of allowance[OWNER][SPENDER] of the stub token (`evmtool token`): base slot 1."""
    return int(checked(['cast', 'index', 'address', f'0x{spender:040x}', f'0x{slot(1, owner):064x}']).strip(), 16)


def token_case(name, runtime, calldata, before, after, result, *, variant, held, moved=None, value=0,
               sender=SENDER, logs=()):
    """expect with the stub token `evmtool token VARIANT` at TOKEN: HELD is the token storage
    before the call, MOVED the token storage after it (None: HELD)."""
    actual = run(name, runtime, calldata, before=before, value=value, sender=sender,
                 token=(TOKEN, token_code(variant), held))
    wanted = dict(status='revert' if result is None else 'success',
                  output='' if result is None else f'{result:064x}',
                  storage={k: v for k, v in after.items() if v}, logs=list(logs),
                  token={k: v for k, v in (held if moved is None else moved).items() if v})
    got = {key: actual[key] for key in wanted}
    require(got == wanted, f'{name}: EVM {got} != {wanted}')
    return 1


def token_cases():
    """O5b carrier: examples/arrow-debreu-token.lang (the Debreu example in token mode, S = 10)
    with the stub token at TOKEN. deposit pulls a by transferFrom and the strict balance delta
    (MY CALLs 147, 148); withdraw pays by transfer after the stores, with no call at paid 0 (149);
    a direct transfer does not change the reserves (152). -> (cases, deploys)."""
    holder = (TOKEN, token_code('standard'), {})
    word = f'{int(TOKEN, 16):064x}'
    runtime = program_code('example-debreu-token', DEBREU_TOKEN, [(1, 5), (2, 5)], suffix=word, token=holder)
    creation = (WORK / 'example-debreu-token-creation.hex').read_text().strip()
    for label, suffix in (('missing', ''), ('no-code', f'{0xb2:064x}'),
                          ('high-bits', f'{2**160 + int(TOKEN, 16):064x}')):
        made = run(f'example-debreu-token-deploy-{label}', creation + suffix, '', create=True, token=holder)
        require(made['status'] == 'revert' and made['output'] == '' and made['logs'] == [] and made['created'] == {},
                f'example-debreu-token-deploy-{label}: the creation did not revert')
    base = data_storage(DEBREU_TOKEN) | {CARRIER: int(TOKEN, 16)}
    me, here, kept = int(SENDER, 16), int(RECEIVER, 16), int(TOKEN, 16)
    rent, sale, mu1, num1, cp1 = slot(RESERVE, 0), slot(RESERVE, 1), slot(MU, 1), slot(NUM, 1), slot(CHECKPOINT, 1)
    one = wallet(4096)
    held = {slot(0, me): 10, allowance(me, here): 7}
    moved = {slot(0, me): 6, slot(0, here): 4, allowance(me, here): 3}
    hook = {**held, slot(0, kept): 4, allowance(kept, here): 4}
    pull, funded = data('deposit', 0, 4), {**base, rent: 4}
    accrued = {**base, INDEX: 3, num1: 4}
    paid = {**base, INDEX: 3, cp1: 3, DUST: 9}
    stock, sent = {slot(0, here): 5}, {slot(0, here): 4, slot(0, 4096): 1}
    rows = [('deposit-token', pull, base, funded, 4, 'standard', held, moved, 0, SENDER, []),
            ('deposit-token-no-approve', pull, base, None, None, 'standard', {slot(0, me): 10}, None, 0, SENDER, []),
            ('deposit-token-value', pull, base, None, None, 'standard', held, None, 1, SENDER, []),
            ('deposit-token-fee', pull, base, None, None, 'fee', held, None, 0, SENDER, []),
            ('deposit-token-hook', pull, base, None, None, 'hook', hook, None, 0, SENDER, []),
            ('deposit-token-noreturn', pull, base, funded, 4, 'noreturn', held, moved, 0, SENDER, []),
            ('deposit-token-false', pull, base, None, None, 'false', held, None, 0, SENDER, []),
            ('deposit-token-revert', pull, base, None, None, 'revert', held, None, 0, SENDER, []),
            ('deposit-token-direct', pull, base, funded, 4, 'standard', {**held, slot(0, here): 50},
             {**moved, slot(0, here): 54}, 0, SENDER, []),
            ('withdraw-token', data('withdraw'), accrued, paid, 1, 'standard', stock, sent, 0, one,
             [paid_log(1, 4096, 1, 9)]),
            ('withdraw-token-revert', data('withdraw'), accrued, None, None, 'revert', stock, None, 0, one, []),
            ('withdraw-token-false', data('withdraw'), accrued, None, None, 'false', stock, None, 0, one, []),
            ('withdraw-token-noreturn', data('withdraw'), accrued, paid, 1, 'noreturn', stock, sent, 0, one,
             [paid_log(1, 4096, 1, 9)]),
            ('withdraw-token-zero', data('withdraw'), {**base, num1: 9}, None, 0, 'revert', stock, None, 0, one,
             [paid_log(1, 4096, 0, 0)]),
            ('withdraw-token-recycle', data('withdraw'), {**accrued, DUST: 5}, {**paid, rent: 1, DUST: 4}, 1,
             'standard', stock, sent, 0, one, [paid_log(1, 4096, 1, 9)]),
            ('withdraw-token-wrap', data('withdraw'), {**base, INDEX: 2**255}, None, None, 'standard', stock, None,
             0, one, []),
            ('distribute-token', data('distribute', 0), {**base, rent: 30, sale: 8},
             {**base, rent: 0, sale: 8, INDEX: 30}, 30, 'standard', stock, None, 0, SENDER, []),
            ('view-token-value', data('mass', 1), base, None, None, 'standard', stock, None, 1, SENDER, [])]
    cases = sum(token_case(f'example-debreu-token-{label}', runtime, calldata, before,
                           before if after is None else after, result, variant=variant, held=store,
                           moved=after_store, value=value, sender=sender, logs=logs)
                for label, calldata, before, after, result, variant, store, after_store, value, sender, logs in rows)
    require(mu1 in base, 'token example: identity 1 holds no units')
    return cases, 4


def gas_check(write):
    """The gas of each EVM call (USED, run order) against BASELINE lines `name gas`:
    the same names in the same order, and no call above its line."""
    if write:
        BASELINE.write_text(''.join(f'{name} {gas}\n' for name, gas in USED.items()))
    pins = [(name, int(gas)) for name, gas in (line.split(' ') for line in BASELINE.read_text().splitlines())]
    require([name for name, _ in pins] == list(USED),
            f'gas: the EVM calls are not the calls of {BASELINE.name}; write it with --write-gas')
    over = [f'{name} {USED[name]}>{gas}' for name, gas in pins if USED[name] > gas]
    require(not over, f'gas: {len(over)} calls above the baseline: {" ".join(over)}')
    for name, gas in pins:
        if USED[name] < gas:
            print(f'GAS note {name} {USED[name]} < {gas}')
    return f'GAS calls={len(USED)} total={sum(USED.values())} max={max(USED.values())} ceiling=test/gas-baseline.txt'


def main(write_gas):
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
    for k, members, codes, vectors in (
            (3, 1, (3, 2, 1), [(1,), (2,), (3,)]),
            (3, 14, tuple(i % 3 + 1 for i in range(120)),
             [tuple((m * j) % 3 + 1 for m in range(14)) for j in range(5)] + [(1,) * 14, (2,) * 14]),
            *(l1_case(k, members) for k, members in ((3, 15), (3, 63), (4, 7), (4, 15)))):
        flags = () if k == 3 else ('-k', k)
        name = f'n{members}' if k == 3 else f'k{k}-n{members}'
        code = bytecode(*flags, 'runtime', members, 'debreu', *codes)
        for j, ballots in enumerate(vectors):
            expect(f'cast-{name}-{j}', code, data('cast', *ballots), {}, {},
                   verdict(members, codes, ballots, k))
            cases += 1
        decision = verdict(members, codes, vectors[0], k)
        expect(f'amend-{name}', code, data('amend', *vectors[0]), {}, {CHARTER: decision}, decision)
        cases += 1
    _, members, codes, _ = l1_case(3, 63)
    deploy('debreu-n63-deploy', bytecode('creation', members, 'debreu', *codes),
           bytecode('runtime', members, 'debreu', *codes), {CHARTER: 1})
    cases += amend_pins() + debreu_example_cases() + impossibility_example_cases()
    carried, deploys = token_cases()
    gas = gas_check(write_gas)
    print(f'SETTLEMENT cases={cases + carried} deploy={5 + deploys} geth=expected OK (logs: {WORK})')
    print(gas)


if __name__ == '__main__':
    try:
        require(sys.argv[1:] in ([], ['--write-gas']), 'usage: settlement.py [--write-gas]')
        main(sys.argv[1:] == ['--write-gas'])
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'SETTLEMENT FAIL: {error}', file=sys.stderr)
        sys.exit(1)
