#!/usr/bin/env python3
"""A reference model of the interest operations (SPEC sections 5 and 7) against geth.

The model keeps the state of the compiled contract (mu, registry, profiles, the active
charter, the reserves, the accrual index, the checkpoints, the numerators and the wei
balance) and gives each entry its SPEC meaning. The program tables are written here from
the example sources, and a cross-check compares them with `interestc data`. Each
operation sequence runs in geth `evm run`, one call per step on the post-state of the
step before, and each step must equal the model. After each step the SPEC M2 laws hold
on the geth state: conservation, sum to inflow, solvency, and the charter changes only at amend.
Then law vectors (transfer then distribute equals distribute on the image, amend moves
neither mu nor the treasury, recover moves mu but no claim and not the treasury,
impossibility reverts leave the state unchanged, R
rejections revert, the dust of two withdraws goes to the rent reserve) and the contract
half of the refusal list (no mint, burn, balanceOf, allowance, charter-write or
issuer-write selector; a withdraw that pays moves num mod S to the dust). One more run
uses the Debreu tables in token mode (O5b): the model keeps the carrier balance and the
allowance of each sender, and the solvency law holds in carrier units after each call.
A third run uses the Debreu tables with the write facade (O5c): the model keeps the allowance
of each (owner, spender) identity pair, and each sequence ends with allowance(o, p) for the
drawn pairs. Then the allowance laws on geth: approve changes only the allowance word, and
transferFrom by the spender is transfer by from, less q of the allowance, or a revert.
A fourth run draws vote and amend() with the moves (O2, MY CALL 190 (a)). After each call of
each run, WEIGHT on geth is the image of mu along the ballots. Then the vote laws on geth (MY
CALL 188): a vote moves only the ballot and WEIGHT, each move of q moves q of the tally, a moved
mass votes once, and the holder of a Dirac measure takes all the seats.
Uses the harness of settlement.py."""
import dataclasses
import functools
import itertools
import random
import re
import subprocess
import sys

import settlement as st
from settlement import (ALLOWANCE, BALLOT, CARRIER, CHARTER, CHECKPOINT, DUST, INDEX, MU, NUM, PROFILE, REGISTRY,
                        RESERVE, SENDER, WEIGHT, data, require, run, slot, vector_index, wallet)

WORD = 2**256
BASE = 10**24
WORDS = dict(deposit=1, distribute=1, withdraw=0, transfer=2, attest=3, recover=3, cast=3, amend=0, vote=1,
             mass=1, supply=0, claimOf=1, charter=0, reserve=1, selfConstituting=0, balanceOf=1, totalSupply=0)
FACADE = ('balanceOf', 'totalSupply')
VIEWS = ('mass', 'supply', 'claimOf', 'charter', 'reserve', 'selfConstituting') + FACADE
IDENTITIES = range(5)
HERE = int(st.RECEIVER, 16)
# The token run (MY CALL 159): the carrier balance and the allowance to the contract of each
# sender at genesis. 4099 approves 60 units only and SENDER approves none, so their deposits
# revert when the allowance is used up. Each balance is at least its allowance, and a deposit
# takes the same amount from both, so the allowance always runs out first.
FUNDS = ((4096, 10**6, 10**6), (4097, 10**6, 10**6), (4098, 10**6, 10**6), (4099, 10**6, 60),
         (4100, 10**6, 10**6), (int(SENDER, 16), 1000, 0))
# Signatures that the contract must not have (SPEC section 2 refusals, I2c MY CALL 8).
FORBIDDEN = ('mint(address,uint256)', 'mint(uint256,uint256)', 'mint(uint256)', 'burn(uint256)',
             'burn(address,uint256)', 'balanceOf(uint256)',
             'allowance(address,address)', 'allowance(uint256,uint256)', 'approve(address,uint256)',
             'transferFrom(address,address,uint256)', 'setCharter(uint256)',
             'setIssuer(uint256,uint256)', 'addIssuer(uint256)')
# Signatures that the contract must have in each regime (the ERC-20 read facade, O5a).
REQUIRED = ('balanceOf(address)', 'totalSupply()')
# The write facade of the Debreu tables (O5c B1, MY CALLs 163 and 164): the ERC-20 transfer and
# the allowance surface. Its FORBIDDEN signatures are entries at Debreu and refusals at impossibility.
WRITE_FACADE = ('transfer(address,uint256)', 'approve(address,uint256)', 'allowance(address,address)',
                'transferFrom(address,address,uint256)')
# The ERC-20 metadata views (O5c B2): entries of every table, in both regimes.
METADATA = ('name()', 'symbol()', 'decimals()')


@dataclasses.dataclass(frozen=True)
class Program:
    """The tables of one program: genesis rows (wallet, identity, profile, units, partition),
    R codes per charter and profile pair (a = any, uN = up to N, d = deny) of partition 1
    (classA, the model R), W gates per charter and kind (p = pass, r = retain), issuer pairs
    (charter, identity), the entry list. CLASSB: the R codes of partition 2 (empty: the classA
    codes); only the `data` text reads them (O4 B1, MY CALL 211)."""
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
    token: bool = False
    erc20_name: str = 'interest'
    erc20_symbol: str = 'INT'
    classb: tuple = ()

    @property
    def supply(self):
        return sum(row[3] for row in self.genesis)

    def data_text(self):
        """The lines of `interestc data` that these tables give."""
        return '\n'.join([
            f'start {self.start}', f'charters {len(self.restrict)}',
            'genesis ' + ' '.join(':'.join(map(str, row)) for row in self.genesis),
            *(f'restrict {part} ' + ' '.join(','.join(row) for row in table)
              for part, table in enumerate((self.restrict, self.classb or self.restrict), 1)),
            'waterfall ' + ' '.join(self.waterfall),
            'issuers ' + ' '.join(f'{c}:{h}' for c, h in self.issuers),
            f'name {self.erc20_name}', f'symbol {self.erc20_symbol}']
                         + (['asset token'] if self.token else [])) + '\n'


# examples/arrow-debreu.lang: restrict open any, restricted upTo 4, frozen deny; wallet 4098
# in partition 2 (classB: an accredited receiver only, the odd profile codes).
DEBREU = Program(
    'debreu', st.DEBREU, 2, ((4096, 1, 0, 5, 1), (4097, 2, 3, 3, 1), (4098, 2, 3, 2, 2)),
    (('a',) * 16, ('u4',) * 16, ('d',) * 16), ('pp', 'pr', 'rr'), ((1, 1), (2, 1)),
    ('deposit', 'distribute', 'withdraw', 'transfer', 'attest', 'recover', 'cast', 'amend', 'vote', 'mass',
     'supply', 'claimOf', 'charter', 'reserve', 'selfConstituting', 'balanceOf', 'totalSupply'), 1,
    erc20_name='Arrow-Debreu', erc20_symbol='AD', classb=(('d', 'a') * 8, ('d', 'u4') * 8, ('d',) * 16))
