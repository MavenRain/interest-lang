#!/usr/bin/env python3
"""Recheck evaluated terms and prove that their observable results survive printing."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/interestc'
WORK = ROOT / 'build/test/normal-forms'
WORK.mkdir(parents=True, exist_ok=True)


def run(path, *args):
    result = subprocess.run([str(COMPILER), *args[:1], str(path), *args[1:]],
                            capture_output=True, text=True, timeout=30)
    if result.returncode or result.stderr:
        raise AssertionError(f'{path.name}: {args}: {result.returncode}: {result.stderr}')
    return result.stdout.strip()


def roundtrip(name, source, ty, observation):
    path = WORK / (name + '.lang')
    source = 'def members : Nat := 3\n' + source + '\n'
    path.write_text(source)
    normal = run(path, 'eval', 'subject')
    path.write_text(source + f'def normalized : {ty} := {normal}\n' + observation + '\n')
    run(path, 'check')
    print(f'ok   normal form {name}')


roundtrip('lambda-capture', '''
def keep : Nat -> Nat -> Nat := fun (x : Nat) (y : Nat) => x
def subject : Nat -> Nat -> Nat := fun (y : Nat) => keep y
''', 'Nat -> Nat -> Nat', 'def witness : EqNat (normalized 1 2) 1 := reflNat 1')

roundtrip('global-capture', '''
def langq0x0 : Nat := 7
def subject : Nat -> Nat := fun (x : Nat) => natAdd x langq0x0
''', 'Nat -> Nat', 'def witness : EqNat (normalized 1) 8 := reflNat 8')

roundtrip('case-capture', '''
def keep : Nat -> Option Nat -> Nat := fun (x : Nat) (b : Option Nat) =>
  case b with | 0 (y : prod ()) => x | 1 (y : Nat) => x
def subject : Nat -> Option Nat -> Nat := fun (y : Nat) => keep y
''', 'Nat -> Option Nat -> Nat',
          'def witness : EqNat (normalized 1 (pureOption Nat 2)) 1 := reflNat 1')

roundtrip('match-capture', '''
def keep : Nat -> Ballots -> Nat := fun (x : Nat) (xs : Ballots) =>
  match xs as w in Ballots return Nat with | bnil => x | bcons h t => x
def subject : Nat -> Ballots -> Nat := fun (h : Nat) => keep h
''', 'Nat -> Ballots -> Nat',
          'def witness : EqNat (normalized 1 (bcons open bnil)) 1 := reflNat 1')

roundtrip('match-motive', '''
def keep : (0 A : Type 0) -> A -> Charter -> A :=
  fun (0 A : Type 0) (x : A) (d : Charter) =>
  match d as w in Charter return A with | open => x | restricted => x | frozen => x
def subject : Nat -> Charter -> Nat := keep Nat
''', 'Nat -> Charter -> Nat',
          'def witness : EqNat (normalized 1 open) 1 := reflNat 1')

roundtrip('indexed-motive', '''
def subject : (0 x : Nat) -> (0 y : Nat) -> EqNat x y -> EqNat y x :=
  fun (0 x : Nat) (0 y : Nat) (e : EqNat x y) => symmNat x y e
''', '(0 x : Nat) -> (0 y : Nat) -> EqNat x y -> EqNat y x',
          'def witness : EqNat 1 1 := normalized 1 1 (reflNat 1)')

roundtrip('dependent-type', '''
def family : Nat -> Type 0 := fun (x : Nat) => (y : Nat) * EqNat x y
def subject : Nat -> Type 0 := fun (y : Nat) => family y
''', 'Nat -> Type 0', 'def witness : normalized 1 := (1, reflNat 1)')

roundtrip('nat-mul', '''
def subject : Nat -> Nat := fun (x : Nat) => natMul x 3
''', 'Nat -> Nat', 'def witness : EqNat (normalized 4) 12 := reflNat 12')

roundtrip('nat-div', '''
def subject : Nat -> Nat := fun (x : Nat) => natDiv x 3
''', 'Nat -> Nat', 'def witness : EqNat (natAdd (normalized 7) (natDiv 7 0)) 2 := reflNat 2')

roundtrip('nat-mod', '''
def subject : Nat -> Nat := fun (x : Nat) => natMod x 3
''', 'Nat -> Nat', 'def witness : EqNat (natAdd (normalized 7) (natMod 7 0)) 1 := reflNat 1')

print('normal-forms.py: all passed')
