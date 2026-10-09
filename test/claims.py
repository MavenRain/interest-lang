#!/usr/bin/env python3
"""A reference model of the interest operations (SPEC sections 5 and 7) against geth.

The model keeps the state of the compiled contract (mu, registry, profiles, the active
charter, the reserves, the accrual index, the checkpoints, the numerators and the wei
balance) and gives each entry its SPEC meaning. The program tables are written here from
the example sources, and a cross-check compares them with `interestc data`. Each
operation sequence runs in geth `evm run`, one call per step on the post-state of the
step before, and each step must equal the model. After each step the SPEC M2 laws hold
on the geth state: conservation, sum to inflow, and the charter changes only at amend.
Then law vectors (transfer then distribute equals distribute on the image, amend moves
neither mu nor the treasury, recover moves mu but no claim and not the treasury,
impossibility reverts leave the state unchanged, R
rejections revert) and the contract half of the refusal list (no mint, burn, balanceOf,
allowance, charter-write or issuer-write selector; withdraw keeps num mod S). Uses the
harness of settlement.py."""
import dataclasses
import itertools
import random
import re
import subprocess
import sys

import settlement as st
from settlement import (CHARTER, CHECKPOINT, INDEX, MU, NUM, PROFILE, REGISTRY, RESERVE,
                        SENDER, data, require, run, slot, vector_index, wallet)

WORD = 2**256
BASE = 10**24
WORDS = dict(deposit=1, distribute=1, withdraw=0, transfer=2, attest=3, recover=3, cast=3, amend=3,
             mass=1, supply=0, claimOf=1, charter=0, reserve=1, selfConstituting=0)
VIEWS = ('mass', 'supply', 'claimOf', 'charter', 'reserve', 'selfConstituting')
IDENTITIES = range(5)
# Signatures that the contract must not have (SPEC section 2 refusals, I2c MY CALL 8).
FORBIDDEN = ('mint(address,uint256)', 'mint(uint256,uint256)', 'mint(uint256)', 'burn(uint256)',
             'burn(address,uint256)', 'balanceOf(address)', 'balanceOf(uint256)',
             'allowance(address,address)', 'allowance(uint256,uint256)', 'approve(address,uint256)',
             'transferFrom(address,address,uint256)', 'setCharter(uint256)',
             'setIssuer(uint256,uint256)', 'addIssuer(uint256)')


@dataclasses.dataclass(frozen=True)
class Program:
    """The tables of one program: genesis rows (wallet, identity, profile, units), R codes
    per charter and profile pair (a = any, uN = up to N, d = deny), W gates per charter and
    kind (p = pass, r = retain), issuer pairs (charter, identity), the entry list."""
    name: str
    path: object
    start: int
    genesis: tuple
    restrict: tuple
    waterfall: tuple
    issuers: tuple
    entries: tuple
    constituting: int
    verdicts: str = ''

    @property
    def supply(self):
        return sum(row[3] for row in self.genesis)

    def data_text(self):
        """The lines of `interestc data` that these tables give."""
        return '\n'.join([
            f'start {self.start}', f'charters {len(self.restrict)}',
            'genesis ' + ' '.join(':'.join(map(str, row)) for row in self.genesis),
            'restrict ' + ' '.join(','.join(row) for row in self.restrict),
            'waterfall ' + ' '.join(self.waterfall),
            'issuers ' + ' '.join(f'{c}:{h}' for c, h in self.issuers)]) + '\n'


# examples/arrow-debreu.lang: restrict open any, restricted upTo 4, frozen deny.
DEBREU = Program(
    'debreu', st.DEBREU, 2, ((4096, 1, 0, 5), (4097, 2, 3, 3), (4098, 2, 3, 2)),
    (('a',) * 16, ('u4',) * 16, ('d',) * 16), ('pp', 'pr', 'rr'), ((1, 1), (2, 1)),
    ('deposit', 'distribute', 'withdraw', 'transfer', 'attest', 'recover', 'cast', 'amend', 'mass',
     'supply', 'claimOf', 'charter', 'reserve', 'selfConstituting'), 1)