# examples/arrow-impossibility.lang: no aggregation, so no distribute, transfer, cast, amend.
IMPOSSIBILITY = Program(
    'impossibility', st.IMPOSSIBILITY, 1, ((4096, 1, 0, 5, 1), (4097, 2, 0, 0, 1)),
    (('d',) * 16,) * 3, ('rr',) * 3, ((1, 1), (2, 1), (3, 1)),
    ('deposit', 'withdraw', 'attest', 'mass', 'supply', 'claimOf', 'charter', 'reserve',
     'selfConstituting', 'balanceOf', 'totalSupply'), 0)
# examples/erc721-dirac.lang: one unit (S = 1) at identity 1; restricted admits an accredited
# receiver only (the profile code is 2 juris + status, so the odd codes).
ERC721 = Program(
    'erc721', st.ROOT / 'examples/erc721-dirac.lang', 1, ((4096, 1, 0, 1, 1), (4097, 2, 3, 0, 1), (4098, 3, 0, 0, 1)),
    (('a',) * 16, ('d', 'a') * 8, ('d',) * 16), ('pp', 'pp', 'rr'), ((1, 1), (2, 1)), DEBREU.entries, 1)
# examples/arrow-debreu-token.lang: the Debreu tables with the ERC-20 carrier (O5b), and no
# `name` or `symbol` def (the defaults, O5c), and every wallet in partition 1.
DEBREU_TOKEN = dataclasses.replace(DEBREU, name='debreu-token', path=st.DEBREU_TOKEN, token=True,
                                   erc20_name='interest', erc20_symbol='INT', classb=(),
                                   genesis=((4096, 1, 0, 5, 1), (4097, 2, 3, 3, 1), (4098, 2, 3, 2, 1)))
# The allowance run (O5c B3, MY CALL 171): the Debreu tables, and the write facade as model entries.
DEBREU_ALLOWANCE = dataclasses.replace(DEBREU, name='debreu-allowance', entries=DEBREU.entries + (
    'erc20Transfer', 'approve', 'transferFrom', 'allowance'))


@dataclasses.dataclass(frozen=True)
class State:
    """BALANCE is the asset balance of the contract: wei, or carrier units in token mode.
    In token mode CARRIER is the carrier address and TOKEN the carrier storage words of
    the senders (the balance of the contract is BALANCE); in wei mode TOKEN is None.
    ALLOWANCE maps an (owner, spender) identity pair to its allowance (O5c). BALLOT maps an
    identity to its code (0 = no ballot) and WEIGHT a code to the mass of its voters (O2)."""
    mu: dict
    registry: dict
    profile: dict
    charter: int
    reserve: tuple
    index: int
    checkpoint: dict
    num: dict
    balance: int
    dust: int = 0
    carrier: int = 0
    token: dict = None
    allowance: dict = dataclasses.field(default_factory=dict)
    ballot: dict = dataclasses.field(default_factory=dict)
    weight: dict = dataclasses.field(default_factory=dict)


def genesis(program):
    mu, registry, profile = {}, {}, {}
    for w, h, p, u, _ in program.genesis:
        registry[w] = h + 1
        mu[h] = mu.get(h, 0) + u
        profile[h] = p
    s = State(mu, registry, profile, program.start, (0, 0), 0, {}, {}, 0)
    held = {key: value for w, balance, allowed in FUNDS
            for key, value in ((slot(0, w), balance), (st.allowance(w, HERE), allowed))}
    return dataclasses.replace(s, carrier=int(st.TOKEN, 16), token=held) if program.token else s


def storage(s):
    """The storage words of state S (zero words left out)."""
    words = {CHARTER: s.charter, INDEX: s.index, DUST: s.dust, CARRIER: s.carrier,
             **{slot(MU, h): u for h, u in s.mu.items()},
             **{slot(REGISTRY, w): v for w, v in s.registry.items()},
             **{slot(PROFILE, h): p for h, p in s.profile.items()},
             **{slot(RESERVE, k): r for k, r in enumerate(s.reserve)},
             **{slot(CHECKPOINT, h): c for h, c in s.checkpoint.items()},
             **{slot(NUM, h): n for h, n in s.num.items()},
             **{allowance_word(o, p): v for (o, p), v in s.allowance.items()},
             **{slot(BALLOT, h): c for h, c in s.ballot.items()},
             **{slot(WEIGHT, c): w for c, w in s.weight.items()}}
    return {key: value for key, value in words.items() if value}


@functools.cache
def allowance_word(owner, spender):
    """The slot of ALLOWANCE[OWNER][SPENDER]: keccak256(spender . keccak256(owner . ALLOWANCE))."""
    return slot(slot(ALLOWANCE, owner), spender)


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


def pull(program, s, sender, value, kind, a):
    """deposit(kind, a) in token mode (O5b): transferFrom(sender, this, a) needs a balance
    and an allowance of a or more; the reserve then grows by a as in deposit."""
    w = int(sender, 16)
    paid, allowed = slot(0, w), st.allowance(w, HERE)
    out = deposit(program, s, sender, a, kind)
    if out is None or a > s.token.get(paid, 0) or a > s.token.get(allowed, 0):
        return None
    return dataclasses.replace(out[0], token={**s.token, paid: s.token[paid] - a,
                                              allowed: s.token[allowed] - a}), out[1]


def distribute(program, s, sender, value, kind):
    if program.supply == 0 or kind > 1:
        return None
    d = s.reserve[kind] if program.waterfall[s.charter - 1][kind] == 'p' else 0
    return None if s.index + d >= WORD else (
        dataclasses.replace(s, reserve=put(s.reserve, kind, s.reserve[kind] - d), index=s.index + d), d)


def withdraw(program, s, sender, value):
    """Pays floor(num h / S) wei; the wei must be there. A withdraw that pays 1 wei or more
    moves num h mod S to the dust, and the whole wei of the dust go to the rent reserve (a
    wrap reverts); a withdraw that pays 0 wei keeps num h mod S (O11)."""
    h = identity(s, sender)
    settled = None if program.supply == 0 or h is None else settle(s, h)
    if settled is None:
        return None
    paid, kept = divmod(settled.num[h], program.supply)
    moved = kept if paid else 0
    whole, dust = divmod(settled.dust + moved, program.supply)
    rent = settled.reserve[0] + whole
    return None if paid > settled.balance or rent >= WORD else (
        dataclasses.replace(settled, num={**settled.num, h: kept - moved}, dust=dust,
                            reserve=put(settled.reserve, 0, rent), balance=settled.balance - paid), paid)


