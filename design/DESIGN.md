# interest-lang domain design

This file is the only source of the core types and the core operations of
interest-lang. `SPEC.md` sections 4 and 5 cite its sections. Do not add a core
type or a core operation that this file does not give.

When the design changes, paste the new version. Then record the change as an
open item in `SPEC.md` section 9.

---

A self-referential ownership-interest DAO denotes as the existing aggregation acting on a claim algebra. The token is a residual claim on an LLC or SPV, not a deed. The design is the one already fixed in [MavenRain/self-referential-dao](https://github.com/MavenRain/self-referential-dao), extended the way `escrow-lang` extends it: the treasury is an action of the aggregation, not a second aggregation.




## Stance

Elliott's rule is the whole method: the instance's meaning is the meaning's instance. Model first, representation second, meaning function total. Every admitted operation is a homomorphism for that meaning. A failed homomorphism is an abstraction leak and is rejected, not patched. Partiality is `Option`, never an exception.

The governance half is not redesigned.

\[
\llbracket\mathrm{DAO}\rrbracket = \mathrm{Aggregation}\,\mathrm{act}\,F = \mathrm{LeftKanExtension}\,(\mathrm{orbitProjection}\,\mathrm{act})\,F
\]

`act` is the symmetry that encodes voter anonymity by the orbit quotient. `F : Obj ⥤ D` is the constitution. Anonymity is the quotient. Legitimacy is the left-Kan universal property.

## Semantic domain

Two meanings, then their dependent pairing.

The interest is a finitely supported measure on verified identities, not on raw addresses:

\[
\mu : \mathrm{Identity} \to \mathbb{Q}_{\ge 0}, \qquad \mathrm{supply} = \sum_h \mu(h)
\]

An identity carries jurisdiction, investor status, and (only at the representation layer) a wallet set. The claim is the pro-rata functional

\[
\mathrm{claim}(h, a) = \begin{cases} (\mu(h)/\mathrm{supply})\cdot a & \mathrm{supply} > 0 \\ 0 & \mathrm{supply} = 0 \end{cases}
\]

on a cancellative commutative monoid `Asset` of rent and sale proceeds. Cancellation is what makes a double-claim observationally impossible.

A waterfall \(W : \{\mathrm{Rent}, \mathrm{Sale}\} \to \mathrm{Asset} \to \mathrm{Asset}\) selects which inflow is distributable. A restriction

\[
R : \mathrm{Identity} \times \mathrm{Identity} \times \mathbb{Q}_{\ge 0} \to \mathrm{Prop}
\]

depends only on identity claims. \(R\) is the image of securities compliance inside the model. A transfer the bytecode allows but \(R\) rejects is not in the semantic image.

\[
\llbracket\mathrm{OwnershipDAO}\rrbracket = \Sigma\,(L : \mathrm{Aggregation}\,\mathrm{act}\,F).\;\mathrm{InterestState}
\]

with \(\mathrm{InterestState} = (\mu, W, R, \mathrm{treasury})\). The objects of \(F\) include the admissible \((R, W)\).

The deed, the land registry, and the operating agreement are an external interpretation of this claim. Putting them inside \(\llbracket\cdot\rrbracket\) is the leak this design refuses: a transfer the contract accepts is not a conveyance of legal title. RealT and Lofty are interpretations of the same model, not extra semantic objects. Liquidity and the existence of a compliant secondary market are outside the denotation.

## Operations are homomorphisms

**Transfer.** \(\tau(h, k, q)\) is defined iff \(R(h, k, q)\) and \(q \le \mu(h)\), and then it is the unique measure update that subtracts \(q\) from \(h\) and adds \(q\) to \(k\). Otherwise `none`. Laws inherited from the free cancellative commutative monoid of finitely supported measures, quotiented by \(R\): identity at quantity zero, conservation of supply, associativity on admissible chains, commutativity of disjoint transfers. A success return while \(R\) fails is a leak. ERC-1644 forced recovery is not a holder transfer. It denotes only when \((\mathrm{Gov}\,L).\mathrm{obj}\,X\) authorizes it, and it does not quotient through the voter orbit.

**Distribution.** Pro-rata is forced, not chosen. It is the unique monoid homomorphism from \((\mathrm{Asset}, +)\) to holder claims that preserves the unit and the sum-to-inflow law:

\[
\sum_h \mathrm{claim}(h, W(\mathrm{kind})(a)) = W(\mathrm{kind})(a)
\]

The claim travels with the token, so transfer-then-distribute equals distribute on the image measure. In the Arrow-Impossibility regime the verdict is `none` and distribution is `none`: funds stay in the treasury, neither burned nor silently paid.

**Amendment.** Forced by the same definitional equality as the repo:

\[
\llbracket\mathrm{amend}\rrbracket = \mathrm{Gov}, \qquad \mathrm{Gov}\,L = \mathrm{orbitProjection}\,\mathrm{act} \ggg L.\mathrm{functor}
\]

Amendment rewrites the constitution the next distribution and the next restricted transfer will read. It does not move \(\mu\) or the treasury. A restriction module, jurisdiction gate, or sale-proceeds toggle that cannot be recovered as \((\mathrm{Gov}\,L).\mathrm{obj}\,X\) is not in the image of \(\llbracket\cdot\rrbracket\).

Compliance labels and voter anonymity are different actions. Identifying them collapses the orbit quotient. A labelled constitution is the impossibility case; an anonymous constitution keeps the quotient.

## Self-reference

Self-reference is an object-wise fixed point of \(\mathrm{Gov}\), not a domain-theoretic least or greatest fixed point and not a coinductive coalgebra.

\[
\mathrm{IsSelfConstituting}\,F \;\equiv\; \exists L.\;\forall X.\;(\mathrm{Gov}\,L).\mathrm{obj}\,X = F.\mathrm{obj}\,X
\]

A self-referential ownership DAO is one whose restriction predicate and waterfall are exactly the rules the holders aggregated. The unit is the Lambek unit of the Kan extension, \(L.\mathrm{unit} : F \Rightarrow \mathrm{Gov}\,L\). Existence for the shipped constant constitution is constructive via `zeroPhaseAggregation`. Knaster-Tarski is not used.

The honesty fix carries over. For the constant constitution, \(\mathrm{IsSelfConstituting}\) is single-valued at every coordination pressure \(\beta\), and it is always the zero phase. Nonzero phases are not fixed points. The fork lives in the cardinality of `Aggregation` objects, which bifurcates from one to two at \(\beta_c = 1\), not in the fixed-point predicate. A genuinely bifurcating self-constitution would need a non-constant, order-parameter-pinning constitution, which this design does not supply.

## Three fates, inherited

The representation lands in exactly one regime of `self_governance_trichotomy`.

- **Arrow-Impossibility.** No legitimate anonymous aggregation, hence no self-constituting charter. Verdict-dependent operations denote to `none`. Claims freeze as measures; they are not deleted.
- **Arrow-Debreu.** Object-unique left Kan extension. One legitimate verdict on \((R, W)\).
- **Schelling-Ising.** Object-distinct left Kan extensions: two equally legitimate restriction regimes or waterfalls. That is a fork of aggregation objects, not by itself a fork of \(\mathrm{IsSelfConstituting}\).

## One meaning, three representations

| Shape | Representation | What it denotes |
| --- | --- | --- |
| ERC-721 whole-property NFT | \(\mathrm{supply} = 1\), Dirac \(\mu\) | the whole inflow is that identity's claim |
| ERC-1400 fractional security token | coproduct of measures, one \(R\) per partition | tranches are summands, not a second algebra |
| ERC-3643 (T-REX) | identity is ONCHAINID; \(R\) reads claim topics from trusted issuers | `canTransfer` is a decision procedure for \(R\) |

A document hash is metadata. If a document changes the waterfall, that change has to come through \(\mathrm{amend} = \mathrm{Gov}\) or it is a leak. The trusted-issuer set is part of the constitution and is amendable only through \(\mathrm{Gov}\).

A later contract is correct exactly when its observable operations agree with this denotation: an accepted transfer is an \(R\)-admissible measure update, a distribution is the pro-rata linear map and is `none` under impossibility, an amendment equals \(\mathrm{Gov}\) and does not move balances, and a self-constituting charter satisfies the object-wise fixed point. Resemblance to an ERC storage layout is not correctness.