# examples/arrow-impossibility.lang: no aggregation, so no distribute, transfer, cast, amend.
IMPOSSIBILITY = Program(
    'impossibility', st.IMPOSSIBILITY, 1, ((4096, 1, 0, 5), (4097, 2, 0, 0)),
    (('d',) * 16,) * 3, ('rr',) * 3, ((1, 1), (2, 1), (3, 1)),
    ('deposit', 'withdraw', 'attest', 'mass', 'supply', 'claimOf', 'charter', 'reserve',
     'selfConstituting'), 0)
# examples/erc721-dirac.lang: one unit (S = 1) at identity 1; restricted admits an accredited
# receiver only (the profile code is 2 juris + status, so the odd codes).
ERC721 = Program(
    'erc721', st.ROOT / 'examples/erc721-dirac.lang', 1, ((4096, 1, 0, 1), (4097, 2, 3, 0), (4098, 3, 0, 0)),
    (('a',) * 16, ('d', 'a') * 8, ('d',) * 16), ('pp', 'pp', 'rr'), ((1, 1), (2, 1)), DEBREU.entries, 1)


@dataclasses.dataclass(frozen=True)
class State:
    mu: dict
    registry: dict
    profile: dict
    charter: int
    reserve: tuple
    index: int
    checkpoint: dict
    num: dict
    balance: int


def genesis(program):
    mu, registry, profile = {}, {}, {}
    for w, h, p, u in program.genesis:
        registry[w] = h + 1
        mu[h] = mu.get(h, 0) + u
        profile[h] = p
    return State(mu, registry, profile, program.start, (0, 0), 0, {}, {}, 0)


def storage(s):
    """The storage words of state S (zero words left out)."""
    words = {CHARTER: s.charter, INDEX: s.index,
             **{slot(MU, h): u for h, u in s.mu.items()},
             **{slot(REGISTRY, w): v for w, v in s.registry.items()},
             **{slot(PROFILE, h): p for h, p in s.profile.items()},
             **{slot(RESERVE, k): r for k, r in enumerate(s.reserve)},
             **{slot(CHECKPOINT, h): c for h, c in s.checkpoint.items()},
             **{slot(NUM, h): n for h, n in s.num.items()}}
    return {key: value for key, value in words.items() if value}


def identity(s, sender):
    stored = s.registry.get(int(sender, 16), 0)
    return stored - 1 if stored else None


def claim(s, h):
    """claimOf(h) = num h + mu h * (INDEX - checkpoint h); None when a word wraps."""
    product = s.mu.get(h, 0) * (s.index - s.checkpoint.get(h, 0))
    total = s.num.get(h, 0) + product
    return total if product < WORD and total < WORD else None


def settle(s, h):
    c = claim(s, h)
    return None if c is None else dataclasses.replace(
        s, num={**s.num, h: c}, checkpoint={**s.checkpoint, h: s.index})


def put(row, kind, value):
    return tuple(value if k == kind else old for k, old in enumerate(row))


def admits(code, q):
    return {'a': True, 'd': False}.get(code, code.startswith('u') and q <= int(code[1:]))


def verdict(program, ballots):
    return int(program.verdicts[vector_index(ballots, 3)]) if all(1 <= b <= 3 for b in ballots) else None


def deposit(program, s, sender, value, kind):
    total = s.reserve[kind] + value if kind < 2 else WORD
    return None if total >= WORD else (
        dataclasses.replace(s, reserve=put(s.reserve, kind, total), balance=s.balance + value), total)


def distribute(program, s, sender, value, kind):
    if program.supply == 0 or kind > 1:
        return None
    d = s.reserve[kind] if program.waterfall[s.charter - 1][kind] == 'p' else 0
    return None if s.index + d >= WORD else (
        dataclasses.replace(s, reserve=put(s.reserve, kind, s.reserve[kind] - d), index=s.index + d), d)