def transfer(program, s, sender, value, to, q):
    """Settles both identities, then debit h q ; credit to q, iff to > 0 (MY CALL 150),
    R(h, to, q) and q <= mass h."""
    h = identity(s, sender)
    return None if h is None else send(program, s, h, to, q)


def send(program, s, h, to, q):
    """The move of transfer by identity h: iff to > 0, R(h, to, q) and q <= mass h."""
    code = program.restrict[s.charter - 1][s.profile.get(h, 0) * 4 + s.profile.get(to, 0)]
    if to == 0 or to + 1 >= WORD or not admits(code, q) or q > s.mu.get(h, 0):
        return None
    return move(s, h, to, q)


def erc20_transfer(program, s, sender, value, to, q):
    """transfer(address to, uint256 q) of the ERC-20 facade (O5c): to >= 2^160 reverts, then transfer."""
    return None if to >= 2**160 else transfer(program, s, sender, value, to, q)


def approve(program, s, sender, value, spender, v):
    """approve(spender, v) (O5c): ALLOWANCE[id(caller)][spender] := v; spender = 0 or
    spender >= 2^160 reverts."""
    o = identity(s, sender)
    if o is None or spender == 0 or spender >= 2**160:
        return None
    return dataclasses.replace(s, allowance={**s.allowance, (o, spender): v}), 1


def transfer_from(program, s, sender, value, h, to, q):
    """transferFrom(from, to, q) (O5c): from or to >= 2^160 and from = 0 revert; q above
    ALLOWANCE[from][id(caller)] reverts; else the move of transfer by from, and the allowance
    falls by q."""
    p = identity(s, sender)
    left = None if p is None else s.allowance.get((h, p), 0) - q
    if left is None or left < 0 or h == 0 or h >= 2**160 or to >= 2**160:
        return None
    moved = send(program, s, h, to, q)
    return None if moved is None else (dataclasses.replace(moved[0], allowance={**s.allowance, (h, p): left}), 1)


def allowance_view(program, s, sender, value, o, p):
    """allowance(owner, spender) (O5c): an argument >= 2^160 reverts; else ALLOWANCE[o][p]."""
    return None if max(o, p) >= 2**160 else (s, s.allowance.get((o, p), 0))


def move(s, h, to, q):
    """Settles h, then to; then debit h q ; credit to q (the credit reads the debited mu); the
    vote of q moves from the ballot of h to the ballot of to (O2)."""
    first = settle(s, h)
    second = None if first is None else settle(first, to)
    if second is None:
        return None
    debited = {**second.mu, h: second.mu.get(h, 0) - q}
    weight = shift(second.weight, second.ballot.get(h, 0), second.ballot.get(to, 0), q)
    return dataclasses.replace(second, mu={**debited, to: debited.get(to, 0) + q}, weight=weight), 1


def issuer(program, s, sender):
    """The caller identity is an issuer of the active charter."""
    c = identity(s, sender)
    return c is not None and (s.charter, c) in program.issuers


def attest(program, s, sender, value, w, h, p):
    """A registry write by a trusted issuer of the active charter; identity 0 reverts."""
    if not issuer(program, s, sender) or w >= 2**160 or p >= 4 or h == 0 or h + 1 >= WORD:
        return None
    return dataclasses.replace(s, registry={**s.registry, w: h + 1}, profile={**s.profile, h: p}), 1


def recover(program, s, sender, value, h, to, q):
    """ERC-1644 forced transfer by a trusted issuer of the active charter (O3): the move
    of transfer iff h > 0, to > 0 and q <= mass h; R does not gate it."""
    if (not issuer(program, s, sender) or 0 in (h, to) or h + 1 >= WORD or to + 1 >= WORD
            or q > s.mu.get(h, 0)):
        return None
    return move(s, h, to, q)


def cast(program, s, sender, value, *ballots):
    v = verdict(program, ballots)
    return None if v is None else (s, v)


def shift(weight, old, new, q):
    """WEIGHT after the vote of mass q moves from code OLD to code NEW (code 0: no term) (O2)."""
    debited = {**weight, old: weight.get(old, 0) - q} if old else weight
    return {**debited, new: debited.get(new, 0) + q} if new else debited


def vote(program, s, sender, value, c):
    """vote(c) (O2): the mass of h = id(caller) moves in WEIGHT from BALLOT[h] to c (vote(0)
    withdraws the ballot); BALLOT[h] := c; no identity and c > k revert."""
    h = identity(s, sender)
    if h is None or c > 3:
        return None
    weight = shift(s.weight, s.ballot.get(h, 0), c, s.mu.get(h, 0))
    return dataclasses.replace(s, ballot={**s.ballot, h: c}, weight=weight), c


def seats(weight):
    """The seats of the 3 members by the largest remainder of w = WEIGHT[1 .. 3], ties to the
    lower code (O2); None when W = 0."""
    w = tuple(weight.get(c, 0) for c in (1, 2, 3))
    if sum(w) == 0:
        return None
    floors, rests = zip(*(divmod(3 * x, sum(w)) for x in w))
    rank = sorted(range(3), key=lambda j: -rests[j])
    return tuple(f + int(rank.index(j) < 3 - sum(floors)) for j, f in enumerate(floors))


def amend(program, s, sender, value):
    """amend() (O2): active charter := the verdict of the seat vector; W = 0 reverts; mu, the
    ballots and the treasury do not move."""
    held = seats(s.weight)
    v = None if held is None else verdict(program, tuple(c for c, n in zip((1, 2, 3), held) for _ in range(n)))
    return None if v is None else (dataclasses.replace(s, charter=v), v)


def view(name):
    def result(program, s, sender, value, *args):
        out = dict(mass=lambda h: s.mu.get(h, 0), supply=lambda: program.supply,
                   claimOf=lambda h: claim(s, h), charter=lambda: s.charter,
                   reserve=lambda kind: s.reserve[kind] if kind < 2 else None,
                   selfConstituting=lambda: program.constituting,
                   balanceOf=lambda h: s.mu.get(h, 0), totalSupply=lambda: program.supply)[name](*args)
        return None if out is None else (s, out)
    return result


OPS = dict(deposit=deposit, distribute=distribute, withdraw=withdraw, transfer=transfer,
           attest=attest, recover=recover, cast=cast, amend=amend, vote=vote, erc20Transfer=erc20_transfer,
           approve=approve, transferFrom=transfer_from, allowance=allowance_view, **{name: view(name) for name in VIEWS})