def withdraw(program, s, sender, value):
    """Pays floor(num h / S) wei and keeps num h mod S (R2); the wei must be there."""
    h = identity(s, sender)
    settled = None if program.supply == 0 or h is None else settle(s, h)
    if settled is None:
        return None
    paid, kept = divmod(settled.num[h], program.supply)
    return None if paid > settled.balance else (
        dataclasses.replace(settled, num={**settled.num, h: kept}, balance=settled.balance - paid), paid)


def transfer(program, s, sender, value, to, q):
    """Settles both identities, then debit h q ; credit to q, iff R(h, to, q) and q <= mass h."""
    h = identity(s, sender)
    code = None if h is None else program.restrict[s.charter - 1][s.profile.get(h, 0) * 4 + s.profile.get(to, 0)]
    if h is None or to + 1 >= WORD or not admits(code, q) or q > s.mu.get(h, 0):
        return None
    return move(s, h, to, q)


def move(s, h, to, q):
    """Settles h, then to; then debit h q ; credit to q (the credit reads the debited mu)."""
    first = settle(s, h)
    second = None if first is None else settle(first, to)
    if second is None:
        return None
    debited = {**second.mu, h: second.mu.get(h, 0) - q}
    return dataclasses.replace(second, mu={**debited, to: debited.get(to, 0) + q}), 1


def issuer(program, s, sender):
    """The caller identity is an issuer of the active charter."""
    c = identity(s, sender)
    return c is not None and (s.charter, c) in program.issuers


def attest(program, s, sender, value, w, h, p):
    """A registry write by a trusted issuer of the active charter."""
    if not issuer(program, s, sender) or w >= 2**160 or p >= 4 or h + 1 >= WORD:
        return None
    return dataclasses.replace(s, registry={**s.registry, w: h + 1}, profile={**s.profile, h: p}), 1


def recover(program, s, sender, value, h, to, q):
    """ERC-1644 forced transfer by a trusted issuer of the active charter (O3): the move
    of transfer iff q <= mass h; R does not gate it."""
    if not issuer(program, s, sender) or h + 1 >= WORD or to + 1 >= WORD or q > s.mu.get(h, 0):
        return None
    return move(s, h, to, q)


def cast(program, s, sender, value, *ballots):
    v = verdict(program, ballots)
    return None if v is None else (s, v)


def amend(program, s, sender, value, *ballots):
    """Active charter := the verdict; mu and the treasury do not move."""
    v = verdict(program, ballots)
    return None if v is None else (dataclasses.replace(s, charter=v), v)


def view(name):
    def result(program, s, sender, value, *args):
        out = dict(mass=lambda h: s.mu.get(h, 0), supply=lambda: program.supply,
                   claimOf=lambda h: claim(s, h), charter=lambda: s.charter,
                   reserve=lambda kind: s.reserve[kind] if kind < 2 else None,
                   selfConstituting=lambda: program.constituting)[name](*args)
        return None if out is None else (s, out)
    return result


OPS = dict(deposit=deposit, distribute=distribute, withdraw=withdraw, transfer=transfer,
           attest=attest, recover=recover, cast=cast, amend=amend, **{name: view(name) for name in VIEWS})


def apply(program, s, call):
    """-> (state, result), or None when the call reverts (the state does not change)."""
    name, args, sender, value = call
    if name not in program.entries or (value and name != 'deposit'):
        return None
    return OPS[name](program, s, sender, value, *args)


def geth_step(label, runtime, before, balance, call):
    name, args, sender, value = call
    return run(label, runtime, data(name, *args), before=before, value=value, sender=sender,
               balance=balance)


def check_step(label, program, runtime, s, chain, call):
    """One call: the model from S, geth from CHAIN = (storage, balance) of the step before.
    -> (model state, geth chain) after the call."""
    model = apply(program, s, call)
    after, result = model if model else (s, None)
    actual = geth_step(label, runtime, *chain, call)
    wanted = dict(status='revert' if model is None else 'success',
                  output='' if model is None else f'{result:064x}',
                  storage=storage(after), receiver=after.balance,
                  sender=BASE + s.balance - after.balance)
    got = dict(status=actual['status'], output=actual['output'], storage=actual['storage'],
               receiver=actual['balances'].get(st.RECEIVER, 0),
               sender=actual['balances'].get(call[2], 0))
    require(got == wanted, f'{label} {call}: geth {got} != model {wanted}')
    return after, (actual['storage'], got['receiver'])


SENDERS = tuple(map(wallet, (4096, 4096, 4096, 4097, 4097, 4098, 4098, 4099, 4100))) + (SENDER,)
ARGS = dict(
    deposit=lambda r: (r.choice((0, 0, 1, 1, 2)),),
    distribute=lambda r: (r.choice((0, 1, 1, 2)),),
    withdraw=lambda r: (),
    transfer=lambda r: (r.randrange(0, 5), r.randrange(0, 7)),
    attest=lambda r: (r.randrange(4096, 4101), r.randrange(1, 5), r.randrange(0, 4)),
    cast=lambda r: tuple(r.randrange(1, 4) for _ in range(3)),
    amend=lambda r: tuple(r.randrange(1, 4) for _ in range(3)),
    mass=lambda r: (r.randrange(0, 5),),
    supply=lambda r: (),
    claimOf=lambda r: (r.choice((0, 1, 1, 2, 2, 3)),),
    charter=lambda r: (),
    reserve=lambda r: (r.choice((0, 1, 2)),),
    selfConstituting=lambda r: ())
CHOICES = ('deposit',) * 4 + ('distribute',) * 3 + ('withdraw',) * 4 + ('transfer',) * 3 + (
    'attest', 'cast', 'amend', 'claimOf', 'claimOf', *VIEWS)
# S = 1: a transfer of quantity 1 moves the whole unit.
DIRAC_ARGS = dict(ARGS, transfer=lambda r: (r.randrange(0, 5), r.choice((0, 1, 1, 1, 2))))

# The sequences must reach these successful calls (name + '+': a nonzero result).
COVER_DEBREU = {'deposit+', 'distribute+', 'distribute0', 'withdraw+', 'withdraw0', 'transfer+',
                'attest+', 'amend+', 'cast+', 'claimOf+'}
COVER_IMPOSSIBILITY = {'deposit+', 'withdraw0'}
COVER_ERC721 = {'deposit+', 'distribute+', 'withdraw+', 'transfer+', 'amend+', 'cast+', 'claimOf+'}


def random_call(rng, args):
    name = rng.choice(CHOICES)
    value = rng.randrange(0, 41) if name == 'deposit' else int(rng.randrange(20) == 0)
    return name, args[name](rng), rng.choice(SENDERS), value


def sequence(program, runtime, seed, length, args):
    """LENGTH random calls from the genesis of PROGRAM; -> the final model state and the
    successful calls seen (name + '+' for a nonzero result, name + '0' for zero)."""
    rng, s, seen = random.Random(seed), genesis(program), set()
    chain, deposits, paid = (storage(s), 0), 0, 0
    for number in range(length):
        label, call = f'{program.name}-seq{seed}-{number}', random_call(rng, args)
        model = apply(program, s, call)
        seen |= {call[0] + ('+' if model and model[1] else '0')} if model else set()
        deposits += call[3] if model and call[0] == 'deposit' else 0
        paid += model[1] if model and call[0] == 'withdraw' else 0
        charter = chain[0].get(CHARTER)
        s, chain = check_step(label, program, runtime, s, chain, call)
        require(laws_hold(program, *chain, deposits, paid),
                f'{label} {call}: conservation or sum to inflow fails on the geth state')
        require(chain[0].get(CHARTER) == charter or call[0] == 'amend', f'{label}: {call[0]} wrote the charter')
        require(program.supply != 1 or [v for v in s.mu.values() if v] == [1], f'{label}: the measure is not a Dirac measure')
    return s, seen