def apply(program, s, call):
    """-> (state, result), or None when the call reverts (the state does not change)."""
    name, args, sender, value = call
    if name not in program.entries or (value and (program.token or name != 'deposit')):
        return None
    out = (pull if program.token and name == 'deposit' else OPS[name])(program, s, sender, value, *args)
    if out is None or not (program.token and name == 'withdraw'):
        return out
    w = slot(0, int(sender, 16))
    return dataclasses.replace(out[0], token={**out[0].token, w: out[0].token.get(w, 0) + out[1]}), out[1]


def geth_step(label, runtime, chain, call):
    """CHAIN: (storage, balance) of the contract, and in token mode the carrier storage."""
    name, args, sender, value = call
    before, balance, *held = chain
    carrier = (st.TOKEN, st.token_code('standard'), held[0]) if held else None
    return run(label, runtime, data(name, *args), before=before, value=value, sender=sender,
               balance=0 if held else balance, token=carrier)


def check_step(label, program, runtime, s, chain, call):
    """One call: the model from S, geth from CHAIN = (storage, balance) of the step before
    (token mode: and the carrier storage; the wei balances stay 0 and BASE).
    -> (model state, geth chain) after the call."""
    model = apply(program, s, call)
    after, result = model if model else (s, None)
    actual = geth_step(label, runtime, chain, call)
    wei = after.token is None
    wanted = dict(status='revert' if model is None else 'success',
                  output='' if model is None else f'{result:064x}',
                  storage=storage(after), receiver=after.balance if wei else 0,
                  sender=BASE + s.balance - after.balance if wei else BASE,
                  logs=records(program, s, call, model), token=holdings(after))
    got = dict(status=actual['status'], output=actual['output'], storage=actual['storage'],
               receiver=actual['balances'].get(st.RECEIVER, 0),
               sender=actual['balances'].get(call[2], 0), logs=actual['logs'], token=actual['token'])
    require(got == wanted, f'{label} {call}: geth {got} != model {wanted}')
    held = actual['token']
    return after, ((actual['storage'], got['receiver']) if wei
                   else (actual['storage'], held.get(slot(0, HERE), 0), held))


def records(program, s, call, model):
    """The records of CALL from S: a successful transfer logs Transfer(h, to, q) and a
    successful recover logs Transfer(from, to, q) (O5a); the ERC-20 transfer and transferFrom
    log as transfer and recover, and approve logs Approval(owner, spender, v) (O5c); a successful
    withdraw logs Paid(h, wallet, paid, moved) (O11); a revert or any other entry logs none."""
    name, args, sender, value = call
    moves = dict(transfer=lambda to, q: (identity(s, sender), to, q), recover=lambda h, to, q: (h, to, q),
                 erc20Transfer=lambda to, q: (identity(s, sender), to, q), transferFrom=lambda h, to, q: (h, to, q))
    if model and name in moves:
        return [st.transfer_log(*moves[name](*args))]
    if model and name == 'approve':
        return [st.approval_log(identity(s, sender), *args)]
    if model and name == 'withdraw':
        h = identity(s, sender)
        paid, kept = divmod(settle(s, h).num[h], program.supply)
        return [st.paid_log(h, int(sender, 16), paid, kept if paid else 0)]
    return []


def fold(measure, logs, minted=False):
    """MEASURE after the Transfer records of LOGS (the Paid records are left out): each record
    debits its source and credits its target by its value; a genesis record (MINTED) only
    credits its target."""
    def step(mu, record):
        source, target, q = (int(word, 16) for word in (*record[1][1:], record[2]))
        debited = mu if minted else {**mu, source: mu.get(source, 0) - q}
        return {**debited, target: debited.get(target, 0) + q}
    return functools.reduce(step, [record for record in logs if record[1][0] == st.topic0()], measure)


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
    selfConstituting=lambda r: (),
    balanceOf=lambda r: (r.randrange(0, 5),),
    totalSupply=lambda r: ())
CHOICES = ('deposit',) * 4 + ('distribute',) * 3 + ('withdraw',) * 4 + ('transfer',) * 3 + (
    'attest', 'cast', 'amend', 'claimOf', 'claimOf', *VIEWS[:-len(FACADE)])
# The draws leave out FACADE, so the sequences do not change (O5a); each sequence ends with balanceOf.
# Transfer draws include identity 0 so the sequences check its rejection.
# S = 1: a transfer of quantity 1 moves the whole unit.
DIRAC_ARGS = dict(ARGS, transfer=lambda r: (r.randrange(0, 5), r.choice((0, 1, 1, 1, 2))))

# The sequences must reach these successful calls (name + '+': a nonzero result).
COVER_DEBREU = {'deposit+', 'distribute+', 'distribute0', 'withdraw+', 'withdraw0', 'transfer+',
                'attest+', 'amend+', 'cast+', 'claimOf+', 'transfer-to-zero-revert'}
COVER_IMPOSSIBILITY = {'deposit+', 'withdraw0'}
COVER_ERC721 = {'deposit+', 'distribute+', 'withdraw+', 'transfer+', 'amend+', 'cast+', 'claimOf+',
                'transfer-to-zero-revert'}
# The token run (MY CALL 159): deposit(kind, a) takes a of 0 .. 40 carrier units, and each
# call sends 1 wei with chance 1/20 (every entry is non-payable in token mode, so it reverts).
TOKEN_ARGS = dict(ARGS, deposit=lambda r: (r.choice((0, 0, 1, 1, 2)), r.randrange(0, 41)))
TOKEN_SEEDS, TOKEN_LENGTH = range(1, 11), 20
COVER_TOKEN = {'deposit+', 'distribute+', 'distribute0', 'withdraw+', 'withdraw0', 'transfer+', 'claimOf+',
               'transfer-to-zero-revert'}
# The allowance run (O5c B3, MY CALL 171): its own seeds, the Debreu draws and the write facade.
# Identity 1 (3 wallets) mostly approves identity 2, and identity 2 (2 wallets) mostly spends
# from identity 1, so the sequences reach a transferFrom that moves units.
ALLOWANCE_ARGS = dict(ARGS, erc20Transfer=ARGS['transfer'],
                      approve=lambda r: (r.choice((0, 1, 2, 2, 2)), r.randrange(0, 8)),
                      transferFrom=lambda r: (r.choice((0, 1, 1, 1, 2)), r.randrange(0, 5), r.randrange(0, 6)),
                      allowance=lambda r: (r.randrange(0, 5), r.randrange(0, 5)))
ALLOWANCE_CHOICES = CHOICES + ('approve',) * 5 + ('transferFrom',) * 6 + ('erc20Transfer',) * 2 + ('allowance',)
ALLOWANCE_SEEDS, ALLOWANCE_LENGTH = range(201, 211), 20
COVER_ALLOWANCE = {'approve+', 'transferFrom-moves', 'erc20Transfer+', 'transfer+'}
# The vote run (O2, MY CALL 190 (a)): its own seeds, 10 sequences of 30 steps, the Debreu tables
# with the write facade. The draws are vote (0 withdraws, 4 > k reverts), amend() alone, the 4
# moves (with approve, so that transferFrom moves), attest and the views. vote-mass is a vote
# for a code by an identity with mass; weight-moves is a call other than vote that moves WEIGHT.
VOTE_ARGS = dict(ALLOWANCE_ARGS, vote=lambda r: (r.choice((0, 1, 1, 2, 2, 3, 3, 4)),), amend=lambda r: (),
                 recover=lambda r: (r.choice((0, 1, 1, 2, 2)), r.randrange(0, 5), r.randrange(0, 6)))
VOTE_CHOICES = (('vote',) * 6 + ('amend',) * 3 + ('transfer',) * 3 + ('erc20Transfer',) * 2 + ('approve',) * 2
                + ('transferFrom',) * 3 + ('recover',) * 2 + ('attest',) + VIEWS)
VOTE_SEEDS, VOTE_LENGTH = range(301, 311), 30
COVER_VOTE = {'vote-mass', 'vote0', 'amend+', 'weight-moves'}


def random_call(rng, args, payable=True, choices=CHOICES):
    name = rng.choice(choices)
    value = rng.randrange(0, 41) if name == 'deposit' and payable else int(rng.randrange(20) == 0)
    return name, args[name](rng), rng.choice(SENDERS), value


def calls_of(call):
    """The calls of a drawn CALL: an amend draw (b1, b2, b3) is vote(b1) by its sender, then
    amend() (MY CALL 190 (a)); any other draw is one call (the amend() draw of the vote run too)."""
    name, args, sender, value = call
    return [('vote', args[:1], sender, value), ('amend', (), sender, value)] if name == 'amend' and args else [call]


def sequence(program, runtime, seed, length, args, choices=CHOICES):
    """LENGTH random calls from the genesis of PROGRAM; -> the final model state and the
    successful calls seen (name + '+' for a nonzero result, name + '0' for zero). After each
    call the fold of the Transfer records since the deploy is mu; at the end balanceOf(h) = mass h,
    and allowance(o, p) for each (owner, spender) pair of an approve or transferFrom draw."""
    rng, s, seen, pairs = random.Random(seed), genesis(program), set(), set()
    measure = fold({}, [st.transfer_log(0, h, u) for h, u in st.data_logs(program.path)], minted=True)
    chain, deposits, paid, recycled = at(s), 0, 0, 0
    calls = [step for _ in range(length) for step in calls_of(random_call(rng, args, not program.token, choices))]
    for number, call in enumerate(calls):
        label = f'{program.name}-seq{seed}-{number}'
        model = apply(program, s, call)
        seen |= {call[0] + ('+' if model and model[1] else '0')} if model else set()
        if call[0] == 'transfer' and call[1][0] == 0 and model is None:
            seen.add('transfer-to-zero-revert')
        if call[0] == 'transferFrom' and call[1][2] and model:
            seen.add('transferFrom-moves')
        if call[0] == 'vote' and model and call[1][0] and s.mu.get(identity(s, call[2]), 0):
            seen.add('vote-mass')
        if call[0] != 'vote' and model and tally(storage(model[0])) != tally(storage(s)):
            seen.add('weight-moves')
        pairs |= drawn_pair(s, call)
        deposits += (call[1][1] if program.token else call[3]) if model and call[0] == 'deposit' else 0
        paid += model[1] if model and call[0] == 'withdraw' else 0
        recycled += model[0].reserve[0] - s.reserve[0] if model and call[0] == 'withdraw' else 0
        charter, logs = chain[0].get(CHARTER), records(program, s, call, model)
        s, chain = check_step(label, program, runtime, s, chain, call)
        measure = fold(measure, logs)
        require({h: u for h, u in measure.items() if u} == {h: u for h, u in s.mu.items() if u},
                f'{label}: the fold of the Transfer records {measure} != mu {s.mu}')
        require(laws_hold(program, *chain[:2], deposits, paid, recycled),
                f'{label} {call}: conservation, sum to inflow or solvency fails on the geth state')
        require(tally(chain[0]) == image(chain[0]),
                f'{label} {call}: WEIGHT {tally(chain[0])} is not the image {image(chain[0])} of mu along the ballots')
        require(chain[0].get(CHARTER) == charter or call[0] == 'amend', f'{label}: {call[0]} wrote the charter')
        require(program.supply != 1 or [v for v in s.mu.values() if v] == [1], f'{label}: the measure is not a Dirac measure')
    for h in IDENTITIES:
        s, chain = check_step(f'{program.name}-seq{seed}-balance-{h}', program, runtime, s, chain,
                              ('balanceOf', (h,), SENDER, 0))
    for o, p in sorted(pairs):
        s, chain = check_step(f'{program.name}-seq{seed}-allowance-{o}-{p}', program, runtime, s, chain,
                              ('allowance', (o, p), SENDER, 0))
    return s, seen


def drawn_pair(s, call):
    """{(owner, spender)} of an approve or transferFrom CALL from S whose caller has an identity, else {}."""
    name, args, sender, value = call
    h = identity(s, sender)
    if h is None or name not in ('approve', 'transferFrom'):
        return set()
    return {(h, args[0]) if name == 'approve' else (args[0], h)}


def tally(words):
    """The nonzero WEIGHT words: {c: WEIGHT[c]} for the codes c of 1 .. 3 (O2)."""
    return {c: w for c, w in ((c, words.get(slot(WEIGHT, c), 0)) for c in (1, 2, 3)) if w}


def image(words):
    """The image of MU along BALLOT (MY CALL 183 (a)): {c: the sum of MU[h] over the identities h
    with BALLOT[h] = c}, nonzero only."""
    sums = ((c, sum(words.get(slot(MU, h), 0) for h in IDENTITIES if words.get(slot(BALLOT, h)) == c))
            for c in (1, 2, 3))
    return {c: w for c, w in sums if w}


def geth_claims(words):
    """claimOf of each identity, from the storage words of geth."""
    index = words.get(INDEX, 0)
    return {h: words.get(slot(NUM, h), 0) + words.get(slot(MU, h), 0) * (index - words.get(slot(CHECKPOINT, h), 0))
            for h in IDENTITIES}