def geth_claims(words):
    """claimOf of each identity, from the storage words of geth."""
    index = words.get(INDEX, 0)
    return {h: words.get(slot(NUM, h), 0) + words.get(slot(MU, h), 0) * (index - words.get(slot(CHECKPOINT, h), 0))
            for h in IDENTITIES}


def laws_hold(program, words, balance, deposits, paid):
    """Conservation: the masses sum to S. Sum to inflow: the claims and S * paid sum to
    S * INDEX (each distribute adds S * d), the deposits are the reserves and INDEX, and
    the wei balance is the deposits less the paid wei."""
    supply, index = program.supply, words.get(INDEX, 0)
    mass = sum(words.get(slot(MU, h), 0) for h in IDENTITIES)
    reserves = words.get(slot(RESERVE, 0), 0) + words.get(slot(RESERVE, 1), 0)
    return (mass == supply and sum(geth_claims(words).values()) + supply * paid == supply * index
            and deposits == reserves + index and balance == deposits - paid)


def play(program, s, calls):
    """The model state after CALLS from S; each call must succeed."""
    for call in calls:
        out = apply(program, s, call)
        require(out is not None, f'{program.name}: the setup call {call} reverts in the model')
        s = out[0]
    return s


def at(s):
    """The geth chain (storage, balance) of model state S."""
    return storage(s), s.balance


def rich_debreu(program):
    """Charter open; claims accrued, 2 units moved, identity 2 paid, both reserves full."""
    one, two = wallet(4096), wallet(4097)
    return play(program, dataclasses.replace(genesis(program), charter=1), [
        ('deposit', (0,), SENDER, 30), ('distribute', (0,), SENDER, 0), ('transfer', (2, 2), one, 0),
        ('deposit', (1,), SENDER, 17), ('withdraw', (), two, 0), ('deposit', (0,), SENDER, 9)])


def transfer_then_distribute(program, runtime):
    """After [transfer, distribute] each claim is the claim before plus the image mass
    times d. From INDEX 0 the claims also equal those of distribute alone on a genesis
    that holds the image measure."""
    one, two = wallet(4096), wallet(4097)
    fresh = play(program, dataclasses.replace(genesis(program), charter=1), [('deposit', (0,), SENDER, 30)])
    count, share = 0, ('distribute', (0,), SENDER, 0)
    for name, s in (('fresh', fresh), ('rich', rich_debreu(program))):
        for number, call in enumerate((('transfer', (2, 2), one, 0), ('transfer', (1, 3), two, 0),
                                       ('transfer', (3, 1), one, 0))):
            label = f'law-image-{name}-{number}'
            moved, chain = check_step(label + '-transfer', program, runtime, s, at(s), call)
            shared, chain = check_step(label + '-distribute', program, runtime, moved, chain, share)
            d, claims = shared.index - moved.index, geth_claims(chain[0])
            require(d > 0 and all(claims[h] == claim(s, h) + moved.mu.get(h, 0) * d for h in IDENTITIES),
                    f'{label}: claims {claims} are not the claims before plus the image mass times {d}')
            image = dataclasses.replace(s, mu=moved.mu)
            alone = check_step(label + '-image', program, runtime, image, at(image), share)[1]
            require(s.index > 0 or geth_claims(alone[0]) == claims,
                    f'{label}: distribute on the image gives {geth_claims(alone[0])} != {claims}')
            count += 1
    return count


def amend_law(program, runtime):
    """At a rich state, every ballot vector: storage changes only at CHARTER, wei stays."""
    s, count = rich_debreu(program), 0
    rest = lambda words: {key: value for key, value in words.items() if key != CHARTER}
    for ballots in itertools.product((1, 2, 3), repeat=3):
        after, chain = check_step(f'law-amend-{"".join(map(str, ballots))}', program, runtime, s, at(s),
                                  ('amend', ballots, SENDER, 0))
        require(rest(chain[0]) == rest(storage(s)) and chain[1] == s.balance and after.charter == verdict(program, ballots),
                f'law-amend {ballots}: amend moved mu or the treasury')
        count += 1
    return count


def recover_law(program, runtime):
    """At a rich state, the issuer moves the whole mass of each identity to each identity:
    on geth the masses sum to S, mu moves by q, and every claim, both reserves, INDEX and
    the wei do not change. A recover by a non-issuer reverts and moves nothing."""
    s, count = rich_debreu(program), 0
    treasury = lambda words: (words.get(slot(RESERVE, 0), 0), words.get(slot(RESERVE, 1), 0), words.get(INDEX, 0))
    for h, to in itertools.product((1, 2, 3), repeat=2):
        q = s.mu.get(h, 0)
        words, balance = check_step(f'law-recover-{h}-{to}', program, runtime, s, at(s),
                                    ('recover', (h, to, q), wallet(4096), 0))[1]
        moved = {k: words.get(slot(MU, k), 0) - s.mu.get(k, 0) for k in IDENTITIES}
        wanted = {k: (q if k == to else 0) - (q if k == h else 0) for k in IDENTITIES}
        require(sum(words.get(slot(MU, k), 0) for k in IDENTITIES) == program.supply and moved == wanted
                and geth_claims(words) == geth_claims(storage(s)) and treasury(words) == treasury(storage(s))
                and balance == s.balance, f'law-recover {h} {to} {q}: recover moved a claim or the treasury')
        count += 1
    chain = check_step('law-recover-non-issuer', program, runtime, s, at(s), ('recover', (1, 2, 1), wallet(4097), 0))[1]
    require(chain == at(s), 'law-recover-non-issuer: the state moved')
    return count + 1


def impossibility_reverts(program, runtime):
    """At impossibility the aggregation has no inhabitant: cast, amend, distribute,
    transfer and recover revert with empty output, and the storage and the wei do not
    change."""
    one = wallet(4096)
    s = play(program, genesis(program), [('deposit', (0,), SENDER, 7), ('deposit', (1,), one, 3),
                                         ('attest', (4099, 3, 2), one, 0), ('withdraw', (), one, 0)])
    calls = (('cast', (1, 1, 1), one, 0), ('amend', (1, 1, 1), one, 0), ('amend', (3, 3, 3), SENDER, 0),
             ('distribute', (0,), one, 0), ('distribute', (1,), SENDER, 0), ('transfer', (2, 0), one, 0),
             ('transfer', (2, 1), one, 0), ('recover', (1, 2, 1), one, 0))
    for number, call in enumerate(calls):
        chain = check_step(f'law-impossible-{number}', program, runtime, s, at(s), call)[1]
        require(apply(program, s, call) is None and chain == at(s), f'law-impossible {call}: the state moved')
    return len(calls)


def r_rejections(program, runtime):
    """R rejections revert and leave the state unchanged: at genesis both identities hold
    5 units; open admits q <= 5 (mass), restricted q <= 4, frozen nothing (not even 0)."""
    limit, count = {1: 5, 2: 4, 3: -1}, 0
    for charter, (sender, to), q in itertools.product((1, 2, 3), ((wallet(4096), 2), (wallet(4097), 1)),
                                                      (0, 1, 4, 5, 6)):
        s, call = dataclasses.replace(genesis(program), charter=charter), ('transfer', (to, q), sender, 0)
        chain = check_step(f'law-r-{charter}-{to}-{q}', program, runtime, s, at(s), call)[1]
        admitted = q <= limit[charter]
        require((apply(program, s, call) is not None) == admitted and (admitted or chain == at(s)),
                f'law-r {charter} {call}: admitted {not admitted}')
        count += 1
    return count