def laws_hold(program, words, balance, deposits, paid, recycled):
    """Conservation: the masses sum to S. Sum to inflow: the claims, DUST, S * paid and
    S * recycled sum to S * INDEX (each distribute adds S * d; RECYCLED is the wei that
    withdraw moved from DUST to RESERVE[0]), the deposits and RECYCLED are the reserves and
    INDEX, and the wei balance is the deposits less the paid wei. Solvency, from the geth
    state only: S * balance is the claims, DUST and S * the reserves (O11)."""
    supply, index, dust = program.supply, words.get(INDEX, 0), words.get(DUST, 0)
    mass = sum(words.get(slot(MU, h), 0) for h in IDENTITIES)
    reserves = words.get(slot(RESERVE, 0), 0) + words.get(slot(RESERVE, 1), 0)
    claims = sum(geth_claims(words).values())
    return (mass == supply and claims + dust + supply * (paid + recycled) == supply * index
            and deposits + recycled == reserves + index and balance == deposits - paid
            and supply * balance == claims + dust + supply * reserves)


def play(program, s, calls):
    """The model state after CALLS from S; each call must succeed."""
    for call in calls:
        out = apply(program, s, call)
        require(out is not None, f'{program.name}: the setup call {call} reverts in the model')
        s = out[0]
    return s


def holdings(s):
    """The carrier storage words of model state S (zero words left out); {} in wei mode."""
    held = {} if s.token is None else {**s.token, slot(0, HERE): s.balance}
    return {key: value for key, value in held.items() if value}