def withdraw_keeps_remainder(program, runtime):
    """withdraw pays claim / S and keeps claim mod S as NUM, with CHECKPOINT := INDEX."""
    count, supply = 0, program.supply
    for n, (index, mark) in itertools.product((0, 1, 9, 10, 11, 29, 99), ((0, 0), (3, 1))):
        s = dataclasses.replace(genesis(program), num={1: n}, index=index, checkpoint={1: mark}, balance=100)
        words, balance = check_step(f'contract-withdraw-{n}-{index}', program, runtime, s, at(s),
                                    ('withdraw', (), wallet(4096), 0))[1]
        c = n + s.mu[1] * (index - mark)
        require(words.get(slot(NUM, 1), 0) == c % supply and balance == 100 - c // supply
                and words.get(slot(CHECKPOINT, 1), 0) == index, f'contract-withdraw {n} {index}: num mod S is not kept')
        count += 1
    return count


def selector_table(runtime):
    """The selectors of the dispatcher (src/evm.c dispatch: DUP1 PUSH4 sel EQ PUSH2 dest JUMPI)."""
    return {m.group(1) for m in re.finditer('8063([0-9a-f]{8})1461[0-9a-f]{4}57', runtime) if m.start() % 2 == 0}


def selector_checks(program, runtime, s):
    """The selector table is the entry list exactly; each forbidden call reverts and the
    state does not change."""
    table = selector_table(runtime)
    wanted = {st.selector(name, WORDS[name]) for name in program.entries}
    require(table == wanted, f'{program.name}: selector table {sorted(table)} != entries {sorted(wanted)}')
    for signature in FORBIDDEN:
        code = st.checked(['cast', 'sig', signature]).strip()[2:]
        actual = run(f'contract-{program.name}-{code}', runtime, code + f'{1:064x}' * 3, before=storage(s),
                     sender=wallet(4096), balance=s.balance)
        require(code not in table and actual['status'] == 'revert' and actual['output'] == ''
                and actual['storage'] == storage(s) and actual['balances'].get(st.RECEIVER, 0) == s.balance,
                f'{program.name}: {signature} is an entry or moved the state')
    return 1 + len(FORBIDDEN)


def dirac_vectors(program, runtime):
    """examples/erc721-dirac.lang (S = 1): after each step one identity holds the one unit, the
    restricted charter admits an accredited receiver only, and withdraw keeps no remainder."""
    one, two = wallet(4096), wallet(4097)
    calls = (('deposit', (0,), SENDER, 9, 9), ('distribute', (0,), SENDER, 0, 9), ('claimOf', (1,), SENDER, 0, 9),
             ('transfer', (2, 2), one, 0, None), ('cast', (2, 3, 2), SENDER, 0, 3), ('amend', (2, 2, 1), SENDER, 0, 2),
             ('transfer', (3, 1), one, 0, None), ('transfer', (2, 1), one, 0, 1), ('transfer', (1, 1), two, 0, None),
             ('deposit', (1,), SENDER, 4, 4), ('distribute', (1,), SENDER, 0, 4), ('claimOf', (2,), SENDER, 0, 4),
             ('withdraw', (), one, 0, 9), ('withdraw', (), two, 0, 4), ('amend', (1, 1, 3), SENDER, 0, 3),
             ('transfer', (1, 1), two, 0, None), ('deposit', (0,), SENDER, 5, 5), ('distribute', (0,), SENDER, 0, 0))
    s, chain = genesis(program), at(genesis(program))
    for number, (name, args, sender, value, want) in enumerate(calls):
        call = (name, args, sender, value)
        model = apply(program, s, call)
        require((model[1] if model else None) == want, f'dirac-{number} {call}: the model gives {model and model[1]}, not {want}')
        s, chain = check_step(f'dirac-{number}', program, runtime, s, chain, call)
        require([v for v in s.mu.values() if v] == [1], f'dirac-{number}: the measure is not a Dirac measure')
    require(not any(s.num.values()), 'dirac: a withdraw kept a remainder')
    return len(calls)


def identity_boundaries(program, runtime):
    """Only encodable identities can receive units; the largest valid one can withdraw."""
    one, recipient = wallet(4096), wallet(4100)
    calls = (('transfer', (WORD - 1, 0), one, 0, None),
             ('transfer', (WORD - 1, 1), one, 0, None),
             ('attest', (4100, WORD - 1, 0), one, 0, None),
             ('attest', (4100, WORD - 2, 0), one, 0, 1),
             ('transfer', (WORD - 2, 1), one, 0, 1),
             ('mass', (WORD - 2,), SENDER, 0, 1),
             ('deposit', (0,), SENDER, 9, 9),
             ('distribute', (0,), SENDER, 0, 9),
             ('claimOf', (WORD - 2,), SENDER, 0, 9),
             ('withdraw', (), recipient, 0, 9),
             ('withdraw', (), recipient, 0, 0))
    s, chain = genesis(program), at(genesis(program))
    for number, (name, args, sender, value, want) in enumerate(calls):
        call = (name, args, sender, value)
        model = apply(program, s, call)
        require((model[1] if model else None) == want, f'identity-{number}: unexpected model result')
        s, chain = check_step(f'identity-{number}', program, runtime, s, chain, call)
    require(sum(s.mu.values()) == program.supply and s.balance == 0 and claim(s, WORD - 2) == 0,
            'identity: the recipient did not retain the unit and withdraw its complete claim')
    return len(calls)


def load(program):
    text = st.interestc('data', program.path)
    require(text == program.data_text(),
            f'{program.name}: interestc data {text!r} != claims.py tables {program.data_text()!r}')
    runtime = st.program_code('claims-' + program.name, program.path)
    require(storage(genesis(program)) == st.data_storage(program.path),
            f'{program.name}: the model genesis is not the deployed genesis')
    verdicts = st.interestc('verdicts', program.path, 'F').strip() if 'cast' in program.entries else ''
    return dataclasses.replace(program, verdicts=verdicts), runtime


def main():
    st.WORK = st.ROOT / '.gatework/claims'
    st.WORK.mkdir(parents=True, exist_ok=True)
    require(st.INTERESTC.exists(), f'{st.INTERESTC} is missing: run make')
    runs = ((DEBREU, range(1, 11), 20, COVER_DEBREU, ARGS), (IMPOSSIBILITY, range(1, 5), 15, COVER_IMPOSSIBILITY, ARGS),
            (ERC721, range(1, 7), 40, COVER_ERC721, DIRAC_ARGS))
    sequences = steps = 0
    built = {}
    for table, seeds, length, cover, args in runs:
        program, runtime = load(table)
        built[table.name] = program, runtime
        seen = set()
        for seed in seeds:
            seen |= sequence(program, runtime, seed, length, args)[1]
            sequences, steps = sequences + 1, steps + length
        require(cover <= seen, f'{program.name}: the sequences miss {sorted(cover - seen)}')
    debreu, impossible, erc721 = built['debreu'], built['impossibility'], built['erc721']
    laws = (transfer_then_distribute(*debreu) + amend_law(*debreu) + recover_law(*debreu)
            + impossibility_reverts(*impossible)
            + r_rejections(*debreu) + dirac_vectors(*erc721))
    contract = (withdraw_keeps_remainder(*debreu) + selector_checks(*debreu, rich_debreu(debreu[0]))
                + selector_checks(*impossible, genesis(impossible[0])) + selector_checks(*erc721, genesis(erc721[0]))
                + identity_boundaries(*erc721))
    print(f'CLAIMS sequences={sequences} steps={steps} laws={laws} contract={contract} geth=model OK'
          f' (logs: {st.WORK})')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'CLAIMS FAIL: {error}', file=sys.stderr)
        sys.exit(1)