def at(s):
    """The geth chain (storage, balance) of model state S, and the carrier storage in token mode."""
    return (storage(s), s.balance) + (() if s.token is None else (holdings(s),))


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
    """At a rich state, identity 1 (mass 3) votes b1 and identity 2 (mass 7) votes b2, for each
    (b1, b2); then amend() changes storage only at CHARTER, the wei stays, and the charter is the
    verdict of (b1, b2, b2) (O2: the largest remainder gives b1 1 seat and b2 2 seats)."""
    base, count = rich_debreu(program), 0
    rest = lambda words: {key: value for key, value in words.items() if key != CHARTER}
    for ballots in itertools.product((1, 2, 3), repeat=2):
        label, s, chain = f'law-amend-{"".join(map(str, ballots))}', base, at(base)
        for h, (b, sender) in enumerate(zip(ballots, (wallet(4096), wallet(4097))), 1):
            s, chain = check_step(f'{label}-vote-{h}', program, runtime, s, chain, ('vote', (b,), sender, 0))
        after, moved = check_step(label, program, runtime, s, chain, ('amend', (), SENDER, 0))
        require(rest(moved[0]) == rest(chain[0]) and moved[1] == chain[1]
                and after.charter == verdict(program, (ballots[0],) + (ballots[1],) * 2),
                f'law-amend {ballots}: amend moved a word other than the charter')
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
    """At impossibility the aggregation has no inhabitant: cast, vote, amend, distribute,
    transfer and recover revert with empty output, and the storage and the wei do not
    change."""
    one = wallet(4096)
    s = play(program, genesis(program), [('deposit', (0,), SENDER, 7), ('deposit', (1,), one, 3),
                                         ('attest', (4099, 3, 2), one, 0), ('withdraw', (), one, 0)])
    calls = (('cast', (1, 1, 1), one, 0), ('vote', (1,), one, 0), ('amend', (), SENDER, 0),
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


def dust_recycle(program, runtime):
    """Charter open, 17 wei distributed: two identities withdraw, and each moves its remainder
    to DUST. When DUST reaches S, its whole wei go to RESERVE[0] (R0 rises by 1). Then
    distribute(0) pays that wei by mass. The laws hold on the geth state after each step."""
    one, two = wallet(4096), wallet(4097)
    s = play(program, dataclasses.replace(genesis(program), charter=1),
             [('deposit', (0,), SENDER, 17), ('distribute', (0,), SENDER, 0)])
    chain, recycled = at(s), 0
    for number, call in enumerate((('withdraw', (), one, 0), ('withdraw', (), two, 0), ('distribute', (0,), SENDER, 0))):
        after, chain = check_step(f'law-dust-recycle-{number}', program, runtime, s, chain, call)
        recycled += after.reserve[0] - s.reserve[0] if call[0] == 'withdraw' else 0
        require(laws_hold(program, *chain, 17, 17 - after.balance, recycled), f'law-dust-recycle-{number}: a law fails')
        s = after
    require(recycled == 1 and s.dust == 0 and s.index == 18 and claim(s, 1) == claim(s, 2) == 5,
            f'law-dust-recycle: recycled {recycled}, dust {s.dust}, claims {claim(s, 1)} {claim(s, 2)}')
    return 1


def withdraw_moves_dust(program, runtime):
    """withdraw pays claim / S, with CHECKPOINT := INDEX. When it pays 1 wei or more, it moves
    claim mod S to DUST, and the whole wei of DUST go to RESERVE[0]; else NUM keeps claim mod S."""
    count, supply = 0, program.supply
    for n, (index, mark), dust in itertools.product((0, 1, 9, 10, 11, 29, 99), ((0, 0), (3, 1)), (0, supply - 1)):
        s = dataclasses.replace(genesis(program), num={1: n}, index=index, checkpoint={1: mark}, balance=100, dust=dust)
        words, balance = check_step(f'contract-withdraw-{n}-{index}-{dust}', program, runtime, s, at(s),
                                    ('withdraw', (), wallet(4096), 0))[1]
        paid, kept = divmod(n + s.mu[1] * (index - mark), supply)
        moved = kept if paid else 0
        whole, rest = divmod(dust + moved, supply)
        require(words.get(slot(NUM, 1), 0) == kept - moved and words.get(DUST, 0) == rest
                and words.get(slot(RESERVE, 0), 0) == whole and balance == 100 - paid
                and words.get(slot(CHECKPOINT, 1), 0) == index, f'contract-withdraw {n} {index} {dust}: the dust does not move')
        count += 1
    return count


def allowance_laws(program, runtime):
    """The allowance laws on the geth state (MY CALL 171), from rich_debreu at the charter of each
    vector. approve changes only the allowance word. transferFrom by the spender equals transfer by
    from (every other word and the balance: mu, the claims and the treasury), and the allowance
    falls by q. Over the allowance, or when transfer by from reverts (R, the mass, to = 0),
    transferFrom reverts and the state does not change. -> the number of law calls."""
    one, two = wallet(4096), wallet(4097)
    # (charter, owner wallet, spender wallet, v, to, q, moves): identity 1 has mass 3 and identity 2
    # mass 7; charter 1 open, 2 restricted (up to 4), 3 frozen.
    vectors = ((1, one, two, 5, 3, 2, True), (1, one, two, 3, 3, 3, True), (1, one, two, 1, 3, 2, False),
               (1, one, two, 9, 3, 4, False), (2, two, one, 6, 3, 5, False), (2, two, one, 6, 3, 4, True),
               (3, one, two, 5, 3, 1, False), (1, one, two, 5, 0, 1, False), (1, one, one, 2, 2, 1, True))
    base = rich_debreu(program)
    for number, (charter, owner, spender, v, to, q, moves) in enumerate(vectors):
        s = dataclasses.replace(base, charter=charter)
        h, p, start, label = identity(s, owner), identity(s, spender), at(s), f'law-allowance-{number}'
        key = allowance_word(h, p)
        approved, chain = check_step(f'{label}-approve', program, runtime, s, start, ('approve', (p, v), owner, 0))
        changed = {k for k in start[0].keys() | chain[0].keys() if start[0].get(k) != chain[0].get(k)}
        require(changed <= {key} and chain[0].get(key, 0) == v and chain[1] == start[1],
                f'{label}: approve {p} {v} moved a word other than the allowance')
        allowed = q <= v and apply(program, approved, ('transfer', (to, q), owner, 0)) is not None
        sent = check_step(f'{label}-transfer', program, runtime, approved, chain, ('transfer', (to, q), owner, 0))[1]
        after = check_step(f'{label}-transferfrom', program, runtime, approved, chain,
                           ('transferFrom', (h, to, q), spender, 0))[1]
        want = (without(sent[0], key), sent[1], v - q) if allowed else (without(chain[0], key), chain[1], v)
        require(allowed == moves and (without(after[0], key), after[1], after[0].get(key, 0)) == want,
                f'{label}: transferFrom {h} {to} {q} with allowance {v} is not transfer by from')
    return 3 * len(vectors)


def vote_laws(program, runtime):
    """The vote laws on the geth state (MY CALL 188), from rich_debreu (identity 1 is the issuer
    with mass 3, identity 2 has mass 7, charter open). recastMoves: identity 1 votes b, then b2;
    each vote changes only the ballot of 1 and WEIGHT, and the tally is mass 1 at the code
    (vote(0) withdraws). transferMovesVote: after each of the 4 moves the tally is the image of mu
    along the ballots. noDoubleVote: 1 votes c, moves 2 to identity 2, 2 votes c2: the tally sums to
    mass 1 + mass 2; then amend() gives the verdict of the seats of the geth tally, and the seats
    sum to 3 with floor <= seat <= ceil of 3 w / W. -> the number of law vectors."""
    one, two, base = wallet(4096), wallet(4097), rich_debreu(program)
    m1, m2, start = base.mu.get(1, 0), base.mu.get(2, 0), at(base)
    keys = {slot(BALLOT, 1)} | {slot(WEIGHT, c) for c in (1, 2, 3)}
    for b, b2 in itertools.product((1, 2, 3), (0, 1, 2, 3)):
        label = f'law-vote-recast-{b}{b2}'
        s, first = check_step(f'{label}-vote', program, runtime, base, start, ('vote', (b,), one, 0))
        second = check_step(label, program, runtime, s, first, ('vote', (b2,), one, 0))[1]
        changed = {k for k in start[0].keys() | second[0].keys() if start[0].get(k) != second[0].get(k)}
        require(tally(first[0]) == {b: m1} and tally(second[0]) == ({b2: m1} if b2 else {})
                and changed <= keys and second[1] == start[1], f'{label}: the recast moved {sorted(changed)}')
    # (calls before the move, the move): the moves of transfer, ERC-20 transfer, transferFrom and recover.
    moves = (((), ('transfer', (2, 2), one, 0)), ((), ('transfer', (1, 4), two, 0)),
             ((), ('transfer', (3, 1), one, 0)), ((), ('erc20Transfer', (2, 2), one, 0)),
             ((('approve', (2, 2), one, 0),), ('transferFrom', (1, 2, 2), two, 0)),
             ((), ('recover', (2, 1, 4), one, 0)))
    for (c1, c2), (setup, move) in itertools.product(((1, 3), (2, 2), (3, 0)), moves):
        label, s, chain = f'law-vote-image-{c1}{c2}-{move[0]}-{"".join(map(str, move[1]))}', base, start
        for number, call in enumerate((('vote', (c1,), one, 0), ('vote', (c2,), two, 0)) + setup):
            s, chain = check_step(f'{label}-{number}', program, runtime, s, chain, call)
        moved, after = check_step(label, program, runtime, s, chain, move)
        require(moved.mu != s.mu and tally(after[0]) == image(after[0]),
                f'{label}: the move reverts, or WEIGHT {tally(after[0])} is not the image {image(after[0])}')
    for c, c2 in itertools.product((1, 2, 3), repeat=2):
        label, s, chain = f'law-vote-once-{c}{c2}', base, start
        for number, call in enumerate((('vote', (c,), one, 0), ('transfer', (2, 2), one, 0), ('vote', (c2,), two, 0))):
            s, chain = check_step(f'{label}-{number}', program, runtime, s, chain, call)
        w = tally(chain[0])
        held = seats(w)
        after = check_step(f'{label}-amend', program, runtime, s, chain, ('amend', (), SENDER, 0))[0]
        quota = all(3 * w.get(j, 0) // m <= n <= -(-3 * w.get(j, 0) // m) for j, n, m in zip((1, 2, 3), held, (m1 + m2,) * 3))
        require(sum(w.values()) == m1 + m2 and w.get(c2, 0) >= m2 + 2 and sum(held) == 3 and quota
                and after.charter == verdict(program, tuple(j for j, n in zip((1, 2, 3), held) for _ in range(n))),
                f'{label}: the tally {w} counts the moved mass twice, or the seats {held} fail')
    return 12 + 3 * len(moves) + 9


def dirac_dictator(program, runtime):
    """erc721-dirac (S = 1, MY CALL 188): the holder (identity 1) votes c, then amend(): the tally
    is {c: 1}, the seats are (3 at c), and the charter is the verdict of (c, c, c). -> 3."""
    for c in (1, 2, 3):
        label, s = f'law-vote-dirac-{c}', genesis(program)
        s, chain = check_step(f'{label}-vote', program, runtime, s, at(s), ('vote', (c,), wallet(4096), 0))
        after = check_step(label, program, runtime, s, chain, ('amend', (), SENDER, 0))[0]
        require(tally(chain[0]) == {c: 1} and after.charter == verdict(program, (c,) * 3),
                f'{label}: the holder of the unit is not a dictator')
    return 3


def without(words, key):
    return {k: w for k, w in words.items() if k != key}


def selector_table(runtime):
    """The selectors of the dispatcher (src/evm.c dispatch: DUP1 PUSH4 sel EQ PUSH2 dest JUMPI)."""
    return {m.group(1) for m in re.finditer('8063([0-9a-f]{8})1461[0-9a-f]{4}57', runtime) if m.start() % 2 == 0}


def selector_checks(program, runtime, s):
    """The selector table is the entry list exactly; each forbidden call reverts and the
    state does not change; each required call gives the model result."""
    table = selector_table(runtime)
    facade = (WRITE_FACADE if 'transfer' in program.entries else ()) + METADATA
    wanted = ({st.selector(name, WORDS[name], st.SIGNATURES.get(name)) for name in program.entries}
              | {st.selector(signature.split('(')[0], 0, signature) for signature in facade})
    require(table == wanted, f'{program.name}: selector table {sorted(table)} != entries {sorted(wanted)}')
    # A FORBIDDEN signature of the Debreu write facade is an entry; table == wanted checks it.
    for signature in (signature for signature in FORBIDDEN if signature not in facade):
        code = st.checked(['cast', 'sig', signature]).strip()[2:]
        actual = run(f'contract-{program.name}-{code}', runtime, code + f'{1:064x}' * 3, before=storage(s),
                     sender=wallet(4096), balance=s.balance)
        require(code not in table and actual['status'] == 'revert' and actual['output'] == ''
                and actual['storage'] == storage(s) and actual['balances'].get(st.RECEIVER, 0) == s.balance,
                f'{program.name}: {signature} is an entry or moved the state')
    for signature in REQUIRED:
        code, name = st.checked(['cast', 'sig', signature]).strip()[2:], signature.split('(')[0]
        require(code in table, f'{program.name}: {signature} is not an entry')
        check_step(f'contract-{program.name}-{code}', program, runtime, s, at(s), (name, (1,) * WORDS[name], wallet(4096), 0))
    return 1 + len(FORBIDDEN) + len(REQUIRED)


def dirac_vectors(program, runtime):
    """examples/erc721-dirac.lang (S = 1): after each step one identity holds the one unit, the
    restricted charter admits an accredited receiver only, and withdraw keeps no remainder."""
    one, two = wallet(4096), wallet(4097)
    calls = (('deposit', (0,), SENDER, 9, 9), ('distribute', (0,), SENDER, 0, 9), ('claimOf', (1,), SENDER, 0, 9),
             ('transfer', (2, 2), one, 0, None), ('cast', (2, 3, 2), SENDER, 0, 3), ('vote', (2,), one, 0, 2),
             ('amend', (), SENDER, 0, 2), ('transfer', (3, 1), one, 0, None), ('transfer', (2, 1), one, 0, 1),
             ('transfer', (1, 1), two, 0, None), ('deposit', (1,), SENDER, 4, 4), ('distribute', (1,), SENDER, 0, 4),
             ('claimOf', (2,), SENDER, 0, 4), ('withdraw', (), one, 0, 9), ('withdraw', (), two, 0, 4),
             ('vote', (3,), two, 0, 3), ('amend', (), SENDER, 0, 3), ('transfer', (1, 1), two, 0, None),
             ('deposit', (0,), SENDER, 5, 5), ('distribute', (0,), SENDER, 0, 0))
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
    word = f'{int(st.TOKEN, 16):064x}' if program.token else ''
    holder = (st.TOKEN, st.token_code('standard'), {}) if program.token else None
    runtime = st.program_code('claims-' + program.name, program.path, suffix=word, token=holder)
    carrier = {CARRIER: int(st.TOKEN, 16)} if program.token else {}
    require(storage(genesis(program)) == st.data_storage(program.path) | carrier,
            f'{program.name}: the model genesis is not the deployed genesis')
    verdicts = st.interestc('verdicts', program.path, 'F').strip() if 'cast' in program.entries else ''
    return dataclasses.replace(program, verdicts=verdicts), runtime


def main():
    st.WORK = st.ROOT / '.gatework/claims'
    st.WORK.mkdir(parents=True, exist_ok=True)
    require(st.INTERESTC.exists(), f'{st.INTERESTC} is missing: run make')
    runs = ((DEBREU, range(1, 11), 20, COVER_DEBREU, ARGS), (IMPOSSIBILITY, range(1, 5), 15, COVER_IMPOSSIBILITY, ARGS),
            (ERC721, range(1, 7), 45, COVER_ERC721, DIRAC_ARGS))
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
    program, runtime = load(DEBREU_TOKEN)
    seen = set()
    for seed in TOKEN_SEEDS:
        seen |= sequence(program, runtime, seed, TOKEN_LENGTH, TOKEN_ARGS)[1]
    require(COVER_TOKEN <= seen, f'{program.name}: the sequences miss {sorted(COVER_TOKEN - seen)}')
    program, runtime = load(DEBREU_ALLOWANCE)
    seen = set()
    for seed in ALLOWANCE_SEEDS:
        seen |= sequence(program, runtime, seed, ALLOWANCE_LENGTH, ALLOWANCE_ARGS, ALLOWANCE_CHOICES)[1]
    require(COVER_ALLOWANCE <= seen, f'{program.name}: the sequences miss {sorted(COVER_ALLOWANCE - seen)}')
    allowed = allowance_laws(program, runtime)
    voter, seen = dataclasses.replace(program, name='debreu-vote'), set()
    for seed in VOTE_SEEDS:
        seen |= sequence(voter, runtime, seed, VOTE_LENGTH, VOTE_ARGS, VOTE_CHOICES)[1]
    require(COVER_VOTE <= seen, f'{voter.name}: the sequences miss {sorted(COVER_VOTE - seen)}')
    debreu, impossible, erc721 = built['debreu'], built['impossibility'], built['erc721']
    laws = (transfer_then_distribute(*debreu) + amend_law(*debreu) + recover_law(*debreu)
            + impossibility_reverts(*impossible)
            + r_rejections(*debreu) + dust_recycle(*debreu) + dirac_vectors(*erc721)
            + vote_laws(program, runtime) + dirac_dictator(*erc721))
    contract = (withdraw_moves_dust(*debreu) + selector_checks(*debreu, rich_debreu(debreu[0]))
                + selector_checks(*impossible, genesis(impossible[0])) + selector_checks(*erc721, genesis(erc721[0]))
                + identity_boundaries(*erc721))
    print(f'CLAIMS sequences={sequences} steps={steps} laws={laws} contract={contract} geth=model OK'
          f' token: sequences={len(TOKEN_SEEDS)} steps={len(TOKEN_SEEDS) * TOKEN_LENGTH}'
          f' allowance: sequences={len(ALLOWANCE_SEEDS)} steps={len(ALLOWANCE_SEEDS) * ALLOWANCE_LENGTH}'
          f' laws={allowed} vote: sequences={len(VOTE_SEEDS)} steps={len(VOTE_SEEDS) * VOTE_LENGTH}'
          f' (logs: {st.WORK})')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'CLAIMS FAIL: {error}', file=sys.stderr)
        sys.exit(1)
